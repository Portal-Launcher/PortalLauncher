// SPDX-License-Identifier: GPL-3.0-only
#include "ModrinthSharedPublishTask.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFutureWatcher>
#include <QIcon>
#include <QImage>
#include <QJsonDocument>
#include <QPainter>
#include <QPixmap>
#include <QSaveFile>
#include <QtConcurrent>

#include "Application.h"
#include "FileSystem.h"
#include "icons/IconList.h"
#include "ModrinthSharedApi.h"
#include "archive/ArchiveWriter.h"
#include "minecraft/Component.h"
#include "minecraft/MinecraftInstance.h"
#include "minecraft/PackProfile.h"
#include "tools/PackSquash.h"

namespace {

struct FolderType {
    const char* folder;
    const char* type;
    const char* extension;
};
const FolderType FOLDER_TYPES[] = {
    { "mods", "mod", ".jar" },
    { "resourcepacks", "resourcepack", ".zip" },
    { "shaderpacks", "shader", ".zip" },
    { "datapacks", "datapack", ".zip" },
};

const QStringList CONFIG_EXTENSIONS = { "json", "json5", "jsonc", "yml",  "yaml",       "css", "toml",
                                        "txt",  "ini",   "cfg",   "conf", "properties", "xml", "nbt" };

QString hashFileSha1(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    QCryptographicHash hash(QCryptographicHash::Sha1);
    if (!hash.addData(&file))
        return {};
    return QString::fromLatin1(hash.result().toHex());
}

/** SHA1s memoized by (path, size, mtime), persisted in the instance root.
 *  Every push used to re-read the whole pack; with this, only files that
 *  actually changed since the last push get hashed again. Only keys touched
 *  this run are saved back, so entries for deleted files fall away. */
class HashCache {
   public:
    explicit HashCache(const QString& instanceRoot)
        : m_path(FS::PathCombine(instanceRoot, "portal-hash-cache.json"))
    {
        QFile file(m_path);
        if (!file.open(QIODevice::ReadOnly))
            return;
        const auto root = QJsonDocument::fromJson(file.readAll()).object();
        for (auto it = root.constBegin(); it != root.constEnd(); ++it) {
            const auto entry = it.value().toObject();
            Entry cached;
            cached.size = entry.value("size").toVariant().toLongLong();
            cached.mtimeMs = entry.value("mtime").toVariant().toLongLong();
            cached.sha1 = entry.value("sha1").toString();
            if (!cached.sha1.isEmpty())
                m_entries.insert(it.key(), cached);
        }
    }

    QString sha1For(const QString& key, const QFileInfo& info)
    {
        const qint64 mtimeMs = info.lastModified().toMSecsSinceEpoch();
        auto it = m_entries.constFind(key);
        if (it != m_entries.constEnd() && it->size == info.size() && it->mtimeMs == mtimeMs) {
            m_touched.insert(key, *it);
            return it->sha1;
        }
        const QString sha1 = hashFileSha1(info.absoluteFilePath());
        if (!sha1.isEmpty())
            m_touched.insert(key, { info.size(), mtimeMs, sha1 });
        return sha1;
    }

    void save() const
    {
        QJsonObject root;
        for (auto it = m_touched.constBegin(); it != m_touched.constEnd(); ++it) {
            QJsonObject entry;
            entry["size"] = it->size;
            entry["mtime"] = it->mtimeMs;
            entry["sha1"] = it->sha1;
            root[it.key()] = entry;
        }
        QSaveFile file(m_path);
        if (!file.open(QIODevice::WriteOnly))
            return;
        file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
        file.commit();
    }

   private:
    struct Entry {
        qint64 size = -1;
        qint64 mtimeMs = 0;
        QString sha1;
    };
    QString m_path;
    QHash<QString, Entry> m_entries;
    QHash<QString, Entry> m_touched;
};

}  // namespace

// Carries fork-specific share metadata (currently the optional-mods lists) as
// a plain external file. Stock clients download it inert; our sync intercepts
// it by name and never writes it to disk.
const char* ModrinthShared::SHARE_META_FILE_NAME = "portal-share-meta.json";

