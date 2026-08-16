// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Prism Launcher - Minecraft Launcher
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
 */

#pragma once

#include <QCoreApplication>
#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

#include "ResourceDownloadTask.h"

class BaseInstance;
class ModFolderModel;

/** Snapshots mod files before they get replaced by the mod updater, so that the
 *  last mod update can be reverted. Snapshots live in
 *  <instance root>/backups/mod-updates/<yyyy-MM-dd-HHmmss>/ and consist of a
 *  'files' folder (the outgoing mod files), an 'index' folder (their metadata)
 *  and a small JSON manifest describing what was replaced by what.
 */
class ModUpdateBackup {
    Q_DECLARE_TR_FUNCTIONS(ModUpdateBackup)

   public:
    struct Entry {
        /** Display name of the mod. */
        QString name;
        /** On-disk file name of the outgoing version (may end in '.disabled'). Empty for fresh installs. */
        QString oldFile;
        /** File name that the update installed. */
        QString newFile;
        /** Metadata file of the outgoing version, as stored in the snapshot's 'index' folder. May be empty. */
        QString oldIndexFile;
    };

    static constexpr int MAX_SNAPSHOTS = 3;

    /** Folder holding all mod update snapshots of the given instance. */
    static QString snapshotsRoot(const BaseInstance* instance);

    /** Copies the files that are about to be replaced by 'tasks' into a new snapshot.
     *  Returns whether a snapshot with at least one entry was created. */
    static bool createSnapshot(const BaseInstance* instance, ModFolderModel* model, const QList<ResourceDownloadTask::Ptr>& tasks);

    /** Path to the most recent snapshot with a manifest, if any. */
    static std::optional<QString> latestSnapshot(const BaseInstance* instance);

    /** Deletes the oldest snapshots so that at most 'keep' remain. */
    static void pruneSnapshots(const BaseInstance* instance, int keep = MAX_SNAPSHOTS);

    /** Reads the manifest of the given snapshot folder. Returns std::nullopt if it can't be read. */
    static std::optional<ModUpdateBackup> load(const QString& snapshotPath);

    auto path() const -> QString { return m_path; }
    auto created() const -> QDateTime { return m_created; }
    auto entries() const -> const QList<Entry>& { return m_entries; }

    /** Restores this snapshot: removes the files the update installed, puts the old
     *  files and their metadata back and refreshes the model. Appends a human readable
     *  line per action to 'summary'. Returns true if there were no warnings. */
    bool revert(ModFolderModel* model, QStringList& summary) const;

    /** Deletes this snapshot from disk. */
    void remove() const;

   private:
    QString m_path;
    QDateTime m_created;
    QList<Entry> m_entries;
};
