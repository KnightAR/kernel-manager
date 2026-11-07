// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <QJsonArray>
#include <QString>
#include <functional>
class EventBus;
class Session;
class Engine;
class Catalog;
class QWidget;
QJsonArray engineeringReport(const QString &workflow, const Engine &engine,
                             const Catalog &catalog);
void showEngineeringWorkbench(EventBus &bus, Session &session, QWidget *parent,
                              std::function<void(int)> inspect);
