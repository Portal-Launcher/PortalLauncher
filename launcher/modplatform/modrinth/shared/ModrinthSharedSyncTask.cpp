// SPDX-License-Identifier: GPL-3.0-only
#include "ModrinthSharedSyncTask.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>

#include "Application.h"
#include "FileSystem.h"
#include "MMCZip.h"
#include "ModrinthSharedApi.h"
#include "minecraft/MinecraftInstance.h"
#include "minecraft/PackProfile.h"
#include "net/ChecksumValidator.h"
#include "net/Download.h"

namespace {

QString folderForProjectType(const QString& projectType)
{
    if (projectType == QLatin1String("resourcepack"))
        return "resourcepacks";
    if (projectType == QLatin1String("shader"))
        return "shaderpacks";
    if (projectType == QLatin1String("datapack"))
        return "datapacks";
    return "mods";
}

QString folderForFileType(const QString& fileType)
{
    if (fileType == QLatin1String("resourcepack"))
        return "resourcepacks";
    if (fileType == QLatin1String("shader"))
        return "shaderpacks";
    if (fileType == QLatin1String("datapack"))
        return "datapacks";
    return "mods";
}

bool isSafeFileName(const QString& name)
{
    return !name.isEmpty() && !name.contains('/') && !name.contains('\\') && !name.startsWith('.') && !name.contains("..");
}

}  // namespace

ModrinthSharedSyncTask::ModrinthSharedSyncTask(BaseInstance* instance, bool softFail) : Task(), m_soft(softFail)
{
    m_instance = dynamic_cast<MinecraftInstance*>(instance);
}

bool ModrinthSharedSyncTask::abort()
{
    if (m_downloadJob)
        return m_downloadJob->abort();
    emitAborted();
    return true;
}

void ModrinthSharedSyncTask::softOrFail(const QString& message)
{
    if (m_soft) {
        setStatus(message);
        emitSucceeded();
    } else {
        emitFailed(message);
    }
}

void ModrinthSharedSyncTask::executeTask()
{
    setAbortable(true);
    if (!m_instance) {
        softOrFail(tr("Not a Minecraft instance."));
        return;
    }
    auto att = ModrinthShared::Attachment::load(m_instance->instanceRoot());
    if (!att || !att->isMember()) {
        softOrFail(tr("This instance is not attached to a shared instance."));
        return;
    }
    m_attachment = *att;

    if (!ModrinthShared::isSignedIn()) {
        softOrFail(tr("Not signed in to Modrinth — skipping shared-pack update check."));
        return;
    }
    ModrinthShared::refreshSessionIfNeeded(this);

    setStatus(tr("Checking for shared pack updates…"));
    ModrinthShared::getLatestVersion(this, m_attachment.id, [this](const ModrinthShared::Response& res) {
        if (!res.ok) {
            if (res.status == 404)
                softOrFail(tr("The shared instance was deleted by its owner."));
            else if (res.status == 401)
                softOrFail(tr("Your access to this shared instance was revoked (or the session expired)."));
            else
                softOrFail(res.error);
            return;
        }
        onLatestVersion(res.json.object());
    });
}

void ModrinthSharedSyncTask::onLatestVersion(const QJsonObject& version)
{
    m_remoteVersion = version;
    const int remote = version.value("version").toInt(-1);
    if (remote < 0) {
        softOrFail(tr("The shared-instances service returned an invalid version."));
        return;
    }
    if (!version.value("ready").toBool(true)) {
        softOrFail(tr("The owner's latest push is still uploading — keeping the current version."));
        return;
    }
    if (remote == m_attachment.appliedVersion) {
        setStatus(tr("Shared pack is up to date."));
        emitSucceeded();
        return;
    }

    setStatus(tr("Updating shared pack to version %1…").arg(remote));
    m_versionIdsPending.clear();
    for (const auto& value : version.value("modrinth_ids").toArray())
        m_versionIdsPending.append(value.toString());
    m_versionChunkIndex = 0;
    m_resolvedVersions = QJsonArray();
    resolveNextVersionChunk();
}

void ModrinthSharedSyncTask::resolveNextVersionChunk()
{
    constexpr int CHUNK = 100;
    if (m_versionChunkIndex * CHUNK >= m_versionIdsPending.size()) {
        resolveProjects();
        return;
    }
    const QStringList chunk = m_versionIdsPending.mid(m_versionChunkIndex * CHUNK, CHUNK);
    m_versionChunkIndex++;
    ModrinthShared::getVersionsBulk(this, chunk, [this](const ModrinthShared::Response& res) {
        if (!res.ok) {
            softOrFail(res.error);
            return;
        }
        for (const auto& value : res.json.array())
            m_resolvedVersions.append(value);
        resolveNextVersionChunk();
    });
}

