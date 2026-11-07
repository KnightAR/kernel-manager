// SPDX-License-Identifier: GPL-2.0-only
#include "engine.h"
#include "lkc.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QProcess>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <algorithm>
#include <fcntl.h>
#include <unistd.h>
#include <sys/statvfs.h>

namespace {
QString readText(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return QString::fromUtf8(file.readAll());
}
QString commandLine(const QString &program, const QStringList &args) {
    QProcess process;
    auto words = QProcess::splitCommand(program);
    if (words.isEmpty()) return {};
    auto executable = words.takeFirst(); process.start(executable, words + args);
    if (!process.waitForFinished(10000)) { process.kill(); process.waitForFinished(); return {}; }
    return QString::fromUtf8(process.readAllStandardOutput()).section('\n', 0, 0);
}
void envDefault(const char *name, const char *value) {
    if (qEnvironmentVariableIsEmpty(name)) qputenv(name, value);
}
}

Engine::Engine() = default;
Engine::~Engine() = default;

bool Engine::open(const QString &source, const QString &config, QString &error, const QString &arch, const QString &compiler) {
    architecture = arch; toolchain = compiler;
    if (arch != "x86_64" && arch != "arm64") { error = "Supported target architectures: x86_64, arm64."; return false; }
    sourcePath = QFileInfo(source).canonicalFilePath();
    if (!QFileInfo::exists(sourcePath + "/Kconfig")) {
        error = "Choose a complete Linux source tree containing Kconfig.";
        return false;
    }
    // Kconfig runs toolchain probes. Keep all their temporary files outside the
    // source tree, like an out-of-tree kernel build does.
    scratch = std::make_unique<QTemporaryDir>(QDir::tempPath() + "/kernel-manager-kconfig-XXXXXX");
    if (!scratch->isValid() || !QDir::setCurrent(scratch->path())) {
        error = "Cannot create the Kconfig scratch directory."; return false;
    }
    qputenv("srctree", sourcePath.toUtf8());
    qputenv("ARCH", arch.toUtf8());
    qputenv("SRCARCH", arch == "arm64" ? "arm64" : "x86");
    qunsetenv("CONFIG_");
    envDefault("CC", "gcc"); envDefault("LD", "ld");
    envDefault("HOSTCC", "gcc"); envDefault("HOSTCXX", "g++");
    envDefault("RUSTC", "rustc"); envDefault("BINDGEN", "bindgen");
    envDefault("OBJCOPY", "objcopy"); envDefault("PAHOLE", "pahole");
    envDefault("AR", "ar"); envDefault("NM", "nm"); envDefault("PYTHON3", "python3");
    if (compiler == "llvm") {
        qputenv("LLVM", "1"); qputenv("LLVM_IAS", "1");
        qputenv("CC", "clang");
        qputenv("CLANG_FLAGS", arch == "arm64" ? "--target=aarch64-linux-gnu -fintegrated-as" : "-fintegrated-as");
        qputenv("LD", "ld.lld"); qputenv("AR", "llvm-ar"); qputenv("NM", "llvm-nm"); qputenv("OBJCOPY", "llvm-objcopy");
    } else {
        qunsetenv("LLVM"); qunsetenv("LLVM_IAS"); qunsetenv("CLANG_FLAGS");
        auto prefix = arch == "arm64" ? qEnvironmentVariable("CROSS_COMPILE", "aarch64-linux-gnu-").toUtf8() : QByteArray();
        qputenv("CC", prefix + "gcc"); qputenv("LD", prefix + "ld"); qputenv("AR", prefix + "ar");
        qputenv("NM", prefix + "nm"); qputenv("OBJCOPY", prefix + "objcopy");
    }
    qputenv("PAHOLE_VERSION", commandLine(sourcePath + "/scripts/pahole-version.sh", {qEnvironmentVariable("PAHOLE")}).toUtf8());
    qputenv("CC_VERSION_TEXT", commandLine(qEnvironmentVariable("CC"), {"--version"}).toUtf8());
    if (qEnvironmentVariableIsEmpty("CC_VERSION_TEXT")) {
        error = "Target compiler is unavailable: " + qEnvironmentVariable("CC") + ". Choose LLVM for cross-architecture configuration, or provide CROSS_COMPILE.";
        return false;
    }
    qputenv("RUSTC_VERSION_TEXT", commandLine(qEnvironmentVariable("RUSTC"), {"--version"}).toUtf8());
    auto makefile = readText(sourcePath + "/Makefile");
    auto field = [&](const QString &name) {
        return QRegularExpression("^" + name + " = (.*)$", QRegularExpression::MultilineOption).match(makefile).captured(1).trimmed();
    };
    version = field("VERSION") + "." + field("PATCHLEVEL") + "." + field("SUBLEVEL") + field("EXTRAVERSION");
    qputenv("KERNELVERSION", version.toUtf8());
    conf_set_message_callback([](const char *) {});
    conf_parse("Kconfig");
    if (conf_errors()) { error = "Kconfig reported errors; see terminal diagnostics."; return false; }
    QString initial = config.isEmpty() ? sourcePath + "/.config" : config;
    if (config.isEmpty() && (!QFileInfo::exists(initial) || (arch == "arm64" && !readText(initial).contains("CONFIG_ARM64=y"))))
        initial = sourcePath + (arch == "arm64" ? "/arch/arm64/configs/defconfig" : "/arch/x86/configs/x86_64_defconfig");
    if (!load(initial, error)) return false;
    collect(rootmenu.list, -1, {});
    return true;
}