ModrinthSharedPublishTask::ModrinthSharedPublishTask(BaseInstance* instance, bool force, std::optional<QString> configSpecOverride)
    : Task(), m_force(force), m_configSpecOverride(std::move(configSpecOverride))
{
    m_instance = dynamic_cast<MinecraftInstance*>(instance);
}

bool ModrinthSharedPublishTask::abort()
{
    // The scan worker polls this flag between files, and every network
    // continuation checks it on entry; aborted() is emitted from there so it
    // can never race a success or failure emission.
    m_aborted = true;
    return true;
}

bool ModrinthSharedPublishTask::bailIfAborted()
{
    if (!m_aborted)
        return false;
    if (!m_abortEmitted) {
        m_abortEmitted = true;
        emitAborted();
    }
    return true;
}

void ModrinthSharedPublishTask::executeTask()
{
    setAbortable(true);
    if (!m_instance) {
        emitFailed(tr("Only Minecraft instances can be shared."));
        return;
    }
    if (!ModrinthShared::isSignedIn()) {
        emitFailed(tr("You are not signed in to Modrinth."));
        return;
    }
    ModrinthShared::refreshSessionIfNeeded(this);

    auto existing = ModrinthShared::Attachment::load(m_instance->instanceRoot());
    m_hasAttachment = existing.has_value();
    if (m_hasAttachment) {
        m_attachment = *existing;
        if (!m_attachment.isOwner()) {
            emitFailed(tr("Only the owner of a shared instance can push updates."));
            return;
        }
    }
    if (m_configSpecOverride)
        m_attachment.configSpec = *m_configSpecOverride;

    auto profile = m_instance->getPackProfile();
    const ComponentPtr minecraft = profile->getComponent("net.minecraft");
    if (!minecraft) {
        emitFailed(tr("Could not determine the Minecraft version of this instance."));
        return;
    }
    m_gameVersion = minecraft->m_version;
    m_loader = "vanilla";
    m_loaderVersion = "";
    const struct {
        const char* uid;
        const char* name;
    } loaders[] = {
        { "net.fabricmc.fabric-loader", "fabric" },
        { "org.quiltmc.quilt-loader", "quilt" },
        { "net.minecraftforge", "forge" },
        { "net.neoforged", "neoforge" },
    };
    for (const auto& loader : loaders) {
        if (const ComponentPtr component = profile->getComponent(QString::fromUtf8(loader.uid))) {
            m_loader = QString::fromUtf8(loader.name);
            m_loaderVersion = component->m_version;
            break;
        }
    }

    // Fast path: if nothing in the content folders changed since the last push
    // (names/sizes/mtimes and the environment all match), skip the expensive
    // full-content hashing entirely. This keeps the pre-launch auto-push
    // instant for large packs.
    const QString configSpec = m_attachment.configSpec;
    const bool configsShared = !configSpec.isEmpty() && configSpec != QLatin1String("none");
    // The optional-mods fingerprint busts the fast path when only optionality
    // changed, so a push still happens even though no file content did. The
    // servers.dat stamp does the same for server list edits.
    const QFileInfo serversInfo(FS::PathCombine(m_instance->gameRoot(), "servers.dat"));
    const QString serversStamp = serversInfo.exists() ? QString::number(serversInfo.size()) + ':' +
                                                            QString::number(serversInfo.lastModified().toMSecsSinceEpoch())
                                                      : QString();
    m_environment = m_gameVersion + '/' + m_loader + '/' + m_loaderVersion + '|' + configSpec + '|' +
                    optionalListsFingerprint() + '|' + serversStamp;
    if (!m_force && m_hasAttachment && m_attachment.appliedVersion >= 0 && !m_attachment.quickFingerprint.isEmpty() &&
        m_environment == m_attachment.lastPushEnvironment &&
        ModrinthShared::quickContentFingerprint(m_instance->gameRoot(), configsShared) == m_attachment.quickFingerprint) {
        const QString oldIconSha1 = m_attachment.iconSha1;
        uploadIconIfChanged([this, oldIconSha1]() {
            if (m_attachment.iconSha1 != oldIconSha1)
                m_attachment.save(m_instance->instanceRoot());
            setStatus(tr("Everything is already up to date."));
            m_pushed = false;
            m_pushedVersion = m_attachment.appliedVersion;
            emitSucceeded();
        });
        return;
    }

    setStatus(tr("Scanning instance content…"));
    setProgress(1, 6);
    // Hashing a big pack means reading hundreds of megabytes; on the calling
    // thread that freezes the whole window (Play press with auto-push on,
    // every manual push). Run it on the pool and continue when it lands.
    auto* watcher = new QFutureWatcher<ScanResult>(this);
    connect(watcher, &QFutureWatcher<ScanResult>::finished, this, [this, watcher]() {
        const ScanResult scan = watcher->result();
        watcher->deleteLater();
        if (bailIfAborted())
            return;
        afterScan(scan);
    });
    const QString gameRoot = m_instance->gameRoot();
    const QString instanceRoot = m_instance->instanceRoot();
    const QString scanConfigSpec = m_attachment.configSpec;
    watcher->setFuture(QtConcurrent::run(
        [this, gameRoot, instanceRoot, scanConfigSpec]() { return scanContent(gameRoot, instanceRoot, scanConfigSpec); }));
}

