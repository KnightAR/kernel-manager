// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <QColor>
#include <QIcon>
#include <QString>

QIcon uiIcon(const QString &name, const QColor &color = {});
QString featureIcon(const QString &file, const QString &symbol);