QString Entry::help() const { return QString::fromUtf8(node->help); }

void Engine::collect(menu *node, int parent, const QString &path) {
    for (auto *m = node; m; m = m->next) {
        QString prompt = QString::fromUtf8(menu_get_prompt(m));
        QString name = m->sym && m->sym->name ? QString::fromUtf8(m->sym->name) : QString();
        int nextParent = parent;
        QString nextPath = path;
        if (!prompt.isEmpty() || !name.isEmpty()) {
            Entry entry;
            entry.node = m; entry.name = name; entry.prompt = prompt.isEmpty() ? name : prompt;
            entry.path = path;
            entry.file = QString::fromUtf8(m->filename); entry.line = m->lineno; entry.parent = parent;
            entry.searchText = (name + " " + prompt + " " + path + " ").toLower().toUtf8();
            entry.helpOffset = entry.searchText.size();
            entry.searchText += entry.help().toLower().toUtf8();
            nextParent = items.size();
            items.append(entry);
            if (!prompt.isEmpty()) nextPath = path.isEmpty() ? prompt : path + " / " + prompt;
        }
        if (m->list) collect(m->list, nextParent, nextPath);
    }
}

bool Engine::load(const QString &path, QString &error) {
    const auto text = readText(path);
    if ((architecture == "x86_64" && text.contains("\nCONFIG_ARM64=y")) || (architecture == "arm64" && text.contains("\nCONFIG_X86_64=y"))) {
        error = "This config targets a different architecture. Open a session for the matching target first."; return false;
    }
    if (!QFileInfo(path).isFile() || conf_read(path.toLocal8Bit().constData())) {
        error = "Could not read config: " + path; return false;
    }
    configPath = QFileInfo(path).absoluteFilePath();
    inputPath = QFileInfo(path).canonicalFilePath();
    edited = false;
    qputenv("KCONFIG_CONFIG", configPath.toUtf8());
    return true;
}

