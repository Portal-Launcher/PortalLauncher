// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Prism Launcher - Minecraft Launcher
 *  Copyright (c) 2022 flowln <flowlnlnln@gmail.com>
 *  Copyright (C) 2022 Sefa Eyeoglu <contact@scrumplex.net>
 *  Copyright (c) 2023 Trial97 <alexandru.tripon97@gmail.com>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *      Copyright 2013-2021 MultiMC Contributors
 *
 *      Licensed under the Apache License, Version 2.0 (the "License");
 *      you may not use this file except in compliance with the License.
 *      You may obtain a copy of the License at
 *
 *          http://www.apache.org/licenses/LICENSE-2.0
 *
 *      Unless required by applicable law or agreed to in writing, software
 *      distributed under the License is distributed on an "AS IS" BASIS,
 *      WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *      See the License for the specific language governing permissions and
 *      limitations under the License.
 */

#pragma once

#include <QAbstractListModel>
#include <QDir>
#include <QHash>
#include <QMap>
#include <QSet>
#include <QString>

#include "Mod.h"
#include "ModGroups.h"
#include "ResourceFolderModel.h"
#include "minecraft/Component.h"
#include "minecraft/mod/Resource.h"

class BaseInstance;
class QFileSystemWatcher;

/**
 * A legacy mod list.
 * Backed by a folder.
 */
class ModFolderModel : public ResourceFolderModel {
    Q_OBJECT
   public:
    enum Columns {
        ActiveColumn = 0,
        ImageColumn,
        NameColumn,
        VersionColumn,
        DateColumn,
        ProviderColumn,
        SizeColumn,
        SideColumn,
        LoadersColumn,
        McVersionsColumn,
        ReleaseTypeColumn,
        RequiresColumn,
        RequiredByColumn,
        GroupColumn,
        NUM_COLUMNS
    };
    ModFolderModel(const QDir& dir, BaseInstance* instance, bool is_indexed, bool create_dir, QObject* parent = nullptr);

    virtual QString id() const override { return "mods"; }

    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;

    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    int columnCount(const QModelIndex& parent) const override;

    QSortFilterProxyModel* createFilterProxyModel(QObject* parent = nullptr) override;

    /* User-defined mod groups (see ModGroups) */
    ModGroups& groups() { return m_groups; }
    const ModGroups& groups() const { return m_groups; }
    QString groupOf(int row) const;
    /** Persist group edits and refresh the Group column. */
    void groupsEdited();
    /** Only show mods in this group; empty shows all, GROUP_FILTER_NONE shows ungrouped mods. */
    void setGroupFilter(const QString& group);
    QString groupFilter() const { return m_groupFilter; }
    static const QString GROUP_FILTER_NONE;

    class ModProxyModel : public ProxyModel {
       public:
        explicit ModProxyModel(QObject* parent = nullptr) : ProxyModel(parent) {}

       protected:
        bool filterAcceptsRow(int source_row, const QModelIndex& source_parent) const override;
        bool lessThan(const QModelIndex& source_left, const QModelIndex& source_right) const override;
    };

    [[nodiscard]] Resource* createResource(const QFileInfo& file) override { return new Mod(file); }
    [[nodiscard]] Task* createParseTask(Resource&) override;

    bool isValid();

    bool setResourceEnabled(const QModelIndexList& indexes, EnableAction action) override;
    bool deleteResources(const QModelIndexList& indexes) override;

    QModelIndexList getAffectedMods(const QModelIndexList& indexes, EnableAction action);

    RESOURCE_HELPERS(Mod)

   public:
    QStringList requiresList(QString id);
    QStringList requiredByList(QString id);

   private slots:
    void onParseSucceeded(int ticket, QString resource_id) override;
    void onParseFinished();

   signals:
    void groupsChanged();

   private:
    QHash<QString, QSet<Mod*>> m_requiredBy;
    QHash<QString, QSet<Mod*>> m_requires;
    ModGroups m_groups;
    QString m_groupFilter;
};
