// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QQueue>
#include <QSet>
#include <functional>
class QProcess;
class QNetworkAccessManager;
class QNetworkReply;
class QTimer;

// All publications and callbacks run on the GUI event loop. Blocking kernel and
// filesystem work lives in the backend process, never behind a UI-thread mutex.
class EventBus : public QObject {
  Q_OBJECT
public:
  explicit EventBus(QObject *parent = nullptr);
  void start();
  quint64 request(const QString &topic, const QJsonObject &payload = {},
                  const QString &replaceKey = {});
  void
  listen(quint64 id, QObject *owner,
         std::function<void(const QString &, const QJsonObject &)> callback);
  quint64 runProcess(const QString &program, const QStringList &arguments,
                     const QString &directory, const QString &label);
  quint64 get(const QString &url);
  void cancel(quint64 id);
  void shutdown();
signals:
  void event(quint64 id, const QString &topic, const QJsonObject &payload);
  void backendFailed(const QString &reason);
  void stopped();

private:
  struct Request {
    quint64 id;
    QString topic, key;
    QJsonObject payload;
  };
  void dispatch();
  void scheduleDrain();
  void drain();
  void publish(quint64 id, const QString &topic, const QJsonObject &payload);
  void stopProcess(QProcess *process);
  void checkStopped();
  void failBackend(const QString &reason);
  QProcess *backend;
  QTimer *deadline;
  QHash<quint64, QProcess *> processes;
  QHash<quint64, QNetworkReply *> replies;
  QNetworkAccessManager *network;
  QQueue<Request> requests;
  QHash<QString, quint64> latest;
  QHash<quint64, QString> keys;
  QQueue<QJsonObject> publications;
  QHash<quint64, qsizetype> queuedOutput;
  QSet<quint64> truncatedOutput, cancelledProcesses;
  QByteArray input;
  QString failureReason;
  quint64 nextId = 1, active = 0;
  bool draining = false, closing = false, available = false,
       startCalled = false, preferBackend = false;
};
