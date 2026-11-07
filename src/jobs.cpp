// SPDX-License-Identifier: GPL-2.0-only
#include "jobs.h"
#include "asyncui.h"
#include "eventbus.h"
#include "session.h"
#include <QComboBox>
#include <QFormLayout>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QThread>
#include <QTimer>
#include <QUuid>
#include <QVBoxLayout>

JobsDialog::JobsDialog(EventBus &events, Session &state, QWidget *parent)
    : QDialog(parent), bus(events), session(state),
      permissionKey("workspace:" + QUuid::createUuid().toString()) {
  setWindowTitle("Kernel workspace · patches and builds");
  resize(1000, 760);
  setAttribute(Qt::WA_DeleteOnClose);
  setWindowModality(Qt::NonModal);
  auto *layout = new QVBoxLayout(this);
  auto *intro = new QLabel(
      "Stage a private source copy and ordered patch stack. Kernel commands "
      "keep the host filesystem read-only.\nClosing this window cancels its "
      "job asynchronously. Artifacts and full logs remain in the workspace.");
  intro->setWordWrap(true);
  layout->addWidget(intro);
  auto *form = new QFormLayout;
  workspace = new QLineEdit(QString(KM_SOURCE_DIR) + "/linux-" +
                            session.version + "-workspace");
  form->addRow("Workspace", workspace);
  target = new QComboBox;
  target->addItems(session.architecture == "arm64"
                       ? QStringList{"Image", "dtbs", "modules", "all",
                                     "bindeb-pkg", "binrpm-pkg"}
                       : QStringList{"bzImage", "modules", "all", "bindeb-pkg",
                                     "binrpm-pkg"});
  form->addRow("Build target", target);
  jobs = new QSpinBox;
  jobs->setRange(1, qMax(1, QThread::idealThreadCount()));
  jobs->setValue(qMax(1, QThread::idealThreadCount() / 2));
  form->addRow("Parallel jobs", jobs);
  layout->addLayout(form);
  auto *row = new QHBoxLayout;
  for (const auto &name :
       QStringList{"Prepare source copy", "Apply patch stack", "Compile"}) {
    auto *button = new QPushButton(name);
    int command = commands.size();
    commands << button;
    button->setEnabled(false);
    button->setToolTip("Checking workspace permissions…");
    row->addWidget(button);
    connect(button, &QPushButton::clicked, this,
            [this, command] { run(command); });
  }
  stop = new QPushButton("Stop job");
  stop->setEnabled(false);
  row->addWidget(stop);
  layout->addLayout(row);
  layout->addWidget(new QLabel("Patch stack · applied from top to bottom"));
  stack = new QListWidget;
  stack->setDragDropMode(QAbstractItemView::InternalMove);
  layout->addWidget(stack);
  auto *patchButtons = new QHBoxLayout;
  auto *add = new QPushButton("Add patch files…");
  auto *remove = new QPushButton("Remove selected");
  patchButtons->addWidget(add);
  patchButtons->addWidget(remove);
  layout->addLayout(patchButtons);
  connect(add, &QPushButton::clicked, this, [this] {
    chooseFiles(bus, this, "Choose patches", session.catalogPath + "/patches",
                true, false,
                [this](const QStringList &files) { stack->addItems(files); });
  });
  connect(remove, &QPushButton::clicked, this,
          [this] { delete stack->takeItem(stack->currentRow()); });
  log = new QPlainTextEdit;
  log->setReadOnly(true);
  log->setMaximumBlockCount(3000);
  layout->addWidget(log, 2);
  auto *timer = new QTimer(this);
  timer->setInterval(100);
  timer->setSingleShot(true);
  connect(workspace, &QLineEdit::textChanged, this, [this, timer] {
    for (auto *button : commands) {
      button->setEnabled(false);
      button->setToolTip("Checking workspace permissions…");
    }
    timer->start();
  });
  connect(timer, &QTimer::timeout, this, [this] { refreshPermissions(); });
  connect(stack->model(), &QAbstractItemModel::rowsInserted, this,
          [this] { refreshPermissions(); });
  connect(stack->model(), &QAbstractItemModel::rowsRemoved, this,
          [this] { refreshPermissions(); });
  connect(stack->model(), &QAbstractItemModel::rowsMoved, this,
          [this] { refreshPermissions(); });
  connect(stop, &QPushButton::clicked, this, [this] {
    if (job)
      bus.cancel(job);
    stop->setEnabled(false);
    log->appendPlainText("Cancellation requested…");
  });
  connect(target, &QComboBox::currentIndexChanged, this,
          [this] { refreshPermissions(); });
  connect(jobs, &QSpinBox::valueChanged, this,
          [this] { refreshPermissions(); });
  refreshPermissions();
}
JobsDialog::~JobsDialog() {
  if (job)
    bus.cancel(job);
  if (planningRequest)
    bus.cancel(planningRequest);
}
void JobsDialog::addPatch(const QString &path) { stack->addItem(path); }
QJsonObject JobsDialog::payload() const {
  QJsonArray patches;
  for (int i = 0; i < stack->count(); ++i)
    patches.append(stack->item(i)->text());
  return {{"workspace", workspace->text()},
          {"patches", patches},
          {"target", target->currentText()},
          {"jobs", jobs->value()}};
}
void JobsDialog::refreshPermissions() {
  for (auto *button : commands) {
    button->setEnabled(false);
    button->setToolTip(job || planning ? "A workspace operation is running."
                                       : "Checking workspace permissions…");
  }
  if (job || planning)
    return;
  auto requested = payload();
  auto id = bus.request("workspace-permissions", requested, permissionKey);
  bus.listen(
      id, this, [this, requested](const QString &topic, const QJsonObject &p) {
        if (job || planning || payload() != requested)
          return;
        if (topic == "failed") {
          for (auto *button : commands)
            button->setToolTip(p[QStringLiteral("error")].toString());
          return;
        }
        if (topic != "completed")
          return;
        auto reasons = p[QStringLiteral("reasons")].toArray();
        for (int i = 0; i < commands.size(); ++i) {
          auto why = i < reasons.size() ? reasons[i].toString()
                                        : "Workspace check did not complete.";
          commands[i]->setEnabled(why.isEmpty());
          commands[i]->setToolTip(why);
        }
      });
}
void JobsDialog::run(int command) {
  if (job || planning)
    return;
  planning = true;
  workspace->setEnabled(false);
  stack->setEnabled(false);
  refreshPermissions();
  auto requested = payload();
  requested[QStringLiteral("command")] = command;
  log->appendPlainText("Validating the workspace operation…");
  auto id = bus.request("prepare-job", requested);
  planningRequest = id;
  bus.listen(id, this, [this](const QString &topic, const QJsonObject &p) {
    if (topic == "completed" || topic == "failed" || topic == "cancelled")
      planningRequest = 0;
    if (topic == "failed") {
      planning = false;
      workspace->setEnabled(true);
      stack->setEnabled(true);
      log->appendPlainText(p[QStringLiteral("error")].toString());
      refreshPermissions();
      return;
    }
    if (topic != "completed")
      return;
    planning = false;
    stop->setEnabled(true);
    auto args = strings(p[QStringLiteral("arguments")].toArray());
    log->appendPlainText(p[QStringLiteral("program")].toString() + " " +
                         args.join(' '));
    job = bus.runProcess(p[QStringLiteral("program")].toString(), args,
                         p[QStringLiteral("directory")].toString(),
                         "kernel workspace");
    bus.listen(job, this, [this](const QString &topic, const QJsonObject &p) {
      if (topic == "output-truncated") {
        log->appendPlainText(
            "[Display output skipped to keep the interface responsive. Full "
            "build output remains in build.log.]");
        return;
      }
      if (topic == "output") {
        constexpr int limit = 512 * 1024;
        if (log->document()->characterCount() > limit) {
          QTextCursor trim(log->document());
          trim.setPosition(0);
          trim.setPosition(log->document()->characterCount() - limit,
                           QTextCursor::KeepAnchor);
          trim.removeSelectedText();
        }
        log->moveCursor(QTextCursor::End);
        log->insertPlainText(p[QStringLiteral("text")].toString());
        return;
      }
      if (topic != "completed" && topic != "failed" && topic != "cancelled")
        return;
      job = 0;
      stop->setEnabled(false);
      workspace->setEnabled(true);
      stack->setEnabled(true);
      log->appendPlainText(topic == "completed"
                               ? "\nJob completed."
                               : "\n" + p[QStringLiteral("error")].toString());
      refreshPermissions();
    });
  });
}
