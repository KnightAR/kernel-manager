// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <QAbstractItemModel>
#include <QHash>
#include <QIcon>
#include <QJsonArray>
#include <QSortFilterProxyModel>
#include <QVector>
#include <QSet>
class Session;
class MenuModel : public QAbstractItemModel {
public:
  MenuModel(Session &session, bool hierarchy, QObject *parent);
  QModelIndex index(int row, int column,
                    const QModelIndex &parent = {}) const override;
  QModelIndex parent(const QModelIndex &index) const override;
  int rowCount(const QModelIndex &parent = {}) const override;
  int columnCount(const QModelIndex & = {}) const override { return 3; }
  QVariant data(const QModelIndex &index, int role) const override;
  QVariant headerData(int section, Qt::Orientation orientation,
                      int role) const override;
  QModelIndex entryIndex(int entry, int column = 0) const;
  void clearMatches();
  void appendMatches(const QJsonArray &indices);
  void updateEntries(int first, int count);

private:
  int entry(const QModelIndex &index) const;
  Session &session;
  bool hierarchy;
  mutable QHash<QString, QIcon> featureIcons;
  QVector<QVector<int>> children;
  QVector<int> roots, position, matches;
  QHash<int, int> matchPositions;
};
class MenuFilter : public QSortFilterProxyModel {
public:
  MenuFilter(Session &session, QObject *parent)
      : QSortFilterProxyModel(parent), session(session) {}
  void restrictTo(QSet<int> entries, bool active) {
    beginChange(); matching = std::move(entries); filtered = active; refreshFilter();
  }
  void showUnavailable(bool show) { beginChange(); unavailable = show; refreshFilter(); }
private:
  void beginChange() {
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    beginFilterChange();
#endif
  }
  void refreshFilter() {
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    endFilterChange(Direction::Rows);
#else
    invalidateFilter();
#endif
  }

protected:
  bool filterAcceptsRow(int row, const QModelIndex &parent) const override;

private:
  Session &session;
  bool unavailable = false, filtered = false;
  QSet<int> matching;
};
