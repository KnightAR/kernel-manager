// SPDX-License-Identifier: GPL-2.0-only
#include "backend.h"
#include "catalog.h"
#include "engine.h"
#include "insights.h"
#include "workbench.h"
#include <QCoreApplication>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QProcess>
#include <QProcessEnvironment>
#include <QQueue>
#include <QRegularExpression>
#include <QSet>
#include <QSocketNotifier>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <algorithm>
#include <fcntl.h>
#include <unistd.h>

namespace {
QString textFile(const QString &path) {
  QFile f(path);
  return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll())
                                     : QString();
}
QString subsystem(const QString &file) {
  auto parts = file.split('/');
  if (parts.size() < 2) return "core";
  if (parts.first() == "drivers" && parts.size() > 2) return parts[0] + "/" + parts[1];
  return parts.first();
}
class Backend : public QObject {
public:
  Backend() {
    ::fcntl(STDIN_FILENO, F_SETFL, ::fcntl(STDIN_FILENO, F_GETFL) | O_NONBLOCK);
    notifier = new QSocketNotifier(STDIN_FILENO, QSocketNotifier::Read, this);
    connect(notifier, &QSocketNotifier::activated, this, [this] {
      char bytes[8192];
      ssize_t size;
      while ((size = ::read(STDIN_FILENO, bytes, sizeof(bytes))) > 0) {
        input.append(bytes, size);
        if (input.size() > 1024 * 1024) {
          QCoreApplication::exit(2);
          return;
        }
      }
      if (size == 0) {
        QCoreApplication::quit();
        return;
      }
      int end;
      while ((end = input.indexOf('\n')) >= 0) {
        auto line = input.left(end);
        input.remove(0, end + 1);
        auto document = QJsonDocument::fromJson(line);
        if (!document.isObject() ||
            !document[QStringLiteral("id")].isString() ||
            !document[QStringLiteral("topic")].isString() ||
            pending.size() >= 128) {
          QCoreApplication::exit(2);
          return;
        }
        pending.enqueue(document.object());
      }
      schedule();
    });
    if (!output.open(stdout, QIODevice::WriteOnly))
      QTimer::singleShot(0, this, [] { QCoreApplication::exit(2); });
  }

private:
  void send(const QString &topic, const QJsonObject &payload = {}) {
    output.write(
        QJsonDocument(
            QJsonObject{{"id", id}, {"topic", topic}, {"payload", payload}})
            .toJson(QJsonDocument::Compact) +
        '\n');
    output.flush();
  }
  void schedule() {
    if (busy || pending.isEmpty())
      return;
    busy = true;
    QTimer::singleShot(0, this, [this] {
      auto command = pending.dequeue();
      id = command[QStringLiteral("id")].toString();
      QString error;
      auto result =
          execute(command[QStringLiteral("topic")].toString(),
                  command[QStringLiteral("payload")].toObject(), error);
      send(error.isEmpty() ? "completed" : "failed",
           error.isEmpty() ? result : QJsonObject{{"error", error}});
      busy = false;
      schedule();
    });
  }
  Insights &insights() {
    if (!ins) {
      send("progress",
           {{"message",
             "Indexing hardware, driver build rules and maintainers…"}});
      ins = std::make_unique<Insights>(engine);
    }
    return *ins;
  }
  void metadata() {
    auto reason = engine.saveDisabledReason(engine.configPath);
    if (reason.isEmpty() && !engine.changed())
      reason = "There are no edits to save.";
    send("metadata", {{"source", engine.sourcePath},
                      {"config", engine.configPath},
                      {"version", engine.version},
                      {"arch", engine.architecture},
                      {"toolchain", engine.toolchain},
                      {"catalog", catalog.directory},
                      {"edited", engine.changed()},
                      {"save_reason", reason},
                      {"revision", revision}});
  }
  bool matchesFilters(int index, const QJsonObject &p) const {
    const auto &e = engine.entries()[index];
    const auto state = p[QStringLiteral("state")].toString();
    if (!state.isEmpty() && engine.value(index) != state) return false;
    const auto group = p[QStringLiteral("subsystem")].toString();
    if (!group.isEmpty() && subsystem(e.file) != group) return false;
    if (p.contains("unavailable") && !p[QStringLiteral("unavailable")].toBool() && !engine.visible(index)) return false;
    const auto distro = p[QStringLiteral("distro")].toString();
    if (!distro.isEmpty()) {
      bool supported = false;
      for (const auto &profile : catalog.profiles) if (profile.distro == distro) {
        const auto value = catalog.value(profile, e.name);
        supported |= value == "y" || value == "m";
      }
      if (!supported) return false;
    }
    return true;
  }
  void snapshot() {
    send("state-loading", {{"revision", revision}});
    metadata();
    const auto &entries = engine.entries();
    QVector<bool> visible(entries.size());
    for (int i = entries.size() - 1; i >= 0; --i) {
      visible[i] = visible[i] || engine.visible(i);
      if (visible[i] && entries[i].parent >= 0)
        visible[entries[i].parent] = true;
    }
    QJsonArray batch;
    for (int i = 0; i < entries.size(); ++i) {
      const auto &e = entries[i];
      QString flags;
      for (const auto &d : catalog.distros) {
        int bits = 0;
        for (const auto &p : catalog.profiles)
          if (p.distro == d.id) {
            auto v = catalog.value(p, e.name);
            if (v == "y")
              bits |= 1;
            if (v == "m")
              bits |= 2;
          }
        flags += QChar('0' + bits);
      }
      batch.append(QJsonObject{
          {"name", e.name},
          {"prompt", e.prompt},
          {"path", e.path},
          {"file", e.file},
          {"parent", e.parent},
          {"line", e.line},
          {"value", engine.value(i)},
          {"type", engine.type(i)},
          {"visible", engine.visible(i)},
          {"tree_visible", visible[i]},
          {"editable", engine.editable(i)},
          {"allowed", QJsonArray::fromStringList(engine.allowed(i))},
          {"coverage", flags},
          {"support", catalog.supportCount(e.name)}});
      if (batch.size() == 128 || i == entries.size() - 1) {
        send("entries", {{"offset", i + 1 - batch.size()},
                         {"items", batch},
                         {"revision", revision}});
        batch = {};
      }
    }
    send("state-ready", {{"revision", revision}});
  }
  QString workspaceReason(const QJsonObject &p, int command) {
    auto path = p[QStringLiteral("workspace")].toString();
    if (!QDir::isAbsolutePath(path))
      return "Choose an absolute workspace path.";
    if (QStandardPaths::findExecutable("bwrap").isEmpty())
      return "Bubblewrap is unavailable. Kernel commands require filesystem "
             "isolation.";
    auto root = QDir(path).absolutePath();
    auto why = engine.saveDisabledReason(
        command == 0 ? root : root + "/workspace.json");
    if (!why.isEmpty())
      return why;
    if (command == 0)
      return QFileInfo::exists(root) ? "Choose a new workspace directory; "
                                       "existing data is kept intact."
                                     : QString();
    auto record =
        QJsonDocument::fromJson(textFile(root + "/workspace.json").toUtf8())
            .object();
    if (record[QStringLiteral("format")].toString() !=
        "kernel-manager-workspace-v1")
      return "Prepare a private source workspace first.";
    if (!QStringList{"prepared", "patched", "built", "build-failed"}.contains(
            record[QStringLiteral("status")].toString()))
      return "Source preparation has not completed.";
    if (QFileInfo(root + "/source").isSymLink() ||
        QFileInfo(root + "/build").isSymLink())
      return "Source and build paths must stay inside the workspace.";
    why = engine.saveDisabledReason(root + "/source/Makefile");
    if (!why.isEmpty())
      return why;
    if (command == 1) {
      auto stack = p[QStringLiteral("patches")].toArray();
      if (stack.isEmpty())
        return "Add at least one patch to the stack.";
      if (QFileInfo::exists(root + "/source-before-patches"))
        return "Review the existing patch backup before another transaction.";
      for (const auto file : stack)
        if (!QFileInfo(file.toString()).isReadable())
          return "A selected patch cannot be read.";
    }
    if (command == 2)
      return engine.saveDisabledReason(root + "/requested.config");
    return {};
  }
  QJsonObject execute(const QString &topic, const QJsonObject &p,
                      QString &error) {
    if (topic == "initialize") {
      if (initialized) {
        error = "This backend already owns a kernel session.";
        return {};
      }
      send("progress", {{"message", "Reading distribution catalog…"}});
      if (!catalog.load(p[QStringLiteral("catalog")].toString(),
                        QString(KM_SOURCE_DIR) + "/assets", error,
                        p[QStringLiteral("arch")].toString()))
        return {};
      QJsonArray profiles, distros;
      for (const auto &profile : catalog.profiles) {
        auto row = profile.metadata;
        row[QStringLiteral("file")] = profile.file;
        profiles.append(row);
      }
      for (const auto &d : catalog.distros)
        distros.append(QJsonObject{{"id", d.id}, {"label", d.label}});
      send("catalog", {{"profiles", profiles},
                       {"distros", distros},
                       {"directory", catalog.directory}});
      QJsonArray batch;
      for (int i = 0; i < catalog.patches.size(); ++i) {
        batch.append(catalog.patches[i]);
        if (batch.size() == 16 || i == catalog.patches.size() - 1) {
          send("patches", {{"items", batch}});
          batch = {};
        }
      }
      QString config = p[QStringLiteral("config")].toString();
      if (!p[QStringLiteral("profile")].toString().isEmpty()) {
        bool found = false;
        for (const auto &profile : catalog.profiles)
          if (profile.id == p[QStringLiteral("profile")].toString()) {
            config = profile.file;
            found = true;
          }
        if (!found) {
          error = "The requested profile is unavailable for this architecture.";
          return {};
        }
      }
      send("progress",
           {{"message", "Parsing native Kconfig and resolving dependencies…"}});
      if (!engine.open(p[QStringLiteral("source")].toString(), config, error,
                       p[QStringLiteral("arch")].toString(),
                       p[QStringLiteral("toolchain")].toString()))
        return {};
      initialized = true;
      ++revision;
      snapshot();
      return {{"ready", true}};
    }
    if (topic == "open-url") {
      QUrl url(p[QStringLiteral("url")].toString());
      if (!url.isValid() ||
          !QStringList{"https", "http", "mailto"}.contains(url.scheme())) {
        error = "This link is not a web or email address.";
        return {};
      }
      QProcess launcher;
      auto env = QProcessEnvironment::systemEnvironment();
      env.insert("QT_QPA_PLATFORM", p[QStringLiteral("platform")].toString());
      launcher.setProcessEnvironment(env);
      launcher.setProgram("xdg-open");
      launcher.setArguments({url.toString()});
      if (!launcher.startDetached())
        error = "The default application could not open this link.";
      return {};
    }
    if (topic == "list-directory") {
      auto path = p[QStringLiteral("path")].toString();
      if (!QDir::isAbsolutePath(path)) {
        error = "Enter an absolute directory or file path, then press Enter.";
        return {};
      }
      QFileInfo target(path);
      QString selected;
      if (target.isFile()) {
        selected = target.absoluteFilePath();
        path = target.absolutePath();
      }
      QDir directory(path);
      if (!directory.exists() ||
          !QFileInfo(directory.absolutePath()).isReadable()) {
        error = "This folder does not exist or cannot be read.";
        return {};
      }
      auto flags = QDir::NoDotAndDotDot | QDir::Dirs;
      if (!p[QStringLiteral("directories")].toBool())
        flags |= QDir::Files;
      if (p[QStringLiteral("hidden")].toBool())
        flags |= QDir::Hidden;
      auto entries = directory.entryInfoList(
          flags, QDir::DirsFirst | QDir::Name | QDir::IgnoreCase);
      const auto query = p[QStringLiteral("query")].toString();
      entries.removeIf([&query](const QFileInfo &file) {
        return !file.fileName().contains(query, Qt::CaseInsensitive);
      });
      int page = qBound(0, p[QStringLiteral("page")].toInt(),
                        qMax(0, int(entries.size()) - 1) / 256),
          offset = page * 256;
      if (!selected.isEmpty())
        for (int i = 0; i < entries.size(); ++i)
          if (entries[i].absoluteFilePath() == selected) {
            page = i / 256;
            offset = page * 256;
            break;
          }
      QDir parent = directory;
      parent.cdUp();
      send("directory", {{"path", directory.absolutePath()},
                         {"parent", parent.absolutePath()},
                         {"selected", selected},
                         {"page", page}});
      QJsonArray batch;
      for (int i = offset; i < qMin(offset + 256, int(entries.size())); ++i) {
        const auto &file = entries[i];
        batch.append(QJsonObject{{"name", file.fileName()},
                                 {"path", file.absoluteFilePath()},
                                 {"directory", file.isDir()},
                                 {"readable", file.isReadable()}});
        if (batch.size() == 128) {
          send("directory-entries", {{"items", batch}});
          batch = {};
        }
      }
      if (!batch.isEmpty())
        send("directory-entries", {{"items", batch}});
      return {{"more", offset + 256 < entries.size()},
              {"total", entries.size()}};
    }
    if (topic == "source-permission") {
      auto source = p[QStringLiteral("source")].toString(),
           config = p[QStringLiteral("config")].toString();
      auto why = !QDir::isAbsolutePath(source)
                     ? "Choose an absolute source directory."
                 : !config.isEmpty() && !QDir::isAbsolutePath(config)
                     ? "Choose an absolute config path."
                 : !QFileInfo(source + "/Kconfig").isReadable()
                     ? "Choose readable sources containing Kconfig."
                 : !config.isEmpty() && !QFileInfo(config).isReadable()
                     ? "The selected config cannot be read."
                     : QString();
      return {{"reason", why}};
    }
    if (topic == "launch-session") {
      auto why =
          execute("source-permission", p, error)[QStringLiteral("reason")]
              .toString();
      if (!why.isEmpty()) {
        error = why;
        return {};
      }
      QStringList args{"--kernel",
                       p[QStringLiteral("source")].toString(),
                       "--arch",
                       p[QStringLiteral("arch")].toString(),
                       "--toolchain",
                       p[QStringLiteral("toolchain")].toString(),
                       "--catalog",
                       catalog.directory.isEmpty()
                           ? QString(KM_SOURCE_DIR) + "/catalog"
                           : catalog.directory};
      if (!p[QStringLiteral("config")].toString().isEmpty())
        args << "--config" << p[QStringLiteral("config")].toString();
      QProcess process;
      auto env = QProcessEnvironment::systemEnvironment();
      env.insert("QT_QPA_PLATFORM", p[QStringLiteral("platform")].toString());
      process.setProcessEnvironment(env);
      process.setProgram(QCoreApplication::applicationFilePath());
      process.setArguments(args);
      process.setWorkingDirectory(QString(KM_SOURCE_DIR));
      if (!process.startDetached())
        error = "The new source session could not start.";
      return {};
    }
    if (!initialized) {
      error = "The kernel session is not ready.";
      return {};
    }
    if (topic == "filter-options") {
      QSet<int> accepted; int total = 0;
      for (int i : engine.search(p[QStringLiteral("query")].toString())) {
        if (!matchesFilters(i, p)) continue;
        ++total;
        for (int ancestor = i; ancestor >= 0; ancestor = engine.entries()[ancestor].parent) {
          if (accepted.contains(ancestor)) break;
          accepted.insert(ancestor);
        }
      }
      auto indices = accepted.values(); std::sort(indices.begin(), indices.end());
      QJsonArray batch;
      for (int i : indices) {
        batch.append(i);
        if (batch.size() == 128) { send("menu-matches", {{"indices", batch}, {"revision", revision}}); batch = {}; }
      }
      if (!batch.isEmpty()) send("menu-matches", {{"indices", batch}, {"revision", revision}});
      return {{"total", total}, {"revision", revision}};
    }
    if (topic == "subsystems") {
      QSet<QString> names;
      for (const auto &e : engine.entries()) names.insert(subsystem(e.file));
      auto sorted = names.values(); sorted.sort();
      return {{"items", QJsonArray::fromStringList(sorted)}};
    }
    if (topic == "select" || topic == "context" || topic == "related") {
      int index = p[QStringLiteral("index")].toInt(-1);
      if (index < 0 || index >= engine.entries().size()) {
        error = "This option is not in the active kernel.";
        return {};
      }
      const bool full = topic == "select" && p[QStringLiteral("details")].toBool(true);
      if (topic == "select") {
        auto option = engine.describe(index);
        option[QStringLiteral("index")] = index;
        option[QStringLiteral("revision")] = revision;
        option[QStringLiteral("profiles")] = catalog.compare(engine.entries()[index].name);
        send("option", option);
      }
      if (topic == "context" || full)
        send("context", {{"index", index}, {"revision", revision}, {"context", insights().context(index)}});
      if (topic == "related" || full)
        send("related", {{"index", index}, {"revision", revision},
             {"patches", catalog.patchesFor(engine.entries()[index].name,engine.entries()[index].file)}});
      return {};
    }
    if (topic == "search" || topic == "outliers") {
      QVector<int> matches;
      if (topic == "search")
        matches = engine.search(p[QStringLiteral("query")].toString());
      else {
        QVector<QPair<int, int>> ranked;
        QSet<QString> seen;
        for (int i = 0; i < engine.entries().size(); ++i) {
          const auto name = engine.entries()[i].name;
          if (name.isEmpty() || seen.contains(name) || engine.value(i) != "n")
            continue;
          seen.insert(name);
          int count = catalog.supportCount(name);
          if (count)
            ranked.append({count, i});
        }
        std::stable_sort(ranked.begin(), ranked.end(),
                         [](auto a, auto b) { return a.first > b.first; });
        for (auto pair : ranked)
          matches << pair.second;
      }
      if (topic == "outliers" && !p[QStringLiteral("query")].toString().trimmed().isEmpty()) {
        const auto found = engine.search(p[QStringLiteral("query")].toString());
        const QSet<int> matching(found.cbegin(), found.cend());
        matches.removeIf([&matching](int i) { return !matching.contains(i); });
      }
      matches.removeIf([this, &p](int index) { return !matchesFilters(index, p); });
      QJsonArray batch;
      for (int i = 0; i < matches.size(); ++i) {
        batch.append(matches[i]);
        if (batch.size() == 128 || i == matches.size() - 1) {
          send("matches", {{"indices", batch},
                           {"total", matches.size()},
                           {"revision", revision}});
          batch = {};
        }
      }
      return {{"total", matches.size()}, {"revision", revision}};
    }
    if (topic == "edit" || topic == "load") {
      bool ok =
          topic == "load"
              ? engine.load(p[QStringLiteral("path")].toString(), error)
              : engine.setValue(p[QStringLiteral("index")].toInt(-1),
                                p[QStringLiteral("value")].toString(), error);
      if (ok) {
        ++revision;
        snapshot();
      }
      return {};
    }
    if (topic == "save-permission") {
      auto path = p[QStringLiteral("path")].toString();
      auto why = engine.saveDisabledReason(path);
      return {{"path", path},
              {"reason", why},
              {"writable", why.isEmpty()},
              {"exists", QFileInfo::exists(path)}};
    }
    if (topic == "save") {
      engine.save(p[QStringLiteral("path")].toString(), error,
                  p[QStringLiteral("minimal")].toBool());
      metadata();
      return {{"path", p[QStringLiteral("path")]}};
    }
    if (topic == "workspace-permissions") {
      QJsonArray reasons;
      for (int i = 0; i < 3; ++i)
        reasons.append(workspaceReason(p, i));
      return {{"reasons", reasons}};
    }
    if (topic == "prepare-job") {
      int command = p[QStringLiteral("command")].toInt(-1);
      if (command < 0 || command > 2) {
        error = "Unknown workspace operation.";
        return {};
      }
      error = workspaceReason(p, command);
      if (!error.isEmpty())
        return {};
      auto root = p[QStringLiteral("workspace")].toString();
      QStringList args{QString(KM_SOURCE_DIR) + "/tools/kernel_work.py"};
      if (command == 0)
        args << "prepare" << "--source" << engine.sourcePath << "--workspace"
             << root;
      if (command == 1) {
        args << "patch" << "--workspace" << root;
        for (const auto file : p[QStringLiteral("patches")].toArray())
          args << "--patch" << file.toString();
      }
      if (command == 2) {
        auto config = root + "/requested.config";
        if (!engine.save(config, error, false, false))
          return {};
        args << "build" << "--workspace" << root << "--config" << config
             << "--target" << p[QStringLiteral("target")].toString() << "--jobs"
             << QString::number(p[QStringLiteral("jobs")].toInt()) << "--arch"
             << engine.architecture << "--toolchain" << engine.toolchain;
      }
      return {{"program", "python3"},
              {"arguments", QJsonArray::fromStringList(args)},
              {"directory", QString(KM_SOURCE_DIR)}};
    }
    if (topic == "machine")
      return insights().machine();
    if (topic == "suggestions")
      return {{"items", insights().suggestions(
                            p[QStringLiteral("workload")].toString())},
              {"revision", revision}};
    if (topic == "workbench") {
      auto report = engineeringReport(p[QStringLiteral("workflow")].toString(),
                                      engine, catalog);
      // Per-profile evidence is available through selection; avoid sending
      // the same complete catalog comparison with every overview row.
      for (int i = 0; i < report.size(); ++i) {
        auto row = report[i].toObject();
        row.remove("profiles");
        report[i] = row;
      }
      return {{"items", report}, {"revision", revision}};
    }
    if (topic == "internals") {
      QJsonArray lifecycle, runtime;
      QRegularExpression word(
          "\\b(deprecated|obsolete|experimental|staging|broken)\\b",
          QRegularExpression::CaseInsensitiveOption);
      for (int i = 0; i < engine.entries().size(); ++i) {
        const auto &e = engine.entries()[i];
        auto match = word.match(e.prompt + " " + e.help());
        if (!match.hasMatch() || e.name.isEmpty())
          continue;
        lifecycle.append(QJsonObject{{"index", i},
                                     {"symbol", e.name},
                                     {"prompt", e.prompt},
                                     {"notice", match.captured(1)},
                                     {"value", engine.value(i)}});
      }
      runtime.append(QJsonObject{
          {"name", "Running kernel taint mask"},
          {"value", textFile("/proc/sys/kernel/tainted").trimmed()}});
      QDir vulnerabilities("/sys/devices/system/cpu/vulnerabilities");
      for (const auto &name : vulnerabilities.entryList(QDir::Files))
        runtime.append(QJsonObject{
            {"name", name},
            {"value", textFile(vulnerabilities.filePath(name)).trimmed()}});
      for (const auto &name :
           QStringList{"SYSTEM_TRUSTED_KEYS", "SYSTEM_REVOCATION_KEYS",
                       "MODULE_SIG_KEY"}) {
        int i = engine.find(name);
        if (i < 0 || engine.value(i).isEmpty())
          continue;
        auto path = engine.value(i);
        runtime.append(QJsonObject{
            {"name", name},
            {"value",
             path + (QFileInfo::exists(
                         QDir(engine.sourcePath).absoluteFilePath(path))
                         ? " · present in source tree"
                         : " · external/generated input not found in source "
                           "tree")}});
      }
      for (int offset = 0; offset < lifecycle.size(); offset += 128) {
        QJsonArray batch;
        for (int n = offset; n < qMin(offset + 128, int(lifecycle.size())); ++n)
          batch.append(lifecycle[n]);
        send("lifecycle", {{"items", batch}});
      }
      return {{"settings", insights().internals()}, {"runtime", runtime}};
    }
    if (topic == "source-index") {
      if (!sourceIndexed) {
        for (const auto &dir :
             QStringList{"arch/arm64/boot/dts/apple",
                         "Documentation/devicetree/bindings",
                         "Documentation/ABI", "Documentation/gpu",
                         "Documentation/firmware-guide", "Documentation/power",
                         "Documentation/driver-api", "Documentation/kbuild"}) {
          QDirIterator it(engine.sourcePath + "/" + dir,
                          QDir::Files | QDir::NoSymLinks,
                          QDirIterator::Subdirectories);
          while (it.hasNext())
            sourcePaths << it.next().mid(engine.sourcePath.size() + 1);
        }
        sourcePaths.sort();
        sourceIndexed = true;
      }
      QJsonArray batch;
      for (const auto &path : sourcePaths) {
        if (!path.contains(p[QStringLiteral("query")].toString(),
                           Qt::CaseInsensitive))
          continue;
        batch.append(path);
        if (batch.size() == 128) {
          send("paths", {{"items", batch}});
          batch = {};
        }
      }
      if (!batch.isEmpty())
        send("paths", {{"items", batch}});
      return {};
    }
    if (topic == "patch-search") {
      auto tokens =
          p[QStringLiteral("query")].toString().split(' ', Qt::SkipEmptyParts);
      QJsonArray batch;
      for (int i = 0; i < catalog.patches.size(); ++i) {
        auto patch = catalog.patches[i].toObject();
        auto text = QString::fromUtf8(
            QJsonDocument(patch).toJson(QJsonDocument::Compact));
        bool match = true;
        for (const auto &token : tokens)
          match &= text.contains(token, Qt::CaseInsensitive);
        if (!match)
          continue;
        batch.append(i);
        if (batch.size() == 128) {
          send("patch-matches", {{"indices", batch}});
          batch = {};
        }
      }
      if (!batch.isEmpty())
        send("patch-matches", {{"indices", batch}});
      return {};
    }
    if (topic == "read-file") {
      auto path =
          QFileInfo(p[QStringLiteral("path")].toString()).canonicalFilePath();
      const auto catalogRoot = QFileInfo(catalog.directory).canonicalFilePath();
      if (path.isEmpty() ||
          (!path.startsWith(engine.sourcePath + "/") &&
           (catalogRoot.isEmpty() || !path.startsWith(catalogRoot + "/")))) {
        error = "File is outside the active source and catalog.";
        return {};
      }
      QFile file(path);
      if (!file.open(QIODevice::ReadOnly)) {
        error = file.errorString();
        return {};
      }
      auto offset = p[QStringLiteral("offset")].toInteger();
      if (offset < 0 || !file.seek(offset)) {
        error = "Invalid source preview offset.";
        return {};
      }
      auto bytes = file.read(32768);
      // Preserve a split UTF-8 character for the next page.
      if (!file.atEnd() && !bytes.isEmpty()) {
        int start = bytes.size() - 1;
        while (start > 0 && (uchar(bytes[start]) & 0xc0) == 0x80)
          --start;
        auto lead = uchar(bytes[start]);
        int length = lead < 0x80 ? 1 : lead < 0xe0 ? 2 : lead < 0xf0 ? 3 : 4;
        if (start + length > bytes.size())
          bytes.truncate(start);
      }
      return {{"text", QString::fromUtf8(bytes)},
              {"next", offset + bytes.size()},
              {"size", file.size()},
              {"more", offset + bytes.size() < file.size()}};
    }
    if (topic == "attribution-plan") {
      int i = p[QStringLiteral("index")].toInt(-1);
      if (i < 0 || i >= engine.entries().size()) {
        error = "Unknown option.";
        return {};
      }
      const auto &e = engine.entries()[i];
      auto lines = textFile(engine.sourcePath + "/" + e.file).split('\n');
      int end = qMin(e.line + 1, lines.size());
      while (end < lines.size() &&
             !QRegularExpression("^(?:menuconfig|config|choice|endchoice|menu|"
                                 "endmenu|comment|source)\\b")
                  .match(lines[end])
                  .hasMatch())
        ++end;
      QStringList args{
          "-C",
          engine.sourcePath,
          "blame",
          "--line-porcelain",
          "-L",
          QString::number(e.line) + "," + QString::number(qMax(e.line, end)),
          "--",
          e.file};
      return {{"program", "git"},
              {"arguments", QJsonArray::fromStringList(args)},
              {"directory", engine.sourcePath},
              {"file", e.file},
              {"version", engine.version}};
    }
    error = "Unknown backend request: " + topic;
    return {};
  }
  QSocketNotifier *notifier;
  QFile output;
  QByteArray input;
  QQueue<QJsonObject> pending;
  QString id;
  bool busy = false, initialized = false;
  int revision = 0;
  QStringList sourcePaths;
  bool sourceIndexed = false;
  Engine engine;
  Catalog catalog;
  std::unique_ptr<Insights> ins;
};
} // namespace
int runBackend() {
  Backend backend;
  return QCoreApplication::exec();
}
