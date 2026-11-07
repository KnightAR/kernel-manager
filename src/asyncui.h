// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>
#include <QVector>
#include <QWidget>
#include <functional>
class EventBus;
class QDialog;
class QPlainTextEdit;
class QPushButton;
class QLabel;
class QTextBrowser;
class QUrl;
QTextBrowser *linkBrowser(EventBus &bus);
quint64 openLink(EventBus &bus, const QUrl &url);
QDialog *toolDialog(QWidget *parent, const QString &title, int width,
                    int height);
void chooseFiles(EventBus &bus, QWidget *parent, const QString &title,
                 const QString &directory, bool multiple, bool directories,
                 std::function<void(const QStringList &)> selected);
QStringList strings(const QJsonArray &array);
QString html(const QString &text);
class TextPreview : public QWidget {
public:
  TextPreview(EventBus &bus, QWidget *parent = nullptr);
  void openFile(const QString &path);

private:
  void requestPage(qint64 offset);
  EventBus &bus;
  QString path, key;
  QPlainTextEdit *text;
  QLabel *status;
  QPushButton *next, *previous;
  QVector<qint64> offsets;
  int page = 0;
  qint64 following = 0;
};
