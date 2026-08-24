// SPDX-License-Identifier: GPL-3.0-only
#include "BackupWorlds.h"

#include <QtConcurrent>

#include "FileSystem.h"
#include "launch/LaunchTask.h"
#include "settings/SettingsObject.h"

BackupWorlds::BackupWorlds(LaunchTask* parent) : LaunchStep(parent) {}

void BackupWorlds::executeTask()
{
    auto instance = m_parent->instance();
    const QString instanceRoot = instance->instanceRoot();
    const QString worldsDir = FS::PathCombine(instance->gameRoot(), "saves");
    const int keep = instance->settings()->get("WorldBackupKeep").toInt();

    setStatus(tr("Backing up worlds…"));
    connect(&m_watcher, &QFutureWatcher<WorldBackup::Result>::finished, this, [this]() {
        const WorldBackup::Result result = m_watcher.result();
        if (result.backedUp > 0)
            emit logLine(tr("Backed up %n world(s) before launching.", "", result.backedUp), MessageLevel::Launcher);
        for (const auto& error : result.errors)
            emit logLine(tr("World backup problem: %1").arg(error), MessageLevel::Warning);
        // Backups never block playing; problems are reported and that is it.
        emitSucceeded();
    });
    m_watcher.setFuture(QtConcurrent::run(
        [instanceRoot, worldsDir, keep]() { return WorldBackup::backupChangedWorlds(instanceRoot, worldsDir, keep); }));
}
