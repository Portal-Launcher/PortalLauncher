// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Prism Launcher - Minecraft Launcher
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 */

#pragma once

#include <QAbstractListModel>
#include <QFuture>
#include <QFutureWatcher>
#include <QIcon>
#include <QSortFilterProxyModel>

#include "modplatform/import_launchers/LauncherScanner.h"

namespace LauncherImport {

/** Instances found in other launchers, one row each, scanned off the GUI
 *  thread. Extra folders the user pointed at are remembered in settings. */
class ListModel : public QAbstractListModel {
    Q_OBJECT

   public:
    explicit ListModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override { return parent.isValid() ? 0 : m_found.size(); }
    QVariant data(const QModelIndex& index, int role) const override;

    /** Rescan the default locations plus every remembered folder. */
    void refresh();
    bool isScanning() const { return m_scanning; }

    QStringList extraPaths() const;
    void addExtraPath(const QString& path);

   signals:
    void scanFinished(int count);

   private:
    QIcon iconFor(const FoundInstance& inst) const;

    QList<FoundInstance> m_found;
    mutable QHash<QString, QIcon> m_iconCache;
    bool m_scanning = false;
    QFuture<QList<FoundInstance>> m_future;
    QFutureWatcher<QList<FoundInstance>> m_watcher;
};

class FilterModel : public QSortFilterProxyModel {
    Q_OBJECT

   public:
    explicit FilterModel(QObject* parent = nullptr);
    enum Sorting { ByLauncher, ByName, ByGameVersion };

    const QMap<QString, Sorting>& availableSortings() const { return m_sortings; }
    QString translateCurrentSorting() const;
    void setSorting(Sorting sorting);
    void setSearchTerm(const QString& term);

   protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override;
    bool lessThan(const QModelIndex& left, const QModelIndex& right) const override;

   private:
    QMap<QString, Sorting> m_sortings;
    Sorting m_currentSorting = ByLauncher;
    QString m_searchTerm;
};

}  // namespace LauncherImport
