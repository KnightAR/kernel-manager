// SPDX-License-Identifier: GPL-2.0-only
#include "window.h"
#include "asyncui.h"
#include "eventbus.h"
#include "jobs.h"
#include "icons.h"
#include "menumodel.h"
#include "session.h"
#include "theme.h"
#include "workbench.h"
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QClipboard>
#include <QButtonGroup>
#include <QGridLayout>
#include <QResizeEvent>
#include <QShowEvent>
#include <QStyle>
#include <QTabBar>
#include <QToolButton>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QPainter>
#include <QPushButton>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStatusBar>
#include <QStyledItemDelegate>
#include <QTableWidget>
#include <QTextBrowser>
#include <QTimer>
#include <QTreeView>
#include <QUrlQuery>
#include <QUuid>
#include <QVBoxLayout>
#include <memory>
#include <array>
#ifdef KM_HAVE_WINDOW_EFFECTS
#include <KWindowEffects>
#endif

namespace {
class DetailTabBar : public QTabBar {
public:
  DetailTabBar() {
    setExpanding(true);
    setElideMode(Qt::ElideRight);
    setUsesScrollButtons(false);
    setDrawBase(false);
  }
protected:
  QSize minimumTabSizeHint(int index) const override {
    auto size = QTabBar::minimumTabSizeHint(index);
    // Keep every tab reachable even with a narrow pane or enlarged text.
    size.setWidth(1);
    return size;
  }
};
class DetailTabs : public QTabWidget {
public:
  DetailTabs() { setTabBar(new DetailTabBar); }
};
QColor stateColor(const QString &state) {
  bool light = qApp->palette().color(QPalette::Base).lightness() > 128;
  return state == "y" ? QColor(light ? "#116239" : "#a0f0c5")
         : state == "m"
             ? QColor(light ? "#154f9d" : "#afd1ff")
             : qApp->palette().color(QPalette::Disabled, QPalette::Text);
}
class CoverageDelegate : public QStyledItemDelegate {
public:
  CoverageDelegate(Session &session, const QString &assets, QObject *parent)
      : QStyledItemDelegate(parent), session(session), assets(assets) {}
  void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &model) const override {
    auto opt = option; initStyleOption(&opt, model); opt.text.clear();
    QApplication::style()->drawControl(QStyle::CE_ItemViewItem, &opt, painter);
    int i = model.data(Qt::UserRole).toInt(); if (i < 0 || i >= session.entries.size()) return;
    const auto &flags = session.entries[i].coverage;
    std::array<int,8> enabled{};
    int enabledCount = 0;
    for (int d = 0; d < flags.size() && d < session.distros.size(); ++d) if (flags[d].digitValue()) {
      if (enabledCount < int(enabled.size())) enabled[enabledCount] = d;
      ++enabledCount;
    }
    const int width = opt.rect.width();
    const QFontMetrics metrics(opt.font);
    const int badgeWidth = metrics.horizontalAdvance("+" + QString::number(enabledCount)) + 16;
    int iconSlots = qMin(8, qMax(0, (width - 12) / 24));
    if (iconSlots < enabledCount) iconSlots = qMin(8, qMax(0, (width - badgeWidth - 12) / 24));
    const int shown = qMin(iconSlots, enabledCount);
    painter->save(); painter->setRenderHint(QPainter::Antialiasing);
    int x = opt.rect.x() + 6;
    for (int n = 0; n < shown; ++n, x += 24) {
      const int d = enabled[n]; auto id = session.distros[d][QStringLiteral("id")].toString();
      if (!icons.contains(id)) icons[id] = QIcon(assets + "/distro/" + id + ".svg");
      QRect rect(x, opt.rect.center().y() - 8, 16, 16);
      painter->setPen(Qt::NoPen); painter->setBrush(QColor("#ffffff")); painter->drawEllipse(rect.adjusted(-1,-1,1,1));
      icons[id].paint(painter, rect);
      const int bits = flags[d].digitValue();
      painter->setPen(QPen(qApp->palette().color(QPalette::Base), 1)); painter->setBrush(stateColor(bits & 1 ? "y" : "m"));
      painter->drawEllipse(QPoint(x + 15, rect.bottom()), 2, 2);
      if (bits == 3) { painter->setBrush(stateColor("m")); painter->drawEllipse(QPoint(x + 15, rect.top()), 2, 2); }
    }
    const int remaining = enabledCount - shown;
    if (remaining > 0) {
      const int height = qMax(20,metrics.height()+4);
      QRect badge(x, opt.rect.center().y() - height/2, qMax(0, qMin(badgeWidth, opt.rect.right() - x - 3)), height);
      painter->setPen(Qt::NoPen); painter->setBrush(opt.palette.color(QPalette::AlternateBase)); painter->drawRoundedRect(badge,10,10);
      painter->setPen(opt.palette.color(QPalette::Text)); painter->drawText(badge, Qt::AlignCenter, "+" + QString::number(remaining));
    }
    painter->restore();
  }
  QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override {
    auto size = QStyledItemDelegate::sizeHint(option,index); size.setHeight(QFontMetrics(option.font).height()+10); return size;
  }
private:
  Session &session; QString assets; mutable QHash<QString,QIcon> icons;
};
class ValueDelegate : public QStyledItemDelegate {
public:
  ValueDelegate(Session &session, QIcon icon, QObject *parent) : QStyledItemDelegate(parent), session(session), icon(icon) {}
  void paint(QPainter *p, const QStyleOptionViewItem &option, const QModelIndex &index) const override {
    int i = index.data(Qt::UserRole).toInt();
    if (i < 0 || i >= session.entries.size()) return;
    const auto &e = session.entries[i];
    if (e.name.isEmpty() || (e.value != "y" && e.value != "m" && e.value != "n")) { QStyledItemDelegate::paint(p,option,index); return; }
    auto opt=option; initStyleOption(&opt,index); opt.text.clear();
    QApplication::style()->drawControl(QStyle::CE_ItemViewItem,&opt,p);
    const bool light=opt.palette.color(QPalette::Base).lightness()>128;
    auto fill=QColor(e.value=="y" ? (light ? "#d0f0e4" : "#234a3e") : e.value=="m" ? (light ? "#dfeaff" : "#273f60") : (light ? "#e9eef6" : "#344154"));
    const int height = qMax(24,QFontMetrics(opt.font).height()+2);
    const int width = qMax(64,QFontMetrics(opt.font).horizontalAdvance(e.value)+38);
    QRect pill(opt.rect.x()+7,opt.rect.center().y()-height/2,qMin(opt.rect.width()-14,width),height);
    p->save(); p->setRenderHint(QPainter::Antialiasing); p->setPen(Qt::NoPen); p->setBrush(fill); p->drawRoundedRect(pill,12,12);
    icon.paint(p,QRect(pill.x()+6,pill.center().y()-7,14,14)); p->setPen(stateColor(e.value)); p->drawText(pill.adjusted(23,0,-5,0),Qt::AlignCenter,e.value); p->restore();
  }
private:
  Session &session; QIcon icon;
};
QString domainHint(QString person) {
  auto domain = person.section('@', -1).section('>', 0, 0).trimmed().toLower();
  if (!person.contains('@'))
    return "Email affiliation not supplied";
  const QStringList personal{"gmail.com",      "googlemail.com",
                             "outlook.com",    "hotmail.com",
                             "yahoo.com",      "proton.me",
                             "protonmail.com", "users.noreply.github.com"};
  return domain + (personal.contains(domain) ? " · personal email provider"
                                             : " · affiliation hint");
}
QTableWidget *table(int columns, const QStringList &headings) {
  auto *view = new QTableWidget;
  view->setColumnCount(columns);
  view->setHorizontalHeaderLabels(headings);
  view->setEditTriggers(QAbstractItemView::NoEditTriggers);
  view->setSelectionBehavior(QAbstractItemView::SelectRows);
  view->setSelectionMode(QAbstractItemView::SingleSelection);
  view->setAlternatingRowColors(true);
  view->verticalHeader()->hide();
  return view;
}
} // namespace