void ModrinthSharedSyncTask::resolveProjects()
{
    QStringList projectIds;
    for (const auto& value : m_resolvedVersions) {
        const QString id = value.toObject().value("project_id").toString();
        if (!id.isEmpty() && !projectIds.contains(id))
            projectIds.append(id);
    }
    ModrinthShared::getProjectsBulk(this, projectIds, [this](const ModrinthShared::Response& res) {
        if (!res.ok) {
            softOrFail(res.error);
            return;
        }
        m_resolvedProjects = res.json.array();
        buildTargetsAndDownload();
    });
}

void ModrinthSharedSyncTask::buildTargetsAndDownload()
{
    QHash<QString, QString> projectTypeById;
    for (const auto& value : m_resolvedProjects) {
        const auto obj = value.toObject();
        projectTypeById[obj.value("id").toString()] = obj.value("project_type").toString();
    }

    const QString instanceLoader = [this]() {
        auto profile = m_instance->getPackProfile();
        if (profile->getComponent("net.fabricmc.fabric-loader"))
            return QStringLiteral("fabric");
        if (profile->getComponent("org.quiltmc.quilt-loader"))
            return QStringLiteral("quilt");
        if (profile->getComponent("net.minecraftforge"))
            return QStringLiteral("forge");
        if (profile->getComponent("net.neoforged"))
            return QStringLiteral("neoforge");
        return QStringLiteral("vanilla");
    }();

    m_targets.clear();
    for (const auto& value : m_resolvedVersions) {
        const auto version = value.toObject();
        const auto files = version.value("files").toArray();
        if (files.isEmpty())
            continue;
        QJsonObject file = files[0].toObject();
        for (const auto& fv : files) {
            if (fv.toObject().value("primary").toBool()) {
                file = fv.toObject();
                break;
            }
        }
        const QString fileName = file.value("filename").toString();
        if (!isSafeFileName(fileName))
            continue;

        bool isDatapackOnly = false;
        const auto loaders = version.value("loaders").toArray();
        for (const auto& lv : loaders) {
            if (lv.toString() == QLatin1String("datapack"))
                isDatapackOnly = true;
            if (lv.toString() == instanceLoader)
                isDatapackOnly = false;
        }
        const QString projectType = projectTypeById.value(version.value("project_id").toString());
        const QString folder = isDatapackOnly ? QStringLiteral("datapacks") : folderForProjectType(projectType);

        TargetFile target;
        target.rel = folder + '/' + fileName;
        target.url = file.value("url").toString();
        target.sha1 = file.value("hashes").toObject().value("sha1").toString();
        target.size = static_cast<qint64>(file.value("size").toDouble(-1));
        target.source = "modrinth:" + version.value("id").toString();
        m_targets.append(target);
    }

    m_configBundleUrl.clear();
    for (const auto& value : m_remoteVersion.value("external_files").toArray()) {
        const auto ext = value.toObject();
        const QString fileType = ext.value("file_type").toString();
        const QString fileName = ext.value("file_name").toString();
        if (fileType == QLatin1String("configs")) {
            m_configBundleUrl = ext.value("url").toString();
            continue;
        }
        if (!isSafeFileName(fileName))
            continue;
        TargetFile target;
        target.rel = folderForFileType(fileType) + '/' + fileName;
        target.url = ext.value("url").toString();
        target.size = static_cast<qint64>(ext.value("file_size").toDouble(-1));
        target.source = "external";
        m_targets.append(target);
    }

    // Delete previously managed files that are gone from the share.
    const QString gameRoot = m_instance->gameRoot();
    QSet<QString> targetRels;
    for (const auto& target : m_targets)
        targetRels.insert(target.rel);
    for (const auto& old : m_attachment.managedFiles) {
        if (targetRels.contains(old.rel))
            continue;
        if (!QFile::remove(FS::PathCombine(gameRoot, old.rel)))
            QFile::remove(FS::PathCombine(gameRoot, old.rel + ".disabled"));
    }

    // Queue downloads for new/changed files, honoring locally disabled copies.
    QHash<QString, ModrinthShared::ManagedFile> oldByRel;
    for (const auto& old : m_attachment.managedFiles)
        oldByRel[old.rel] = old;

    m_downloadJob = makeShared<NetJob>(tr("Shared pack update"), APPLICATION->network(), 6);
    int queued = 0;
    for (const auto& target : m_targets) {
        const QString abs = FS::PathCombine(gameRoot, target.rel);
        const QString absDisabled = abs + ".disabled";
        const bool haveOld = oldByRel.contains(target.rel);
        const auto& old = oldByRel[target.rel];

        bool exists = QFile::exists(abs);
        bool existsDisabled = !exists && QFile::exists(absDisabled);
        if (exists || existsDisabled) {
            const bool sameByHash = !target.sha1.isEmpty() && haveOld && old.sha1 == target.sha1;
            const bool sameByExternal = target.sha1.isEmpty() && haveOld && (target.size < 0 || old.size == target.size);
            if (sameByHash || sameByExternal)
                continue;
            if (existsDisabled)
                QFile::remove(absDisabled);
        }

        auto download = Net::Download::makeFile(QUrl(target.url), abs);
        if (!target.sha1.isEmpty())
            download->addValidator(new Net::ChecksumValidator(QCryptographicHash::Sha1, target.sha1));
        m_downloadJob->addNetAction(download);
        queued++;
    }
    if (!m_configBundleUrl.isEmpty() && m_tempDir.isValid()) {
        m_downloadJob->addNetAction(
            Net::Download::makeFile(QUrl(m_configBundleUrl), FS::PathCombine(m_tempDir.path(), "configs.zip")));
        queued++;
    }

    if (queued == 0) {
        afterDownloads();
        return;
    }
    setStatus(tr("Downloading %1 files…").arg(queued));
    connect(m_downloadJob.get(), &Task::succeeded, this, &ModrinthSharedSyncTask::afterDownloads);
    connect(m_downloadJob.get(), &Task::failed, this, [this](QString reason) { softOrFail(reason); });
    connect(m_downloadJob.get(), &Task::progress, this,
            [this](qint64 current, qint64 total) { setProgress(current, total); });
    m_downloadJob->start();
}

