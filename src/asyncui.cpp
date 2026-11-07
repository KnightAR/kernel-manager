// SPDX-License-Identifier: GPL-2.0-only
#include "asyncui.h"
#include "eventbus.h"
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QGuiApplication>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStyle>
#include <QTextBrowser>
#include <QTimer>
#include <QUrl>
#include <QUuid>
#include <QVBoxLayout>
#include <memory>

quint64 openLink(EventBus &bus, const QUrl &url) {
  return bus.request(
      "open-url",
      {{"url", url.toString()}, {"platform", QGuiApplication::platformName()}});
}
QTextBrowser *linkBrowser(EventBus &bus) {
  auto *view = new QTextBrowser;
  view->setOpenLinks(false);
  view->setOpenExternalLinks(false);
  QObject::connect(
      view, &QTextBrowser::anchorClicked, view, [&bus, view](const QUrl &url) {
        auto id = openLink(bus, url);
        bus.listen(
            id, view, [view](const QString &topic, const QJsonObject &p) {
              if (topic == "failed")
                view->append("<p>" +
                             html(p[QStringLiteral("error")].toString()) +
                             "</p>");
            });
      });
  return view;
}

QDialog *toolDialog(QWidget *parent, const QString &title, int width,
                    int height) {
  auto *dialog = new QDialog(parent);
  dialog->setWindowTitle(title);
  dialog->resize(width, height);
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->setWindowModality(Qt::NonModal);
  return dialog;
}
void chooseFiles(EventBus &bus, QWidget *parent, const QString &title,
                 const QString &directory, bool multiple, bool directories,
                 std::function<void(const QStringList &)> selected) {
  auto *dialog = toolDialog(parent, title, 850, 600);
  auto *layout = new QVBoxLayout(dialog);
  auto *location = new QLineEdit(directory);
  location->setAccessibleName("Directory or file path");
  auto *row = new QHBoxLayout;
  auto *up = new QPushButton("Parent folder");
  row->addWidget(up);
  row->addWidget(location, 1);
  layout->addLayout(row);
  auto *filter = new QLineEdit;
  filter->setPlaceholderText("Filter filenames…");
  filter->setAccessibleName("Filter filenames");
  layout->addWidget(filter);
  auto *hidden = new QCheckBox("Show hidden files");
  hidden->setChecked(true);
  layout->addWidget(hidden);
  auto *files = new QListWidget;
  files->setAccessibleName(directories ? "Folders" : "Files and folders");
  files->setSelectionMode(multiple ? QAbstractItemView::ExtendedSelection
                                   : QAbstractItemView::SingleSelection);
  layout->addWidget(files, 1);
  auto *status = new QLabel;
  status->setWordWrap(true);
  layout->addWidget(status);
  auto *pages = new QHBoxLayout;
  auto *previous = new QPushButton("Previous page"),
       *next = new QPushButton("Next page");
  pages->addWidget(previous);
  pages->addStretch();
  pages->addWidget(next);
  layout->addLayout(pages);
  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel);
  auto *choose =
      buttons->addButton(directories ? "Choose folder" : "Open selected",
                         QDialogButtonBox::AcceptRole);
  choose->setEnabled(false);
  layout->addWidget(buttons);
  struct State {
    QString path, parent, selectedFile;
    int page = 0;
    quint64 request = 0;
    bool ready = false;
  };
  auto state = std::make_shared<State>();
  const auto key = "file-picker:" + QUuid::createUuid().toString();
  auto updateChoice = [state, choose, files, directories] {
    bool valid = state->ready;
    if (directories && files->currentItem())
      valid &= files->currentItem()->data(Qt::UserRole + 2).toBool();
    if (!directories) {
      const auto selected = files->selectedItems();
      valid &= !selected.isEmpty();
      for (auto *item : selected)
        valid &= !item->data(Qt::UserRole + 1).toBool() &&
                 item->data(Qt::UserRole + 2).toBool();
    }
    choose->setEnabled(valid);
    choose->setToolTip(valid ? "Use this selection."
                             : "Choose a readable " +
                                   QString(directories ? "folder." : "file."));
  };
  auto load = [&bus, dialog, location, filter, hidden, files, status, previous,
               next, up, state, key, directories, updateChoice](int page) {
    state->ready = false;
    state->page = page;
    files->clear();
    updateChoice();
    previous->setEnabled(false);
    next->setEnabled(false);
    up->setEnabled(false);
    status->setText("Reading directory…");
    auto id = bus.request("list-directory",
                          {{"path", location->text()},
                           {"query", filter->text()},
                           {"hidden", hidden->isChecked()},
                           {"directories", directories},
                           {"page", page}},
                          key);
    state->request = id;
    bus.listen(
        id, dialog,
        [id, state, location, files, status, previous, next, up,
         updateChoice](const QString &topic, const QJsonObject &p) {
          if (state->request != id)
            return;
          if (topic == "failed") {
            status->setText(p[QStringLiteral("error")].toString());
            return;
          }
          if (topic == "directory") {
            state->page = p[QStringLiteral("page")].toInt();
            state->path = p[QStringLiteral("path")].toString();
            state->parent = p[QStringLiteral("parent")].toString();
            state->selectedFile = p[QStringLiteral("selected")].toString();
            location->setText(state->path);
          }
          if (topic == "directory-entries")
            for (const auto entry : p[QStringLiteral("items")].toArray()) {
              auto e = entry.toObject();
              const bool folder = e[QStringLiteral("directory")].toBool();
              auto *item = new QListWidgetItem(
                  files->style()->standardIcon(folder ? QStyle::SP_DirIcon
                                                      : QStyle::SP_FileIcon),
                  e[QStringLiteral("name")].toString(), files);
              item->setData(Qt::UserRole, e[QStringLiteral("path")].toString());
              item->setData(Qt::UserRole + 1, folder);
              item->setData(Qt::UserRole + 2,
                            e[QStringLiteral("readable")].toBool());
              item->setToolTip(e[QStringLiteral("path")].toString());
              if (e[QStringLiteral("path")].toString() == state->selectedFile)
                item->setSelected(true);
            }
          if (topic == "completed") {
            state->ready = true;
            previous->setEnabled(state->page > 0);
            next->setEnabled(p[QStringLiteral("more")].toBool());
            up->setEnabled(state->parent != state->path);
            status->setText(QString("Page %1 · %2 entries in folder · "
                                    "double-click a folder to enter it")
                                .arg(state->page + 1)
                                .arg(p[QStringLiteral("total")].toInt()));
            updateChoice();
          }
        });
  };
  QObject::connect(location, &QLineEdit::textEdited, dialog,
                   [&bus, state, updateChoice] {
                     bus.cancel(state->request);
                     state->request = 0;
                     state->ready = false;
                     updateChoice();
                   });
  QObject::connect(location, &QLineEdit::returnPressed, dialog,
                   [load] { load(0); });
  auto *timer = new QTimer(dialog);
  timer->setSingleShot(true);
  timer->setInterval(100);
  QObject::connect(filter, &QLineEdit::textChanged, dialog,
                   [&bus, state, timer, updateChoice] {
                     bus.cancel(state->request);
                     state->request = 0;
                     state->ready = false;
                     updateChoice();
                     timer->start();
                   });
  QObject::connect(timer, &QTimer::timeout, dialog, [load] { load(0); });
  QObject::connect(hidden, &QCheckBox::toggled, dialog, [load] { load(0); });
  QObject::connect(previous, &QPushButton::clicked, dialog,
                   [load, state] { load(state->page - 1); });
  QObject::connect(next, &QPushButton::clicked, dialog,
                   [load, state] { load(state->page + 1); });
  QObject::connect(up, &QPushButton::clicked, dialog, [load, state, location] {
    location->setText(state->parent);
    load(0);
  });
  QObject::connect(files, &QListWidget::itemSelectionChanged, dialog,
                   updateChoice);
  QObject::connect(
      files, &QListWidget::itemDoubleClicked, dialog,
      [load, location, choose, directories](QListWidgetItem *item) {
        if (item->data(Qt::UserRole + 1).toBool()) {
          location->setText(item->data(Qt::UserRole).toString());
          load(0);
        } else if (!directories && choose->isEnabled())
          choose->click();
      });
  QObject::connect(
      choose, &QPushButton::clicked, dialog,
      [dialog, state, files, directories, selected = std::move(selected)] {
        QStringList paths;
        if (directories)
          paths << (files->currentItem()
                        ? files->currentItem()->data(Qt::UserRole).toString()
                        : state->path);
        else
          for (auto *item : files->selectedItems())
            paths << item->data(Qt::UserRole).toString();
        if (!paths.isEmpty()) {
          selected(paths);
          dialog->close();
        }
      });
  QObject::connect(buttons, &QDialogButtonBox::rejected, dialog,
                   &QDialog::close);
  load(0);
  dialog->show();
}
QStringList strings(const QJsonArray &array) {
  QStringList result;
  for (const auto value : array)
    result << value.toString();
  return result;
}
QString html(const QString &text) { return text.toHtmlEscaped(); }