void ModrinthSharedPublishTask::afterScan(const ScanResult& scan)
{
    m_files = scan.files;
    m_configPaths = scan.configPaths;
    m_configHashLines = scan.configHashLines;
    m_sharedServers = scan.servers;
    m_skippedDisabled = scan.skippedDisabled;

    m_pendingHashes.clear();
    for (const auto& file : m_files)
        m_pendingHashes.append(file.sha1);

    setStatus(tr("Matching files against Modrinth…"));
    setProgress(2, 6);
    m_hashChunkIndex = 0;
    classifyNextChunk();
}

ModrinthSharedPublishTask::ScanResult ModrinthSharedPublishTask::scanContent(const QString& gameRoot,
                                                                             const QString& instanceRoot,
                                                                             const QString& configSpec)
{
    // Runs on a worker thread: everything it needs arrived as value copies,
    // and the only task state it touches is the atomic abort flag.
    ScanResult result;
    HashCache hashCache(instanceRoot);
    for (const auto& folderType : FOLDER_TYPES) {
        QDir dir(FS::PathCombine(gameRoot, folderType.folder));
        if (!dir.exists())
            continue;
        for (const auto& info : dir.entryInfoList(QDir::Files)) {
            if (m_aborted)
                return result;
            const QString name = info.fileName();
            if (name.endsWith(".disabled", Qt::CaseInsensitive)) {
                result.skippedDisabled++;
                continue;
            }
            if (name == QLatin1String(ModrinthShared::SHARE_META_FILE_NAME))
                continue;
            if (!name.endsWith(folderType.extension, Qt::CaseInsensitive))
                continue;
            ContentFile file;
            file.fileName = name;
            file.absPath = info.absoluteFilePath();
            file.type = folderType.type;
            file.size = info.size();
            file.sha1 = hashCache.sha1For(QString::fromUtf8(folderType.folder) + '/' + name, info);
            if (!file.sha1.isEmpty())
                result.files.append(file);
        }
    }

    // Config selection
    const QString& spec = configSpec;
    if (!spec.isEmpty() && spec != QLatin1String("none")) {
        const QString configRoot = FS::PathCombine(gameRoot, "config");
        QStringList allConfigs;
        QDirIterator it(configRoot, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            it.next();
            const auto info = it.fileInfo();
            if (!CONFIG_EXTENSIONS.contains(info.suffix().toLower()))
                continue;
            if (info.size() > 16 * 1024 * 1024)
                continue;
            QString rel = QDir(configRoot).relativeFilePath(info.absoluteFilePath());
            allConfigs.append(rel.replace('\\', '/'));
        }
        allConfigs.sort();
        if (spec == QLatin1String("all")) {
            result.configPaths = allConfigs;
        } else {
            const QStringList prefixes = spec.split(',', Qt::SkipEmptyParts);
            for (const auto& rel : allConfigs) {
                for (auto prefix : prefixes) {
                    prefix = prefix.trimmed();
                    if (!prefix.isEmpty() && rel.startsWith(prefix, Qt::CaseInsensitive)) {
                        result.configPaths.append(rel);
                        break;
                    }
                }
            }
        }
        if (result.configPaths.size() > 4096)
            result.configPaths = result.configPaths.mid(0, 4096);

        // Hash the selected configs here too, so computeSignature() later is
        // pure string work instead of a second read of every config file.
        for (const auto& rel : result.configPaths) {
            if (m_aborted)
                return result;
            const QFileInfo info(FS::PathCombine(configRoot, rel));
            result.configHashLines.append(rel + ':' + hashCache.sha1For("config/" + rel, info));
        }
    }
    hashCache.save();

    // The owner's server list rides along so friends land on the same
    // servers, not just the same pack.
    result.servers = ServersDat::read(FS::PathCombine(gameRoot, "servers.dat"));
    return result;
}