Window::Window(EventBus &events, Session &state, const QString &assetRoot)
    : bus(events), session(state), assets(assetRoot) {
  setWindowTitle("Kernel Manager · loading source");
  setWindowIcon(QIcon(assets + "/kernel-manager.svg"));
  setAttribute(Qt::WA_TranslucentBackground);
  resize(1680, 1020);
  auto *body = new QWidget;
  auto *shell = new QHBoxLayout(body); shell->setContentsMargins(0,0,0,0); shell->setSpacing(0);
  rail = new QWidget; rail->setObjectName("navigationRail"); rail->setFixedWidth(190);
  auto *railLayout = new QVBoxLayout(rail); railLayout->setContentsMargins(12,16,12,14); railLayout->setSpacing(6);
  auto *railGroup = new QButtonGroup(rail); railGroup->setExclusive(true);
  const QStringList pageNames{"Kernel menu", "Search", "Outliers"};
  const QStringList pageIcons{"settings", "search", "outliers"};
  for (int i=0;i<3;++i) {
    auto *button = new QToolButton; button->setObjectName("railButton"); button->setText(pageNames[i]); button->setAccessibleName(pageNames[i]);
    button->setIcon(uiIcon(pageIcons[i], QColor("#d7e5fa"))); button->setIconSize({20,20}); button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    button->setCheckable(true); button->setChecked(i==0); button->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Fixed);
    railGroup->addButton(button,i); railButtons << button; railLayout->addWidget(button);
    connect(button,&QToolButton::clicked,this,[this,i] { changePage(i); });
  }
  railLayout->addStretch();
  railSummary = new QLabel("<b>Current config</b><br><br>y · built in<br>m · module<br>n · disabled<br><br>Loading source…");
  railSummary->setObjectName("railSummary"); railSummary->setWordWrap(true); railLayout->addWidget(railSummary);
  shell->addWidget(rail);
  auto *workspace = new QWidget; workspace->setObjectName("workspace");
  auto *layout = new QVBoxLayout(workspace); layout->setContentsMargins(12,12,12,12);
  shell->addWidget(workspace,1);
  query = new QLineEdit; query->setObjectName("globalSearch"); query->setClearButtonEnabled(true);
  query->setPlaceholderText("Search kernel options…  Ctrl+K"); query->setAccessibleName("Search all kernel options");
  query->addAction(uiIcon("search",QColor("#50627c")),QLineEdit::LeadingPosition);
  query->setMinimumWidth(220); query->setMaximumWidth(520);
  query->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Fixed);
  query->setToolTip("Search symbols and full Kconfig help. All words must match. Quoted phrases and name:, help:, path: fields are supported.");
  auto *searchTimer = new QTimer(this); searchTimer->setSingleShot(true); searchTimer->setInterval(90);
  connect(query,&QLineEdit::textChanged,searchTimer,[searchTimer] { searchTimer->start(); });
  connect(searchTimer,&QTimer::timeout,this,[this] { search(); });
  browserCard = new QWidget; browserCard->setObjectName("browserCard"); browserCard->setMinimumWidth(430); browserCard->installEventFilter(this);
  auto *browserLayout = new QVBoxLayout(browserCard); browserLayout->setContentsMargins(12,12,12,10); browserLayout->setSpacing(10);
  auto *pageHeader = new QHBoxLayout;
  auto *pageTitles = new QVBoxLayout; pageTitles->setSpacing(2);
  pageHeading = new QLabel("Kernel menu"); pageHeading->setObjectName("pageHeading");
  pageSubtitle = new QLabel("Browse and configure kernel options"); pageSubtitle->setObjectName("muted"); pageSubtitle->setWordWrap(true);
  pageTitles->addWidget(pageHeading); pageTitles->addWidget(pageSubtitle); pageHeader->addLayout(pageTitles,1);
  catalogCount = new QLabel("Loading profiles…"); catalogCount->setObjectName("muted"); pageHeader->addWidget(catalogCount);
  browserLayout->addLayout(pageHeader);
  filterGrid = new QGridLayout; filterGrid->setContentsMargins(0,4,0,2); filterGrid->setHorizontalSpacing(8); filterGrid->setVerticalSpacing(8);
  filterQuery = new QLineEdit; filterQuery->setObjectName("optionFilter"); filterQuery->setPlaceholderText("Filter options…"); filterQuery->setAccessibleName("Filter options in this view"); filterQuery->setClearButtonEnabled(true); filterQuery->setMinimumWidth(130);
  filterQuery->addAction(uiIcon("search"),QLineEdit::LeadingPosition);
  stateFilter = new QComboBox; stateFilter->setAccessibleName("Filter by your config value");
  stateFilter->addItem("All states", ""); stateFilter->addItem("Built in · y", "y"); stateFilter->addItem("Module · m", "m"); stateFilter->addItem("Disabled · n", "n");
  subsystemFilter = new QComboBox; subsystemFilter->setAccessibleName("Filter by kernel source subsystem"); subsystemFilter->addItem("All subsystems", "");
  distroFilter = new QComboBox; distroFilter->setAccessibleName("Filter by enabling distribution"); distroFilter->addItem("All distros", "");
  for (auto *combo : {stateFilter,subsystemFilter,distroFilter}) { combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon); combo->setMinimumContentsLength(7); combo->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Fixed); }
  filterGrid->addWidget(filterQuery,0,0); filterGrid->addWidget(stateFilter,0,1); filterGrid->addWidget(subsystemFilter,0,2); filterGrid->addWidget(distroFilter,0,3); filterGrid->setColumnStretch(0,2);
  browserLayout->addLayout(filterGrid);
  auto *filterTimer = new QTimer(this); filterTimer->setSingleShot(true); filterTimer->setInterval(100);
  connect(filterQuery,&QLineEdit::textChanged,filterTimer,[filterTimer] { filterTimer->start(); });
  connect(filterTimer,&QTimer::timeout,this,[this] { applyFilters(); });
  for (auto *combo : {stateFilter,subsystemFilter,distroFilter}) connect(combo,&QComboBox::currentIndexChanged,this,[this] { applyFilters(); });
  mainSplit = new QSplitter(Qt::Horizontal);
  mainSplit->setChildrenCollapsible(false);
  navigation = new QTabWidget;
  navigation->setObjectName("navigation");
  navigation->setMinimumWidth(0);
  navigation->tabBar()->hide();
  menuModel =
      new MenuModel(session, true, this);
  searchModel = new MenuModel(session, false, this);
  outlierModel = new MenuModel(session, false, this);
  menuFilter = new MenuFilter(session, this);
  menuFilter->setSourceModel(menuModel);
  auto makeTree = [this](QAbstractItemModel *model, const QString &name) {
    auto *tree = new QTreeView;
    tree->setObjectName("kernelMenu");
    tree->setAccessibleName(name);
    tree->setModel(model);
    tree->setUniformRowHeights(true);
    tree->setAlternatingRowColors(true);
    tree->setIndentation(14);
    tree->setSelectionMode(QAbstractItemView::SingleSelection);
    tree->header()->setStretchLastSection(false);
    tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    tree->header()->setSectionResizeMode(2, QHeaderView::Fixed);
    tree->setColumnWidth(1, 78);
    tree->setItemDelegateForColumn(1,new ValueDelegate(session,windowIcon(),tree));
    tree->setIconSize({18,18});
    tree->setColumnWidth(2, 250);
    tree->setItemDelegateForColumn(2,
                                   new CoverageDelegate(session, assets, tree));
    connect(tree->selectionModel(), &QItemSelectionModel::currentChanged, this,
            [this](const QModelIndex &index) {
              if (index.isValid())
                select(index.data(Qt::UserRole).toInt());
            });
    connect(
        tree, &QTreeView::doubleClicked, this,
        [this](const QModelIndex &index) {
          if (index.column() != 1)
            return;
          int i = index.data(Qt::UserRole).toInt();
          const auto &e = session.entries[i];
          if (!session.ready)
            return;
          if (e.nextValue() != e.value) {
            current = i;
            edit(e.nextValue());
          }
        });
    return tree;
  };
  tree = makeTree(menuFilter, "Kernel configuration menu");
  results = makeTree(searchModel, "Ranked kernel search results");
  outlierTree =
      makeTree(outlierModel, "Disabled here, enabled by other distros");
  results->setRootIsDecorated(false);
  outlierTree->setRootIsDecorated(false);
  navigation->addTab(tree, "Kernel menu");
  navigation->addTab(results, "Search");
  navigation->addTab(outlierTree, "Outliers");
  browserLayout->addWidget(navigation,1);
  mainSplit->addWidget(browserCard);
  connect(navigation, &QTabWidget::currentChanged, this, [this](int i) {
    railButtons[i]->setChecked(true);
    pageHeading->setText(QStringList{"Kernel menu","Search","Outliers"}.value(i));
    pageSubtitle->setText(QStringList{"Browse and configure kernel options","Search symbols and full Kconfig help","Disabled here, enabled by other distros"}.value(i));
    applyFilters();
  });
  auto *detail = new QWidget;
  detail->setObjectName("contentPane");
  detail->setMinimumWidth(420);
  auto *right = new QVBoxLayout(detail);
  right->setContentsMargins(18,16,18,14);
  right->setSpacing(10);
  breadcrumb = new QLabel;
  breadcrumb->setWordWrap(true);
  breadcrumb->setObjectName("muted");
  right->addWidget(breadcrumb);
  heading = new QLabel("Loading kernel options…");
  heading->setObjectName("heading");
  heading->setWordWrap(true);
  symbolLabel = new QLabel;
  symbolLabel->setObjectName("symbol");
  symbolLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
  auto *titleRow = new QHBoxLayout;
  featureGlyph = new QLabel; featureGlyph->setFixedSize(42,42); titleRow->addWidget(featureGlyph);
  auto *titleText = new QVBoxLayout; titleText->setSpacing(3); titleText->addWidget(heading); titleText->addWidget(symbolLabel); titleRow->addLayout(titleText,1);
  copySymbol = new QToolButton; copySymbol->setIcon(uiIcon("link")); copySymbol->setAccessibleName("Copy kernel symbol name"); copySymbol->setToolTip("Copy the CONFIG_ symbol name"); copySymbol->setEnabled(false); titleRow->addWidget(copySymbol);
  connect(copySymbol,&QToolButton::clicked,this,[this] { if (current>=0) { auto name=session.entries[current].name; if (!name.isEmpty()) { qApp->clipboard()->setText("CONFIG_"+name); message("Copied CONFIG_"+name); } } });
  right->addLayout(titleRow);
  coverage = new QLabel;
  coverage->setObjectName("coverage");
  coverage->setWordWrap(true);
  auto *coverageRow = new QHBoxLayout; coverageRow->addWidget(coverage,1);
  supportBadge = new QLabel; supportBadge->setObjectName("supportBadge"); supportBadge->setWordWrap(true); supportBadge->hide(); coverageRow->addWidget(supportBadge); right->addLayout(coverageRow);
  stateLabel = new QLabel;
  stateLabel->setWordWrap(true);
  right->addWidget(stateLabel);
  auto *buttons = new QHBoxLayout;
  noButton = new QPushButton("N · disabled");
  modButton = new QPushButton("M · module");
  yesButton = new QPushButton("Y · built in");
  for (auto *button : {noButton, modButton, yesButton}) {
    button->setObjectName("stateButton");
    button->setCheckable(true);
    button->setEnabled(false);
    buttons->addWidget(button);
  }
  right->addLayout(buttons);
  connect(noButton, &QPushButton::clicked, this, [this] { edit("n"); });
  connect(modButton, &QPushButton::clicked, this, [this] { edit("m"); });
  connect(yesButton, &QPushButton::clicked, this, [this] { edit("y"); });
  auto *scalar = new QHBoxLayout;
  valueInput = new QLineEdit;
  setButton = new QPushButton("Set value");
  scalar->addWidget(valueInput);
  scalar->addWidget(setButton);
  right->addLayout(scalar);
  valueInput->hide();
  setButton->hide();
  connect(setButton, &QPushButton::clicked, this,
          [this] { edit(valueInput->text()); });
  connect(valueInput, &QLineEdit::returnPressed, this,
          [this] { edit(valueInput->text()); });
  tabs = new DetailTabs; tabs->setObjectName("detailTabs");
  tabs->setDocumentMode(true);
  tabs->tabBar()->setDrawBase(false);
  tabs->tabBar()->setExpanding(true);
  help = linkBrowser(bus);
  tabs->addTab(help, "Details");
  driverInfo = linkBrowser(bus);
  tabs->addTab(driverInfo, "Driver");
  auto *distroPage = new QWidget;
  auto *dl = new QVBoxLayout(distroPage);
  comparison = table(4, {"Distro / release", "Flavor", "Kernel", "Value"});
  dl->addWidget(comparison);
  auto *copy = new QPushButton("Use selected distro’s value");
  auto *source = new QPushButton("View selected config’s source");
  dl->addWidget(copy);
  dl->addWidget(source);
  tabs->addTab(distroPage, "Distros");
  connect(copy, &QPushButton::clicked, this, [this] {
    int row = comparison->currentRow();
    auto profiles = option[QStringLiteral("profiles")].toArray();
    if (row >= 0 && row < profiles.size() &&
        !profiles[row][QStringLiteral("value")].isNull())
      edit(profiles[row][QStringLiteral("value")].toString());
  });
  connect(source, &QPushButton::clicked, this, [this] {
    int row = comparison->currentRow();
    auto profiles = option[QStringLiteral("profiles")].toArray();
    if (row >= 0 && row < profiles.size())
      openLink(bus, QUrl(profiles[row][QStringLiteral("source")].toString()));
  });
  auto *patchPage = new QWidget;
  auto *pl = new QVBoxLayout(patchPage);
  related = new QListWidget;
  pl->addWidget(related);
  auto *all = new QPushButton("Browse all patches");
  pl->addWidget(all);
  connect(all, &QPushButton::clicked, this, [this] { patchBrowser(); });
  tabs->addTab(patchPage, "Patches");
  connect(related, &QListWidget::itemDoubleClicked, this,
          [this](QListWidgetItem *item) {
            if (item->data(Qt::UserRole).toString().isEmpty())
              return;
            auto *dialog = toolDialog(this, item->text(), 1100, 800);
            auto *layout = new QVBoxLayout(dialog);
            auto *preview = new TextPreview(bus);
            layout->addWidget(preview);
            preview->openFile(session.catalogPath + "/" +
                              item->data(Qt::UserRole).toString());
            dialog->show();
          });
  auto *people = new QWidget;
  auto *peopleLayout = new QVBoxLayout(people);
  history = linkBrowser(bus);
  peopleLayout->addWidget(history);
  auto *blame = new QPushButton("Load attribution / Git history");
  peopleLayout->addWidget(blame);
  connect(blame, &QPushButton::clicked, this, [this] { attribution(); });
  tabs->addTab(people, "People");
  const QStringList tabHints{"Description and dependencies", "Driver / device", "Distribution configurations", "Related patches", "People and attribution"};
  for (int i=0;i<tabs->count();++i) tabs->setTabToolTip(i,tabHints[i]);
  connect(tabs,&QTabWidget::currentChanged,this,[this] { requestDetails(); });
  right->addWidget(tabs, 1);
  mainSplit->addWidget(detail);
  mainSplit->setSizes({760, 600});
  mainSplit->setStretchFactor(0, 56);
  mainSplit->setStretchFactor(1, 44);
  layout->addWidget(mainSplit, 1);
  setCentralWidget(body);
  busy = new QLabel;
  statusBar()->addPermanentWidget(busy);
  auto *header = new QWidget; header->setObjectName("windowHeader");
  auto *headerLayout = new QHBoxLayout(header);
  headerLayout->setContentsMargins(12,6,16,6); headerLayout->setSpacing(20);
  commandMenu = new QMenuBar;
  commandMenu->setSizePolicy(QSizePolicy::Minimum,QSizePolicy::Preferred);
  headerLayout->addWidget(commandMenu);
  headerLayout->addStretch(1);
  headerLayout->addWidget(query,1);
  setMenuWidget(header);
  createMenus();
  uiReady = true;
  connect(mainSplit,&QSplitter::splitterMoved,this,[this] { updateLayoutMetrics(); });
  updateLayoutMetrics();
  connect(&session, &Session::metadataChanged, this, [this] { refresh(); });
  connect(&session, &Session::catalogChanged, this, [this] {
    populateProfiles();
    const QSignalBlocker blocker(distroFilter);
    distroFilter->clear(); distroFilter->addItem("All distros", "");
    for (const auto d : session.distros) distroFilter->addItem(QIcon(assets+"/distro/"+d[QStringLiteral("id")].toString()+".svg"),d[QStringLiteral("label")].toString(),d[QStringLiteral("id")].toString());
    catalogCount->setText(QString("%1 configs · %2 distros").arg(session.profiles.size()).arg(session.distros.size()));
    railSummary->setText(QString("<b>Current config</b><br><br>y · built in<br>m · module<br>n · disabled<br><br>%1 configs<br>%2 distros").arg(session.profiles.size()).arg(session.distros.size()));
  });
  connect(&session, &Session::stateReady, this, [this] {
    refresh();
    if (!initialized) {
      initialized = true;
      auto id=bus.request("subsystems",{},"subsystems");
      bus.listen(id,this,[this](const QString &topic,const QJsonObject &p) {
        if (topic!="completed") return;
        const QSignalBlocker blocker(subsystemFilter);
        for (const auto group : p[QStringLiteral("items")].toArray()) subsystemFilter->addItem(group.toString(),group.toString());
      });
      tree->expandToDepth(0);
      current = session.find("DRM_AMDGPU");
      if (current < 0 && !session.entries.isEmpty())
        current = 0;
      auto index = menuFilter->mapFromSource(menuModel->entryIndex(current));
      for (auto parent = index.parent(); parent.isValid();
           parent = parent.parent())
        tree->expand(parent);
      {
        const QSignalBlocker blocker(tree->selectionModel());
        tree->setCurrentIndex(index);
      }
      tree->scrollTo(index);
    }
    if (current >= 0)
      select(current);
    if (!query->text().isEmpty())
      search();
    applyFilters();
  });
  connect(&bus, &EventBus::event, this,
          [this](quint64 id, const QString &topic, const QJsonObject &p) {
            if (topic == "started")
              pending.insert(id);
            if (topic == "completed" || topic == "failed" ||
                topic == "cancelled")
              pending.remove(id);
            busy->setText(pending.isEmpty() ? ""
                                            : QString::number(pending.size()) +
                                                  " background operation(s)");
            if (topic == "failed" && !session.ready) {
              heading->setText("Source session unavailable");
              help->setPlainText(p[QStringLiteral("error")].toString());
            }
            if (topic == "progress")
              message(p[QStringLiteral("message")].toString());
            if (topic == "diagnostic")
              message(p[QStringLiteral("text")].toString().section('\n', 0, 0));
          });
  connect(&bus, &EventBus::backendFailed, this, [this](const QString &error) {
    saveAction->setEnabled(false);
    saveAction->setToolTip(error);
    for (auto *b : {noButton, modButton, yesButton, setButton})
      b->setEnabled(false);
    message(error);
  });
}

