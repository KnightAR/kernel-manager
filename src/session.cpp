// SPDX-License-Identifier: GPL-2.0-only
#include "session.h"
#include "eventbus.h"

QString Session::intern(const QString &text) {
  const auto found = sharedStrings.constFind(text);
  if (found != sharedStrings.cend()) return found.value();
  sharedStrings.insert(text,text);
  return text;
}
Session::Session(EventBus &bus, QObject *parent) : QObject(parent) {
  connect(&bus, &EventBus::backendFailed, this, [this](const QString &reason) {
    ready = false;
    saveReason = reason;
    emit metadataChanged();
  });
  connect(&bus, &EventBus::event, this,
          [this](quint64, const QString &topic, const QJsonObject &payload) {
            if (topic == "state-loading") {
              ready = false;
              saveReason = "Resolving configuration changes…";
              emit metadataChanged();
            } else if (topic == "metadata") {
              sourcePath = payload[QStringLiteral("source")].toString();
              configPath = payload[QStringLiteral("config")].toString();
              version = payload[QStringLiteral("version")].toString();
              architecture = payload[QStringLiteral("arch")].toString();
              toolchain = payload[QStringLiteral("toolchain")].toString();
              catalogPath = payload[QStringLiteral("catalog")].toString();
              saveReason = payload[QStringLiteral("save_reason")].toString();
              edited = payload[QStringLiteral("edited")].toBool();
              revision = payload[QStringLiteral("revision")].toInt();
              emit metadataChanged();
            } else if (topic == "catalog") {
              patches = {};
              profiles = payload[QStringLiteral("profiles")].toArray();
              distros = payload[QStringLiteral("distros")].toArray();
              catalogPath = payload[QStringLiteral("directory")].toString();
              emit catalogChanged();
            } else if (topic == "patches") {
              for (const auto p : payload[QStringLiteral("items")].toArray())
                patches.append(p);
            } else if (topic == "entries") {
              int start = payload[QStringLiteral("offset")].toInt();
              auto batch = payload[QStringLiteral("items")].toArray();
              if (entries.size() < start + batch.size())
                entries.resize(start + batch.size());
              for (int n = 0; n < batch.size(); ++n) {
                auto obj = batch[n].toObject();
                auto &e = entries[start + n];
                e.name = obj[QStringLiteral("name")].toString();
                e.prompt = obj[QStringLiteral("prompt")].toString();
                e.path = intern(obj[QStringLiteral("path")].toString());
                e.file = intern(obj[QStringLiteral("file")].toString());
                e.parent = obj[QStringLiteral("parent")].toInt(-1);
                e.line = obj[QStringLiteral("line")].toInt();
                e.value = obj[QStringLiteral("value")].toString();
                if (e.value == "n") e.value = QStringLiteral("n");
                else if (e.value == "m") e.value = QStringLiteral("m");
                else if (e.value == "y") e.value = QStringLiteral("y");
                e.type = intern(obj[QStringLiteral("type")].toString());
                e.visible = obj[QStringLiteral("visible")].toBool();
                e.treeVisible = obj[QStringLiteral("tree_visible")].toBool();
                e.editable = obj[QStringLiteral("editable")].toBool();
                e.coverage = obj[QStringLiteral("coverage")].toString();
                e.support = obj[QStringLiteral("support")].toInt();
                e.allowedMask = 0;
                for (const auto value :
                     obj[QStringLiteral("allowed")].toArray())
                  e.allowedMask |= value == "n" ? 1 : value == "m" ? 2 : value == "y" ? 4 : 0;
                if (!e.name.isEmpty())
                  symbols[e.name] = start + n;
              }
              emit entriesChanged(start, batch.size());
            } else if (topic == "state-ready") {
              ready = true;
              emit stateReady();
            }
          });
}
int Session::find(QString name) const {
  if (name.startsWith("CONFIG_"))
    name.remove(0, 7);
  return symbols.value(name, -1);
}
