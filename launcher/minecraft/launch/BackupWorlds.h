// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Prism Launcher fork - world backups
 *
 *  Pre-launch step: zip every world that changed since its newest backup.
 *  Appended only when the instance's BackupWorldsOnLaunch setting is on.
 */
#pragma once

#include <QFutureWatcher>

#include <launch/LaunchStep.h>

#include "minecraft/WorldBackup.h"

class BackupWorlds : public LaunchStep {
    Q_OBJECT
   public:
    explicit BackupWorlds(LaunchTask* parent);
    virtual ~BackupWorlds() = default;

    void executeTask() override;
    bool canAbort() const override { return false; }

   private:
    QFutureWatcher<WorldBackup::Result> m_watcher;
};