void Window::createMenus() {
  auto *menus = commandMenu;
  menus->setNativeMenuBar(false);
  menus->setAccessibleName("Application menus");
  auto addMenu = [menus](const QString &title) {
    auto *menu = menus->addMenu(title);
    menu->setToolTipsVisible(true);
    return menu;
  };
  auto action = [this](QMenu *menu, const QString &label, auto callback,
                       const QKeySequence &shortcut = QKeySequence()) {
    auto *item = menu->addAction(label);
    item->setShortcut(shortcut);
    connect(item, &QAction::triggered, this, callback);
    return item;
  };

  auto *file = addMenu("&File");
  action(
      file, "&Open Config…",
      [this] {
        chooseFiles(bus, this, "Open kernel config", session.sourcePath, false,
                    false,
                    [this](const QStringList &files) { load(files.first()); });
      },
      QKeySequence::Open);
  profilesMenu = file->addMenu("Load &Distribution Config");
  profilesMenu->setToolTipsVisible(true);
  profilesMenu->setEnabled(false);
  profilesMenu->menuAction()->setToolTip("Distribution profiles are loading.");
  file->addSeparator();
  saveAction = action(
      file, "&Save",
      [this] {
        saveInFlight = true;
        saveAction->setEnabled(false);
        auto id = bus.request("save", {{"path", session.configPath}});
        bus.listen(id, this,
                   [this](const QString &topic, const QJsonObject &p) {
                     if (topic == "completed" || topic == "failed") {
                       saveInFlight = false;
                       refresh();
                     }
                     if (topic == "failed")
                       message(p[QStringLiteral("error")].toString());
                   });
      },
      QKeySequence::Save);
  saveAction->setEnabled(false);
  saveAction->setToolTip("The kernel session is loading.");
  auto *saveAs =
      action(file, "Save &As…", [this] { save(); }, QKeySequence::SaveAs);
  saveAs->setToolTip(
      "Choose a writable destination for a separate working copy.");
  action(file, "&Export Minimal Defconfig…", [this] { save(true); });
  file->addSeparator();
  action(file, "&Quit", [this] { close(); }, QKeySequence::Quit);

  auto *view = addMenu("&View");
  auto *find = action(view, "&Find Option", [this] {
    query->setFocus();
    query->selectAll();
  });
  find->setShortcuts({QKeySequence("Ctrl+K"), QKeySequence::Find});
  view->addSeparator();
  auto *pages = new QActionGroup(view);
  for (int i = 0; i < navigation->count(); ++i) {
    auto *page = action(
        view, QStringList{"&Kernel Menu", "&Search Results", "&Outliers"}[i],
        [this, i] { navigation->setCurrentIndex(i); },
        QKeySequence(QString("Alt+%1").arg(i + 1)));
    page->setCheckable(true);
    page->setChecked(i == 0);
    pages->addAction(page);
  }
  connect(navigation, &QTabWidget::currentChanged, this, [pages](int index) {
    if (index >= 0 && index < pages->actions().size())
      pages->actions()[index]->setChecked(true);
  });
  view->addSeparator();
  auto *unavailable = view->addAction("Show &Unavailable Options");
  unavailable->setCheckable(true);
  connect(unavailable, &QAction::toggled, this,
          [this](bool show) { showUnavailable = show; menuFilter->showUnavailable(show); applyFilters(); });
  view->addSeparator();
  auto *colors = view->addMenu("&Colors");
  auto *colorGroup = new QActionGroup(colors);
  for (const auto &name :
       QStringList{"Paper", "Slate", "High contrast", "Menuconfig"}) {
    auto *color = action(colors, name, [this, name] {
      themeName = name;
      applyTheme(themeName, fontSize);
      refresh();
    });
    color->setCheckable(true);
    color->setChecked(name == themeName);
    colorGroup->addAction(color);
  }
  auto *text = view->addMenu("&Text Size");
  auto *sizes = new QActionGroup(text);
  auto *larger = text->addAction("&Larger Text");
  larger->setShortcut(QKeySequence::ZoomIn);
  auto *smaller = text->addAction("&Smaller Text");
  smaller->setShortcut(QKeySequence::ZoomOut);
  auto *reset = text->addAction("&Default Size");
  reset->setShortcut(QKeySequence("Ctrl+0"));
  text->addSeparator();
  auto setSize = [this, sizes, larger, smaller](int points) {
    fontSize = qBound(10, points, 24);
    for (auto *item : sizes->actions())
      item->setChecked(item->data().toInt() == fontSize);
    larger->setEnabled(fontSize < 24);
    smaller->setEnabled(fontSize > 10);
    applyTheme(themeName, fontSize);
    refresh();
  };
  for (int points = 10; points <= 24; ++points) {
    auto *size = action(text, QString("%1 pt").arg(points),
                        [setSize, points] { setSize(points); });
    size->setData(points);
    size->setCheckable(true);
    size->setChecked(points == fontSize);
    sizes->addAction(size);
  }
  connect(larger, &QAction::triggered, this,
          [this, setSize] { setSize(fontSize + 1); });
  connect(smaller, &QAction::triggered, this,
          [this, setSize] { setSize(fontSize - 1); });
  connect(reset, &QAction::triggered, this, [setSize] { setSize(12); });
  auto *layout = view->addMenu("&Layout");
  auto *layouts = new QActionGroup(layout);
  const QStringList choices{"Balanced · 56 / 44", "Sidebar · 38 / 62", "Sidebar · one third",
                            "Wide menu · details below"};
  for (int i = 0; i < choices.size(); ++i) {
    auto *choice = action(layout, choices[i], [this, i] {
      mainSplit->setOrientation(i == 3 ? Qt::Vertical : Qt::Horizontal);
      mainSplit->setSizes(i == 3 ? QList<int>{490, 440}
                          : i == 2 ? QList<int>{550, 1100}
                          : i == 1 ? QList<int>{630, 1020}
                                   : QList<int>{760, 600});
      updateLayoutMetrics();
    });
    choice->setCheckable(true);
    choice->setChecked(i == 0);
    layouts->addAction(choice);
  }

  auto *kernel = addMenu("&Kernel");
  action(
      kernel, "&Source / Target…", [this] { sourceSession(); },
      QKeySequence("Ctrl+Shift+O"));
  kernel->addSeparator();
  action(kernel, "&Patch Library…", [this] { patchBrowser(); });
  action(kernel, "&Build Workspace…",
         [this] { (new JobsDialog(bus, session, this))->show(); });

  auto *tools = addMenu("&Tools");
  action(tools, "&This Machine…", [this] { hardware(); });
  action(tools, "&Suggestions…", [this] { suggestions(); });
  tools->addSeparator();
  action(tools, "&Maintainer Overview…", [this] { internals(); });
  action(tools, "&Engineering Workbench…", [this] {
    showEngineeringWorkbench(bus, session, this, [this](int i) {
      select(i);
      query->setText(session.entries[i].name);
    });
  });

  auto *helpMenu = addMenu("&Help");
  action(
      helpMenu, "&Keyboard Shortcuts…",
      [this, menus] {
        auto *dialog = toolDialog(this, "Keyboard shortcuts", 650, 620);
        auto *layout = new QVBoxLayout(dialog);
        auto *description = linkBrowser(bus);
        QString text = "<h2>Keyboard shortcuts</h2><p>Press Alt to navigate "
                       "the application menus.</p><table cellspacing='10'>";
        std::function<void(QMenu *)> collect = [&](QMenu *menu) {
          for (auto *item : menu->actions()) {
            if (item->menu()) {
              collect(item->menu());
              continue;
            }
            QStringList shortcuts;
            for (const auto &key : item->shortcuts())
              shortcuts << key.toString(QKeySequence::NativeText);
            if (!shortcuts.isEmpty())
              text += "<tr><td>" + html(QString(item->text()).remove('&')) +
                      "</td><td>" + html(shortcuts.join(" / ")) + "</td></tr>";
          }
        };
        for (auto *item : menus->actions())
          if (item->menu())
            collect(item->menu());
        description->setHtml(text + "</table>");
        layout->addWidget(description);
        dialog->show();
      },
      QKeySequence::HelpContents);
  action(helpMenu, "&About Kernel Manager…", [this] {
    auto *dialog = toolDialog(this, "About Kernel Manager", 470, 250);
    auto *layout = new QVBoxLayout(dialog);
    auto *icon = new QLabel;
    icon->setPixmap(windowIcon().pixmap(64, 64));
    icon->setAlignment(Qt::AlignCenter);
    layout->addWidget(icon);
    auto *description =
        new QLabel("<h2>Kernel Manager</h2><p>" +
                   html(QCoreApplication::applicationVersion()) +
                   "</p><p>Explore, compare and configure Linux "
                   "kernels.</p><p>GNU General Public License, version 2.</p>");
    description->setAlignment(Qt::AlignCenter);
    description->setWordWrap(true);
    layout->addWidget(description);
    dialog->show();
  });
}

