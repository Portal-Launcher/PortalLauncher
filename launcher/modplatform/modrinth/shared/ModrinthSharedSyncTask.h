// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Prism Launcher fork - Modrinth Shared Instances support
 *
 *  Brings a joined ("member") instance up to the latest version of its
 *  shared instance: downloads added/changed content, removes content the
 *  owner removed, applies shared configs, and updates the Minecraft/loader
 *  versions if the owner changed them.
 */
#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QPair>
#include <QSet>
#include <QTemporaryDir>
#include <atomic>

#include "ModrinthSharedAttachment.h"
#include "net/NetJob.h"
#include "tasks/Task.h"

class MinecraftInstance;
class BaseInstance;

class ModrinthSharedSyncTask : public Task {
    Q_OBJECT
   public:
    /** softFail: never report failure (used before launch, where playing the
     *  previous version beats blocking the game). */
    ModrinthSharedSyncTask(BaseInstance* instance, bool softFail);
    ~ModrinthSharedSyncTask() override = default;

    bool updated() const { return m_updated; }
    QStringList changeLog() const { return m_changeLog; }
    bool abort() override;

   protected:
    void executeTask() override;

   private:
    struct TargetFile {
        QString rel;   // relative to game root
        QString url;
        QString sha1;  // empty for external files
        qint64 size = -1;
        QString source;
        QString fileName;
        QString projectId;    // Modrinth-hosted files only
        QString optionalKey;  // non-empty when the owner marked this optional
    };

    void onLatestVersion(const QJsonObject& version);
    void resolveNextVersionChunk();
    void resolveProjects();
    void fetchShareMeta();
    void buildTargetsAndDownload();
    void afterDownloads();
    void applyConfigBundle(std::function<void()> next);
    void adoptOwnerIcon(std::function<void()> next);
    void finish();

    void softOrFail(const QString& message);
    /** True when the user aborted; emits aborted() exactly once. Call at the
     *  top of every async continuation so a task that was aborted during the
     *  resolution phase never emits succeeded or failed afterwards. */
    bool bailIfAborted();

    MinecraftInstance* m_instance = nullptr;
    bool m_soft = false;
    bool m_updated = false;
    std::atomic_bool m_aborted{ false };
    bool m_abortEmitted = false;

    ModrinthShared::Attachment m_attachment;
    QJsonObject m_remoteVersion;
    QStringList m_versionIdsPending;
    int m_versionChunkIndex = 0;
    QJsonArray m_resolvedVersions;
    QJsonArray m_resolvedProjects;

    QList<TargetFile> m_targets;
    QList<ModrinthShared::ManagedFile> m_removalCarryover;  // stale files a locked disk kept alive
    QList<QPair<QString, QString>> m_downloadedFiles;  // (path, sha1) fetched this run
    QString m_configBundleUrl;
    QString m_shareMetaUrl;
    QSet<QString> m_optionalProjects;
    QSet<QString> m_optionalFiles;
    QStringList m_changeLog;
    QTemporaryDir m_tempDir;

    NetJob::Ptr m_downloadJob;
};
