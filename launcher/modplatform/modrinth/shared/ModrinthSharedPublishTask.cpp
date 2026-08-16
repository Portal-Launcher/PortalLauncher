// SPDX-License-Identifier: GPL-3.0-only
#include "ModrinthSharedPublishTask.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QIcon>
#include <QImage>
#include <QJsonDocument>
#include <QPainter>
#include <QPixmap>
#include <QTemporaryDir>

#include "Application.h"
#include "FileSystem.h"
#include "icons/IconList.h"
#include "ModrinthSharedApi.h"
#include "archive/ArchiveWriter.h"
#include "minecraft/Component.h"
#include "minecraft/MinecraftInstance.h"
#include "minecraft/PackProfile.h"

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

}  // namespace

ModrinthSharedPublishTask::ModrinthSharedPublishTask(BaseInstance* instance, bool force, std::optional<QString> configSpecOverride)
    : Task(), m_force(force), m_configSpecOverride(std::move(configSpecOverride))
{
    m_instance = dynamic_cast<MinecraftInstance*>(instance);
}

void ModrinthSharedPublishTask::executeTask()
{
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

    setStatus(tr("Scanning instance content…"));
    setProgress(1, 6);
    scanContent();

    m_pendingHashes.clear();
    for (const auto& file : m_files)
        m_pendingHashes.append(file.sha1);

    setStatus(tr("Matching files against Modrinth…"));
    setProgress(2, 6);
    m_hashChunkIndex = 0;
    classifyNextChunk();
}

void ModrinthSharedPublishTask::scanContent()
{
    const QString gameRoot = m_instance->gameRoot();
    m_files.clear();
    m_skippedDisabled = 0;
    for (const auto& folderType : FOLDER_TYPES) {
        QDir dir(FS::PathCombine(gameRoot, folderType.folder));
        if (!dir.exists())
            continue;
        for (const auto& info : dir.entryInfoList(QDir::Files)) {
            const QString name = info.fileName();
            if (name.endsWith(".disabled", Qt::CaseInsensitive)) {
                m_skippedDisabled++;
                continue;
            }
            if (!name.endsWith(folderType.extension, Qt::CaseInsensitive))
                continue;
            ContentFile file;
            file.fileName = name;
            file.absPath = info.absoluteFilePath();
            file.type = folderType.type;
            file.size = info.size();
            file.sha1 = hashFileSha1(file.absPath);
            if (!file.sha1.isEmpty())
                m_files.append(file);
        }
    }

    // Config selection
    m_configPaths.clear();
    const QString spec = m_attachment.configSpec;
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
            m_configPaths = allConfigs;
        } else {
            const QStringList prefixes = spec.split(',', Qt::SkipEmptyParts);
            for (const auto& rel : allConfigs) {
                for (auto prefix : prefixes) {
                    prefix = prefix.trimmed();
                    if (!prefix.isEmpty() && rel.startsWith(prefix, Qt::CaseInsensitive)) {
                        m_configPaths.append(rel);
                        break;
                    }
                }
            }
        }
        if (m_configPaths.size() > 4096)
            m_configPaths = m_configPaths.mid(0, 4096);
    }
}