void Window::populateProfiles() {
  profilesMenu->clear();
  profilesMenu->setEnabled(!session.profiles.isEmpty());
  profilesMenu->menuAction()->setToolTip(
      session.profiles.isEmpty()
          ? "No distribution profiles are available for this architecture."
          : "Load a distribution config into this session.");
  for (const auto family : session.distros) {
    auto distro = family[QStringLiteral("id")].toString();
    auto *group =
        profilesMenu->addMenu(QIcon(assets + "/distro/" + distro + ".svg"),
                              family[QStringLiteral("label")].toString());
    group->setToolTipsVisible(true);
    for (const auto value : session.profiles) {
      auto profile = value.toObject();
      if (profile[QStringLiteral("distro")].toString() != distro)
        continue;
      auto label = profile[QStringLiteral("release")].toString() + " · " +
                   profile[QStringLiteral("flavor")].toString();
      auto *item = group->addAction(label.replace('&', "&&"));
      item->setToolTip(profile[QStringLiteral("source")].toString());
      const auto path = profile[QStringLiteral("file")].toString();
      connect(item, &QAction::triggered, this, [this, path] { load(path); });
    }
  }
}

void Window::message(const QString &text) { statusBar()->showMessage(text); }
void Window::refresh() {
  setWindowTitle("Kernel Manager · Linux " + session.version + " · " +
                 session.architecture);
  saveAction->setEnabled(session.ready && !saveInFlight &&
                         session.saveReason.isEmpty());
  saveAction->setToolTip(session.ready ? session.saveReason
                                       : "The kernel session is not ready.");
  if (!session.ready)
    for (auto *button : {noButton, modButton, yesButton, setButton})
      button->setEnabled(false);
  message("Linux " + session.version + " · " +
          QString::number(session.entries.size()) + " menu entries · " +
          session.configPath + (session.edited ? " · edits in memory" : ""));
  updateLayoutMetrics();
  if (!option.isEmpty()) renderOption(option);
  if (current >= 0 && current < session.entries.size()) {
    const auto &e = session.entries[current];
    featureGlyph->setPixmap(uiIcon(featureIcon(e.file,e.name),palette().color(QPalette::Link)).pixmap(38,38));
  }
  tree->viewport()->update();
  results->viewport()->update();
  outlierTree->viewport()->update();
}
void Window::resizeEvent(QResizeEvent *event) {
  QMainWindow::resizeEvent(event);
  if (uiReady) {
    updateLayoutMetrics();
    updateBackgroundBlur();
  }
}
void Window::showEvent(QShowEvent *event) {
  QMainWindow::showEvent(event);
  // Wait until Qt has shown the native surface before committing its region.
  QTimer::singleShot(0,this,[this] { updateBackgroundBlur(); });
}
void Window::updateBackgroundBlur() {
#ifdef KM_HAVE_WINDOW_EFFECTS
  if (windowHandle() && isVisible())
    KWindowEffects::enableBlurBehind(windowHandle(),true,QRegion(rect()));
#endif
}
void Window::paintEvent(QPaintEvent *) {
  QPainter painter(this);
  // Each surface paints its own translucent background. An opaque fill here
  // would hide the desktop behind every child, regardless of child alpha.
  painter.setCompositionMode(QPainter::CompositionMode_Source);
  painter.fillRect(rect(),Qt::transparent);
}
bool Window::eventFilter(QObject *watched, QEvent *event) {
  if (uiReady && watched == browserCard && event->type() == QEvent::Resize)
    QTimer::singleShot(0,this,[this] { updateLayoutMetrics(); });
  return QMainWindow::eventFilter(watched,event);
}
void Window::updateLayoutMetrics() {
  if (!uiReady) return;
  const auto metrics = fontMetrics();
  rail->setFixedWidth(qMax(180,metrics.horizontalAdvance("Kernel menu") + 70));
  const int stateWidth = metrics.horizontalAdvance("Disabled · n") + 36;
  const int groupWidth = metrics.horizontalAdvance("All subsystems") + 36;
  const int distroWidth = metrics.horizontalAdvance("All distros") + 36;
  stateFilter->setMinimumWidth(stateWidth);
  subsystemFilter->setMinimumWidth(groupWidth);
  distroFilter->setMinimumWidth(distroWidth);
  const bool wrap = browserCard->width() < 130 + stateWidth + groupWidth + distroWidth + 50;
  if (wrap != filterWrapped) {
    filterWrapped = wrap;
    for (auto *w : QList<QWidget *>{filterQuery,stateFilter,subsystemFilter,distroFilter}) filterGrid->removeWidget(w);
    for (int col=0; col<4; ++col) filterGrid->setColumnStretch(col,0);
    if (wrap) {
      filterGrid->addWidget(filterQuery,0,0,1,3);
      filterGrid->addWidget(stateFilter,1,0); filterGrid->addWidget(subsystemFilter,1,1); filterGrid->addWidget(distroFilter,1,2);
      for (int col=0;col<3;++col) filterGrid->setColumnStretch(col,1);
    } else {
      filterGrid->addWidget(filterQuery,0,0); filterGrid->addWidget(stateFilter,0,1); filterGrid->addWidget(subsystemFilter,0,2); filterGrid->addWidget(distroFilter,0,3);
      filterGrid->setColumnStretch(0,2);
    }
  }
  for (auto *view : {tree,results,outlierTree}) {
    view->setColumnWidth(1,qMax(78,metrics.horizontalAdvance("Yours")+18));
    view->setColumnWidth(2,qBound(140,int(view->viewport()->width()*.41),300));
  }
}
void Window::changePage(int page) {
  navigation->setCurrentIndex(page);
  if (page == 1) query->setFocus();
}
QJsonObject Window::filterPayload() const {
  auto text = filterQuery->text().trimmed();
  if (navigation->currentIndex() == 1) text = (query->text().trimmed() + " " + text).trimmed();
  QJsonObject p{{"query",text}, {"state",stateFilter->currentData().toString()},
                {"subsystem",subsystemFilter->currentData().toString()},
                {"distro",distroFilter->currentData().toString()}};
  if (navigation->currentIndex() == 0) p[QStringLiteral("unavailable")] = showUnavailable;
  return p;
}
void Window::applyFilters() {
  if (!uiReady || !session.ready) return;
  const int page = navigation->currentIndex(), revision = session.revision;
  const auto payload = filterPayload();
  if (page == 0 && filterQuery->text().trimmed().isEmpty() && stateFilter->currentIndex() == 0 &&
      subsystemFilter->currentIndex() == 0 && distroFilter->currentIndex() == 0) {
    filterRequest = 0;
    menuFilter->restrictTo({},false);
    pageSubtitle->setText("Browse and configure kernel options");
    return;
  }
  const auto topic = page == 0 ? "filter-options" : page == 1 ? "search" : "outliers";
  auto *model = page == 1 ? searchModel : outlierModel;
  if (page) model->clearMatches();
  pageSubtitle->setText(page == 2 ? "Finding options enabled elsewhere…" : "Finding matching options…");
  const auto id = bus.request(topic,payload,QString("view-filter:%1").arg(page));
  filterRequest = id;
  auto accepted = std::make_shared<QSet<int>>();
  bus.listen(id,this,[this,id,page,revision,payload,model,accepted](const QString &topic,const QJsonObject &p) {
    if (id != filterRequest || navigation->currentIndex() != page || session.revision != revision || filterPayload() != payload) return;
    if (topic == "menu-matches") for (const auto i : p[QStringLiteral("indices")].toArray()) accepted->insert(i.toInt());
    if (topic == "matches") {
      model->appendMatches(p[QStringLiteral("indices")].toArray());
      if (page == 1 && !results->currentIndex().isValid()) results->setCurrentIndex(model->index(0,0));
    }
    if (topic == "completed") {
      if (page == 0) menuFilter->restrictTo(std::move(*accepted),true);
      pageSubtitle->setText(page == 2 ? QString("%1 options disabled here, enabled elsewhere").arg(p[QStringLiteral("total")].toInt())
                                    : QString("%1 matching options").arg(p[QStringLiteral("total")].toInt()));
    }
    if (topic == "failed") { pageSubtitle->setText("Filter unavailable"); message(p[QStringLiteral("error")].toString()); }
  });
}
void Window::search() {
  if (query->text().trimmed().isEmpty()) {
    searchModel->clearMatches();
    if (navigation->currentIndex() == 1) changePage(0);
    return;
  }
  if (navigation->currentIndex() != 1) changePage(1);
  else applyFilters();
}
void Window::outliers() { if (navigation->currentIndex() != 2) changePage(2); else applyFilters(); }
void Window::select(int i) {
  if (i < 0 || i >= session.entries.size())
    return;
  current = i;
  const auto &e = session.entries[i];
  heading->setText(e.prompt);
  symbolLabel->setText(e.name.isEmpty() ? e.type : "CONFIG_" + e.name);
  breadcrumb->setText(QString(e.path).replace(" / ", "  ›  "));
  featureGlyph->setPixmap(uiIcon(featureIcon(e.file,e.name),palette().color(QPalette::Link)).pixmap(38,38));
  copySymbol->setEnabled(!e.name.isEmpty());
  supportBadge->setVisible(!e.name.isEmpty() && e.support > session.distros.size()/2);
  supportBadge->setText(e.value == "n" ? "Common elsewhere" : "Enabled in most distros");
  supportBadge->setToolTip("More than half the collected distro families enable this option in at least one profile.");
  coverage->setText(e.name.isEmpty()
                        ? ""
                        : QString("%1 of %2 distros enable this option")
                              .arg(e.support)
                              .arg(session.distros.size()));
  coverage->setToolTip(
      "Unique distributions with y or m in a collected profile of this target "
      "architecture. Build evidence, not a hardware-support guarantee.");
  stateLabel->setText(
      "Your value: " + e.value + " · " + e.type +
      (e.visible ? "" : " · unavailable under current dependencies"));
  bool tristate =
      e.type == "bool" || e.type == "boolean" || e.type == "tristate";
  noButton->setVisible(tristate);
  modButton->setVisible(tristate);
  yesButton->setVisible(tristate);
  noButton->setEnabled(session.ready && e.allows('n'));
  modButton->setEnabled(session.ready && e.allows('m'));
  yesButton->setEnabled(session.ready && e.allows('y'));
  noButton->setChecked(e.value == "n");
  modButton->setChecked(e.value == "m");
  yesButton->setChecked(e.value == "y");
  bool scalar = e.type == "string" || e.type == "integer" || e.type == "int" ||
                e.type == "hex";
  valueInput->setVisible(scalar);
  setButton->setVisible(scalar);
  valueInput->setText(e.value);
  setButton->setEnabled(session.ready && e.editable);
  valueInput->setEnabled(session.ready && e.editable);
  help->setPlainText("Loading full Kconfig help…");
  driverInfo->setPlainText("Driver context loads when this tab is opened.");
  history->setPlainText("Maintainers load when this tab is opened.");
  related->clear();
  comparison->setRowCount(0);
  option = {};
  auto id = bus.request("select", {{"index", i},{"details",false}}, "selection");
  selectedRequest = id;
  contextRequested = patchesRequested = false;
  listenSelected(id,i);
  requestDetails();
}
void Window::requestDetails() {
  if (!session.ready || current < 0 || !selectedRequest) return;
  const int tab = tabs->currentIndex();
  if ((tab == 1 || tab == 4) && !contextRequested) {
    contextRequested = true;
    driverInfo->setPlainText("Loading driver and device context…");
    history->setPlainText("Loading subsystem maintainers…");
    listenSelected(bus.request("context",{{"index",current}},"selection-context"),current);
  }
  if (tab == 3 && !patchesRequested) {
    patchesRequested = true;
    related->clear(); related->addItem("Finding related patches…");
    listenSelected(bus.request("related",{{"index",current}},"selection-patches"),current);
  }
}
void Window::listenSelected(quint64 id, int i) {
  const auto selection = selectedRequest;
  bus.listen(id, this, [this, i, selection](const QString &topic, const QJsonObject &p) {
    if (current != i || selection != selectedRequest)
      return;
    if (p.contains("revision") &&
        p[QStringLiteral("revision")].toInt() != session.revision)
      return;
    if (topic == "option")
      renderOption(p);
    if (topic == "context")
      renderContext(p[QStringLiteral("context")].toObject());
    if (topic == "related") {
      related->clear();
      for (const auto value : p[QStringLiteral("patches")].toArray()) {
        auto patch = value.toObject();
        auto *item = new QListWidgetItem(
            patch[QStringLiteral("name")].toString() + "\n" +
                patch[QStringLiteral("family")].toString() + " · " +
                patch[QStringLiteral("match")].toString(),
            related);
        item->setData(Qt::UserRole, patch[QStringLiteral("path")].toString());
      }
      if (!related->count())
        related->addItem(
            "No collected patch matches this symbol or its Kconfig file.");
    }
    if (topic == "failed")
      message(p[QStringLiteral("error")].toString());
  });
}
void Window::renderOption(const QJsonObject &p) {
  option = p;
  const auto colors = palette();
  const auto card = QString("<table width='100%' border='1' cellspacing='0' cellpadding='14' style='border-color:%1'><tr><td>")
      .arg(colors.color(QPalette::Mid).name());
  QString text = card + "<h3>" + html(p[QStringLiteral("description")].toString()) + "</h3>";
  auto description = p[QStringLiteral("help")].toString();
  if (description.isEmpty())
    description = "No additional help text is supplied by this kernel source.";
  for (auto paragraph : description.split("\n\n")) {
    paragraph.replace(QRegularExpression("\\s*\n\\s*"), " ");
    text += "<p>" + html(paragraph.trimmed()) + "</p>";
  }
  auto relations = p[QStringLiteral("relations")].toString();
  int definition = relations.indexOf("Symbol:");
  if (definition >= 0)
    relations = relations.mid(definition);
  text += "</td></tr></table><br>" + card + "<h3>Dependencies and relationships</h3><pre "
          "style='white-space:pre-wrap'>" +
          html(relations) +
          "</pre><p>Source: " + html(p[QStringLiteral("source")].toString()) +
          ":" + QString::number(p[QStringLiteral("line")].toInt()) + "</p></td></tr></table>";
  help->setHtml(text);
  auto profiles = p[QStringLiteral("profiles")].toArray();
  comparison->setRowCount(profiles.size());
  for (int row = 0; row < profiles.size(); ++row) {
    auto profile = profiles[row].toObject();
    auto distro = profile[QStringLiteral("distro")].toString();
    auto *name = new QTableWidgetItem(
        QIcon(assets + "/distro/" + distro + ".svg"),
        distro + " " + profile[QStringLiteral("release")].toString());
    name->setToolTip(profile[QStringLiteral("source")].toString() + "\n" +
                     profile[QStringLiteral("kind")].toString() + "\n" +
                     profile[QStringLiteral("note")].toString());
    comparison->setItem(row, 0, name);
    comparison->setItem(
        row, 1,
        new QTableWidgetItem(profile[QStringLiteral("flavor")].toString()));
    comparison->setItem(
        row, 2,
        new QTableWidgetItem(profile[QStringLiteral("kernel")].toString()));
    auto value = profile[QStringLiteral("value")].isNull()
                     ? "not recorded"
                     : profile[QStringLiteral("value")].toString();
    auto *cell = new QTableWidgetItem(value);
    cell->setForeground(stateColor(value));
    comparison->setItem(row, 3, cell);
  }
  comparison->resizeColumnsToContents();
  comparison->horizontalHeader()->setStretchLastSection(true);
}
void Window::renderContext(const QJsonObject &context) {
  QString text =
      "<h3>What this driver is for</h3><p>" + html(heading->text()) + "</p>";
  for (const auto notice : context[QStringLiteral("notices")].toArray())
    text += "<p><b>" + html(notice[QStringLiteral("kind")].toString()) +
            "</b> · " + html(notice[QStringLiteral("evidence")].toString()) +
            "</p>";
  text +=
      "<p><b>Build objects:</b> " +
      html(strings(context[QStringLiteral("objects")].toArray()).join(' ')) +
      "</p>";
  auto devices = context[QStringLiteral("devices")].toArray();
  if (devices.isEmpty())
    text += "<p>No directly matched, currently bound device was found. "
            "Indirect builds, unplugged devices and virtual drivers may not "
            "appear here.</p>";
  for (const auto device : devices)
    text += "<h3>" + html(device[QStringLiteral("product")].toString()) +
            "</h3><p><b>Company:</b> " +
            html(device[QStringLiteral("vendor")].toString()) +
            "<br><b>Device:</b> " +
            html(device[QStringLiteral("bus")].toString() + " " +
                 device[QStringLiteral("address")].toString()) +
            "<br><b>Bound driver:</b> " +
            html(device[QStringLiteral("driver")].toString()) +
            "<br><b>Module:</b> " +
            html(device[QStringLiteral("module")].toString()) + "</p>";
  text += "<h3>Source mapping</h3><p>" +
          html(context[QStringLiteral("mapping")].toString()) + "</p>";
  for (const auto file : context[QStringLiteral("source_files")].toArray())
    text += "<p>" + html(file.toString()) + "</p>";
  driverInfo->setHtml(text);
  QString owners = "<h3>Subsystem maintainers</h3><p>From the selected source "
                   "tree’s MAINTAINERS file. Maintainer responsibility is "
                   "distinct from authorship.</p>";
  for (const auto section : context[QStringLiteral("maintainers")].toArray()) {
    owners += "<h3>" + html(section[QStringLiteral("subsystem")].toString()) +
              "</h3><p>" + html(section[QStringLiteral("status")].toString()) +
              "</p>";
    for (const auto person : section[QStringLiteral("people")].toArray())
      owners += "<p>" + html(person.toString()) + "<br>" +
                html(domainHint(person.toString())) + "</p>";
  }
  history->setHtml(owners +
                   "<p>Email domains are affiliation hints. Sponsorship is not "
                   "established by an email address.</p>");
}
void Window::edit(const QString &value) {
  if (current < 0 || !session.ready)
    return;
  auto id = bus.request("edit", {{"index", current}, {"value", value}});
  bus.listen(id, this, [this](const QString &topic, const QJsonObject &p) {
    if (topic == "failed")
      message(p[QStringLiteral("error")].toString());
  });
}

