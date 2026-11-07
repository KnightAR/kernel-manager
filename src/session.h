// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QVector>
class EventBus;
struct ViewEntry {
  QString name, prompt, path, file, value, type, coverage;
  int parent = -1, line = 0, support = 0;
  bool visible = false, treeVisible = false, editable = false;
  quint8 allowedMask = 0;
  bool allows(char value) const { return allowedMask & (value == 'n' ? 1 : value == 'm' ? 2 : value == 'y' ? 4 : 0); }
  QString nextValue() const {
    constexpr char states[] = "nmy";
    int index = value == "n" ? 0 : value == "m" ? 1 : 2;
    for (int n=1;n<=3;++n) if (allows(states[(index+n)%3])) return QString(QChar(states[(index+n)%3]));
    return value;
  }
};
// A GUI-only projection of backend events. No filesystem access or Kconfig
// calls.
class Session : public QObject {
  Q_OBJECT
public:
  explicit Session(EventBus &bus, QObject *parent = nullptr);
  int find(QString name) const;
  QVector<ViewEntry> entries;
  QJsonArray profiles, distros, patches;
  QString sourcePath, configPath, version, architecture, toolchain, catalogPath,
      saveReason;
  bool ready = false, edited = false;
  int revision = 0;
signals:
  void metadataChanged();
  void entriesChanged(int first, int count);
  void catalogChanged();
  void stateReady();

private:
  QHash<QString, int> symbols;
  QHash<QString, QString> sharedStrings;
  QString intern(const QString &text);
};
