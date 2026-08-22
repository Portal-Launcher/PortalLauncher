// SPDX-License-Identifier: GPL-3.0-only
#include "ModrinthSharedSyncTask.h"

#include <QApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QMessageBox>

#include "Application.h"
#include "icons/IconList.h"
#include "FileSystem.h"
#include "MMCZip.h"
#include "ModrinthSharedApi.h"
#include "minecraft/MinecraftInstance.h"
#include "minecraft/PackProfile.h"
#include "modplatform/helpers/HashUtils.h"
#include "net/ChecksumValidator.h"
#include "net/ContentCache.h"
#include "net/Download.h"

namespace {

QString folderForType(const QString& type)
{
    if (type == QLatin1String("resourcepack"))
        return "resourcepacks";
    if (type == QLatin1String("shader"))
        return "shaderpacks";
    if (type == QLatin1String("datapack"))
        return "datapacks";
    return "mods";
}

bool isSafeFileName(const QString& name)
{
    return !name.isEmpty() && !name.contains('/') && !name.contains('\\') && !name.startsWith('.') && !name.contains("..");
}

/** Every URL the service hands us must be https on a Modrinth host; anything
 *  else could feed unverified executable files straight into mods/. */
bool isTrustedDownloadUrl(const QUrl& url)
{
    if (url.scheme() != QLatin1String("https"))
        return false;
    const QString host = url.host().toLower();
    return host == QLatin1String("modrinth.com") || host.endsWith(QLatin1String(".modrinth.com"));
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
        softOrFail(tr("Not signed in to Modrinth - skipping shared-pack update check."));
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
        softOrFail(tr("The owner's latest push is still uploading - keeping the current version."));
        return;
    }
    if (remote == m_attachment.appliedVersion) {
        // Content unchanged - still mirror an owner icon change, but only
        // once a day. The icon is cosmetic and not worth two extra network
        // round-trips on every single launch.
        const qint64 now = QDateTime::currentSecsSinceEpoch();
        if (m_attachment.iconCheckedAt > 0 && now - m_attachment.iconCheckedAt < 24 * 3600) {
            setStatus(tr("Shared pack is up to date."));
            emitSucceeded();
            return;
        }
        adoptOwnerIcon([this]() {
            m_attachment.save(m_instance->instanceRoot());
            setStatus(tr("Shared pack is up to date."));
            emitSucceeded();
        });
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
        fetchShareMeta();
    });
}

