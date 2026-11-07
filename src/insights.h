// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <QHash>
#include <QCache>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <QString>
#include <QVector>
class Engine;

struct Device {
    QString bus, address, vendor, product, vendorId, deviceId, driver, module, modalias;
};
class Insights {
public:
    explicit Insights(Engine &engine);
    QJsonObject context(int index) const;
    QJsonArray suggestions(const QString &workload) const;
    QJsonObject machine() const;
    QJsonArray internals() const;
    QVector<Device> devices;
private:
    void scanDevices();
    void scanBuildFiles();
    void scanMaintainers();
    QStringList modulesFor(const QString &symbol) const;
    Engine &engine;
    QHash<QString, QStringList> symbolObjects;
    QHash<QString, QStringList> symbolFiles;
    QVector<QJsonObject> maintainers;
    mutable QHash<QString, QByteArray> maintainerPatterns;
    mutable QCache<int, QJsonObject> contexts{64};
    QStringList installed;
    QString cpu, memory, rootFs, runningKernel;
};
