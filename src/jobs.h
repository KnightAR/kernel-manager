// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <QDialog>
#include <QJsonObject>
class EventBus;
class Session;
class QLineEdit;
class QPlainTextEdit;
class QComboBox;
class QSpinBox;
class QListWidget;
class QPushButton;
class JobsDialog : public QDialog {
public:
  JobsDialog(EventBus &bus, Session &session, QWidget *parent = nullptr);
  ~JobsDialog() override;
  void addPatch(const QString &path);

private:
  QJsonObject payload() const;
  void refreshPermissions();
  void run(int command);
  EventBus &bus;
  Session &session;
  QLineEdit *workspace;
  QPlainTextEdit *log;
  QComboBox *target;
  QSpinBox *jobs;
  QListWidget *stack;
  QList<QPushButton *> commands;
  QPushButton *stop;
  QString permissionKey;
  quint64 job = 0, planningRequest = 0;
  bool planning = false;
};