void ModrinthSharedSyncTask::afterDownloads()
{
    applyConfigBundle([this]() { finish(); });
}

void ModrinthSharedSyncTask::applyConfigBundle(std::function<void()> next)
{
    if (m_configBundleUrl.isEmpty()) {
        next();
        return;
    }
    const QString zipPath = FS::PathCombine(m_tempDir.path(), "configs.zip");
    if (!QFile::exists(zipPath)) {
        next();
        return;
    }
    setStatus(tr("Applying shared configs…"));
    const QString configRoot = FS::PathCombine(m_instance->gameRoot(), "config");
    FS::ensureFolderPathExists(configRoot);
    auto extracted = MMCZip::extractDir(zipPath, configRoot);
    if (extracted) {
        m_attachment.managedConfigs.clear();
        for (const auto& path : *extracted)
            m_attachment.managedConfigs.append(QDir(configRoot).relativeFilePath(path));
    } else {
        setStatus(tr("Warning: could not apply the shared config bundle."));
    }
    next();
}

void ModrinthSharedSyncTask::finish()
{
    // Track the new managed set.
    m_attachment.managedFiles.clear();
    for (const auto& target : m_targets) {
        ModrinthShared::ManagedFile mf;
        mf.rel = target.rel;
        mf.sha1 = target.sha1;
        mf.size = target.size;
        mf.source = target.source;
        m_attachment.managedFiles.append(mf);
    }

    // Update Minecraft / loader versions if the owner changed them.
    const QString gameVersion = m_remoteVersion.value("game_version").toString();
    const QString loader = m_remoteVersion.value("loader").toString();
    const QString loaderVersion = m_remoteVersion.value("loader_version").toString();
    auto profile = m_instance->getPackProfile();
    if (!gameVersion.isEmpty() && profile->getComponentVersion("net.minecraft") != gameVersion)
        profile->setComponentVersion("net.minecraft", gameVersion, true);
    const QHash<QString, QString> loaderUids = {
        { "fabric", "net.fabricmc.fabric-loader" },
        { "quilt", "org.quiltmc.quilt-loader" },
        { "forge", "net.minecraftforge" },
        { "neoforge", "net.neoforged" },
    };
    if (loaderUids.contains(loader) && !loaderVersion.isEmpty()) {
        const QString uid = loaderUids[loader];
        if (profile->getComponent(uid) && profile->getComponentVersion(uid) != loaderVersion)
            profile->setComponentVersion(uid, loaderVersion);
    }
    profile->saveNow();

    m_attachment.appliedVersion = m_remoteVersion.value("version").toInt(-1);
    m_attachment.save(m_instance->instanceRoot());
    m_updated = true;
    setStatus(tr("Shared pack updated to version %1.").arg(m_attachment.appliedVersion));
    emitSucceeded();
}