TextPreview::TextPreview(EventBus &bus, QWidget *parent)
    : QWidget(parent), bus(bus), key(QUuid::createUuid().toString()) {
  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  text = new QPlainTextEdit;
  text->setReadOnly(true);
  layout->addWidget(text, 1);
  auto *row = new QHBoxLayout;
  previous = new QPushButton("Previous page");
  next = new QPushButton("Next page");
  status = new QLabel("Choose a file.");
  row->addWidget(previous);
  row->addWidget(status, 1);
  row->addWidget(next);
  layout->addLayout(row);
  previous->setEnabled(false);
  next->setEnabled(false);
  auto *find = new QLineEdit;
  find->setPlaceholderText("Find on this page · Enter for next match");
  find->setAccessibleName("Find in source preview");
  layout->addWidget(find);
  connect(find, &QLineEdit::returnPressed, this, [this, find] {
    if (!text->find(find->text())) {
      text->moveCursor(QTextCursor::Start);
      text->find(find->text());
    }
  });
  connect(next, &QPushButton::clicked, this, [this] {
    ++page;
    if (page >= offsets.size())
      offsets << following;
    requestPage(offsets[page]);
  });
  connect(previous, &QPushButton::clicked, this, [this] {
    if (page > 0)
      requestPage(offsets[--page]);
  });
}
void TextPreview::openFile(const QString &file) {
  path = file;
  page = 0;
  offsets = {0};
  requestPage(0);
}
void TextPreview::requestPage(qint64 offset) {
  previous->setEnabled(false);
  next->setEnabled(false);
  status->setText("Loading source text…");
  auto id = bus.request("read-file", {{"path", path}, {"offset", offset}}, key);
  bus.listen(id, this,
             [this, offset](const QString &topic, const QJsonObject &p) {
               if (topic == "failed") {
                 status->setText(p[QStringLiteral("error")].toString());
                 return;
               }
               if (topic != "completed")
                 return;
               text->setPlainText(p[QStringLiteral("text")].toString());
               following = p[QStringLiteral("next")].toInteger();
               status->setText(QString("Bytes %1–%2 of %3")
                                   .arg(offset)
                                   .arg(following)
                                   .arg(p[QStringLiteral("size")].toInteger()));
               next->setEnabled(p[QStringLiteral("more")].toBool());
               previous->setEnabled(page > 0);
             });
}