void ModrinthSharedPublishTask::classifyNextChunk()
{
    if (bailIfAborted())
        return;
    constexpr int CHUNK = 500;
    if (m_hashChunkIndex * CHUNK >= m_pendingHashes.size()) {
        afterClassify();
        return;
    }
    const QStringList chunk = m_pendingHashes.mid(m_hashChunkIndex * CHUNK, CHUNK);
    m_hashChunkIndex++;
    ModrinthShared::lookupVersionFiles(this, chunk, [this](const ModrinthShared::Response& res) {
        if (bailIfAborted())
            return;
        if (!res.ok) {
            emitFailed(res.error);
            return;
        }
        const auto found = res.json.object();
        for (auto& file : m_files) {
            if (file.versionId.isEmpty() && found.contains(file.sha1))
                file.versionId = found.value(file.sha1).toObject().value("id").toString();
        }
        classifyNextChunk();
    });
}

void ModrinthSharedPublishTask::afterClassify()
{
    m_modrinthIds.clear();
    m_externalFiles.clear();
    QSet<QString> seenIds;
    for (const auto& file : m_files) {
        if (!file.versionId.isEmpty()) {
            if (!seenIds.contains(file.versionId)) {
                seenIds.insert(file.versionId);
                m_modrinthIds.append(file.versionId);
            }
        } else {
            m_externalFiles.append(file);
        }
    }

    m_signature = computeSignature();
    if (!m_force && m_hasAttachment && m_signature == m_attachment.lastPushSignature && m_attachment.appliedVersion >= 0) {
        // Content unchanged - but the icon may still have changed.
        uploadIconIfChanged([this]() {
            m_attachment.quickFingerprint =
                ModrinthShared::quickContentFingerprint(m_instance->gameRoot(), !m_configPaths.isEmpty());
            m_attachment.lastPushEnvironment = m_environment;
            m_attachment.save(m_instance->instanceRoot());
            setStatus(tr("Everything is already up to date."));
            m_pushed = false;
            m_pushedVersion = m_attachment.appliedVersion;
            emitSucceeded();
        });
        return;
    }

    setProgress(3, 6);
    ensureRemoteInstance([this]() { createRemoteVersion(); });
}

