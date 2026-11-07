// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <QJsonObject>
#include <QString>
#include <QVector>
#include <memory>
struct menu;
struct symbol;
class QTemporaryDir;

struct Entry {
    menu *node = nullptr;
    QString name, prompt, path, file;
    QString help() const;
    int line = 0, parent = -1, helpOffset = 0;
    QByteArray searchText;
};

class Engine {
public:
    Engine();
    ~Engine();
    bool open(const QString &source, const QString &config, QString &error, const QString &arch = "x86_64", const QString &compiler = "gcc");
    bool load(const QString &path, QString &error);
    QString saveDisabledReason(const QString &path) const;
    bool save(const QString &path, QString &error, bool minimal = false, bool updateWorkingPath = true);
    bool setValue(int index, QString value, QString &error);
    QString value(int index) const;
    QString type(int index) const;
    QString details(int index) const;
    QStringList allowed(int index) const;
    bool visible(int index) const;
    bool editable(int index) const;
    QVector<int> search(const QString &query) const;
    QJsonObject describe(int index) const;
    int find(const QString &name) const;
    bool changed() const;
    const QVector<Entry> &entries() const { return items; }
    QString sourcePath, configPath, inputPath, version, architecture, toolchain;
private:
    void collect(menu *node, int parent, const QString &path);
    QVector<Entry> items;
    std::unique_ptr<QTemporaryDir> scratch;
    bool edited = false;
};
