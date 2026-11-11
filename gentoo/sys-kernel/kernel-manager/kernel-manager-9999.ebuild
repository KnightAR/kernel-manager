# Copyright 2026 Gentoo Authors
# Distributed under the terms of the GNU General Public License v2

EAPI=8

PYTHON_COMPAT=( python3_{12..14} )

inherit cmake desktop git-r3 python-single-r1 xdg

DESCRIPTION="Explore, compare, and configure Linux kernels in a native Qt 6 desktop app"
HOMEPAGE="https://github.com/KnightAR/kernel-manager"
EGIT_REPO_URI="https://github.com/KnightAR/kernel-manager.git"

LICENSE="GPL-2"
SLOT="0"
IUSE="kde"
REQUIRED_USE="${PYTHON_REQUIRED_USE}"

# The Kconfig engine is compiled from the kernel source tree at build time.
DEPEND="
	dev-qt/qtbase:6[gui,network,widgets]
	dev-qt/qtsvg:6
	virtual/linux-sources
	kde? ( kde-frameworks/kwindowsystem:6 )
"
RDEPEND="
	${DEPEND}
	${PYTHON_DEPS}
	app-arch/xz-utils
	app-arch/zstd
	dev-vcs/git
	sys-apps/bubblewrap
"
BDEPEND="
	sys-devel/bison
	sys-devel/flex
"

PATCHES=(
	"${FILESDIR}"/${PN}-9999-home-defaults.patch
)

pkg_setup() {
	KM_KERNEL_SOURCE="${KERNEL_DIR:-${EPREFIX}/usr/src/linux}"
	if [[ ! -f ${KM_KERNEL_SOURCE}/scripts/kconfig/parser.y ]]; then
		eerror "${KM_KERNEL_SOURCE} is not a complete Linux source tree."
		eerror "Point /usr/src/linux at one (eselect kernel) or set KERNEL_DIR."
		die "Linux source tree not found"
	fi
	python-single-r1_pkg_setup
}

src_prepare() {
	# Resources are looked up relative to KM_SOURCE_DIR; point it at the
	# installed data directory instead of the build checkout.
	sed -i \
		-e 's|KM_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"|KM_SOURCE_DIR="${CMAKE_INSTALL_PREFIX}/share/kernel-manager"|' \
		CMakeLists.txt || die
	grep -q 'KM_SOURCE_DIR="${CMAKE_INSTALL_PREFIX}' CMakeLists.txt \
		|| die "failed to relocate KM_SOURCE_DIR"
	cmake_src_prepare
}

src_configure() {
	local mycmakeargs=(
		-DKERNEL_SOURCE="${KM_KERNEL_SOURCE}"
		-DCMAKE_DISABLE_FIND_PACKAGE_KF6WindowSystem=$(usex !kde)
	)
	cmake_src_configure
}

src_install() {
	cmake_src_install

	insinto /usr/share/${PN}
	doins -r tools
	python_fix_shebang "${ED}"/usr/share/${PN}/tools

	doicon -s scalable assets/${PN}.svg
	make_desktop_entry ${PN} "Kernel Manager" ${PN} "System;Settings;Qt;"

	dodoc README.md docs/*.md
}

pkg_postinst() {
	xdg_pkg_postinst
	elog "Kernel Manager embeds the Kconfig engine from ${KM_KERNEL_SOURCE:-/usr/src/linux}."
	elog "Kconfig's C API changes between kernel releases, so re-emerge this package"
	elog "after switching to a new kernel source tree."
	elog
	elog "The bundled catalog is installed read-only in /usr/share/${PN}/catalog."
	elog "The catalog collectors write next to their own directory, so run them"
	elog "from a git checkout and pass --catalog <checkout>/catalog to kernel-manager."
}