void Window::save(bool minimal) {
  auto *dialog = toolDialog(
      this, minimal ? "Export minimal defconfig" : "Save working config", 800,
      250);
  auto *layout = new QVBoxLayout(dialog);
  auto *intro = new QLabel("Export a working copy. The imported config and "
                           "kernel source stay unchanged.");
  intro->setWordWrap(true);
  layout->addWidget(intro);
  auto *path = new QLineEdit(QString(KM_SOURCE_DIR) +
                             (minimal ? "/defconfig" : "/navi.config"));
  path->setAccessibleName("Export destination");
  auto *row = new QHBoxLayout;
  row->addWidget(path, 1);
  auto *browse = new QPushButton("Choose folder…");
  row->addWidget(browse);
  layout->addLayout(row);
  auto *reason = new QLabel("Checking destination permissions…");
  reason->setWordWrap(true);
  layout->addWidget(reason);
  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel);
  auto *save =
      buttons->addButton("Save working copy", QDialogButtonBox::AcceptRole);
  save->setEnabled(false);
  save->setToolTip(reason->text());
  layout->addWidget(buttons);
  const auto key = "save-permission:" + QUuid::createUuid().toString();
  auto check = [this, dialog, path, reason, save, key] {
    if (!path->isEnabled())
      return;
    save->setEnabled(false);
    save->setToolTip("Checking destination permissions…");
    reason->setText(save->toolTip());
    auto target = path->text();
    auto id = bus.request("save-permission", {{"path", target}}, key);
    bus.listen(
        id, dialog,
        [path, reason, save, target](const QString &topic,
                                     const QJsonObject &p) {
          if (!path->isEnabled() || path->text() != target)
            return;
          if (topic == "failed") {
            reason->setText(p[QStringLiteral("error")].toString());
            save->setToolTip(reason->text());
            return;
          }
          if (topic != "completed")
            return;
          auto why = p[QStringLiteral("reason")].toString();
          save->setEnabled(p[QStringLiteral("writable")].toBool());
          save->setToolTip(why.isEmpty() ? "Write only this working copy."
                                         : why);
          reason->setText(
              why.isEmpty()
                  ? (p[QStringLiteral("exists")].toBool()
                         ? "This working file exists and will be replaced."
                         : "This destination is writable.")
                  : why);
        });
  };
  auto *timer = new QTimer(dialog);
  timer->setSingleShot(true);
  timer->setInterval(100);
  connect(path, &QLineEdit::textChanged, dialog, [save, reason, timer] {
    save->setEnabled(false);
    save->setToolTip("Checking destination permissions…");
    reason->setText(save->toolTip());
    timer->start();
  });
  connect(timer, &QTimer::timeout, dialog, check);
  connect(browse, &QPushButton::clicked, dialog, [this, dialog, path] {
    chooseFiles(bus, dialog, "Choose an export folder", QString(KM_SOURCE_DIR),
                false, true, [path](const QStringList &files) {
                  if (!path->isEnabled())
                    return;
                  auto name = path->text().section('/', -1);
                  path->setText(files.first() + "/" +
                                (name.isEmpty() ? "navi.config" : name));
                });
  });
  connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::close);
  connect(
      save, &QPushButton::clicked, dialog,
      [this, dialog, path, browse, save, reason, minimal] {
        save->setEnabled(false);
        path->setEnabled(false);
        browse->setEnabled(false);
        reason->setText("Saving working copy…");
        auto id =
            bus.request("save", {{"path", path->text()}, {"minimal", minimal}});
        bus.listen(id, dialog,
                   [dialog, path, browse, save, reason](const QString &topic,
                                                        const QJsonObject &p) {
                     if (topic == "completed")
                       dialog->close();
                     if (topic == "failed") {
                       path->setEnabled(true);
                       browse->setEnabled(true);
                       reason->setText(p[QStringLiteral("error")].toString());
                       save->setToolTip(reason->text());
                     }
                   });
      });
  check();
  dialog->show();
}
void Window::discardThen(std::function<void()> continuation) {
  if (!session.edited) {
    continuation();
    return;
  }
  if (discardDialog) {
    discardDialog->raise();
    return;
  }
  auto *dialog = toolDialog(this, "In-memory config changes", 620, 170);
  discardDialog = dialog;
  auto *layout = new QVBoxLayout(dialog);
  auto *text = new QLabel("Discard the edits in this session? Your source "
                          "config has not been changed.");
  text->setWordWrap(true);
  layout->addWidget(text);
  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Discard |
                                       QDialogButtonBox::Cancel);
  layout->addWidget(buttons);
  connect(buttons->button(QDialogButtonBox::Cancel), &QPushButton::clicked,
          dialog, &QDialog::close);
  connect(buttons->button(QDialogButtonBox::Discard), &QPushButton::clicked,
          dialog, [this, dialog, continuation = std::move(continuation)] {
            discardDialog.clear();
            dialog->close();
            continuation();
          });
  dialog->show();
}
void Window::load(const QString &file) {
  discardThen([this, file] {
    auto id = bus.request("load", {{"path", file}});
    bus.listen(id, this, [this](const QString &topic, const QJsonObject &p) {
      if (topic == "failed")
        message(p[QStringLiteral("error")].toString());
    });
  });
}
void Window::closeEvent(QCloseEvent *event) {
  if (session.edited && !allowClose) {
    event->ignore();
    discardThen([this] {
      allowClose = true;
      close();
    });
    return;
  }
  event->accept();
}
void Window::sourceSession() {
  auto *dialog = toolDialog(this, "Open a source session", 800, 330);
  auto *layout = new QVBoxLayout(dialog);
  auto *intro = new QLabel("Choose source and target architecture. The current "
                           "session stays intact.");
  intro->setWordWrap(true);
  layout->addWidget(intro);
  auto *source = new QLineEdit(session.sourcePath);
  source->setAccessibleName("Kernel source directory");
  layout->addWidget(source);
  auto *browse = new QPushButton("Choose source directory…");
  layout->addWidget(browse);
  auto *arch = new QComboBox;
  arch->addItems({"x86_64", "arm64"});
  arch->setCurrentText(session.architecture);
  arch->setAccessibleName("Target architecture");
  layout->addWidget(arch);
  auto *compiler = new QComboBox;
  compiler->addItems({"gcc", "llvm"});
  compiler->setCurrentText(session.toolchain);
  compiler->setAccessibleName("Kernel compiler");
  layout->addWidget(compiler);
  auto *config = new QLineEdit;
  config->setPlaceholderText("Optional absolute .config path; blank uses the "
                             "source config or target defconfig");
  config->setAccessibleName("Initial target config");
  layout->addWidget(config);
  auto *reason = new QLabel;
  reason->setWordWrap(true);
  layout->addWidget(reason);
  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel);
  auto *open =
      buttons->addButton("Open source session", QDialogButtonBox::AcceptRole);
  open->setEnabled(false);
  layout->addWidget(buttons);
  auto payload = [source, config, arch, compiler] {
    return QJsonObject{{"source", source->text()},
                       {"config", config->text()},
                       {"arch", arch->currentText()},
                       {"toolchain", compiler->currentText()},
                       {"platform", QGuiApplication::platformName()}};
  };
  const auto key = "source-permission:" + QUuid::createUuid().toString();
  auto validate = [this, dialog, payload, reason, open, key] {
    open->setEnabled(false);
    open->setToolTip("Checking source access…");
    reason->setText(open->toolTip());
    auto requested = payload();
    auto id = bus.request("source-permission", requested, key);
    bus.listen(id, dialog,
               [payload, requested, reason, open](const QString &topic,
                                                  const QJsonObject &p) {
                 if (payload() != requested)
                   return;
                 if (topic == "completed") {
                   auto why = p[QStringLiteral("reason")].toString();
                   reason->setText(why);
                   open->setToolTip(why);
                   open->setEnabled(why.isEmpty());
                 }
                 if (topic == "failed") {
                   reason->setText(p[QStringLiteral("error")].toString());
                   open->setToolTip(reason->text());
                 }
               });
  };
  connect(source, &QLineEdit::textChanged, dialog, validate);
  connect(config, &QLineEdit::textChanged, dialog, validate);
  connect(arch, &QComboBox::currentTextChanged, dialog, validate);
  connect(compiler, &QComboBox::currentTextChanged, dialog, validate);
  connect(arch, &QComboBox::currentTextChanged, dialog,
          [compiler](const QString &value) {
            if (value == "arm64")
              compiler->setCurrentText("llvm");
          });
  connect(browse, &QPushButton::clicked, dialog, [this, dialog, source] {
    chooseFiles(
        bus, dialog, "Choose kernel sources", source->text(), false, true,
        [source](const QStringList &files) { source->setText(files.first()); });
  });
  connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::close);
  connect(open, &QPushButton::clicked, dialog,
          [this, dialog, payload, reason, open] {
            open->setEnabled(false);
            auto id = bus.request("launch-session", payload());
            bus.listen(
                id, dialog,
                [dialog, reason](const QString &topic, const QJsonObject &p) {
                  if (topic == "completed")
                    dialog->close();
                  if (topic == "failed")
                    reason->setText(p[QStringLiteral("error")].toString());
                });
          });
  validate();
  dialog->show();
}
void Window::hardware() {
  auto *dialog =
      toolDialog(this, "This machine · devices and bound drivers", 1150, 700);
  auto *layout = new QVBoxLayout(dialog);
  auto *status = new QLabel("Loading device evidence…");
  status->setWordWrap(true);
  layout->addWidget(status);
  auto *view =
      table(5, {"Device", "Company", "Product", "Driver / module", "Modalias"});
  layout->addWidget(view, 1);
  auto id =
      bus.request("machine", {}, "machine:" + QUuid::createUuid().toString());
  bus.listen(
      id, dialog, [view, status](const QString &topic, const QJsonObject &p) {
        if (topic == "failed") {
          status->setText(p[QStringLiteral("error")].toString());
          return;
        }
        if (topic != "completed")
          return;
        auto devices = p[QStringLiteral("devices")].toArray();
        view->setRowCount(devices.size());
        for (int row = 0; row < devices.size(); ++row) {
          auto d = devices[row].toObject();
          QStringList cells{d[QStringLiteral("bus")].toString() + " " +
                                d[QStringLiteral("address")].toString(),
                            d[QStringLiteral("vendor")].toString(),
                            d[QStringLiteral("product")].toString(),
                            d[QStringLiteral("module")].toString(),
                            d[QStringLiteral("modalias")].toString()};
          for (int col = 0; col < cells.size(); ++col)
            view->setItem(row, col, new QTableWidgetItem(cells[col]));
        }
        view->resizeColumnsToContents();
        status->setText("Observed PCI/USB devices and currently bound drivers. "
                        "Double-click a driver to inspect its options.");
      });
  connect(view, &QTableWidget::cellDoubleClicked, dialog,
          [this, view](int row, int) {
            if (auto *item = view->item(row, 3);
                item && !item->text().isEmpty())
              query->setText(item->text());
          });
  dialog->show();
}
void Window::suggestions() {
  auto *dialog = toolDialog(this, "Workload-aware suggestions", 1180, 800);
  auto *layout = new QVBoxLayout(dialog);
  auto *workload = new QComboBox;
  workload->addItems({"Desktop", "Gaming / low latency", "Build workstation",
                      "Server / storage"});
  workload->setAccessibleName("Machine workload");
  layout->addWidget(workload);
  auto *split = new QSplitter;
  auto *list = new QListWidget;
  auto *explanation = linkBrowser(bus);
  split->addWidget(list);
  split->addWidget(explanation);
  split->setSizes({420, 760});
  layout->addWidget(split, 1);
  auto *buttons = new QHBoxLayout;
  auto *inspect = new QPushButton("Inspect option");
  auto *apply = new QPushButton("Apply selected value in memory");
  buttons->addWidget(inspect);
  buttons->addWidget(apply);
  layout->addLayout(buttons);
  apply->setEnabled(false);
  inspect->setEnabled(false);
  const auto key = "suggestions:" + QUuid::createUuid().toString();
  auto populate = [this, dialog, workload, list, explanation, apply, inspect,
                   key] {
    list->clear();
    apply->setEnabled(false);
    inspect->setEnabled(false);
    explanation->setPlainText("Evaluating workload and machine evidence…");
    auto requested = workload->currentText();
    auto id = bus.request("suggestions", {{"workload", requested}}, key);
    bus.listen(
        id, dialog,
        [workload, requested, list, explanation](const QString &topic,
                                                 const QJsonObject &p) {
          if (workload->currentText() != requested)
            return;
          if (topic == "failed") {
            explanation->setPlainText(p[QStringLiteral("error")].toString());
            return;
          }
          if (topic != "completed")
            return;
          for (const auto value : p[QStringLiteral("items")].toArray()) {
            auto proposal = value.toObject();
            auto *item = new QListWidgetItem(
                proposal[QStringLiteral("title")].toString() + "\nCONFIG_" +
                    proposal[QStringLiteral("symbol")].toString() + ": " +
                    proposal[QStringLiteral("current")].toString() + " → " +
                    proposal[QStringLiteral("proposed")].toString(),
                list);
            item->setData(Qt::UserRole, proposal);
          }
          if (list->count())
            list->setCurrentRow(0);
          else
            explanation->setPlainText(
                "No proposals from the current rule set. This is not a "
                "complete compatibility or security audit.");
        });
  };
  connect(workload, &QComboBox::currentTextChanged, dialog, populate);
  connect(list, &QListWidget::currentItemChanged, dialog,
          [explanation, apply, inspect](QListWidgetItem *item) {
            if (!item)
              return;
            auto p = item->data(Qt::UserRole).toJsonObject();
            QString text =
                "<h2>" + html(p[QStringLiteral("title")].toString()) + "</h2>";
            for (const auto &field :
                 QStringList{"reason", "security", "performance", "utility",
                             "helpfulness", "confidence"})
              text += "<h3>" + html(field) + "</h3><p>" +
                      html(p[field].toString()) + "</p>";
            auto source = p[QStringLiteral("reference")].toString();
            text +=
                "<p>" +
                (source.startsWith("https://")
                     ? "<a href='" + html(source) + "'>" + html(source) + "</a>"
                     : html(source)) +
                "</p>";
            explanation->setHtml(text);
            apply->setEnabled(p[QStringLiteral("can_apply")].toBool());
            inspect->setEnabled(true);
          });
  connect(inspect, &QPushButton::clicked, dialog, [this, list] {
    if (list->currentItem())
      query->setText(list->currentItem()
                         ->data(Qt::UserRole)
                         .toJsonObject()[QStringLiteral("symbol")]
                         .toString());
  });
  connect(apply, &QPushButton::clicked, dialog,
          [this, dialog, list, explanation, apply, populate] {
            if (!list->currentItem())
              return;
            auto p = list->currentItem()->data(Qt::UserRole).toJsonObject();
            int i = session.find(p[QStringLiteral("symbol")].toString());
            if (i < 0)
              return;
            apply->setEnabled(false);
            auto id =
                bus.request("edit", {{"index", i},
                                     {"value", p[QStringLiteral("proposed")]}});
            bus.listen(id, dialog,
                       [populate, explanation](const QString &topic,
                                               const QJsonObject &p) {
                         if (topic == "completed")
                           populate();
                         if (topic == "failed")
                           explanation->setPlainText(
                               p[QStringLiteral("error")].toString());
                       });
          });
  populate();
  dialog->show();
}
void Window::internals() {
  auto *dialog = toolDialog(this, "Kernel maintainer overview", 1200, 800);
  auto *layout = new QVBoxLayout(dialog);
  auto *status = new QLabel("Loading maintainer evidence…");
  layout->addWidget(status);
  auto *pages = new QTabWidget;
  layout->addWidget(pages, 1);
  auto *settings = table(3, {"Kernel option", "Working value", "Description"});
  pages->addTab(settings, "ABI / signing / build inputs");
  auto *lifecycle = new QListWidget;
  pages->addTab(lifecycle, "Lifecycle notices");
  auto *runtime = linkBrowser(bus);
  pages->addTab(runtime, "Runtime and external inputs");
  auto id = bus.request("internals", {},
                        "internals:" + QUuid::createUuid().toString());
  bus.listen(
      id, dialog,
      [settings, lifecycle, runtime, status](const QString &topic,
                                             const QJsonObject &p) {
        if (topic == "failed") {
          status->setText(p[QStringLiteral("error")].toString());
          return;
        }
        if (topic == "lifecycle") {
          for (const auto value : p[QStringLiteral("items")].toArray()) {
            auto r = value.toObject();
            auto *item = new QListWidgetItem(
                r[QStringLiteral("symbol")].toString() + " · " +
                    r[QStringLiteral("notice")].toString() + " · " +
                    r[QStringLiteral("value")].toString() + "\n" +
                    r[QStringLiteral("prompt")].toString(),
                lifecycle);
            item->setData(Qt::UserRole, r[QStringLiteral("index")].toInt());
          }
          return;
        }
        if (topic != "completed")
          return;
        auto records = p[QStringLiteral("settings")].toArray();
        settings->setRowCount(records.size());
        for (int row = 0; row < records.size(); ++row) {
          auto r = records[row].toObject();
          QStringList cells{r[QStringLiteral("symbol")].toString(),
                            r[QStringLiteral("value")].toString(),
                            r[QStringLiteral("description")].toString()};
          for (int col = 0; col < cells.size(); ++col)
            settings->setItem(row, col, new QTableWidgetItem(cells[col]));
        }
        settings->resizeColumnsToContents();
        QString text =
            "<h3>Running kernel and external build inputs</h3><p>Runtime "
            "observations describe the currently running kernel, not the "
            "in-memory config.</p>";
        for (const auto value : p[QStringLiteral("runtime")].toArray())
          text += "<p><b>" + html(value[QStringLiteral("name")].toString()) +
                  ":</b> " + html(value[QStringLiteral("value")].toString()) +
                  "</p>";
        runtime->setHtml(text);
        status->setText("Lifecycle notices are textual matches. Inspect the "
                        "full wording before drawing a conclusion.");
      });
  connect(settings, &QTableWidget::cellDoubleClicked, dialog,
          [this, settings](int row, int) {
            if (auto *item = settings->item(row, 0))
              query->setText(item->text());
          });
  connect(lifecycle, &QListWidget::itemDoubleClicked, dialog,
          [this](QListWidgetItem *item) {
            select(item->data(Qt::UserRole).toInt());
          });
  dialog->show();
}

