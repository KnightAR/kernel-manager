// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <QJsonObject>
#include <QMainWindow>
#include <QPointer>
#include <QSet>
#include <functional>
class EventBus;
class Session;
class MenuModel;
class MenuFilter;
class QTreeView;
class QLineEdit;
class QLabel;
class QMenu;
class QMenuBar;
class QTabWidget;
class QTextBrowser;
class QTableWidget;
class QListWidget;
class QPushButton;
class QAction;
class QSplitter;
class QDialog;
class QToolButton;
class QComboBox;
class QGridLayout;
class QResizeEvent;
class Window : public QMainWindow {
public:
  Window(EventBus &bus, Session &session, const QString &assets);

protected:
  void closeEvent(QCloseEvent *event) override;
  void resizeEvent(QResizeEvent *event) override;
  void showEvent(QShowEvent *event) override;
  void paintEvent(QPaintEvent *event) override;
  bool eventFilter(QObject *watched, QEvent *event) override;

private:
  void createMenus();
  void changePage(int page);
  void updateLayoutMetrics();
  void updateBackgroundBlur();
  void applyFilters();
  QJsonObject filterPayload() const;
  void populateProfiles();
  void refresh();
  void search();
  void outliers();
  void select(int index);
  void requestDetails();
  void listenSelected(quint64 request, int index);
  void renderOption(const QJsonObject &option);
  void renderContext(const QJsonObject &context);
  void edit(const QString &value);
  void save(bool minimal = false);
  void load(const QString &file);
  void discardThen(std::function<void()> continuation);
  void attribution();
  void hardware();
  void suggestions();
  void internals();
  void patchBrowser();
  void sourceSession();
  void message(const QString &text);
  EventBus &bus;
  Session &session;
  QString assets;
  QString themeName = "Paper";
  int fontSize = 12;
  int current = -1;
  bool initialized = false, allowClose = false, saveInFlight = false;
  bool showUnavailable = false, filterWrapped = false, uiReady = false;
  quint64 filterRequest = 0;
  quint64 selectedRequest = 0;
  bool contextRequested = false, patchesRequested = false;
  QPointer<QDialog> discardDialog;
  QSet<quint64> pending;
  QTreeView *tree, *results, *outlierTree;
  MenuModel *menuModel, *searchModel, *outlierModel;
  MenuFilter *menuFilter;
  QLineEdit *query, *valueInput, *filterQuery;
  QComboBox *stateFilter, *subsystemFilter, *distroFilter;
  QGridLayout *filterGrid;
  QWidget *browserCard, *rail;
  QList<QToolButton *> railButtons;
  QToolButton *copySymbol;
  QLabel *pageHeading, *pageSubtitle, *catalogCount, *railSummary, *featureGlyph, *supportBadge;
  QLabel *heading, *symbolLabel, *coverage, *stateLabel, *breadcrumb, *busy;
  QMenu *profilesMenu;
  QMenuBar *commandMenu;
  QTabWidget *navigation, *tabs;
  QSplitter *mainSplit;
  QTextBrowser *help, *driverInfo, *history;
  QTableWidget *comparison;
  QListWidget *related;
  QPushButton *yesButton, *modButton, *noButton, *setButton;
  QAction *saveAction;
  QJsonObject option;
};
