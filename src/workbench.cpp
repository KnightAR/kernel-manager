// SPDX-License-Identifier: GPL-2.0-only
#include "workbench.h"
#include "asyncui.h"
#include "catalog.h"
#include "engine.h"
#include "eventbus.h"
#include "session.h"
#include <QComboBox>
#include <QDialog>
#include <QHeaderView>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QTabWidget>
#include <QTableWidget>
#include <QTextBrowser>
#include <QTimer>
#include <QUuid>
#include <QVBoxLayout>
#include <memory>

namespace {
struct Area {
  QString name, reason, symbols;
};
QList<Area> areas(const QString &workflow) {
  if (workflow == "asahi")
    return {{"Platform and memory",
             "Review the target page size, interrupt controller, IOMMU and "
             "firmware communication together. Compare against the ARM64 "
             "catalog and the exact downstream source.",
             "ARCH_APPLE ARM64_4K_PAGES ARM64_16K_PAGES ARM64_64K_PAGES "
             "APPLE_AIC APPLE_DART APPLE_SART APPLE_RTKIT APPLE_MAILBOX"},
            {"Graphics and Rust",
             "Inspect GPU/display support and the selected Rust toolchain. A "
             "symbol missing from an upstream tree may exist in Asahi's "
             "downstream tree; missing is not disabled.",
             "DRM_ASAHI DRM_APPLE DRM_APPLE_AUDIO RUST RUST_IS_AVAILABLE "
             "RUSTC_VERSION_TEXT"},
            {"Storage, USB and input",
             "Follow the boot-storage path and the bus dependencies needed by "
             "the target machine. Keep model-specific device-tree bindings "
             "beside the driver config.",
             "NVME_APPLE PCIE_APPLE USB_DWC3_APPLE PHY_APPLE_ATC "
             "SPI_HID_APPLE_OF TOUCHSCREEN_APPLE_Z2 I2C_APPLE SPI_APPLE"},
            {"Power, audio and camera",
             "Inspect power domains, clocks and audio routing. Kernel config "
             "alone does not establish firmware availability, speaker safety "
             "or userspace integration.",
             "ARM_APPLE_CPUIDLE ARM_APPLE_SOC_CPUFREQ APPLE_PMGR_PWRSTATE "
             "APPLE_ADMAC SND_SOC_APPLE_MACAUDIO SND_SOC_APPLE_MCA "
             "VIDEO_APPLE_ISP"}};
  if (workflow == "valve")
    return {{"Graphics and frame delivery",
             "Compare graphics, synchronization and display options with the "
             "collected SteamOS configs. Use measured frame timing to assess a "
             "proposed change.",
             "DRM DRM_AMDGPU DRM_AMDGPU_USERPTR DRM_SCHED SYNC_FILE "
             "DMA_SHARED_BUFFER DRM_DISPLAY_DP_AUX_CEC"},
            {"Latency and observability",
             "Review preemption and scheduler choices alongside tracing and "
             "pressure data. These are experiment inputs, not guaranteed "
             "performance improvements.",
             "PREEMPT_DYNAMIC PREEMPT PREEMPT_RT HZ_1000 SCHED_AUTOGROUP PSI "
             "FTRACE SCHEDSTATS BPF_SYSCALL"},
            {"Controllers and handheld input",
             "Inspect the specific HID/input drivers and userspace interfaces. "
             "Downstream SteamOS additions are shown only when present in the "
             "selected source.",
             "HID_STEAM STEAM_FF HID_NINTENDO JOYSTICK_XPAD INPUT_JOYDEV "
             "INPUT_EVDEV INPUT_UINPUT HID_MULTITOUCH"},
            {"Power, suspend and thermals",
             "Review platform power management and suspend support with the "
             "machine's drivers. Battery life, resume reliability and latency "
             "require workload measurements.",
             "PM SUSPEND HIBERNATION X86_AMD_PSTATE AMD_PMC "
             "ACPI_PLATFORM_PROFILE CPU_FREQ_GOV_SCHEDUTIL CPU_IDLE THERMAL"}};
  return {{"Boot and recovery",
           "Review early boot, root filesystem and module availability "
           "together. Built-in versus module placement matters before the root "
           "filesystem and module loader are available.",
           "EFI EFI_STUB BLK_DEV_INITRD DEVTMPFS DEVTMPFS_MOUNT MODULES "
           "NVME_CORE BLK_DEV_NVME EXT4_FS BTRFS_FS"},
          {"ABI and signing",
           "Inspect module ABI, signing policy and external key inputs before "
           "exporting a config for another source version or toolchain.",
           "MODVERSIONS MODULE_SIG MODULE_SIG_FORCE MODULE_SIG_KEY "
           "SYSTEM_TRUSTED_KEYS SYSTEM_REVOCATION_KEYS DEBUG_INFO_BTF "
           "LOCALVERSION"},
          {"Debugging and regression work",
           "Choose instrumentation for the failure being investigated. Read "
           "each option's help for dependencies and costs; preserve "
           "build/config provenance with results.",
           "KASAN KCSAN UBSAN LOCKDEP PROVE_LOCKING DEBUG_ATOMIC_SLEEP "
           "DEBUG_INFO FTRACE FUNCTION_TRACER KPROBES MAGIC_SYSRQ"},
          {"Hardening and isolation",
           "Compare mitigations, access-control frameworks and namespace "
           "support against workload requirements and the selected kernel's "
           "own documentation.",
           "STACKPROTECTOR_STRONG VMAP_STACK HARDENED_USERCOPY SECURITY "
           "SECURITY_LANDLOCK SECCOMP SECCOMP_FILTER NAMESPACES USER_NS"}};
}
} // namespace