QString Engine::saveDisabledReason(const QString &path) const {
    if (path.trimmed().isEmpty()) return "Choose an export filename.";
    if (!QDir::isAbsolutePath(path)) return "Use an absolute destination path, or choose a folder.";
    auto absolute = QFileInfo(path).absoluteFilePath();
    // Resolve existing ancestors too: a new filename inside a symlinked directory
    // must receive the same protection as an existing source file.
    auto ancestor = absolute; QStringList suffix;
    while (!QFileInfo::exists(ancestor) && !QFileInfo(ancestor).isSymLink()) {
        suffix.prepend(QFileInfo(ancestor).fileName()); auto parent = QFileInfo(ancestor).absolutePath();
        if (parent == ancestor) break;
        ancestor = parent;
    }
    auto resolved = QFileInfo(ancestor).canonicalFilePath();
    if (resolved.isEmpty()) return "The destination cannot be resolved. Choose an existing folder.";
    if (!suffix.isEmpty()) resolved += "/" + suffix.join('/');
    bool protectedFile = resolved == inputPath;
    for (const auto &root : QStringList{sourcePath, "/usr/src", "/boot", "/lib/modules", "/usr/lib/modules", QString(KM_SOURCE_DIR) + "/catalog"}) {
        auto canonical = QFileInfo(root).canonicalFilePath(); if (canonical.isEmpty()) canonical = root;
        protectedFile |= resolved == canonical || resolved.startsWith(canonical + "/");
    }
    if (protectedFile) {
        return "This location is a protected kernel or imported config. Choose a separate working file.";
    }
    QFileInfo destination(absolute); auto folder = destination.absolutePath();
    if (!QFileInfo(folder).isDir()) return "The destination folder does not exist. Choose an existing folder.";
    if (destination.isSymLink()) return "The destination is a symbolic link. Choose a direct working-file path.";
    if (destination.exists() && !destination.isFile()) return "The destination is not a regular file.";
    if (::faccessat(AT_FDCWD, QFile::encodeName(folder).constData(), W_OK | X_OK, AT_EACCESS) != 0)
        return "Your account cannot write to this folder: " + folder;
    if (destination.exists() && ::faccessat(AT_FDCWD, QFile::encodeName(absolute).constData(), W_OK, AT_EACCESS) != 0)
        return "Your account cannot write to this file: " + absolute;
    struct statvfs filesystem;
    if (::statvfs(QFile::encodeName(folder).constData(), &filesystem) == 0 && (filesystem.f_flag & ST_RDONLY))
        return "The destination filesystem is mounted read-only.";
    return {};
}

bool Engine::save(const QString &path, QString &error, bool minimal, bool updateWorkingPath) {
    error = saveDisabledReason(path);
    if (!error.isEmpty()) return false;
    auto absolute = QFileInfo(path).absoluteFilePath();
    int status = minimal ? conf_write_defconfig(absolute.toLocal8Bit().constData()) : conf_write(absolute.toLocal8Bit().constData());
    if (status) { error = "Could not write config: " + absolute; return false; }
    if (!minimal && updateWorkingPath) { configPath = absolute; qputenv("KCONFIG_CONFIG", absolute.toUtf8()); edited = false; }
    return true;
}

QString Engine::value(int index) const {
    auto *m = items[index].node;
    if (m->type == M_CHOICE) {
        auto *selected = sym_calc_choice(m);
        return selected && selected->name ? QString::fromUtf8(selected->name) : QString();
    }
    return m->sym ? QString::fromUtf8(sym_get_string_value(m->sym)) : QString();
}
QString Engine::type(int index) const {
    auto *m = items[index].node;
    if (m->type == M_CHOICE) return "choice";
    return m->sym ? QString::fromUtf8(sym_type_name(sym_get_type(m->sym))) : "menu";
}
bool Engine::visible(int index) const { return menu_is_visible(items[index].node); }
bool Engine::editable(int index) const {
    auto *s = items[index].node->sym;
    return s && visible(index) && sym_is_changeable(s);
}
QStringList Engine::allowed(int index) const {
    auto *s = items[index].node->sym;
    if (!s || !visible(index)) return {};
    sym_calc_value(s);
    QStringList result;
    if (sym_is_choice_value(s)) { result << "y"; return result; }
    if (sym_get_type(s) != S_BOOLEAN && sym_get_type(s) != S_TRISTATE) return result;
    if (sym_tristate_within_range(s, no)) result << "n";
    if (sym_tristate_within_range(s, mod)) result << "m";
    if (sym_tristate_within_range(s, yes)) result << "y";
    return result;
}