void ModrinthSharedSyncTask::fetchShareMeta()
{
    // Fork clients attach an extra metadata file to the share (optional-mods
    // lists). Absent or unreadable metadata just means "everything required".
    m_shareMetaUrl.clear();
    m_optionalProjects.clear();
    m_optionalFiles.clear();
    for (const auto& value : m_remoteVersion.value("external_files").toArray()) {
        const auto ext = value.toObject();
        if (ext.value("file_name").toString() == QLatin1String(ModrinthShared::SHARE_META_FILE_NAME))
            m_shareMetaUrl = ext.value("url").toString();
    }
    if (m_shareMetaUrl.isEmpty() || !isTrustedDownloadUrl(QUrl(m_shareMetaUrl))) {
        buildTargetsAndDownload();
        return;
    }
    ModrinthShared::fetchBytes(this, QUrl(m_shareMetaUrl), [this](const ModrinthShared::Response& res) {
        if (res.ok && !res.body.isEmpty()) {
            const auto root = QJsonDocument::fromJson(res.body).object();
            const auto optional = root.value("optional").toObject();
            for (const auto& value : optional.value("projects").toArray())
                m_optionalProjects.insert(value.toString());
            for (const auto& value : optional.value("files").toArray())
                m_optionalFiles.insert(value.toString());
        }
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

        bool hasDatapackLoader = false;
        bool hasInstanceLoader = false;
        const auto loaders = version.value("loaders").toArray();
        for (const auto& lv : loaders) {
            if (lv.toString() == QLatin1String("datapack"))
                hasDatapackLoader = true;
            if (lv.toString() == instanceLoader)
                hasInstanceLoader = true;
        }
        const bool isDatapackOnly = hasDatapackLoader && !hasInstanceLoader;
        const QString projectType = projectTypeById.value(version.value("project_id").toString());
        const QString folder = isDatapackOnly ? QStringLiteral("datapacks") : folderForType(projectType);

        TargetFile target;
        target.rel = folder + '/' + fileName;
        target.url = file.value("url").toString();
        target.sha1 = file.value("hashes").toObject().value("sha1").toString();
        target.size = static_cast<qint64>(file.value("size").toDouble(-1));
        target.source = "modrinth:" + version.value("id").toString();
        target.fileName = fileName;
        target.projectId = version.value("project_id").toString();
        if (m_optionalProjects.contains(target.projectId))
            target.optionalKey = target.projectId;
        else if (m_optionalFiles.contains(fileName))
            target.optionalKey = fileName;
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
        if (fileName == QLatin1String(ModrinthShared::SHARE_META_FILE_NAME))
            continue;  // fork metadata, consumed in fetchShareMeta, never written to disk
        if (!isSafeFileName(fileName))
            continue;
        TargetFile target;
        target.rel = folderForType(fileType) + '/' + fileName;
        target.url = ext.value("url").toString();
        target.size = static_cast<qint64>(ext.value("file_size").toDouble(-1));
        target.source = "external";
        target.fileName = fileName;
        if (m_optionalFiles.contains(fileName))
            target.optionalKey = fileName;
        m_targets.append(target);
    }

    // Validate every download URL before touching any local file.
    for (const auto& target : m_targets) {
        if (!isTrustedDownloadUrl(QUrl(target.url))) {
            softOrFail(tr("Refusing to download %1 from an untrusted address (%2).").arg(target.rel, target.url));
            return;
        }
    }
    if (!m_configBundleUrl.isEmpty() && !isTrustedDownloadUrl(QUrl(m_configBundleUrl))) {
        softOrFail(tr("Refusing to download the shared config bundle from an untrusted address (%1).").arg(m_configBundleUrl));
        return;
    }

    // Human-readable changelog for this update (shown before playing and kept
    // on the Sharing page). Pretty names come from the resolved version data.
    QHash<QString, QString> titleByProject;
    for (const auto& value : m_resolvedProjects) {
        const auto obj = value.toObject();
        titleByProject[obj.value("id").toString()] = obj.value("title").toString();
    }
    QHash<QString, QString> prettyBySource;
    for (const auto& value : m_resolvedVersions) {
        const auto version = value.toObject();
        QString pretty = titleByProject.value(version.value("project_id").toString());
        const QString number = version.value("version_number").toString();
        if (pretty.isEmpty())
            pretty = version.value("name").toString();
        if (!number.isEmpty())
            pretty += ' ' + number;
        prettyBySource["modrinth:" + version.value("id").toString()] = pretty.trimmed();
    }

    const QString gameRoot = m_instance->gameRoot();
    QSet<QString> targetRels;
    for (const auto& target : m_targets)
        targetRels.insert(target.rel);

    QHash<QString, ModrinthShared::ManagedFile> oldByRel;
    for (const auto& old : m_attachment.managedFiles)
        oldByRel[old.rel] = old;

    // Reconcile this user's optional-mod choices from what is on disk: a
    // .disabled marker on an optional mod means "keep this off", re-enabling
    // it means "give me updates again". Tracked by key (project id or file
    // name) so the choice survives file renames across versions.
    QSet<QString> disabledOptional(m_attachment.disabledOptional.begin(), m_attachment.disabledOptional.end());
    for (const auto& old : m_attachment.managedFiles) {
        if (old.optionalKey.isEmpty())
            continue;
        if (QFile::exists(FS::PathCombine(gameRoot, old.rel)))
            disabledOptional.remove(old.optionalKey);
        else if (QFile::exists(FS::PathCombine(gameRoot, old.rel + ".disabled")))
            disabledOptional.insert(old.optionalKey);
    }
    // Only keys the share still marks optional are honored.
    QSet<QString> validKeys = m_optionalProjects;
    validKeys.unite(m_optionalFiles);
    disabledOptional.intersect(validKeys);
    m_attachment.disabledOptional = QStringList(disabledOptional.begin(), disabledOptional.end());
    m_attachment.disabledOptional.sort();

    m_changeLog.clear();
    const bool firstInstall = m_attachment.appliedVersion < 0;
    if (!firstInstall) {
        for (const auto& target : m_targets) {
            const QString pretty = prettyBySource.value(target.source, QFileInfo(target.rel).fileName());
            if (!oldByRel.contains(target.rel))
                m_changeLog.append(tr("Added: %1").arg(pretty));
            else if (!target.sha1.isEmpty() && oldByRel.value(target.rel).sha1 != target.sha1)
                m_changeLog.append(tr("Updated: %1").arg(pretty));
            else if (target.sha1.isEmpty() && target.size >= 0 && oldByRel.value(target.rel).size != target.size)
                m_changeLog.append(tr("Updated: %1").arg(pretty));
        }
        for (const auto& old : m_attachment.managedFiles) {
            if (!targetRels.contains(old.rel))
                m_changeLog.append(tr("Removed: %1").arg(QFileInfo(old.rel).fileName()));
        }
        if (!m_configBundleUrl.isEmpty())
            m_changeLog.append(tr("Shared config files updated"));
    }

    // New external files are the one thing in an update Modrinth cannot vouch
    // for (no project page, no hash database entry). Before touching any
    // local file, show what the update does and let the user decline it.
    if (!firstInstall) {
        QStringList newExternal;
        for (const auto& target : m_targets) {
            if (target.source == QLatin1String("external") && !oldByRel.contains(target.rel))
                newExternal.append(QFileInfo(target.rel).fileName());
        }
        if (!newExternal.isEmpty()) {
            QString text = tr("The owner's update for \"%1\" adds files that are not verified by Modrinth:\n\n%2\n\n")
                               .arg(m_instance->name(), newExternal.join('\n'));
            if (!m_changeLog.isEmpty())
                text += tr("Full change list:\n%1\n\n").arg(m_changeLog.join('\n'));
            text += tr("Only apply updates from owners you trust. Apply this update?");
            QMessageBox confirm(QMessageBox::Question, tr("Apply shared pack update?"), text,
                                QMessageBox::Yes | QMessageBox::No, QApplication::activeWindow());
            confirm.setTextFormat(Qt::PlainText);
            if (confirm.exec() != QMessageBox::Yes) {
                softOrFail(tr("Update skipped - the shared pack was left at version %1.")
                               .arg(m_attachment.appliedVersion < 0 ? tr("none") : QString::number(m_attachment.appliedVersion)));
                return;
            }
        }
    }

    // Delete previously managed files that are gone from the share.
    for (const auto& old : m_attachment.managedFiles) {
        if (targetRels.contains(old.rel))
            continue;
        if (!QFile::remove(FS::PathCombine(gameRoot, old.rel)))
            QFile::remove(FS::PathCombine(gameRoot, old.rel + ".disabled"));
    }

    // No explicit limit: NetJob uses the NumberOfConcurrentDownloads setting.
    m_downloadJob = makeShared<NetJob>(tr("Shared pack update"), APPLICATION->network());
    int queued = 0;
    for (const auto& target : m_targets) {
        const QString abs = FS::PathCombine(gameRoot, target.rel);
        const QString absDisabled = abs + ".disabled";
        const bool haveOld = oldByRel.contains(target.rel);
        const auto old = oldByRel.value(target.rel);
        const bool keepDisabled = !target.optionalKey.isEmpty() && disabledOptional.contains(target.optionalKey);

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

        // Updates to an optional mod this user turned off land as .disabled,
        // so the mod stays off without falling behind the pack.
        const QString dest = keepDisabled ? absDisabled : abs;

        // Friends install the same mods; serve repeat downloads from the
        // local content pool when the hash matches. deploy() re-verifies and
        // drops corrupted entries, falling through to a fresh download.
        if (!target.sha1.isEmpty()) {
            const QString cached = ContentCache::find("sha1", target.sha1);
            if (!cached.isEmpty() && ContentCache::deploy(cached, dest, "sha1", target.sha1))
                continue;
        }

        auto download = Net::Download::makeFile(QUrl(target.url), dest);
        if (!target.sha1.isEmpty()) {
            download->addValidator(new Net::ChecksumValidator(QCryptographicHash::Sha1, target.sha1));
            m_downloadedFiles.append({ dest, target.sha1 });
        }
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
    connect(m_downloadJob.get(), &Task::aborted, this, [this]() { emitAborted(); });
    connect(m_downloadJob.get(), &Task::progress, this,
            [this](qint64 current, qint64 total) { setProgress(current, total); });
    m_downloadJob->start();
}

void ModrinthSharedSyncTask::afterDownloads()
{
    // Every download was hash-validated by the job; remember them for other
    // instances and future joins.
    for (const auto& downloaded : m_downloadedFiles)
        ContentCache::store("sha1", downloaded.second, downloaded.first);
    m_downloadedFiles.clear();

    applyConfigBundle([this]() { adoptOwnerIcon([this]() { finish(); }); });
}

void ModrinthSharedSyncTask::adoptOwnerIcon(std::function<void()> next)
{
    // Mirror the owner's instance icon locally (best-effort, never fatal).
    ModrinthShared::getInstanceInfo(this, m_attachment.id, [this, next](const ModrinthShared::Response& res) {
        m_attachment.iconCheckedAt = QDateTime::currentSecsSinceEpoch();
        const QString iconUrl = res.ok && res.json.isObject() ? res.json.object().value("icon").toString() : QString();
        if (iconUrl.isEmpty() || !isTrustedDownloadUrl(QUrl(iconUrl))) {
            next();
            return;
        }
        ModrinthShared::fetchBytes(this, QUrl(iconUrl), [this, next](const ModrinthShared::Response& iconRes) {
            if (!iconRes.ok || iconRes.body.isEmpty()) {
                next();
                return;
            }
            const QString sha1 = QString::fromLatin1(
                QCryptographicHash::hash(iconRes.body, QCryptographicHash::Sha1).toHex());
            if (sha1 == m_attachment.iconSha1) {
                next();
                return;
            }
            // Write straight into the icon folder: IconList::installIcon uses
            // QFile::copy, which refuses to overwrite, so icon *changes* would
            // silently never land after the first adoption.
            const QString iconName = "shared-" + m_attachment.id;
            const QString target = FS::PathCombine(APPLICATION->icons()->getDirectory(), iconName + ".png");
            QFile out(target);
            if (out.open(QIODevice::WriteOnly | QIODevice::Truncate) && out.write(iconRes.body) == iconRes.body.size()) {
                out.close();
                m_instance->setIconKey(iconName);
                m_attachment.iconSha1 = sha1;
            }
            next();
        });
    });
}

void ModrinthSharedSyncTask::applyConfigBundle(std::function<void()> next)
{
    if (m_configBundleUrl.isEmpty() || !m_tempDir.isValid()) {
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
        mf.optionalKey = target.optionalKey;
        m_attachment.managedFiles.append(mf);
    }

    // Mirror the owner's optional lists so the Sharing page can describe them.
    m_attachment.optionalProjects = QStringList(m_optionalProjects.begin(), m_optionalProjects.end());
    m_attachment.optionalFiles = QStringList(m_optionalFiles.begin(), m_optionalFiles.end());
    m_attachment.optionalProjects.sort();
    m_attachment.optionalFiles.sort();

    // Update Minecraft / loader versions if the owner changed them.
    const QString gameVersion = m_remoteVersion.value("game_version").toString();
    const QString loader = m_remoteVersion.value("loader").toString();
    const QString loaderVersion = m_remoteVersion.value("loader_version").toString();
    auto profile = m_instance->getPackProfile();
    if (!gameVersion.isEmpty() && profile->getComponentVersion("net.minecraft") != gameVersion) {
        m_changeLog.append(tr("Minecraft: %1 to %2").arg(profile->getComponentVersion("net.minecraft"), gameVersion));
        profile->setComponentVersion("net.minecraft", gameVersion, true);
    }
    const QHash<QString, QString> loaderUids = {
        { "fabric", "net.fabricmc.fabric-loader" },
        { "quilt", "org.quiltmc.quilt-loader" },
        { "forge", "net.minecraftforge" },
        { "neoforge", "net.neoforged" },
    };
    if (loaderUids.contains(loader) && !loaderVersion.isEmpty()) {
        const QString uid = loaderUids[loader];
        if (profile->getComponent(uid) && profile->getComponentVersion(uid) != loaderVersion) {
            m_changeLog.append(tr("%1 loader: %2 to %3").arg(loader, profile->getComponentVersion(uid), loaderVersion));
            profile->setComponentVersion(uid, loaderVersion);
        }
    }
    profile->saveNow();

    m_attachment.appliedVersion = m_remoteVersion.value("version").toInt(-1);
    if (!m_changeLog.isEmpty()) {
        m_attachment.lastChangeLog = m_changeLog;
        m_attachment.lastChangeVersion = m_attachment.appliedVersion;
    }
    m_attachment.save(m_instance->instanceRoot());
    m_updated = true;
    setStatus(tr("Shared pack updated to version %1.").arg(m_attachment.appliedVersion));
    emitSucceeded();
}
