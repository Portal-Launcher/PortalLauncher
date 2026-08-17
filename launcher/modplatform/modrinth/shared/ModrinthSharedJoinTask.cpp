// SPDX-License-Identifier: GPL-3.0-only
#include "ModrinthSharedJoinTask.h"

#include "FileSystem.h"
#include "ModrinthSharedAttachment.h"
#include "minecraft/MinecraftInstance.h"
#include "minecraft/PackProfile.h"
#include "settings/INISettingsObject.h"

ModrinthSharedJoinTask::ModrinthSharedJoinTask(const QString& sharedInstanceId,
                                               const QString& instanceName,
                                               const QJsonObject& remoteVersion)
    : InstanceCreationTask(), m_sharedInstanceId(sharedInstanceId), m_remoteVersion(remoteVersion)
{
    m_original_name = instanceName;
    m_original_version = remoteVersion.value("game_version").toString();
}

std::unique_ptr<MinecraftInstance> ModrinthSharedJoinTask::createInstance()
{
    setStatus(tr("Creating the shared instance…"));

    const QString gameVersion = m_remoteVersion.value("game_version").toString();
    const QString loader = m_remoteVersion.value("loader").toString();
    const QString loaderVersion = m_remoteVersion.value("loader_version").toString();
    if (gameVersion.isEmpty()) {
        setError(tr("The shared instance has no Minecraft version."));
        return nullptr;
    }

    QString configPath = FS::PathCombine(m_stagingPath, "instance.cfg");
    auto instanceSettings = std::make_unique<INISettingsObject>(configPath);
    auto instance = std::make_unique<MinecraftInstance>(m_globalSettings, std::move(instanceSettings), m_stagingPath);

    auto* components = instance->getPackProfile();
    components->buildingFromScratch();
    components->setComponentVersion("net.minecraft", gameVersion, true);
    if (loader == QLatin1String("fabric") && !loaderVersion.isEmpty())
        components->setComponentVersion("net.fabricmc.fabric-loader", loaderVersion);
    else if (loader == QLatin1String("quilt") && !loaderVersion.isEmpty())
        components->setComponentVersion("org.quiltmc.quilt-loader", loaderVersion);
    else if (loader == QLatin1String("forge") && !loaderVersion.isEmpty())
        components->setComponentVersion("net.minecraftforge", loaderVersion);
    else if (loader == QLatin1String("neoforge") && !loaderVersion.isEmpty())
        components->setComponentVersion("net.neoforged", loaderVersion);

    instance->setIconKey(m_instIcon.isEmpty() || m_instIcon == "default" ? QStringLiteral("modrinth") : m_instIcon);
    instance->setName(name());
    instance->saveNow();

    // Attach as a member; the first sync fills in the content. Without this
    // file the instance would exist but never sync, so a failed write has to
    // fail the join loudly instead of reporting success.
    ModrinthShared::Attachment attachment;
    attachment.id = m_sharedInstanceId;
    attachment.role = "member";
    attachment.appliedVersion = -1;
    if (!attachment.save(m_stagingPath)) {
        setError(tr("Could not write the shared-instance link file."));
        return nullptr;
    }

    return instance;
}
