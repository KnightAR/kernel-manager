// SPDX-License-Identifier: GPL-2.0-only
#include "eventbus.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStringDecoder>
#include <QTimer>
#include <memory>
#include <signal.h>
#include <unistd.h>

EventBus::EventBus(QObject *parent)
    : QObject(parent), backend(new QProcess(this)), deadline(new QTimer(this)),
      network(new QNetworkAccessManager(this)) {
  deadline->setSingleShot(true);
  connect(deadline, &QTimer::timeout, this, [this] {
    failBackend("The kernel backend did not finish its request within two "
                "minutes. Reopen the app to start a new session.");
  });
  backend->setChildProcessModifier([] { ::setsid(); });
  connect(backend, &QProcess::started, this, [this] {
    if (closing) {
      stopProcess(backend);
      return;
    }
    available = true;
    dispatch();
  });
  connect(backend, &QProcess::readyReadStandardOutput, this, [this] {
    auto bytes = backend->readAllStandardOutput();
    if (closing || !failureReason.isEmpty())
      return;
    input += bytes;
    if (input.size() > 32 * 1024 * 1024) {
      failBackend("Backend message buffer exceeded its limit.");
      return;
    }
    scheduleDrain();
  });
  connect(backend, &QProcess::readyReadStandardError, this, [this] {
    auto text = QString::fromUtf8(backend->readAllStandardError());
    publish(active, "diagnostic", {{"text", text.left(32768)}});
  });
  connect(backend, &QProcess::errorOccurred, this,
          [this](QProcess::ProcessError error) {
            if (error == QProcess::FailedToStart ||
                error == QProcess::WriteError)
              failBackend(backend->errorString());
          });
  connect(backend, &QProcess::finished, this,
          [this](int code, QProcess::ExitStatus) {
            if (failureReason.isEmpty())
              failBackend("The kernel backend stopped (exit " +
                          QString::number(code) +
                          "). Reopen the app to start a new kernel session.");
            checkStopped();
          });
}

void EventBus::failBackend(const QString &reason) {
  deadline->stop();
  available = false;
  input.clear();
  if (failureReason.isEmpty()) {
    failureReason = reason;
    if (!closing)
      emit backendFailed(reason);
  }
  if (active) {
    publish(active, "failed", {{"error", failureReason}});
    active = 0;
  }
  while (!requests.isEmpty()) {
    auto r = requests.dequeue();
    publish(r.id, "failed", {{"error", failureReason}});
  }
  stopProcess(backend);
  checkStopped();
}

void EventBus::start() {
  if (startCalled || closing)
    return;
  startCalled = true;
  auto env = QProcessEnvironment::systemEnvironment();
  env.insert("QT_QPA_PLATFORM", "offscreen");
  backend->setProcessEnvironment(env);
  backend->start(QCoreApplication::applicationFilePath(), {"--worker"});
}

quint64 EventBus::request(const QString &topic, const QJsonObject &payload,
                          const QString &key) {
  quint64 id = nextId++;
  if (closing) {
    publish(id, "failed", {{"error", "Application is closing."}});
    return id;
  }
  if (!failureReason.isEmpty()) {
    publish(id, "failed", {{"error", failureReason}});
    return id;
  }
  if (!key.isEmpty()) {
    latest[key] = id;
    keys[id] = key;
    for (int i = requests.size() - 1; i >= 0; --i)
      if (requests[i].key == key) {
        auto stale = requests.takeAt(i);
        keys.remove(stale.id);
        publish(stale.id, "cancelled", {});
      }
  }
  if (requests.size() >= 128) {
    publish(
        id, "failed",
        {{"error",
          "The request queue is full. Let pending work finish or cancel it."}});
    return id;
  }
  requests.enqueue({id, topic, key, payload});
  QTimer::singleShot(0, this, [this] { dispatch(); });
  return id;
}

void EventBus::dispatch() {
  if (!available || closing || active || requests.isEmpty())
    return;
  auto r = requests.dequeue();
  active = r.id;
  deadline->start(120000);
  QJsonObject packet{{"id", QString::number(r.id)},
                     {"topic", r.topic},
                     {"payload", r.payload}};
  backend->write(QJsonDocument(packet).toJson(QJsonDocument::Compact) + '\n');
  publish(r.id, "started", {{"operation", r.topic}});
}

void EventBus::listen(
    quint64 id, QObject *owner,
    std::function<void(const QString &, const QJsonObject &)> callback) {
  auto connection = std::make_shared<QMetaObject::Connection>();
  auto lifetime = std::make_shared<QMetaObject::Connection>();
  *lifetime = connect(owner, &QObject::destroyed, this, [this, id] {
    // Closing a view cancels its replaceable reads and child jobs. Explicit
    // config mutations already accepted by the backend remain serialized.
    if (keys.contains(id) || processes.contains(id) || replies.contains(id))
      cancel(id);
  });
  *connection = connect(
      this, &EventBus::event, owner,
      [id, callback = std::move(callback), connection, lifetime](
          quint64 received, const QString &topic, const QJsonObject &payload) {
        if (received != id)
          return;
        const bool terminal =
            topic == "completed" || topic == "failed" || topic == "cancelled";
        if (terminal) {
          QObject::disconnect(*lifetime);
          QObject::disconnect(*connection);
        }
        callback(topic, payload);
      });
}

