// SPDX-License-Identifier: GPL-2.0-only
#include "insights.h"
#include "engine.h"
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QSysInfo>
#include <fnmatch.h>

namespace {
QString read(const QString &path) {
    QFile file(path); return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
}
QString normalize(QString name) { return name.replace('-', '_'); }
QJsonObject deviceJson(const Device &d) {
    return {{"bus", d.bus}, {"address", d.address}, {"vendor", d.vendor}, {"product", d.product},
            {"vendor_id", d.vendorId}, {"device_id", d.deviceId}, {"driver", d.driver}, {"module", d.module}, {"modalias", d.modalias}};
}
}

Insights::Insights(Engine &engine) : engine(engine) {
    scanDevices(); scanBuildFiles(); scanMaintainers();
    auto info = read("/proc/cpuinfo");
    cpu = QRegularExpression("^model name\\s*:\\s*(.+)$", QRegularExpression::MultilineOption).match(info).captured(1);
    memory = QRegularExpression("^MemTotal:\\s*(.+)$", QRegularExpression::MultilineOption).match(read("/proc/meminfo")).captured(1);
    runningKernel = QSysInfo::kernelVersion();
    for (auto line : read("/proc/self/mounts").split('\n')) {
        auto fields = line.split(' '); if (fields.size() > 2 && fields[1] == "/") rootFs = fields[2];
    }
    // Package inventory is read only. Gentoo uses installed package directories;
    // dpkg and pacman's local metadata cover Debian/Ubuntu and Arch families.
    QDirIterator portage("/var/db/pkg", QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (portage.hasNext()) { auto path = portage.next(); if (path.count('/') == 5) installed << path.mid(12); }
    if (installed.isEmpty()) {
        for (auto block : read("/var/lib/dpkg/status").split("\n\n")) {
            if (block.contains("Status: install ok installed")) installed << QRegularExpression("^Package: (.+)$", QRegularExpression::MultilineOption).match(block).captured(1);
        }
    }
    if (installed.isEmpty()) installed = QDir("/var/lib/pacman/local").entryList(QDir::Dirs | QDir::NoDotAndDotDot);
}

void Insights::scanDevices() {
    QHash<QString, QString> vendors, products;
    auto ids = read("/usr/share/hwdata/pci.ids");
    if (ids.isEmpty()) ids = read("/usr/share/misc/pci.ids");
    QString vendor;
    for (auto line : ids.split('\n')) {
        if (QRegularExpression("^[0-9a-f]{4}  ").match(line).hasMatch()) { vendor = line.left(4); vendors[vendor] = line.mid(6); }
        else if (QRegularExpression("^\t[0-9a-f]{4}  ").match(line).hasMatch()) products[vendor + ":" + line.mid(1, 4)] = line.mid(7);
    }
    for (const auto &bus : {QString("pci"), QString("usb")}) {
        auto directory = "/sys/bus/" + bus + "/devices";
        for (const auto &name : QDir(directory).entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            auto base = directory + "/" + name; Device d; d.bus = bus; d.address = name;
            d.vendorId = read(base + (bus == "pci" ? "/vendor" : "/idVendor")).trimmed().remove("0x");
            d.deviceId = read(base + (bus == "pci" ? "/device" : "/idProduct")).trimmed().remove("0x");
            if (d.vendorId.isEmpty()) continue;
            d.vendor = bus == "pci" ? vendors.value(d.vendorId, d.vendorId) : read(base + "/manufacturer").trimmed();
            d.product = bus == "pci" ? products.value(d.vendorId + ":" + d.deviceId, "Device " + d.deviceId) : read(base + "/product").trimmed();
            d.driver = QFileInfo(base + "/driver").symLinkTarget().section('/', -1);
            d.module = QFileInfo(base + "/driver/module").symLinkTarget().section('/', -1);
            if (d.module.isEmpty()) d.module = d.driver;
            d.modalias = read(base + "/modalias").trimmed();
            devices.append(d);
        }
    }
}

void Insights::scanBuildFiles() {
    QDirIterator files(engine.sourcePath, {"Makefile", "Kbuild"}, QDir::Files, QDirIterator::Subdirectories);
    QRegularExpression assignment("^obj-\\$\\(CONFIG_(\\w+)\\)\\s*[:+]?=\\s*(.+)$", QRegularExpression::MultilineOption);
    while (files.hasNext()) {
        auto path = files.next(); auto relative = QDir(engine.sourcePath).relativeFilePath(path);
        if (relative.startsWith("tools/") || relative.startsWith("scripts/")) continue;
        auto content = read(path).replace("\\\n", " ");
        auto matches = assignment.globalMatch(content);
        while (matches.hasNext()) {
            auto match = matches.next(); auto symbol = match.captured(1);
            for (auto target : match.captured(2).split(QRegularExpression("\\s+"), Qt::SkipEmptyParts)) {
                if (target.startsWith('#')) break;
                if (!target.endsWith(".o")) continue;
                auto object = target.chopped(2); symbolObjects[symbol] << object;
                auto file = QFileInfo(path).dir().filePath(object + ".c");
                if (QFileInfo::exists(file)) symbolFiles[symbol] << QDir(engine.sourcePath).relativeFilePath(file);
                else symbolFiles[symbol] << relative;
            }
        }
    }
    for (auto it = symbolObjects.begin(); it != symbolObjects.end(); ++it) it.value().removeDuplicates();
    for (auto it = symbolFiles.begin(); it != symbolFiles.end(); ++it) it.value().removeDuplicates();
}

void Insights::scanMaintainers() {
    for (auto block : read(engine.sourcePath + "/MAINTAINERS").split("\n\n")) {
        auto lines = block.split('\n'); if (lines.isEmpty()) continue;
        QJsonObject section{{"subsystem", lines.first()}}; QJsonArray people, patterns, excludes, links;
        for (auto line : lines) {
            if (line.startsWith("M:\t") || line.startsWith("R:\t")) people.append(line.mid(3).trimmed());
            else if (line.startsWith("F:\t")) patterns.append(line.mid(3).trimmed());
            else if (line.startsWith("X:\t")) excludes.append(line.mid(3).trimmed());
            else if (line.startsWith("S:\t")) section[QStringLiteral("status")] = line.mid(3).trimmed();
            else if (line.startsWith("W:\t") || line.startsWith("T:\t") || line.startsWith("B:\t")) links.append(line.mid(3).trimmed());
        }
        if (!patterns.isEmpty()) {
            section[QStringLiteral("people")] = people; section[QStringLiteral("patterns")] = patterns;
            section[QStringLiteral("excludes")] = excludes; section[QStringLiteral("links")] = links; maintainers.append(section);
        }
    }
}

QStringList Insights::modulesFor(const QString &symbol) const { return symbolObjects.value(symbol); }

QJsonObject Insights::context(int index) const {
    if (const auto *cached = contexts.object(index)) return *cached;
    const auto &e = engine.entries()[index];
    auto modules = modulesFor(e.name); auto files = symbolFiles.value(e.name);
    if (files.isEmpty()) files << e.file;
    QJsonArray hardware, owners, notices;
    QStringList keywords;
    for (const auto &device : devices) {
        bool matches = false;
        for (const auto &module : modules) matches |= normalize(module) == normalize(device.module);
        if (matches) { hardware.append(deviceJson(device)); keywords << device.vendor << device.product; }
    }
    auto matchesFile = [this](QString pattern, const QString &file) {
        if (pattern.endsWith('/')) return file.startsWith(pattern);
        auto match = maintainerPatterns.find(pattern);
        if (match == maintainerPatterns.end()) match = maintainerPatterns.insert(pattern, pattern.toUtf8());
        return ::fnmatch(match.value().constData(), file.toUtf8().constData(), FNM_PATHNAME | FNM_NOESCAPE) == 0;
    };
    for (auto section : maintainers) {
        bool found = false;
        for (const auto &file : files) {
            bool include = false, exclude = false;
            for (const auto pattern : section[QStringLiteral("patterns")].toArray()) include |= matchesFile(pattern.toString(), file);
            for (const auto pattern : section[QStringLiteral("excludes")].toArray()) exclude |= matchesFile(pattern.toString(), file);
            found |= include && !exclude;
        }
        // The global catch-all does not identify a feature-specific owner.
        if (found && section[QStringLiteral("subsystem")].toString() != "THE REST") owners.append(section);
    }
    QString text = (e.prompt + " " + e.help()).toLower();
    for (auto word : {"deprecated", "obsolete", "experimental", "staging", "broken"}) {
        if (text.contains(word)) notices.append(QJsonObject{{"kind", QString(word)}, {"evidence", "The selected Kconfig description/help contains this term."}});
    }
    for (const auto owner : owners) {
        auto status = owner[QStringLiteral("status")].toString();
        if (status.contains("Obsolete", Qt::CaseInsensitive) || status.contains("Orphan", Qt::CaseInsensitive))
            notices.append(QJsonObject{{"kind", status}, {"evidence", "MAINTAINERS: " + owner[QStringLiteral("subsystem")].toString()}});
    }
    QJsonObject result{{"objects", QJsonArray::fromStringList(modules)}, {"source_files", QJsonArray::fromStringList(files)},
            {"devices", hardware}, {"maintainers", owners}, {"notices", notices},
            {"mapping", "Direct obj-$(CONFIG_SYMBOL) Makefile assignments matched to currently bound module names. Composite or indirect builds may not resolve."}};
    contexts.insert(index, new QJsonObject(result));
    return result;
}

QJsonObject Insights::machine() const {
    QJsonArray items; for (const auto &device : devices) items.append(deviceJson(device));
    return {{"cpu", cpu}, {"memory", memory}, {"running_kernel", runningKernel}, {"root_filesystem", rootFs},
            {"devices", items}, {"installed_packages", QJsonArray::fromStringList(installed)},
            {"package_inventory", installed.isEmpty() ? "unavailable" : "local package metadata"}};
}

QJsonArray Insights::suggestions(const QString &workload) const {
    QJsonArray suggestions;
    auto add = [&](QString symbol, QString target, QString title, QString reason, QString security,
                   QString performance, QString utility, QString helpfulness, QString reference, QString confidence) {
        int index = engine.find(symbol); if (index < 0) return;
        QString val = engine.value(index);
        if (val == target) return;
        suggestions.append(QJsonObject{{"symbol", symbol}, {"current", val}, {"proposed", target}, {"title", title},
            {"reason", reason}, {"security", security}, {"performance", performance}, {"utility", utility},
            {"helpfulness", helpfulness}, {"reference", reference}, {"confidence", confidence}, {"workload", workload},
            {"can_apply", engine.allowed(index).contains(target)}});
    };
    const QString protection = "https://docs.kernel.org/security/self-protection.html";
    add("STACKPROTECTOR_STRONG", "y", "Strengthen kernel stack corruption detection",
        "This target config does not enable the strong stack protector. The kernel's self-protection documentation describes stack canaries as a defense against stack overwrites.",
        "Detects some stack corruption before a function returns; defense in depth, not a complete exploit barrier.",
        "Adds checking code. Measure overhead on your workload; no performance gain is claimed.",
        "Applies to general kernel robustness.", "Relevant across all workload profiles.", protection, "Documented mechanism; workload cost unmeasured");
    add("VMAP_STACK", "y", "Use guarded virtual kernel stacks",
        "The target config does not enable VMAP_STACK. Guard pages make stack overflows fail promptly when this architecture supports the feature.",
        "Limits silent kernel stack overflow corruption.", "Changes stack allocation; measure workload overhead.",
        "Improves diagnosis of kernel stack overflows.", "Relevant across workload profiles.", "https://docs.kernel.org/mm/vmalloced-kernel-stacks.html", "Documented mechanism");
    if (workload == "Desktop" || workload == "Gaming / low latency") {
        add("PREEMPT_DYNAMIC", "y", "Keep the preemption mode selectable",
            "You selected an interactive workload. PREEMPT_DYNAMIC allows supported preemption behavior to be chosen at boot; inspect the selected kernel's help and benchmark frame-time or audio latency.",
            "No direct security improvement claimed.", "Latency and throughput tradeoff depends on runtime preemption mode and workload.",
            "Allows changing supported preemption mode without rebuilding.", "Useful for comparing desktop latency against throughput.",
            "kernel/Kconfig.preempt", "Workload-based recommendation; benchmark required");
    }
    bool pressureConsumer = false;
    for (auto package : installed) pressureConsumer |= package.contains("earlyoom") || package.contains("systemd") || package.contains("systemd-oomd");
    if (pressureConsumer || workload == "Build workstation") {
        add("PSI", "y", "Expose CPU, memory, and I/O pressure",
            pressureConsumer ? "Local package metadata includes a pressure-management-capable userspace package; service use is not assumed. PSI also helps inspect resource stalls." : "Build workstations can saturate CPU, memory, and I/O. PSI provides stall measurements useful for finding the bottleneck.",
            "No direct security improvement claimed.", "Accounting has a cost; this adds observability, not automatic speed.",
            "Provides pressure data to administrators and compatible userspace tools.", "Useful when investigating interactive stalls under load.",
            "https://docs.kernel.org/accounting/psi.html", "Installed-package/workload evidence; service configuration unverified");
    }
    for (auto it = symbolObjects.cbegin(); it != symbolObjects.cend(); ++it) {
        int index = engine.find(it.key()); if (index < 0 || engine.value(index) != "n") continue;
        for (const auto &d : devices) {
            bool matches = false;
            for (auto module : it.value()) matches |= normalize(module) == normalize(d.module);
            if (!matches || d.module.isEmpty()) continue;
            add(it.key(), "m", "Retain the driver for " + d.product,
                "sysfs reports " + d.bus + " " + d.address + " bound to " + d.module + ". The target tree's Makefile maps this module to CONFIG_" + it.key() + ", which is disabled in the working config.",
                "Adds driver code. Retain only drivers you need; no security benefit is claimed.", "No speed improvement claimed.",
                "Preserves support for an observed device.", "High when booting this target config on the current machine. Storage drivers may need built-in or initramfs placement.",
                symbolFiles.value(it.key()).value(0), "Direct bound-module and Makefile evidence");
            break;
        }
    }
    return suggestions;
}

QJsonArray Insights::internals() const {
    QJsonArray rows;
    const QStringList names = {"LOCALVERSION", "MODULES", "MODVERSIONS", "MODULE_SIG", "MODULE_SIG_FORCE", "SYSTEM_TRUSTED_KEYS",
        "SYSTEM_REVOCATION_KEYS", "BLK_DEV_INITRD", "DEVTMPFS", "IKCONFIG", "IKCONFIG_PROC", "KALLSYMS", "DEBUG_INFO_BTF",
        "RUST", "PREEMPT", "PREEMPT_DYNAMIC", "PREEMPT_RT", "HZ", "HZ_1000", "NO_HZ_FULL", "PSI", "NUMA",
        "IOMMU_SUPPORT", "AMD_IOMMU", "INTEL_IOMMU", "SECURITY", "SECURITY_LOCKDOWN_LSM", "RANDOMIZE_BASE",
        "STACKPROTECTOR_STRONG", "HARDENED_USERCOPY", "VMAP_STACK", "DEBUG_KERNEL", "STAGING", "EXPERT", "WERROR"};
    for (auto name : names) {
        int index = engine.find(name); if (index < 0) continue;
        auto description = engine.describe(index); auto context = this->context(index);
        description[QStringLiteral("notices")] = context[QStringLiteral("notices")]; rows.append(description);
    }
    return rows;
}
