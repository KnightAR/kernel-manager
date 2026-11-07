// SPDX-License-Identifier: GPL-2.0-only
#include "menumodel.h"
#include "session.h"
#include "icons.h"
#include <QApplication>
#include <QBrush>
#include <QPalette>

MenuModel::MenuModel(Session &s, bool hierarchy, QObject *parent)
    : QAbstractItemModel(parent), session(s), hierarchy(hierarchy) {
  connect(&session, &Session::entriesChanged, this,
          [this](int first, int count) { updateEntries(first, count); });
}
int MenuModel::entry(const QModelIndex &index) const {
  return index.isValid() ? int(index.internalId()) - 1 : -1;
}
QModelIndex MenuModel::index(int row, int column,
                             const QModelIndex &parent) const {
  if (row < 0 || column < 0 || column >= 3 ||
      (parent.isValid() && (!hierarchy || parent.column() != 0)))
    return {};
  int p = entry(parent);
  if (hierarchy && p >= children.size())
    return {};
  const auto &list = hierarchy ? (p < 0 ? roots : children[p]) : matches;
  return row < list.size() ? createIndex(row, column, quintptr(list[row] + 1))
                           : QModelIndex();
}
QModelIndex MenuModel::parent(const QModelIndex &index) const {
  int i = entry(index);
  if (!hierarchy || i < 0)
    return {};
  return entryIndex(session.entries[i].parent);
}
int MenuModel::rowCount(const QModelIndex &parent) const {
  if (parent.isValid() && (!hierarchy || parent.column() != 0))
    return 0;
  int p = entry(parent);
  if (hierarchy && p >= children.size())
    return 0;
  return hierarchy ? (p < 0 ? roots.size() : children[p].size())
                   : matches.size();
}
QModelIndex MenuModel::entryIndex(int i, int column) const {
  if (i < 0 || i >= session.entries.size())
    return {};
  int row = hierarchy ? (i < position.size() ? position[i] : -1)
                      : matchPositions.value(i, -1);
  return row >= 0 ? createIndex(row, column, quintptr(i + 1)) : QModelIndex();
}
QVariant MenuModel::data(const QModelIndex &index, int role) const {
  int i = entry(index);
  if (i < 0 || i >= session.entries.size())
    return {};
  const auto &e = session.entries[i];
  if (role == Qt::UserRole)
    return i;
  if (role == Qt::DisplayRole)
    return index.column() == 0   ? e.prompt
           : index.column() == 1 ? e.value
                                 : QString();
  if (role == Qt::DecorationRole && index.column() == 0) {
    const auto kind = featureIcon(e.file, e.name);
    if (!featureIcons.contains(kind)) featureIcons.insert(kind, uiIcon(kind));
    return featureIcons.value(kind);
  }
  if (role == Qt::ForegroundRole && !e.visible)
    return QBrush(qApp->palette().color(QPalette::Disabled, QPalette::Text));
  if (role == Qt::ToolTipRole || role == Qt::AccessibleTextRole) {
    QString text = e.prompt + "\nCONFIG_" + e.name +
                   " · working value: " + e.value + "\n" + e.path;
    text += "\n" + QString::number(e.support) + " distros enable this option.";
    for (int d = 0; d < e.coverage.size() && d < session.distros.size(); ++d) {
      int flags = e.coverage[d].digitValue();
      if (!flags)
        continue;
      text += "\n" + session.distros[d][QStringLiteral("label")].toString() +
              ": " +
              (flags == 1   ? "built in"
               : flags == 2 ? "module"
                            : "built in / module across profiles");
    }
    return text;
  }
  return {};
}
QVariant MenuModel::headerData(int section, Qt::Orientation orientation,
                               int role) const {
  if (orientation == Qt::Horizontal && role == Qt::DisplayRole)
    return QStringList{"Kernel option", "Yours", "Enabled by distro"}.value(
        section);
  return {};
}
void MenuModel::clearMatches() {
  beginResetModel();
  matches.clear();
  matchPositions.clear();
  endResetModel();
}
void MenuModel::appendMatches(const QJsonArray &indices) {
  if (indices.isEmpty())
    return;
  beginInsertRows({}, matches.size(), matches.size() + indices.size() - 1);
  for (const auto value : indices) {
    matchPositions[value.toInt()] = matches.size();
    matches << value.toInt();
  }
  endInsertRows();
}
void MenuModel::updateEntries(int first, int count) {
  if (!hierarchy) {
    for (int i = first; i < first + count; ++i) {
      const auto index = entryIndex(i);
      if (index.isValid())
        emit dataChanged(index, entryIndex(i, 2));
    }
    return;
  }
  if (children.size() < session.entries.size())
    children.resize(session.entries.size());
  for (int i = first; i < first + count; ++i) {
    if (i >= position.size()) {
      int p = session.entries[i].parent;
      auto &list = p < 0 ? roots : children[p];
      int row = list.size();
      beginInsertRows(entryIndex(p), row, row);
      position << row;
      list << i;
      endInsertRows();
    } else
      emit dataChanged(entryIndex(i), entryIndex(i, 2));
  }
}
bool MenuFilter::filterAcceptsRow(int row, const QModelIndex &parent) const {
  int i = sourceModel()->index(row, 0, parent).data(Qt::UserRole).toInt();
  return i >= 0 && i < session.entries.size() && (!filtered || matching.contains(i)) && (unavailable || session.entries[i].treeVisible);
}
