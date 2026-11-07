# Collected kernel profiles

44 profiles across 21 distribution families; 929 patch records.

These are captured source/package snapshots. A build input or fragment is explicitly labeled and is not silently presented as a normalized binary-kernel config. Architecture is kept separate in the application.

| Distribution | Release / flavor | Architecture | Kernel | Input kind | Source |
|---|---|---|---|---|---|
| alma | 10 / generic x86_64 | x86_64 | 6.12.0 | build-config | [source](https://git.almalinux.org/rpms/kernel/raw/branch/a10/kernel-x86_64-rhel.config) |
| alpine | 3.24 / lts | x86_64 | see recipe | fragment | [source](https://raw.githubusercontent.com/alpinelinux/aports/7bcd076df9b730019762e4407e82466e2315e2fd/main/linux-lts/lts.x86_64.config) |
| arch | rolling / linux | x86_64 | 7.2.3-arch1 | complete | [source](https://gitlab.archlinux.org/archlinux/packaging/packages/linux/-/raw/fae9fc0d8c9531fe1875b9032fff999e714e6527/config.x86_64) |
| arch | rolling / linux-hardened | x86_64 | 7.1.9-hardened1 | complete | [source](https://gitlab.archlinux.org/archlinux/packaging/packages/linux-hardened/-/raw/09ab38f5dd62a0e4a3ffb8f02c2c66cf28464540/config.x86_64) |
| arch | rolling / linux-lts | x86_64 | 6.18.49 | complete | [source](https://gitlab.archlinux.org/archlinux/packaging/packages/linux-lts/-/raw/6eeb89a652657d03cb488405b09d5afbb5b2de4b/config.x86_64) |
| arch | rolling / linux-zen | x86_64 | 7.2.3-zen1 | complete | [source](https://gitlab.archlinux.org/archlinux/packaging/packages/linux-zen/-/raw/61b4c7f94c66ac0448cc6703054b0a4d48a1ad93/config.x86_64) |
| asahi | rolling / linux-asahi | arm64 | 7.1.6 | complete | [source](https://raw.githubusercontent.com/asahi-alarm/PKGBUILDs/f091f3dae5ea0da1584965ca511d8e259ca40348/linux-asahi/config) |
| centos | 10 / generic x86_64 | x86_64 | 6.12.0 | build-config | [source](https://gitlab.com/redhat/centos-stream/rpms/kernel/-/raw/c10s/kernel-x86_64-rhel.config) |
| debian | 12 / amd64 | x86_64 | 6.1.176 | complete | [source](https://deb.debian.org/debian/pool/main/l/linux/linux-headers-6.1.0-50-amd64_6.1.176-1_amd64.deb) |
| debian | 13 / amd64 | x86_64 | 6.12.94 | complete | [source](https://deb.debian.org/debian/pool/main/l/linux/linux-headers-6.12.94+deb13-amd64_6.12.94-1_amd64.deb) |
| fedora | 43 / Workstation | x86_64 | see kernel.spec | build-config | [source](https://src.fedoraproject.org/rpms/kernel/raw/f43/f/kernel-x86_64-fedora.config) |
| fedora | 44 / Workstation | x86_64 | see kernel.spec | build-config | [source](https://src.fedoraproject.org/rpms/kernel/raw/f44/f/kernel-x86_64-fedora.config) |
| fedora | 44 / generic aarch64 | arm64 | see recipe | build-config | [source](https://src.fedoraproject.org/rpms/kernel/raw/f44/f/kernel-aarch64-fedora.config) |
| gentoo | 6.1 / gentoo-kernel-bin | x86_64 | 6.1.187 | complete | [source](https://distfiles.gentoo.org/pub/proj/dist-kernel/binpkg/amd64/6.1/gentoo-kernel-6.1.187-1.amd64.gpkg.tar) |
| gentoo | 6.12 / gentoo-kernel-bin | x86_64 | 6.12.108 | complete | [source](https://distfiles.gentoo.org/pub/proj/dist-kernel/binpkg/amd64/6.12/gentoo-kernel-6.12.108-1.amd64.gpkg.tar) |
| gentoo | 6.18 / gentoo-kernel-bin | x86_64 | 6.18.49 | complete | [source](https://distfiles.gentoo.org/pub/proj/dist-kernel/binpkg/amd64/6.18/gentoo-kernel-6.18.49-1.amd64.gpkg.tar) |
| gentoo | 6.6 / gentoo-kernel-bin | x86_64 | 6.6.156 | complete | [source](https://distfiles.gentoo.org/pub/proj/dist-kernel/binpkg/amd64/6.6/gentoo-kernel-6.6.156-1.amd64.gpkg.tar) |
| gentoo | 7.2 / gentoo-kernel-bin | x86_64 | 7.2.3 | complete | [source](https://distfiles.gentoo.org/pub/proj/dist-kernel/binpkg/amd64/7.2/gentoo-kernel-7.2.3-1.amd64.gpkg.tar) |
| kali | rolling / amd64 | x86_64 | 7.1.5 | complete | [source](https://kali.download/kali/pool/main/l/linux/linux-headers-7.1.5+kali-amd64_7.1.5-1kali1_amd64.deb) |
| cachyos | rolling / linux-cachyos | x86_64 | 7.2.0 | complete | [source](https://raw.githubusercontent.com/CachyOS/linux-cachyos/bf83c8e65c1e801fb6ed4734824dbee4a73031be/linux-cachyos/config) |
| cachyos | rolling / linux-cachyos-lts | x86_64 | 6.18.3 | complete | [source](https://raw.githubusercontent.com/CachyOS/linux-cachyos/bf83c8e65c1e801fb6ed4734824dbee4a73031be/linux-cachyos-lts/config) |
| manjaro | rolling / linux618 | x86_64 | 6.18.49 | complete | [source](https://gitlab.manjaro.org/packages/core/linux618/-/raw/4cda70bbc232e71b068eb3bb91906a0f9dab9c95/config) |
| manjaro | rolling / linux72 | x86_64 | 7.2.3 | complete | [source](https://gitlab.manjaro.org/packages/core/linux72/-/raw/00ab07d1eccf2344bd06644bf2d228736c6a5452/config) |
| nixos | 26.05 / linuxPackages | x86_64 | 6.18.49 | complete | [source](https://hydra.nixos.org/build/344273474) |
| nobara | 44 / mainline | x86_64 | 7.2 | merged-build-config | [source](https://raw.githubusercontent.com/Nobara-Project/rpm-sources/599f81f5073b88ee08d803785f52de8cd739da75/baseos/kernel/mainline/7.2/config) |
| nobara | 44 / LTS | x86_64 | 6.18 | merged-build-config | [source](https://raw.githubusercontent.com/Nobara-Project/rpm-sources/599f81f5073b88ee08d803785f52de8cd739da75/baseos/kernel/LTS/6.18/config) |
| opensuse | Leap-16.0 / default | x86_64 | 6.12.0 | complete | [source](https://raw.githubusercontent.com/openSUSE/kernel-source/7777c96306f55b9399a469b65202c691ae094967/config/x86_64/default) |
| opensuse | Tumbleweed / default | x86_64 | 7.2.2 | complete | [source](https://raw.githubusercontent.com/openSUSE/kernel-source/7f3a2e791bf1ae4411f41fba121212863b8138be/config/x86_64/default) |
| popos | 22.04 / generic | x86_64 | 7.1.1 | complete | [source](https://apt.pop-os.org/release/pool/jammy/linux/dfed2f1508608c2a1267db784df84e975e1c30e3/linux-headers-7.1.1-76070101-generic_7.1.1-76070101.202606191302~1782222967~22.04~dfed2f1_amd64.deb) |
| popos | 24.04 / generic | x86_64 | 7.1.5 | complete | [source](https://apt.pop-os.org/release/pool/noble/linux/38a419fc95c5eae36afa2f13e459a7fc4f4a68f5/linux-headers-7.1.5-76070105-generic_7.1.5-76070105.202607241434~1787955023~24.04~38a419f_amd64.deb) |
| rocky | 10 / generic x86_64 | x86_64 | 6.12.0 | build-config | [source](https://git.rockylinux.org/staging/rpms/kernel/-/raw/r10/SOURCES/kernel-x86_64-rhel.config) |
| slackware | 15.0 / huge | x86_64 | 5.15.19 | complete | [source](https://mirrors.slackware.com/slackware/slackware64-15.0/kernels/huge.s/config) |
| slackware | current / generic | x86_64 | 6.18.49 | complete | [source](https://mirrors.slackware.com/slackware/slackware64-current/kernels/generic.s/config) |
| solus | rolling / linux-current | x86_64 | 7.1.3-347.current | complete | [source](https://raw.githubusercontent.com/getsolus/packages/8093e43f5640c43b3168df5a34f0dfcf47d24d27/packages/l/linux-current/files/config) |
| solus | rolling / linux-lts | x86_64 | 6.18.20-282.lts | complete | [source](https://raw.githubusercontent.com/getsolus/packages/8093e43f5640c43b3168df5a34f0dfcf47d24d27/packages/l/linux-lts/files/config) |
| steamos | 3.8 / linux-neptune-618 | x86_64 | 6.18.46-valve1 | complete | [source](https://steamdeck-packages.steamos.cloud/archlinux-mirror/jupiter-3.8/os/x86_64/linux-neptune-618-headers-6.18.46.valve1-1-x86_64.pkg.tar.zst) |
| steamos | main / linux-neptune-72 | x86_64 | 7.2.0-valve1 | complete | [source](https://steamdeck-packages.steamos.cloud/archlinux-mirror/jupiter-main/os/x86_64/linux-neptune-72-headers-7.2.0.valve1-1-x86_64.pkg.tar.zst) |
| ubuntu | 22.04 / generic GA | x86_64 | 5.15.0-191-generic | complete | [source](https://archive.ubuntu.com/ubuntu/pool/main/l/linux/linux-headers-5.15.0-191-generic_5.15.0-191.201_amd64.deb) |
| ubuntu | 22.04 / generic HWE | x86_64 | 6.8.12 | complete | [source](https://archive.ubuntu.com/ubuntu/pool/main/l/linux-hwe-6.8/linux-headers-6.8.0-138-generic_6.8.0-138.138~22.04.1_amd64.deb) |
| ubuntu | 24.04 / generic GA | x86_64 | 6.8.12 | complete | [source](https://archive.ubuntu.com/ubuntu/pool/main/l/linux/linux-headers-6.8.0-139-generic_6.8.0-139.139_amd64.deb) |
| ubuntu | 24.04 / generic HWE | x86_64 | 7.0.14 | complete | [source](https://archive.ubuntu.com/ubuntu/pool/main/l/linux-hwe-7.0/linux-headers-7.0.0-31-generic_7.0.0-31.31~24.04.1_amd64.deb) |
| ubuntu | 26.04 / generic GA | x86_64 | 7.0.14 | complete | [source](https://archive.ubuntu.com/ubuntu/pool/main/l/linux/linux-headers-7.0.0-31-generic_7.0.0-31.31_amd64.deb) |
| void | rolling / linux6.18 | x86_64 | 6.18.49 | complete | [source](https://raw.githubusercontent.com/void-linux/void-packages/29e9a571e5805c68033e1d9480bdf6398c8f67ac/srcpkgs/linux6.18/files/x86_64-dotconfig) |
| void | rolling / linux7.2 | x86_64 | 7.2.3 | complete | [source](https://raw.githubusercontent.com/void-linux/void-packages/29e9a571e5805c68033e1d9480bdf6398c8f67ac/srcpkgs/linux7.2/files/x86_64-dotconfig) |

Patch records are inspectable source material, not one universally applicable stack. The matching recipe determines patch selection and order. An unverified record stays unverified until checked against a concrete tree.

Full hashes, package members, pinned revisions where available, authors, dates and patch-base evidence are in `index.json`. Unknown authorship, date, base or sponsorship stays explicitly unknown.
