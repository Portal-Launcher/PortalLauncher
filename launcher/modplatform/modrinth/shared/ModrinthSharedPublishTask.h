// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Prism Launcher fork - Modrinth Shared Instances support
 *
 *  Publishes ("pushes") the current content of an instance to its shared
 *  instance on the Modrinth service, creating the share on first use.
 */
#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <functional>
#include <optional>

#include "ModrinthSharedAttachment.h"
#include "tasks/Task.h"

class MinecraftInstance;
class BaseInstance;

class ModrinthSharedPublishTask : public Task {
    Q_OBJECT
   public:
    ModrinthSharedPublishTask(BaseInstance* instance, bool force, std::optional<QString> configSpecOverride = std::nullopt);
    ~ModrinthSharedPublishTask() override = default;

    bool pushed() const { return m_pushed; }
    int pushedVersion() const { return m_pushedVersion; }
    int skippedDisabled() const { return m_skippedDisabled; }

   protected:
    void executeTask() override;

   private:
    struct ContentFile {
        QString fileName;
        QString absPath;
        QString type;  // mod / resourcepack / shader / datapack
        QString sha1;
        qint64 size = 0;
        QString versionId;  // set when the file is hosted on Modrinth
    };

    void scanContent();
    void classifyNextChunk();
    void afterClassify();
    void ensureRemoteInstance(std::function<void()> next);
    void createRemoteVersion();
    void pumpUploads();
    void startOneUpload(const QJsonObject& upload);
    void uploadIconIfChanged(std::function<void()> next);
    void finish(int version);

    QString computeSignature() const;
    QByteArray buildConfigBundle();

    MinecraftInstance* m_instance = nullptr;
    bool m_force = false;
    std::optional<QString> m_configSpecOverride;

    ModrinthShared::Attachment m_attachment;
    bool m_hasAttachment = false;

    QList<ContentFile> m_files;
    QStringList m_pendingHashes;
    int m_hashChunkIndex = 0;
    QStringList m_modrinthIds;
    QList<ContentFile> m_externalFiles;
    QStringList m_configPaths;  // relative to <gameRoot>/config

    QString m_gameVersion;
    QString m_loader;
    QString m_loaderVersion;
    QString m_environment;  // gameVersion/loader/loaderVersion|configSpec
    QString m_signature;    // full content signature, computed once per run

    QJsonArray m_uploads;  // external_files from the createVersion response
    int m_uploadIndex = 0;
    int m_activeUploads = 0;
    int m_uploadedCount = 0;
    bool m_uploadFailed = false;
    int m_newVersion = -1;

    bool m_pushed = false;
    int m_pushedVersion = -1;
    int m_skippedDisabled = 0;
};
