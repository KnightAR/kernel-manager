# Memory design and measurements

The interface uses C++ Qt Widgets, item models and QPainter delegates. Each
kernel option is a data row, not a QWidget or QObject. The tree paints visible
rows; distro strips use a fixed eight-element array plus an overflow count.
Qt supplies the tree's keyboard navigation and accessibility interface, including
text descriptions of the painted state and distro evidence.

The native worker and command-line modes use QCoreApplication. They initialize
an event loop without a GUI platform, font database or widget hierarchy, as
specified by [Qt's QCoreApplication documentation](https://doc.qt.io/qt-6/qcoreapplication.html).
The executable still links Qt Widgets for the desktop entry point; shared-library
mappings are not the same as private allocated memory.

## Representation and bounds

- One shared symbol table serves every distro profile. Each profile stores one
  byte per known symbol: absent, n, m, y or scalar. Scalar strings share backing
  storage across profiles. Missing values remain distinct from explicit n and
  empty scalar strings. Config SHA-256 validation remains in place.
- Full Kconfig help stays in the native parser's source data and is converted to
  QString only when requested. The full-text search index uses lowercase UTF-8.
  Search terms are parsed and encoded once per request, including field scopes
  and quoted phrases.
- The GUI interns repeated static paths, source filenames and types. Allowed
  n/m/y transitions occupy three bits per row. It receives neither complete
  distro config dictionaries nor patch bodies.
- Patch matching reads 64 KiB chunks, with overlap for tokens and paths spanning
  reads. A cost-accounted 512 KiB LRU stores patch indices and match reasons,
  rather than copies of full metadata or diffs. Metadata remains available for
  every collected patch; previews use the existing bounded text reader.
- Maintainer matching uses native fnmatch with cached UTF-8 patterns. It does not
  keep thousands of compiled regular expressions. Per-option context has a
  64-record LRU; evicted context can be recomputed.
- Description and Distros load with selection. Driver/device and People request
  context when opened; Patches requests matching when opened. These requests
  keep the existing EventBus ownership, replacement and revision checks.
- Main menu category icons are QPainter paths. Distro logos are embedded SVG
  resources. Backgrounds use translucent widget fills and an optional native KDE
  compositor blur request. The app keeps no desktop captures, software blur
  buffers, full-panel bitmap backgrounds or shadow effects. Compositor blur has
  its own rendering cost, outside the worker figures below.
- Single-configuration CMake builds default to MinSizeRel. An explicitly selected
  build type is respected. This reduces executable code size without replacing
  the native Qt controls.

## Measured worker memory

Measured on this machine on 2026-09-05 with Linux 7.0.3, Qt 6.11, 19,347 menu
entries, 42 x86_64 profiles / 20 distro families, and 929 patch records. The full
catalog also has two ARM64 profiles. Both versions used the same source and
catalog. Each measurement used a newly started `--worker`, drained its protocol,
and read that owned child's smaps_rollup once at each operation boundary. No
existing desktop process was inspected or relaunched.

| State | Before RSS | After RSS | Reduction | Before → after PSS |
| --- | ---: | ---: | ---: | ---: |
| Loaded source and catalog | 221.9 MiB | 69.4 MiB | 68.7% | 205.3 → 58.2 MiB |
| IWLWIFI with driver context and related patches | 272.1 MiB | 85.7 MiB | 68.5% | 255.4 → 74.4 MiB |

RSS is resident memory including shared mappings. PSS apportions shared pages.
These are operation-boundary snapshots, **not peak memory or combined GUI plus
worker totals**. The original binary had an unspecified CMake build type; the
final binary uses MinSizeRel. Results include both representation changes and
the smaller build. Filesystem caches and desktop load were not controlled, so
single-run timings are not presented as a general speedup.

The new description-only IWLWIFI request took 1.1 ms and left worker RSS at
69.4 MiB, equal to its initialized RSS. Three illustrative name/help/path
searches took 4–14 ms. Loading the maintainer overview after the detailed
selections left worker RSS at 86.0 MiB. Caches are bounded, but native Kconfig,
complete catalog metadata and hardware/build indices still scale with source
and catalog size. This is a measured reduction, not a universal minimum claim.

## Verification and remaining limits

The build passed. Manual headless checks compared all 19,347 distro-coverage
rows against the collected config bytes, including deduplicated family counts.
Five complete option, maintainer-context and related-patch responses matched
captures from the original binary. Scalar values and absent values were checked
against source config bytes. Scoped search, text/state/subsystem/distro filters,
ancestor preservation, empty results and filtered outliers passed.

Six disposable patch-input cases exercised actual worker reads at 64 KiB
boundaries: split symbols, a symbol at EOF, rejected prefixes/suffixes, a split
path, and preference for symbol evidence over file evidence. Lazy tab requests
and the compatible full-selection protocol produced the original responses.
The source .config SHA-256 stayed unchanged. No test suite was introduced.

The background update was revised again on 2026-09-15 after Aku reported that
the corrected palette still looked opaque. The original palette definitions
remain byte-for-byte identical to the saved original. Background transparency
no longer depends on the result of a compositor-support query: the window
clears its backing surface and paints only translucent surface backgrounds.
Tree/text view backgrounds and description HTML do not add opaque fills.
Combined workspace/card opacity was reduced from about 75% to 45.47%.

The blur request is queued after the native window is shown, supplies an
explicit full-client rectangle, and updates on resize. Verification used the
actual build/kernel-manager executable with its normal startup path. The
running executable inode matched the rebuilt binary; it loaded Qt Wayland and
the KF6 WindowSystem Wayland plugin. Its protocol trace showed KDE advertising
the background-effect capability, a 1680-by-1020 blur region being assigned, and
a following surface commit. A Spectacle desktop capture of the final running
application showed the underlying browser windows through the lighter panels
with blurred boundaries. That application was left open for user review.

Earlier widget-only captures established alpha, and a synthetic preview showed
blur over stripes, but those checks did not establish the user's actual startup
path or that the treatment was sufficiently translucent. No compositor settings
were changed, and the exact blur kernel/radius has not been measured.

The search/tab layout was previously checked at 1100, 1280 and 1680 pixels wide,
including a narrow detail pane and 24-point tab text. Search retained its right
margin, all five tabs remained inside the pane, and scoped search returned
results. These layout fixes remain in place. Screen-reader behavior, compositor
blur radius/cost and total desktop memory remain unmeasured. No kernel
installation, elevation, or automatic source/config write was added.