void EventBus::publish(quint64 id, const QString &topic,
                       const QJsonObject &payload) {
  if (topic == "diagnostic") {
    for (auto &pending : publications) {
      if (pending[QStringLiteral("topic")] == topic &&
          pending[QStringLiteral("id")].toString().toULongLong() == id) {
        pending[QStringLiteral("payload")] = payload;
        return;
      }
    }
  }
  if (topic == "output") {
    // Keep UI output bounded even when a producer outruns rendering. The
    // worker owns full build logs; structured consumers reject truncation.
    constexpr qsizetype limit = 1024 * 1024;
    const auto size = payload[QStringLiteral("text")].toString().size();
    if (queuedOutput.value(id) + size > limit) {
      for (qsizetype i = 0;
           i < publications.size() && queuedOutput.value(id) + size > limit;) {
        const auto &p = publications[i];
        if (p[QStringLiteral("id")].toString().toULongLong() == id &&
            p[QStringLiteral("topic")] == "output") {
          queuedOutput[id] -= p[QStringLiteral("payload")]
                                  .toObject()[QStringLiteral("text")]
                                  .toString()
                                  .size();
          publications.removeAt(i);
        } else
          ++i;
      }
      if (!truncatedOutput.contains(id)) {
        truncatedOutput.insert(id);
        publications.enqueue({{"id", QString::number(id)},
                              {"topic", "output-truncated"},
                              {"payload", QJsonObject{}}});
      }
    }
    queuedOutput[id] += size;
  }
  QJsonObject packet{
      {"id", QString::number(id)}, {"topic", topic}, {"payload", payload}};
  if (topic == "started")
    publications.prepend(packet);
  else
    publications.enqueue(packet);
  scheduleDrain();
}
void EventBus::scheduleDrain() {
  if (draining)
    return;
  draining = true;
  QTimer::singleShot(0, this, [this] { drain(); });
}
void EventBus::drain() {
  // Bound delivery work per event-loop turn, including snapshot ingestion.
  QElapsedTimer time;
  time.start();
  while (time.elapsed() < 4) {
    QJsonObject packet;
    const bool backendReady = input.contains('\n');
    if (!publications.isEmpty() &&
        (!backendReady || !preferBackend ||
         publications.head()[QStringLiteral("topic")] == "started"))
      packet = publications.dequeue();
    else {
      int newline = input.indexOf('\n');
      if (newline < 0)
        break;
      auto line = input.left(newline);
      input.remove(0, newline + 1);
      QJsonParseError error;
      auto doc = QJsonDocument::fromJson(line, &error);
      if (error.error != QJsonParseError::NoError || !doc.isObject()) {
        failBackend("Invalid backend event stream.");
        continue;
      }
      packet = doc.object();
    }
    preferBackend = !preferBackend;
    quint64 id = packet[QStringLiteral("id")].toString().toULongLong();
    auto topic = packet[QStringLiteral("topic")].toString();
    if (topic == "output")
      queuedOutput[id] -= packet[QStringLiteral("payload")]
                              .toObject()[QStringLiteral("text")]
                              .toString()
                              .size();
    bool terminal =
        topic == "completed" || topic == "failed" || topic == "cancelled";
    if (terminal) {
      auto payload = packet[QStringLiteral("payload")].toObject();
      if (truncatedOutput.remove(id))
        payload[QStringLiteral("output_truncated")] = true;
      if (cancelledProcesses.remove(id)) {
        topic = "cancelled";
        payload[QStringLiteral("error")] = "Job cancelled.";
      }
      packet[QStringLiteral("payload")] = payload;
      queuedOutput.remove(id);
    }
    auto key = keys.value(id);
    bool stale = !key.isEmpty() && latest.value(key) != id;
    if (!stale)
      emit event(id, topic, packet[QStringLiteral("payload")].toObject());
    else if (terminal)
      emit event(id, "cancelled", {});
    if (terminal) {
      keys.remove(id);
      if (!key.isEmpty() && latest.value(key) == id)
        latest.remove(key);
      if (id == active) {
        deadline->stop();
        active = 0;
        dispatch();
      }
    }
  }
  draining = false;
  if (!publications.isEmpty() || input.contains('\n'))
    scheduleDrain();
}