QJsonArray engineeringReport(const QString &workflow, const Engine &engine,
                             const Catalog &catalog) {
  QJsonArray result;
  for (const auto &area : areas(workflow))
    for (const auto &symbol : area.symbols.split(' ')) {
      int i = engine.find(symbol);
      QJsonObject record{{"area", area.name},
                         {"reason", area.reason},
                         {"symbol", symbol},
                         {"present", i >= 0},
                         {"index", i},
                         {"architecture", engine.architecture},
                         {"distros_enabled", catalog.supportCount(symbol)},
                         {"profiles", catalog.compare(symbol)}};
      if (i >= 0)
        record[QStringLiteral("option")] = engine.describe(i);
      result.append(record);
    }
  return result;
}

void showEngineeringWorkbench(EventBus &bus, Session &session, QWidget *parent,
                              std::function<void(int)> selected) {
  auto *dialog = toolDialog(parent, "Engineering workbench", 1320, 840);
  auto *layout = new QVBoxLayout(dialog);
  auto *workflow = new QComboBox;
  workflow->addItem("Kernel maintenance", "kernel");
  workflow->addItem("Apple Silicon / Asahi", "asahi");
  workflow->addItem("Desktop / handheld / Valve", "valve");
  workflow->setAccessibleName("Engineering workflow");
  layout->addWidget(workflow);
  auto *scope =
      new QLabel("Linux " + session.version + " · " + session.architecture +
                 " · " + session.toolchain + " · " + session.sourcePath);
  scope->setWordWrap(true);
  layout->addWidget(scope);
  auto *tabs = new QTabWidget;
  layout->addWidget(tabs, 1);
  auto *options = new QSplitter(Qt::Vertical);
  auto *table = new QTableWidget;
  options->addWidget(table);
  table->setColumnCount(4);
  table->setHorizontalHeaderLabels({"Engineering area", "Kernel symbol",
                                    "Working value", "Distros enabling"});
  table->setEditTriggers(QAbstractItemView::NoEditTriggers);
  table->setSelectionBehavior(QAbstractItemView::SelectRows);
  table->setSelectionMode(QAbstractItemView::SingleSelection);
  table->setAlternatingRowColors(true);
  table->verticalHeader()->hide();
  auto *detail = linkBrowser(bus);
  options->addWidget(detail);
  options->setSizes({440, 240});
  tabs->addTab(options, "Configuration");
  auto records = std::make_shared<QJsonArray>();
  auto *inspect = new QPushButton("Inspect selected option in the kernel menu");
  inspect->setEnabled(false);
  layout->addWidget(inspect);
  const auto key = "workbench:" + QUuid::createUuid().toString();
  auto populate = [&bus, dialog, workflow, table, detail, records, inspect,
                   key] {
    table->setRowCount(0);
    inspect->setEnabled(false);
    detail->setPlainText("Loading engineering evidence…");
    auto requested = workflow->currentData().toString();
    auto id = bus.request("workbench", {{"workflow", requested}}, key);
    bus.listen(
        id, dialog,
        [workflow, requested, table, detail, records](const QString &topic,
                                                      const QJsonObject &p) {
          if (workflow->currentData().toString() != requested)
            return;
          if (topic == "failed") {
            detail->setPlainText(p[QStringLiteral("error")].toString());
            return;
          }
          if (topic != "completed")
            return;
          *records = p[QStringLiteral("items")].toArray();
          table->setRowCount(records->size());
          for (int row = 0; row < records->size(); ++row) {
            auto r = (*records)[row].toObject();
            QStringList cells{
                r[QStringLiteral("area")].toString(),
                r[QStringLiteral("symbol")].toString(),
                r[QStringLiteral("present")].toBool()
                    ? r[QStringLiteral("option")][QStringLiteral("value")]
                          .toString()
                    : "Not in this source tree",
                QString::number(r[QStringLiteral("distros_enabled")].toInt())};
            for (int col = 0; col < cells.size(); ++col)
              table->setItem(row, col, new QTableWidgetItem(cells[col]));
          }
          table->resizeColumnsToContents();
          table->horizontalHeader()->setStretchLastSection(true);
          table->selectRow(0);
        });
  };
  QObject::connect(
      table, &QTableWidget::currentCellChanged, dialog,
      [records, detail, inspect, workflow, &session](int row, int, int, int) {
        if (row < 0 || row >= records->size())
          return;
        auto r = (*records)[row].toObject();
        auto option = r[QStringLiteral("option")].toObject();
        inspect->setEnabled(r[QStringLiteral("present")].toBool());
        QString text = "<h3>" + html(r[QStringLiteral("area")].toString()) +
                       "</h3><p>" +
                       html(r[QStringLiteral("reason")].toString()) + "</p>";
        if (r[QStringLiteral("present")].toBool())
          text += "<h3>" +
                  html(option[QStringLiteral("description")].toString()) +
                  "</h3><pre style='white-space:pre-wrap'>" +
                  html(option[QStringLiteral("help")].toString()) + "</pre>";
        else
          text +=
              "<p>This source tree does not define the symbol. Select a "
              "matching architecture and downstream source to evaluate it.</p>";
        if (workflow->currentData().toString() == "asahi" &&
            session.architecture != "arm64")
          text += "<p><b>This session targets x86_64. Open an ARM64 source "
                  "session for Apple Silicon configuration.</b></p>";
        detail->setHtml(text);
      });
  QObject::connect(
      inspect, &QPushButton::clicked, dialog, [records, table, selected] {
        int row = table->currentRow();
        if (row >= 0 && row < records->size()) {
          int i = (*records)[row][QStringLiteral("index")].toInt(-1);
          if (i >= 0)
            selected(i);
        }
      });
  auto *sources = new QWidget;
  auto *sourceLayout = new QVBoxLayout(sources);
  auto *filter = new QLineEdit;
  filter->setPlaceholderText(
      "Filter source paths · apple, amdgpu, firmware, bindings, ABI…");
  filter->setAccessibleName("Filter engineering source paths");
  sourceLayout->addWidget(filter);
  auto *sourceSplit = new QSplitter;
  auto *files = new QListWidget;
  auto *preview = new TextPreview(bus);
  sourceSplit->addWidget(files);
  sourceSplit->addWidget(preview);
  sourceSplit->setSizes({430, 780});
  sourceLayout->addWidget(sourceSplit, 1);
  tabs->addTab(sources, "Device trees, bindings and documentation");
  const auto sourceKey = "source-index:" + QUuid::createUuid().toString();
  auto loadPaths = [&bus, dialog, filter, files, sourceKey] {
    files->clear();
    auto query = filter->text();
    auto id = bus.request("source-index", {{"query", query}}, sourceKey);
    bus.listen(
        id, dialog,
        [filter, query, files](const QString &topic, const QJsonObject &p) {
          if (filter->text() != query)
            return;
          if (topic == "paths")
            files->addItems(strings(p[QStringLiteral("items")].toArray()));
        });
  };
  QObject::connect(tabs, &QTabWidget::currentChanged, dialog,
                   [loadPaths](int i) {
                     if (i == 1)
                       loadPaths();
                   });
  auto *timer = new QTimer(dialog);
  timer->setSingleShot(true);
  timer->setInterval(100);
  QObject::connect(filter, &QLineEdit::textChanged, timer,
                   [timer] { timer->start(); });
  QObject::connect(timer, &QTimer::timeout, dialog, loadPaths);
  QObject::connect(files, &QListWidget::currentTextChanged, dialog,
                   [preview, &session](const QString &path) {
                     if (!path.isEmpty())
                       preview->openFile(session.sourcePath + "/" + path);
                   });
  QObject::connect(workflow, &QComboBox::currentIndexChanged, dialog,
                   [populate](int) { populate(); });
  populate();
  dialog->show();
}