void ModrinthSharedPublishTask::ensureRemoteInstance(std::function<void()> next)
{
    if (m_hasAttachment) {
        ModrinthShared::renameRemoteInstance(this, m_attachment.id, m_instance->name(),
                                             [this, next](const ModrinthShared::Response&) {
                                                 if (bailIfAborted())
                                                     return;
                                                 next();
                                             });
        return;
    }
    setStatus(tr("Creating the shared instance on Modrinth…"));
    ModrinthShared::createRemoteInstance(this, m_instance->name(), [this, next](const ModrinthShared::Response& res) {
        if (bailIfAborted())
            return;
        if (!res.ok) {
            emitFailed(res.error);
            return;
        }
        auto obj = res.json.object();
        QString id = obj.value("id").toString();
        if (id.isEmpty())
            id = obj.value("instance_id").toString();
        if (id.isEmpty()) {
            emitFailed(tr("The shared-instances service returned no instance id."));
            return;
        }
        m_attachment.id = id;
        m_attachment.role = "owner";
        m_attachment.appliedVersion = -1;
        m_hasAttachment = true;
        if (!m_attachment.save(m_instance->instanceRoot())) {
            // Without the attachment file there is no local record of the
            // share; carrying on would orphan the pack we just created.
            ModrinthShared::deleteRemoteInstance(this, m_attachment.id, [](const ModrinthShared::Response&) {});
            emitFailed(tr("Could not save the shared-instance link file in the instance folder."));
            return;
        }
        next();
    });
}

void ModrinthSharedPublishTask::createRemoteVersion()
{
    setStatus(tr("Publishing content list…"));
    setProgress(4, 6);

    QJsonArray externalData;
    for (const auto& file : m_externalFiles) {
        QJsonObject obj;
        obj["file_name"] = file.fileName;
        obj["file_type"] = file.type;
        externalData.append(obj);
    }
    if (!m_configPaths.isEmpty()) {
        QJsonObject obj;
        obj["file_name"] = "configs.zip";
        obj["file_type"] = "configs";
        externalData.append(obj);
    }
    if (!m_attachment.optionalProjects.isEmpty() || !m_attachment.optionalFiles.isEmpty() || !m_sharedServers.isEmpty()) {
        QJsonObject obj;
        obj["file_name"] = ModrinthShared::SHARE_META_FILE_NAME;
        obj["file_type"] = "mod";
        externalData.append(obj);
    }

    QJsonObject payload;
    payload["modrinth_ids"] = QJsonArray::fromStringList(m_modrinthIds);
    payload["external_files"] = externalData;
    payload["modpack_id"] = QJsonValue::Null;
    payload["game_version"] = m_gameVersion;
    payload["loader"] = m_loader;
    payload["loader_version"] = m_loaderVersion;

    ModrinthShared::createVersion(this, m_attachment.id, payload, [this](const ModrinthShared::Response& res) {
        if (bailIfAborted())
            return;
        if (!res.ok) {
            emitFailed(res.error);
            return;
        }
        auto obj = res.json.object();
        m_newVersion = obj.value("version").toInt(-1);
        if (m_newVersion < 0) {
            emitFailed(tr("The shared-instances service returned no version number."));
            return;
        }
        m_uploads = obj.value("external_files").toArray();
        m_uploadIndex = 0;
        m_activeUploads = 0;
        m_uploadedCount = 0;
        m_uploadFailed = false;
        setProgress(0, qMax(1, static_cast<int>(m_uploads.size())));
        pumpUploads();
    });
}

void ModrinthSharedPublishTask::pumpUploads()
{
    // The service asks for every non-Modrinth file on each version; upload a
    // few at a time so large packs push much faster than one-by-one.
    constexpr int MAX_CONCURRENT_UPLOADS = 4;
    if (m_uploadFailed)
        return;
    while (!m_uploadFailed && m_activeUploads < MAX_CONCURRENT_UPLOADS && m_uploadIndex < m_uploads.size())
        startOneUpload(m_uploads[m_uploadIndex++].toObject());
    if (!m_uploadFailed && m_activeUploads == 0 && m_uploadIndex >= m_uploads.size())
        uploadIconIfChanged([this]() { finish(m_newVersion); });
}