quint64 EventBus::runProcess(const QString &program,
                             const QStringList &arguments,
                             const QString &directory, const QString &label) {
  quint64 id = nextId++;
  if (processes.size() >= 8) {
    publish(id, "failed",
            {{"error", "Eight jobs are already running. Finish or cancel one "
                       "before starting another."}});
    return id;
  }
  auto *process = new QProcess(this);
  processes[id] = process;
  process->setChildProcessModifier([] { ::setsid(); });
  process->setWorkingDirectory(directory);
  process->setProcessChannelMode(QProcess::MergedChannels);
  connect(process, &QProcess::started, this, [this, process, id, label] {
    publish(id, "started", {{"operation", label}});
    if (closing || cancelledProcesses.contains(id))
      stopProcess(process);
  });
  auto decoder = std::make_shared<QStringDecoder>(QStringDecoder::Utf8);
  connect(process, &QProcess::readyReadStandardOutput, this,
          [this, process, id, decoder] {
            auto bytes = process->readAllStandardOutput();
            for (int offset = 0; offset < bytes.size(); offset += 16384)
              publish(id, "output",
                      {{"text", QString((*decoder)(QByteArrayView(bytes).sliced(
                                    offset, qMin(qsizetype(16384),
                                                 bytes.size() - offset))))}});
          });
  auto finish = [this, process, id](int code, const QString &error) {
    if (!processes.remove(id))
      return;
    publish(id, code == 0 ? "completed" : "failed",
            {{"exit_code", code}, {"error", error}});
    process->deleteLater();
    checkStopped();
  };
  connect(process, &QProcess::finished, this,
          [finish](int code, QProcess::ExitStatus) {
            finish(code,
                   code ? "Process exited with code " + QString::number(code)
                        : QString());
          });
  connect(process, &QProcess::errorOccurred, this,
          [process, finish](QProcess::ProcessError error) {
            if (error == QProcess::FailedToStart)
              finish(-1, process->errorString());
          });
  if (closing) {
    finish(-1, "Application is closing.");
    return id;
  }
  process->start(program, arguments);
  return id;
}
void EventBus::stopProcess(QProcess *process) {
  if (process->state() == QProcess::NotRunning)
    return;
  const auto pid = process->processId();
  if (pid > 0)
    ::kill(-pid, SIGTERM);
  else
    process->terminate();
  QPointer<QProcess> guard(process);
  QTimer::singleShot(3000, this, [guard, pid] {
    if (guard && guard->state() != QProcess::NotRunning) {
      if (pid > 0)
        ::kill(-pid, SIGKILL);
      else
        guard->kill();
    }
  });
}
void EventBus::cancel(quint64 id) {
  for (int i = 0; i < requests.size(); ++i)
    if (requests[i].id == id) {
      requests.removeAt(i);
      publish(id, "cancelled", {});
      return;
    }
  if (auto *reply = replies.value(id))
    reply->abort();
  else if (auto *process = processes.value(id)) {
    cancelledProcesses.insert(id);
    stopProcess(process);
  }
  // Read requests already in Kconfig cannot be preempted safely. Their replies
  // are invalidated; kernel state remains owned by the one backend actor.
  else if (id == active) {
    auto key = keys.value(id);
    if (key.isEmpty())
      key = "cancelled:" + QString::number(id);
    keys[id] = key;
    latest[key] = 0;
  }
}
void EventBus::shutdown() {
  closing = true;
  requests.clear();
  stopProcess(backend);
  for (auto *process : processes)
    stopProcess(process);
  const auto pendingReplies = replies.values();
  for (auto *reply : pendingReplies)
    reply->abort();
  checkStopped();
}
void EventBus::checkStopped() {
  if (closing && backend->state() == QProcess::NotRunning &&
      processes.isEmpty() && replies.isEmpty())
    QTimer::singleShot(0, this, [this] { emit stopped(); });
}

quint64 EventBus::get(const QString &url) {
  quint64 id = nextId++;
  if (closing) {
    publish(id, "failed", {{"error", "Application is closing."}});
    return id;
  }
  QNetworkRequest request{QUrl(url)};
  request.setHeader(QNetworkRequest::UserAgentHeader, "kernel-manager/0.1");
  request.setTransferTimeout(20000);
  auto *reply = network->get(request);
  replies[id] = reply;
  publish(id, "started", {{"operation", "upstream-attribution"}});
  connect(reply, &QNetworkReply::downloadProgress, this,
          [reply](qint64 received, qint64) {
            if (received > 1024 * 1024)
              reply->abort();
          });
  connect(reply, &QNetworkReply::finished, this, [this, reply, id] {
    auto data = reply->readAll();
    publish(
        id, reply->error() == QNetworkReply::NoError ? "completed" : "failed",
        {{"body", QString::fromUtf8(data)}, {"error", reply->errorString()}});
    replies.remove(id);
    reply->deleteLater();
    checkStopped();
  });
  return id;
}