void Window::patchBrowser() {
  auto *dialog = toolDialog(
      this,
      "Patch library · " + QString::number(session.patches.size()) + " records",
      1380, 920);
  auto *layout = new QVBoxLayout(dialog);
  auto *filter = new QLineEdit;
  filter->setPlaceholderText(
      "Search subject, author, rationale, distro or exact base");
  filter->setAccessibleName("Search patch library");
  layout->addWidget(filter);
  auto *sourceRow = new QHBoxLayout;
  sourceRow->addWidget(new QLabel("Check against source tree:"));
  auto *source = new QLineEdit(session.sourcePath);
  source->setAccessibleName("Patch check source tree");
  sourceRow->addWidget(source, 1);
  auto *browse = new QPushButton("Browse…");
  sourceRow->addWidget(browse);
  layout->addLayout(sourceRow);
  connect(browse, &QPushButton::clicked, dialog, [this, dialog, source] {
    chooseFiles(bus, dialog, "Kernel source for patch checks", source->text(),
                false, true, [source](const QStringList &files) {
                  source->setText(files.first());
                });
  });
  auto *split = new QSplitter;
  auto *list = new QListWidget;
  auto *pages = new QTabWidget;
  auto *summary = linkBrowser(bus);
  auto *preview = new TextPreview(bus);
  auto *checks = linkBrowser(bus);
  pages->addTab(summary, "Why / who / base");
  pages->addTab(preview, "Full patch");
  pages->addTab(checks, "Clean-apply result");
  split->addWidget(list);
  split->addWidget(pages);
  split->setSizes({480, 860});
  layout->addWidget(split, 1);
  auto *buttons = new QHBoxLayout;
  auto *check = new QPushButton("Check selected patch");
  auto *stop = new QPushButton("Cancel check");
  auto *build = new QPushButton("Use selected patch in build workspace");
  buttons->addWidget(check);
  buttons->addWidget(stop);
  buttons->addWidget(build);
  layout->addLayout(buttons);
  check->setEnabled(false);
  stop->setEnabled(false);
  build->setEnabled(false);
  auto job = std::make_shared<quint64>(0);
  const auto key = "patch-search:" + QUuid::createUuid().toString();
  auto populate = [this, dialog, filter, list, key] {
    list->clear();
    auto query = filter->text();
    auto id = bus.request("patch-search", {{"query", query}}, key);
    bus.listen(
        id, dialog,
        [this, filter, query, list](const QString &topic,
                                    const QJsonObject &p) {
          if (filter->text() != query || topic != "patch-matches")
            return;
          for (const auto value : p[QStringLiteral("indices")].toArray()) {
            int i = value.toInt();
            if (i < 0 || i >= session.patches.size())
              continue;
            auto patch = session.patches[i].toObject();
            auto subjects =
                strings(patch[QStringLiteral("subjects")].toArray());
            auto authors = strings(patch[QStringLiteral("authors")].toArray());
            auto *item = new QListWidgetItem(
                (subjects.isEmpty() ? patch[QStringLiteral("name")].toString()
                                    : subjects.first()) +
                    "\n" + patch[QStringLiteral("family")].toString() + " · " +
                    (authors.isEmpty() ? "Author not supplied"
                                       : authors.join(", ")),
                list);
            item->setData(Qt::UserRole, patch);
          }
          if (!list->currentItem() && list->count())
            list->setCurrentRow(0);
        });
  };
  auto *timer = new QTimer(dialog);
  timer->setSingleShot(true);
  timer->setInterval(100);
  connect(filter, &QLineEdit::textChanged, timer, [timer] { timer->start(); });
  connect(timer, &QTimer::timeout, dialog, populate);
  connect(list, &QListWidget::currentItemChanged, dialog,
          [this, summary, preview, checks, check, build,
           job](QListWidgetItem *item) {
            build->setEnabled(item);
            check->setEnabled(item && !*job);
            if (!item)
              return;
            auto p = item->data(Qt::UserRole).toJsonObject();
            QString text = "<h2>" + html(p[QStringLiteral("name")].toString()) +
                           "</h2><p><b>Collection:</b> " +
                           html(p[QStringLiteral("family")].toString()) +
                           "</p><h3>Who wrote it</h3>";
            auto authors = strings(p[QStringLiteral("authors")].toArray());
            if (authors.isEmpty())
              text += "<p>No author was supplied by the published patch.</p>";
            for (const auto &author : authors)
              text += "<p>" + html(author) + "</p>";
            text +=
                "<p><b>Date:</b> " +
                html(p[QStringLiteral("date")].toString()) +
                "</p><h3>Why it exists</h3><p style='white-space:pre-wrap'>" +
                html(p[QStringLiteral("rationale")].toString()) + "</p>";
            for (const auto &field :
                 QStringList{"base", "base_revision", "preceding_release",
                             "base_evidence", "note"})
              text += "<p><b>" + html(field) + ":</b> " +
                      html(p[field].toString()) + "</p>";
            text += "<p><a href='" +
                    html(p[QStringLiteral("source")].toString()) +
                    "'>Published source</a><br>SHA256: " +
                    html(p[QStringLiteral("sha256")].toString()) + "</p>";
            summary->setHtml(text);
            preview->openFile(session.catalogPath + "/" +
                              p[QStringLiteral("path")].toString());
            if (!*job)
              checks->setPlainText(
                  "Not checked against this source tree. A check reads the "
                  "current files without changing them.");
          });
  connect(
      check, &QPushButton::clicked, dialog,
      [this, dialog, list, source, pages, checks, check, stop, job] {
        if (!list->currentItem() || *job)
          return;
        auto p = list->currentItem()->data(Qt::UserRole).toJsonObject();
        auto name = p[QStringLiteral("name")].toString();
        auto tree = source->text();
        check->setEnabled(false);
        stop->setEnabled(true);
        pages->setCurrentWidget(checks);
        checks->setPlainText("Checking " + name + " against " + tree + "…");
        auto output = std::make_shared<QString>();
        *job = bus.runProcess(
            "python3",
            {QString(KM_SOURCE_DIR) + "/tools/kernel_work.py", "check",
             "--source", tree, "--patch",
             session.catalogPath + "/" + p[QStringLiteral("path")].toString()},
            QString(KM_SOURCE_DIR), "patch applicability");
        bus.listen(
            *job, dialog,
            [checks, check, stop, job, output, name,
             tree](const QString &topic, const QJsonObject &p) {
              if (topic == "output") {
                if (output->size() < 2 * 1024 * 1024)
                  *output += p[QStringLiteral("text")].toString();
                return;
              }
              if (topic != "completed" && topic != "failed" &&
                  topic != "cancelled")
                return;
              *job = 0;
              check->setEnabled(true);
              stop->setEnabled(false);
              if (topic == "cancelled") {
                checks->setPlainText("Patch check cancelled.");
                return;
              }
              if (p[QStringLiteral("output_truncated")].toBool()) {
                checks->setPlainText(
                    "The check output exceeded the display limit. No "
                    "applicability conclusion is available.");
                return;
              }
              if (topic == "failed") {
                checks->setPlainText(p[QStringLiteral("error")].toString() +
                                     "\n" + output->left(32768));
                return;
              }
              QJsonParseError error;
              auto doc = QJsonDocument::fromJson(output->toUtf8(), &error);
              auto result = doc.object();
              auto status = result[QStringLiteral("status")].toString();
              if (error.error != QJsonParseError::NoError || status.isEmpty()) {
                checks->setPlainText(
                    "The check did not produce a valid result.\n" +
                    output->left(32768));
                return;
              }
              auto title = status == "applies-cleanly" ? "Applies cleanly"
                           : status == "already-applied-match"
                               ? "Matches an already-applied patch"
                               : "Does not apply cleanly";
              checks->setHtml(
                  "<h2>" + QString(title) + "</h2><p><b>" + html(name) +
                  "</b><br>" + html(tree) + "<br>" +
                  html(result[QStringLiteral("checked_at")].toString()) +
                  "</p><p>" + html(result[QStringLiteral("scope")].toString()) +
                  "</p><pre style='white-space:pre-wrap'>" +
                  html(result[QStringLiteral("details")].toString().left(
                      32768)) +
                  "</pre>");
            });
      });
  connect(stop, &QPushButton::clicked, dialog, [this, job, stop] {
    if (*job)
      bus.cancel(*job);
    stop->setEnabled(false);
  });
  connect(dialog, &QObject::destroyed, this, [this, job] {
    if (*job)
      bus.cancel(*job);
  });
  connect(build, &QPushButton::clicked, dialog, [this, list] {
    if (!list->currentItem())
      return;
    auto *jobs = new JobsDialog(bus, session, this);
    jobs->addPatch(session.catalogPath + "/" +
                   list->currentItem()
                       ->data(Qt::UserRole)
                       .toJsonObject()[QStringLiteral("path")]
                       .toString());
    jobs->show();
  });
  populate();
  dialog->show();
}
void Window::attribution() {
  if (current < 0)
    return;
  int index = current;
  auto selection = selectedRequest;
  history->setPlainText("Loading attribution…");
  auto id =
      bus.request("attribution-plan", {{"index", index}}, "attribution-plan");
  bus.listen(
      id, this,
      [this, index, selection](const QString &topic, const QJsonObject &p) {
        if (current != index || selectedRequest != selection)
          return;
        if (topic == "failed") {
          history->setPlainText(p[QStringLiteral("error")].toString());
          return;
        }
        if (topic != "completed")
          return;
        auto file = p[QStringLiteral("file")].toString(),
             version = p[QStringLiteral("version")].toString();
        auto output = std::make_shared<QString>();
        auto job = bus.runProcess(
            p[QStringLiteral("program")].toString(),
            strings(p[QStringLiteral("arguments")].toArray()),
            p[QStringLiteral("directory")].toString(), "Git attribution");
        bus.listen(
            job, this,
            [this, index, selection, file, version,
             output](const QString &topic, const QJsonObject &p) {
              if (current != index || selectedRequest != selection)
                return;
              if (topic == "output") {
                if (output->size() < 1024 * 1024)
                  *output += p[QStringLiteral("text")].toString();
                return;
              }
              if (topic != "completed" && topic != "failed")
                return;
              if (topic == "completed" && !output->isEmpty() &&
                  output->size() < 1024 * 1024 &&
                  !p[QStringLiteral("output_truncated")].toBool()) {
                QMap<QString, int> authors;
                QString name, email;
                for (const auto &line : output->split('\n')) {
                  if (line.startsWith("author "))
                    name = line.mid(7);
                  else if (line.startsWith("author-mail "))
                    email = line.mid(12);
                  else if (line.startsWith('\t'))
                    ++authors[name + " " + email];
                }
                QString text = "<h3>Git blame · option definition</h3><p>" +
                               html(file) + "</p>";
                for (auto it = authors.cbegin(); it != authors.cend(); ++it)
                  text += "<p><b>" + html(it.key()) + "</b><br>" +
                          html(domainHint(it.key())) + " · " +
                          QString::number(it.value()) + " attributed lines</p>";
                history->setHtml(
                    text + "<p>Blame identifies the last editor of definition "
                           "lines, not every implementation contributor. Email "
                           "domains do not prove sponsorship.</p>");
                return;
              }
              QUrl url("https://api.github.com/repos/gregkh/linux/commits");
              QUrlQuery query;
              query.addQueryItem("path", file);
              query.addQueryItem("sha", "v" + version);
              query.addQueryItem("per_page", "20");
              url.setQuery(query);
              auto request = bus.get(url.toString());
              bus.listen(
                  request, this,
                  [this, index, selection, file,
                   version](const QString &topic, const QJsonObject &p) {
                    if (current != index || selectedRequest != selection)
                      return;
                    if (topic == "failed") {
                      history->setPlainText(
                          "Local Git blame was unavailable. Upstream lookup: " +
                          p[QStringLiteral("error")].toString());
                      return;
                    }
                    if (topic != "completed")
                      return;
                    QString text =
                        "<h3>Upstream file history</h3><p>" + html(file) +
                        " · Linux " + html(version) +
                        "</p><p>These commits affect the containing file; they "
                        "are not verified feature-specific attribution.</p>";
                    for (const auto r :
                         QJsonDocument::fromJson(
                             p[QStringLiteral("body")].toString().toUtf8())
                             .array()) {
                      auto commit = r[QStringLiteral("commit")].toObject();
                      auto author = commit[QStringLiteral("author")].toObject();
                      text += "<p><a href='" +
                              html(r[QStringLiteral("html_url")].toString()) +
                              "'>" +
                              html(commit[QStringLiteral("message")]
                                       .toString()
                                       .section('\n', 0, 0)) +
                              "</a><br>" +
                              html(author[QStringLiteral("name")].toString()) +
                              " · " +
                              html(author[QStringLiteral("date")].toString()) +
                              "<br>" +
                              html(author[QStringLiteral("email")].toString()) +
                              "</p>";
                    }
                    history->setHtml(text);
                  });
            });
      });
}
