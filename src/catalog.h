// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <QHash>
#include <QCache>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QVector>

struct Profile {
    QJsonObject metadata;
    // One byte per shared symbol: absent, n, m, y, or scalar. Only scalars
    // need strings; their backing storage is shared across profiles.
    QByteArray states;
    QHash<int, QString> scalars;
    QString id, distro, label, file;
};
struct Distro {
    QString id, label;
};
class Catalog {
public:
    bool load(const QString &directory, const QString &assets, QString &error, const QString &architecture = {});
    QJsonArray compare(const QString &symbol) const;
    int supportCount(const QString &symbol) const;
    QString tooltip(const QString &symbol) const;
    QJsonArray patchesFor(const QString &symbol, const QString &path) const;
    QString value(const Profile &profile, const QString &symbol, const QString &missing = {}) const;
    QString directory;
    QVector<Profile> profiles;
    QVector<Distro> distros;
    QJsonArray patches;
private:
    void parse(Profile &profile, const QByteArray &data);
    QString value(const Profile &profile, int symbol, const QString &missing = {}) const;
    QHash<QString, int> symbolIds;
    QHash<QString, QString> scalarValues;
    struct PatchMatch { int index; bool symbol; };
    mutable QCache<QString, QVector<PatchMatch>> patchMatches{512 * 1024};
};
