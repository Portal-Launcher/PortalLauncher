// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Prism Launcher fork - Modrinth Shared Instances support
 *
 *  Creates a new local instance for a shared instance the user was invited
 *  to. The actual content download happens through ModrinthSharedSyncTask
 *  (run right after creation, and before every launch).
 */
#pragma once

#include <QJsonObject>

#include "InstanceCreationTask.h"

class ModrinthSharedJoinTask : public InstanceCreationTask {
    Q_OBJECT
   public:
    /** remoteVersion: the latest-version JSON from the shared-instances service. */
    ModrinthSharedJoinTask(const QString& sharedInstanceId, const QString& instanceName, const QJsonObject& remoteVersion);
    ~ModrinthSharedJoinTask() override = default;

   protected:
    std::unique_ptr<MinecraftInstance> createInstance() override;

   private:
    QString m_sharedInstanceId;
    QJsonObject m_remoteVersion;
};