void ModrinthSharedPublishTask::classifyNextChunk()
{
    constexpr int CHUNK = 500;
    if (m_hashChunkIndex * CHUNK >= m_pendingHashes.size()) {
        afterClassify();
        return;
    }
    const QStringList chunk = m_pendingHashes.mid(m_hashChunkIndex * CHUNK, CHUNK);
    m_hashChunkIndex++;
    ModrinthShared::lookupVersionFiles(this, chunk, [this](const ModrinthShared::Response& res) {
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

    const QString signature = computeSignature();
    if (!m_force && m_hasAttachment && signature == m_attachment.lastPushSignature && m_attachment.appliedVersion >= 0) {
        // Content unchanged - but the icon may still have changed.
        uploadIconIfChanged([this]() {
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
                                             [next](const ModrinthShared::Response&) { next(); });
        return;
    }
    setStatus(tr("Creating the shared instance on Modrinth…"));
    ModrinthShared::createRemoteInstance(this, m_instance->name(), [this, next](const ModrinthShared::Response& res) {
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
        m_attachment.save(m_instance->instanceRoot());
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

    QJsonObject payload;
    payload["modrinth_ids"] = QJsonArray::fromStringList(m_modrinthIds);
    payload["external_files"] = externalData;
    payload["modpack_id"] = QJsonValue::Null;
    payload["game_version"] = m_gameVersion;
    payload["loader"] = m_loader;
    payload["loader_version"] = m_loaderVersion;

    ModrinthShared::createVersion(this, m_attachment.id, payload, [this](const ModrinthShared::Response& res) {
        if (!res.ok) {
            emitFailed(res.error);
            return;
        }
        auto obj = res.json.object();
        m_newVersion = obj.value("version").toInt(-1);
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
    while (m_activeUploads < MAX_CONCURRENT_UPLOADS && m_uploadIndex < m_uploads.size())
        startOneUpload(m_uploads[m_uploadIndex++].toObject());
    if (m_activeUploads == 0 && m_uploadIndex >= m_uploads.size())
        uploadIconIfChanged([this]() { finish(m_newVersion); });
}

void ModrinthSharedPublishTask::startOneUpload(const QJsonObject& upload)
{
    const QString fileName = upload.value("file_name").toString();
    const QString fileType = upload.value("file_type").toString();
    const QUrl url(upload.value("url").toString());

    QByteArray bytes;
    if (fileType == QLatin1String("configs")) {
        bytes = buildConfigBundle();
        if (bytes.isEmpty()) {
            m_uploadFailed = true;
            emitFailed(tr("Could not build the config bundle."));
            return;
        }
    } else {
        const ContentFile* candidate = nullptr;
        for (const auto& file : m_externalFiles) {
            if (file.fileName == fileName && file.type == fileType) {
                candidate = &file;
                break;
            }
        }
        if (!candidate)
            return;  // service asked for something we do not have; skip
        QFile file(candidate->absPath);
        if (!file.open(QIODevice::ReadOnly)) {
            m_uploadFailed = true;
            emitFailed(tr("Could not read %1 for upload.").arg(fileName));
            return;
        }
        bytes = file.readAll();
    }

    m_activeUploads++;
    setStatus(tr("Uploading %1 of %2 files…").arg(m_uploadedCount + 1).arg(m_uploads.size()));
    ModrinthShared::uploadBytes(this, url, bytes, [this, fileName](const ModrinthShared::Response& res) {
        m_activeUploads--;
        if (m_uploadFailed)
            return;
        if (!res.ok) {
            m_uploadFailed = true;
            emitFailed(tr("Uploading %1 failed: %2").arg(fileName, res.error));
            return;
        }
        m_uploadedCount++;
        setProgress(m_uploadedCount, qMax(1, static_cast<int>(m_uploads.size())));
        pumpUploads();
    });
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
        if (res.ok)
            m_attachment.iconSha1 = sha1;
        next();
    });
}

void ModrinthSharedPublishTask::finish(int version)
{
    m_attachment.appliedVersion = version;
    m_attachment.lastPushSignature = computeSignature();
    m_attachment.save(m_instance->instanceRoot());
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
    QStringList configs;
    for (const auto& rel : m_configPaths) {
        const QString abs = FS::PathCombine(m_instance->gameRoot(), "config", rel);
        configs.append(rel + ':' + hashFileSha1(abs));
    }
    configs.sort();
    parts << "cfg:" + configs.join(',');
    parts << "env:" + m_gameVersion + '/' + m_loader + '/' + m_loaderVersion;
    return QString::fromLatin1(
        QCryptographicHash::hash(parts.join('\n').toUtf8(), QCryptographicHash::Sha256).toHex());
}

QByteArray ModrinthSharedPublishTask::buildConfigBundle()
{
    QTemporaryDir tempDir;
    if (!tempDir.isValid())
        return {};
    const QString zipPath = FS::PathCombine(tempDir.path(), "configs.zip");
    const QString configRoot = FS::PathCombine(m_instance->gameRoot(), "config");
    {
        MMCZip::ArchiveWriter writer(zipPath);
        if (!writer.open())
            return {};
        for (const auto& rel : m_configPaths) {
            if (!writer.addFile(FS::PathCombine(configRoot, rel), rel))
                return {};
        }
        if (!writer.close())
            return {};
    }
    QFile zip(zipPath);
    if (!zip.open(QIODevice::ReadOnly))
        return {};
    return zip.readAll();
}
