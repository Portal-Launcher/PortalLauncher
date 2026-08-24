// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Prism Launcher fork - world backups
 *
 *  Zips worlds into <instance root>/backups/worlds/<world folder>/<timestamp>.zip.
 *  Each zip contains the world under its own folder name, so it is also a
 *  normal importable world zip. Backups are skipped when the world has not
 *  changed since its newest backup, and pruned to a per-world keep count.
 */
#pragma once

#include <QCoreApplication>
#include <QFileInfoList>
#include <QString>
#include <QStringList>

namespace WorldBackup {

struct Result {
    int backedUp = 0;
    int skipped = 0;  // unchanged since their newest backup
    QStringList errors;
};

/** Folder holding all world backups of an instance. */
QString backupsRoot(const QString& instanceRoot);

/** Existing backup zips for one world, newest first. */
QFileInfoList listBackups(const QString& instanceRoot, const QString& worldFolderName);

/** Zip one world now (even if unchanged) and prune to 'keep'. */
bool backupOneWorld(const QString& instanceRoot, const QString& worldDir, int keep, QString* error = nullptr);

/** Zip every world under worldsDir that changed since its newest backup.
 *  Runs fine off the GUI thread; touches only the paths given. */
Result backupChangedWorlds(const QString& instanceRoot, const QString& worldsDir, int keep);

/** Restore a backup zip over the world it came from. The current state is
 *  zipped first (if it changed), so a restore is itself undoable. */
bool restoreBackup(const QString& instanceRoot, const QString& worldsDir, const QString& worldFolderName, const QString& zipPath, QString* error = nullptr);

}  // namespace WorldBackup