void ModrinthSharedPublishTask::startOneUpload(const QJsonObject& upload)
{
    const QString fileName = upload.value("file_name").toString();
    const QString fileType = upload.value("file_type").toString();
    const QUrl url(upload.value("url").toString());

    auto onDone = [this, fileName](const ModrinthShared::Response& res) {
        m_activeUploads--;
        if (bailIfAborted() || m_uploadFailed)
            return;
        if (!res.ok) {
            m_uploadFailed = true;
            emitFailed(tr("Uploading %1 failed: %2").arg(fileName, res.error));
            return;
        }
        m_uploadedCount++;
        setProgress(m_uploadedCount, qMax(1, static_cast<int>(m_uploads.size())));
        pumpUploads();
    };

    if (fileType == QLatin1String("configs")) {
        const QString bundlePath = buildConfigBundleFile();
        if (bundlePath.isEmpty()) {
            m_uploadFailed = true;
            emitFailed(tr("Could not build the config bundle."));
            return;
        }
        m_activeUploads++;
        setStatus(tr("Uploading %1 of %2 files…").arg(m_uploadedCount + 1).arg(m_uploads.size()));
        // Streamed from disk like the jars; config bundles can reach hundreds
        // of megabytes on config-heavy packs.
        ModrinthShared::uploadFile(this, url, bundlePath, onDone);
        return;
    }

    if (fileName == QLatin1String(ModrinthShared::SHARE_META_FILE_NAME)) {
        m_activeUploads++;
        setStatus(tr("Uploading %1 of %2 files…").arg(m_uploadedCount + 1).arg(m_uploads.size()));
        ModrinthShared::uploadBytes(this, url, buildShareMetaBytes(), onDone);
        return;
    }

    const ContentFile* candidate = nullptr;
    for (const auto& file : m_externalFiles) {
        if (file.fileName == fileName && file.type == fileType) {
            candidate = &file;
            break;
        }
    }
    if (!candidate)
        return;  // service asked for something we do not have; skip

    m_activeUploads++;
    setStatus(tr("Uploading %1 of %2 files…").arg(m_uploadedCount + 1).arg(m_uploads.size()));

    // Resource packs get squashed first, so friends download less and the
    // re-upload on every version costs less. Falls back to the original file
    // whenever that is not possible.
    if (PackSquash::canOptimize(fileType) && PackSquash::isAvailable()) {
        setStatus(tr("Optimizing %1…").arg(fileName));
        PackSquash::optimize(this, candidate->absPath, candidate->sha1, [this, url, onDone](QString pathToUpload) {
            ModrinthShared::uploadFile(this, url, pathToUpload, onDone);
        });
        return;
    }

    // Streamed from disk: four concurrent 200 MB jars used to mean 800 MB of
    // launcher memory during a push.
    ModrinthShared::uploadFile(this, url, candidate->absPath, onDone);
}

void ModrinthSharedPublishTask::uploadIconIfChanged(std::function<void()> next)
{
    // Share the instance icon so friends' copies look the same. Never fails
    // the push - the icon is cosmetic.
    QByteArray png;
    {
        const QIcon icon = APPLICATION->icons()->getIcon(m_instance->iconKey());
        const QPixmap pixmap = icon.pixmap(128, 128);
        if (!pixmap.isNull()) {
            // Modrinth's icon endpoint strips the alpha channel (verified: an
            // uploaded RGBA PNG comes back as RGB), which turns transparency
            // into black. Composite onto a neutral dark tile instead so
            // transparent icons still look intentional for friends.
            const QImage source = pixmap.toImage().convertToFormat(QImage::Format_ARGB32);
            QImage composited(source.size(), QImage::Format_RGB32);
            composited.fill(QColor(45, 47, 51));
            {
                QPainter painter(&composited);
                painter.drawImage(0, 0, source);
            }
            QBuffer buffer(&png);
            buffer.open(QIODevice::WriteOnly);
            composited.save(&buffer, "PNG");
        }
    }
    if (png.isEmpty()) {
        next();
        return;
    }
    const QString sha1 =
        QString::fromLatin1(QCryptographicHash::hash(png, QCryptographicHash::Sha1).toHex());
    if (sha1 == m_attachment.iconSha1) {
        next();
        return;
    }
    setStatus(tr("Uploading the instance icon…"));
    ModrinthShared::uploadIcon(this, m_attachment.id, png, [this, sha1, next](const ModrinthShared::Response& res) {
        if (bailIfAborted())
            return;
        if (res.ok)
            m_attachment.iconSha1 = sha1;
        next();
    });
}

