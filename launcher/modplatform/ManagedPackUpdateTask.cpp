// SPDX-License-Identifier: GPL-3.0-only
#include "ManagedPackUpdateTask.h"

#include <QUrl>

#include "Application.h"
#include "BaseInstance.h"
#include "InstanceImportTask.h"
#include "InstanceList.h"
#include "InstanceTask.h"
#include "modplatform/PackUpdateChecker.h"

ManagedPackUpdateTask::ManagedPackUpdateTask(BaseInstance* instance, QWidget* parentWidget)
    : Task(), m_instance(instance), m_parentWidget(parentWidget)
{}

bool ManagedPackUpdateTask::abort()
{
    if (m_importTask)
        return m_importTask->abort();
    if (m_versionsJob)
        return m_versionsJob->abort();
    emitAborted();
    return true;
}

void ManagedPackUpdateTask::executeTask()
{
    setAbortable(true);
    if (!m_instance || !m_instance->isManagedPack()) {
        emitFailed(tr("This instance is not a managed modpack."));
        return;
    }
    const QString type = m_instance->getManagedPackType();
    if (type != "modrinth" && type != "flame") {
        emitFailed(tr("Only Modrinth and CurseForge packs can update automatically."));
        return;
    }

    setStatus(tr("Looking up the newest pack version…"));

    ResourceAPI::Callback<QVector<ModPlatform::IndexedVersion>> callbacks{};
    callbacks.on_succeed = [this, type](QVector<ModPlatform::IndexedVersion>& versions) {
        auto newer = PackUpdateChecker::findNewerVersion(m_instance, type, versions);
        if (!newer) {
            emitFailed(tr("No newer pack version could be found."));
            return;
        }
        if (newer->downloadUrl.isEmpty()) {
            emitFailed(tr("The newest version has no downloadable file. Update from the Managed Pack page instead."));
            return;
        }
        startImport(QUrl(newer->downloadUrl), newer->fileId.toString(), newer->version);
    };
    callbacks.on_fail = [this](const QString& reason, int) { emitFailed(reason); };
    callbacks.on_abort = [this]() { emitAborted(); };

    ModPlatform::IndexedPack pack;
    pack.addonId = m_instance->getManagedPackID();

    ResourceAPI* api = type == "modrinth" ? static_cast<ResourceAPI*>(&m_modrinthApi) : static_cast<ResourceAPI*>(&m_flameApi);
    m_versionsJob = api->getProjectVersions({ .pack = std::make_shared<ModPlatform::IndexedPack>(pack),
                                             .mcVersions = {},
                                             .loaders = {},
                                             .resourceType = ModPlatform::ResourceType::Modpack,
                                             .includeChangelog = false },
                                            std::move(callbacks));
    m_versionsJob->start();
}

void ManagedPackUpdateTask::startImport(const QUrl& url, const QString& versionId, const QString& versionName)
{
    setStatus(tr("Updating the pack to %1…").arg(versionName));

    QMap<QString, QString> extraInfo;
    extraInfo.insert("pack_id", m_instance->getManagedPackID());
    extraInfo.insert("pack_version_id", versionId);
    extraInfo.insert("original_instance_id", m_instance->id());

    auto* importTask = new InstanceImportTask(url, m_parentWidget, std::move(extraInfo));
    InstanceName name(m_instance->getManagedPackName(), versionName);
    name.setName(QString(m_instance->name()).replace(m_instance->getManagedPackVersionName(), versionName));
    importTask->setName(name);
    importTask->setGroup(APPLICATION->instances()->getInstanceGroup(m_instance->id()));
    importTask->setIcon(m_instance->iconKey());
    importTask->setConfirmUpdate(false);

    m_importTask = Task::Ptr(APPLICATION->instances()->wrapInstanceTask(importTask));
    connect(m_importTask.get(), &Task::succeeded, this, [this]() { emitSucceeded(); });
    connect(m_importTask.get(), &Task::failed, this, [this](QString reason) { emitFailed(reason); });
    connect(m_importTask.get(), &Task::aborted, this, [this]() { emitAborted(); });
    connect(m_importTask.get(), &Task::status, this, [this](QString status) { setStatus(status); });
    connect(m_importTask.get(), &Task::progress, this, [this](qint64 current, qint64 total) { setProgress(current, total); });
    m_importTask->start();
}