bool Engine::setValue(int index, QString val, QString &error) {
    if (index < 0 || index >= items.size()) { error = "This option is not in the active kernel."; return false; }
    auto *s = items[index].node->sym;
    if (!s || !visible(index)) { error = "This option is hidden by its Kconfig dependencies."; return false; }
    sym_calc_value(s);
    if (sym_is_choice_value(s) && val == "y") {
        choice_set_value(sym_get_choice_menu(s), s);
        edited = true;
        return true;
    }
    bool ok;
    if (sym_get_type(s) == S_BOOLEAN || sym_get_type(s) == S_TRISTATE) {
        ok = allowed(index).contains(val) && sym_set_tristate_value(s, val == "y" ? yes : val == "m" ? mod : no);
    } else {
        if (sym_get_type(s) == S_STRING && val.startsWith('"') && val.endsWith('"')) {
            val = val.mid(1, val.size() - 2);
            val.replace("\\\"", "\""); val.replace("\\\\", "\\");
        }
        ok = sym_is_changeable(s) && sym_set_string_value(s, val.toUtf8().constData());
    }
    if (!ok) error = "Kconfig rejected this value. Check the dependencies, type, and allowed range below.";
    if (ok) edited = true;
    return ok;
}

QString Engine::details(int index) const {
    auto text = str_new();
    menu_get_ext_help(items[index].node, &text);
    QString result = QString::fromUtf8(str_get(&text));
    str_free(&text);
    return result;
}

QVector<int> Engine::search(const QString &query) const {
    QString q = query.trimmed().toLower();
    q.replace("config_", "");
    struct Token { QString text; QByteArray bytes; int field; };
    QVector<Token> tokens;
    auto matches = QRegularExpression(R"((?:name:|help:|path:)?"[^"]*"|[^\s]+)").globalMatch(q);
    while (matches.hasNext()) {
        auto token = matches.next().captured(); int field = 0;
        if (token.startsWith("name:")) { field = 1; token.remove(0,5); }
        else if (token.startsWith("help:")) { field = 2; token.remove(0,5); }
        else if (token.startsWith("path:")) { field = 3; token.remove(0,5); }
        if (token.startsWith('"') && token.endsWith('"')) token = token.mid(1,token.size()-2);
        tokens.append({token,token.toUtf8(),field});
    }
    QVector<QPair<int, int>> ranked;
    for (int i = 0; i < items.size(); ++i) {
        const auto &entry = items[i];
        int score = 0;
        bool match = true;
        for (const auto &token : tokens) {
            bool found = token.field == 1 ? entry.name.contains(token.text,Qt::CaseInsensitive)
                       : token.field == 2 ? QByteArrayView(entry.searchText).sliced(entry.helpOffset).contains(token.bytes)
                       : token.field == 3 ? entry.path.contains(token.text,Qt::CaseInsensitive)
                                          : entry.searchText.contains(token.bytes);
            if (!found) { match = false; break; }
            score += entry.name.compare(token.text,Qt::CaseInsensitive) == 0 ? 1000
                   : entry.name.startsWith(token.text,Qt::CaseInsensitive) ? 350
                   : entry.name.contains(token.text,Qt::CaseInsensitive) ? 150
                   : entry.prompt.contains(token.text,Qt::CaseInsensitive) ? 80 : 10;
        }
        if (match) ranked.append({score, i});
    }
    std::stable_sort(ranked.begin(), ranked.end(), [](auto a, auto b) { return a.first > b.first; });
    QVector<int> results;
    for (auto pair : ranked) results.append(pair.second);
    return results;
}

QJsonObject Engine::describe(int index) const {
    const auto &e = items[index];
    return {{"symbol", e.name}, {"description", e.prompt}, {"help", e.help()}, {"menu", e.path},
            {"type", type(index)}, {"value", value(index)}, {"visible", visible(index)},
            {"editable", editable(index)}, {"allowed", QJsonArray::fromStringList(allowed(index))},
            {"source", e.file}, {"line", e.line}, {"relations", details(index)}};
}
int Engine::find(const QString &name) const {
    QString symbol = name;
    if (symbol.startsWith("CONFIG_")) symbol.remove(0, 7);
    for (int i = 0; i < items.size(); ++i) if (items[i].name == symbol) return i;
    return -1;
}
bool Engine::changed() const { return edited; }
