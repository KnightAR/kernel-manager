// SPDX-License-Identifier: GPL-2.0-only
#include "backend.h"
#include "catalog.h"
#include "engine.h"
#include "eventbus.h"
#include "insights.h"
#include "session.h"
#include "theme.h"
#include "window.h"
#include "workbench.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSet>
#include <QStyleFactory>
#include <QTextStream>
#include <QTimer>
#include <algorithm>

int main(int argc, char **argv) {
  bool cli = false, worker = false;
  for (int i = 1; i < argc; ++i) {
    QString arg = QString::fromLocal8Bit(argv[i]).section('=',0,0);
    if (arg == "--worker") {
      worker = true;
      cli = true;
    }
    if (arg == "--search" || arg == "--symbol" || arg == "--set" ||
        arg == "--export" || arg == "--list-profiles" || arg == "--machine" ||
        arg == "--suggestions" || arg == "--internals" ||
        arg == "--workbench" || arg == "--outliers" || arg == "--help" ||
        arg == "--version" || arg == "-h" || arg == "-v")
      cli = true;
  }
  std::unique_ptr<QCoreApplication> app;
  if (cli) app = std::make_unique<QCoreApplication>(argc, argv);
  else app = std::make_unique<QApplication>(argc, argv);
  QApplication::setApplicationName("kernel-manager");
  QApplication::setApplicationVersion("0.1.0");
  QApplication::setOrganizationName("Navi");
  if (worker)
    return runBackend();
  QCommandLineParser parser;
  parser.setApplicationDescription(
      "Native Kconfig editor with distro evidence, patch library, and local "
      "build workspaces.");
  parser.addHelpOption();
  parser.addVersionOption();
  parser.addOption({"kernel", "Linux source tree.", "path", KM_KERNEL_SOURCE});
  parser.addOption({"arch",
                    "Target architecture: x86_64 or arm64. Distro comparisons "
                    "use this architecture.",
                    "architecture", "x86_64"});
  parser.addOption({"toolchain",
                    "Toolchain for Kconfig probes: gcc or llvm (defaults to "
                    "llvm for arm64).",
                    "compiler"});
  parser.addOption({"config",
                    "Initial .config (defaults to the source tree's config).",
                    "path"});
  parser.addOption({"catalog", "Config/patch catalog directory.", "path",
                    QString(KM_SOURCE_DIR) + "/catalog"});
  parser.addOption(
      {"search", "Ranked full-text search; return JSON.", "query"});
  parser.addOption({"symbol",
                    "Describe one symbol with distro values, help, and "
                    "dependencies as JSON.",
                    "symbol"});
  parser.addOption(
      {"list-profiles", "List collected config profiles as JSON."});
  parser.addOption(
      {"machine",
       "Describe local hardware and installed package evidence as JSON."});
  parser.addOption(
      {"suggestions",
       "Explain configuration suggestions for a workload (Desktop, Gaming / "
       "low latency, Build workstation, Server / storage).",
       "workload"});
  parser.addOption(
      {"outliers", "List options disabled here and enabled by other distros, "
                   "ranked by unique-distro count."});
  parser.addOption({"workbench",
                    "Engineering option report: kernel, asahi, or valve.",
                    "workflow"});
  parser.addOption(
      {"internals", "Summarize maintainer-relevant kernel settings as JSON."});
  parser.addOption(
      {"copy-profile", "Load a collected distro config by ID.", "id"});
  parser.addOption({"set",
                    "Set SYMBOL=value using Kconfig dependencies; repeatable, "
                    "requires --export.",
                    "assignment"});
  parser.addOption(
      {"export", "Write the resolved config to this file and exit.", "path"});
  parser.process(*app);
  QTextStream out(stdout), err(stderr);
  auto json = [&](const QJsonDocument &doc) {
    out << doc.toJson(QJsonDocument::Indented);
  };
  QString catalogPath = QDir(parser.value("catalog")).absolutePath();
  QString source = QDir(parser.value("kernel")).absolutePath();
  QString config = parser.isSet("config")
                       ? QFileInfo(parser.value("config")).absoluteFilePath()
                       : QString();
  QString output = parser.isSet("export")
                       ? QFileInfo(parser.value("export")).absoluteFilePath()
                       : QString();
  QString error;
  const QString assets = QString(KM_SOURCE_DIR) + "/assets";
  QString architecture = parser.value("arch");
  QString compiler = parser.isSet("toolchain") ? parser.value("toolchain")
                     : architecture == "arm64" ? "llvm"
                                               : "gcc";
  if (compiler != "gcc" && compiler != "llvm") {
    err << "Choose gcc or llvm for --toolchain\n";
    return 2;
  }
  if (parser.isSet("set") && !parser.isSet("export")) {
    err << "--set requires --export so changes are saved explicitly.\n";
    return 2;
  }
  if (!cli) {
    QApplication::setStyle(QStyleFactory::create("Fusion"));
    applyTheme("Paper", 12);
    QApplication::setQuitOnLastWindowClosed(false);
    EventBus bus;
    Session session(bus);
    session.sourcePath = source;
    session.configPath = config;
    session.catalogPath = catalogPath;
    session.architecture = architecture;
    session.toolchain = compiler;
    Window window(bus, session, ":/assets");
    window.show();
    QObject::connect(static_cast<QApplication *>(app.get()), &QGuiApplication::lastWindowClosed, &bus,
                     &EventBus::shutdown);
    QObject::connect(&bus, &EventBus::stopped, app.get(), &QCoreApplication::quit);
    QJsonObject initial{
        {"source", source},       {"config", config},
        {"catalog", catalogPath}, {"arch", architecture},
        {"toolchain", compiler},  {"profile", parser.value("copy-profile")}};
    QTimer::singleShot(0, &bus, [&bus, initial] {
      bus.start();
      bus.request("initialize", initial);
    });
    return app->exec();
  }
  Catalog catalog;
  if (!catalog.load(catalogPath, assets, error,
                    parser.isSet("list-profiles") ? QString() : architecture)) {
    err << error << '\n';
    return 1;
  }
  if (parser.isSet("list-profiles")) {
    QJsonArray profiles;
    for (const auto &profile : catalog.profiles)
      profiles.append(profile.metadata);
    json(QJsonDocument(profiles));
    return 0;
  }
  if (parser.isSet("copy-profile")) {
    bool found = false;
    for (const auto &p : catalog.profiles)
      if (p.id == parser.value("copy-profile")) {
        config = p.file;
        found = true;
        break;
      }
    if (!found) {
      err << "Unknown config profile\n";
      return 2;
    }
  }
  Engine engine;
  if (!engine.open(source, config, error, architecture, compiler)) {
    err << error << '\n';
    return 1;
  }
  Insights insights(engine);
  for (auto assignment : parser.values("set")) {
    int split = assignment.indexOf('=');
    int index = engine.find(assignment.left(split));
    if (split < 1 || index < 0 ||
        !engine.setValue(index, assignment.mid(split + 1), error)) {
      err << "Cannot set " << assignment << ": " << error << '\n';
      return 2;
    }
  }
  if (parser.isSet("export")) {
    if (!engine.save(output, error)) {
      err << error << '\n';
      return 1;
    }
    json(QJsonDocument(
        QJsonObject{{"saved", output}, {"kernel", engine.version}}));
    return 0;
  }
  auto describe = [&](int index) {
    auto object = engine.describe(index);
    object[QStringLiteral("distros_enabled")] =
        catalog.supportCount(engine.entries()[index].name);
    object[QStringLiteral("profiles")] =
        catalog.compare(engine.entries()[index].name);
    object[QStringLiteral("patches")] = catalog.patchesFor(
        engine.entries()[index].name, engine.entries()[index].file);
    object[QStringLiteral("context")] = insights.context(index);
    return object;
  };
  if (parser.isSet("workbench")) {
    auto workflow = parser.value("workbench");
    if (!QStringList{"kernel", "asahi", "valve"}.contains(workflow)) {
      err << "Choose kernel, asahi, or valve.\n";
      return 2;
    }
    json(QJsonDocument(engineeringReport(workflow, engine, catalog)));
    return 0;
  }
  if (parser.isSet("outliers")) {
    QVector<QPair<int, int>> ranked;
    QSet<QString> seen;
    for (int i = 0; i < engine.entries().size(); ++i) {
      auto symbol = engine.entries()[i].name;
      if (symbol.isEmpty() || seen.contains(symbol) || engine.value(i) != "n")
        continue;
      seen.insert(symbol);
      int count = catalog.supportCount(symbol);
      if (count)
        ranked.append({count, i});
    }
    std::stable_sort(ranked.begin(), ranked.end(),
                     [](auto a, auto b) { return a.first > b.first; });
    QJsonArray result;
    for (auto pair : ranked) {
      auto row = engine.describe(pair.second);
      row[QStringLiteral("distros_enabled")] = pair.first;
      row[QStringLiteral("profiles")] =
          catalog.compare(engine.entries()[pair.second].name);
      result.append(row);
    }
    json(QJsonDocument(result));
    return 0;
  }
  if (parser.isSet("machine")) {
    json(QJsonDocument(insights.machine()));
    return 0;
  }
  if (parser.isSet("suggestions")) {
    json(QJsonDocument(insights.suggestions(parser.value("suggestions"))));
    return 0;
  }
  if (parser.isSet("internals")) {
    json(QJsonDocument(insights.internals()));
    return 0;
  }
  if (parser.isSet("symbol")) {
    int index = engine.find(parser.value("symbol"));
    if (index < 0) {
      err << "Symbol not present in selected kernel\n";
      return 2;
    }
    json(QJsonDocument(describe(index)));
    return 0;
  }
  if (parser.isSet("search")) {
    QJsonArray matches;
    for (int index : engine.search(parser.value("search")))
      matches.append(engine.describe(index));
    json(QJsonDocument(matches));
    return 0;
  }
  return 0;
}