void ModrinthSharedPublishTask::finish(int version)
{
    m_attachment.appliedVersion = version;
    m_attachment.lastPushSignature = m_signature;
    m_attachment.quickFingerprint =
        ModrinthShared::quickContentFingerprint(m_instance->gameRoot(), !m_configPaths.isEmpty());
    m_attachment.lastPushEnvironment = m_environment;
    if (!m_attachment.save(m_instance->instanceRoot())) {
        // The push itself landed; failing the whole task would be misleading,
        // but silence would hide that the next push will re-scan everything.
        qWarning() << "Could not persist the shared-instance state after pushing version" << version;
    }
    m_pushed = true;
    m_pushedVersion = version;
    setProgress(6, 6);
    setStatus(tr("Pushed version %1.").arg(version));
    emitSucceeded();
}

QString ModrinthSharedPublishTask::computeSignature() const
{
    QStringList parts;
    QStringList ids = m_modrinthIds;
    ids.sort();
    parts << "ids:" + ids.join(',');
    QStringList externals;
    for (const auto& file : m_externalFiles)
        externals.append(file.fileName + ':' + file.type + ':' + file.sha1);
    externals.sort();
    parts << "ext:" + externals.join(',');
    QStringList configs = m_configHashLines;  // hashed during the scan
    configs.sort();
    parts << "cfg:" + configs.join(',');
    parts << "env:" + m_gameVersion + '/' + m_loader + '/' + m_loaderVersion;
    parts << "opt:" + optionalListsFingerprint();
    QStringList servers;
    for (const auto& server : m_sharedServers)
        servers.append(server.address.trimmed().toLower() + ':' + server.name);
    servers.sort();
    parts << "srv:" + servers.join(',');
    return QString::fromLatin1(
        QCryptographicHash::hash(parts.join('\n').toUtf8(), QCryptographicHash::Sha256).toHex());
}

QString ModrinthSharedPublishTask::optionalListsFingerprint() const
{
    QStringList projects = m_attachment.optionalProjects;
    QStringList files = m_attachment.optionalFiles;
    projects.sort();
    files.sort();
    if (projects.isEmpty() && files.isEmpty())
        return {};
    return QString::fromLatin1(
        QCryptographicHash::hash((projects.join(',') + '|' + files.join(',')).toUtf8(), QCryptographicHash::Sha1).toHex());
}

QByteArray ModrinthSharedPublishTask::buildShareMetaBytes() const
{
    QJsonObject optional;
    optional["projects"] = QJsonArray::fromStringList(m_attachment.optionalProjects);
    optional["files"] = QJsonArray::fromStringList(m_attachment.optionalFiles);
    QJsonObject root;
    root["format"] = 1;
    root["optional"] = optional;
    QJsonArray servers;
    for (const auto& server : m_sharedServers) {
        QJsonObject serverObj;
        serverObj["name"] = server.name;
        serverObj["ip"] = server.address;
        servers.append(serverObj);
    }
    root["servers"] = servers;
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

QString ModrinthSharedPublishTask::buildConfigBundleFile()
{
    // The temp dir member keeps the zip alive for the whole streamed upload;
    // it is cleaned up with the task.
    m_configTempDir = std::make_unique<QTemporaryDir>();
    if (!m_configTempDir->isValid())
        return {};
    const QString zipPath = FS::PathCombine(m_configTempDir->path(), "configs.zip");
    const QString configRoot = FS::PathCombine(m_instance->gameRoot(), "config");
    MMCZip::ArchiveWriter writer(zipPath);
    if (!writer.open())
        return {};
    for (const auto& rel : m_configPaths) {
        if (!writer.addFile(FS::PathCombine(configRoot, rel), rel))
            return {};
    }
    if (!writer.close())
        return {};
    return zipPath;
}
