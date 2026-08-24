// SPDX-License-Identifier: GPL-3.0-only
#include "ModrinthSharedAttachment.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

#include "FileSystem.h"

namespace ModrinthShared {

QString Attachment::filePath(const QString& instanceRoot)
{
    return FS::PathCombine(instanceRoot, "shared-instance.json");
}

std::optional<Attachment> Attachment::load(const QString& instanceRoot)
{
    QFile file(filePath(instanceRoot));
    if (!file.exists() || !file.open(QIODevice::ReadOnly))
        return std::nullopt;

    QJsonParseError parseError{};
    auto doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject())
        return std::nullopt;

    auto obj = doc.object();
    Attachment att;
    att.id = obj.value("id").toString();
    att.role = obj.value("role").toString();
    att.appliedVersion = obj.value("appliedVersion").toInt(-1);
    att.configSpec = obj.value("configSpec").toString();
    att.lastPushSignature = obj.value("lastPushSignature").toString();
    att.iconSha1 = obj.value("iconSha1").toString();
    att.iconCheckedAt = static_cast<qint64>(obj.value("iconCheckedAt").toDouble(0));
    att.autoPush = obj.value("autoPush").toBool(false);
    att.quickFingerprint = obj.value("quickFingerprint").toString();
    att.lastPushEnvironment = obj.value("lastPushEnvironment").toString();
    for (const auto& value : obj.value("lastChangeLog").toArray())
        att.lastChangeLog.append(value.toString());
    att.lastChangeVersion = obj.value("lastChangeVersion").toInt(-1);
    att.lastInviteLink = obj.value("lastInviteLink").toString();
    for (const auto& value : obj.value("managedFiles").toArray()) {
        auto fileObj = value.toObject();
        ManagedFile mf;
        mf.rel = fileObj.value("rel").toString();
        mf.sha1 = fileObj.value("sha1").toString();
        mf.size = static_cast<qint64>(fileObj.value("size").toDouble(-1));
        mf.source = fileObj.value("source").toString();
        mf.optionalKey = fileObj.value("optionalKey").toString();
        if (!mf.rel.isEmpty())
            att.managedFiles.append(mf);
    }
    for (const auto& value : obj.value("managedConfigs").toArray())
        att.managedConfigs.append(value.toString());
    for (const auto& value : obj.value("optionalProjects").toArray())
        att.optionalProjects.append(value.toString());
    for (const auto& value : obj.value("optionalFiles").toArray())
        att.optionalFiles.append(value.toString());
    for (const auto& value : obj.value("disabledOptional").toArray())
        att.disabledOptional.append(value.toString());
    for (const auto& value : obj.value("sharedServers").toArray()) {
        const auto serverObj = value.toObject();
        SharedServer server;
        server.name = serverObj.value("name").toString();
        server.address = serverObj.value("ip").toString();
        if (!server.address.trimmed().isEmpty())
            att.sharedServers.append(server);
    }

    // The id ends up in service URL paths and in an icon filename, so a
    // malformed one (from a hand-edited or malicious file) must never load.
    static const QRegularExpression idPattern(QStringLiteral("^[0-9A-Za-z]{1,64}$"));
    if (att.id.isEmpty() || att.role.isEmpty() || !idPattern.match(att.id).hasMatch())
        return std::nullopt;
    return att;
}

bool Attachment::save(const QString& instanceRoot) const
{
    QJsonObject obj;
    obj["id"] = id;
    obj["role"] = role;
    obj["appliedVersion"] = appliedVersion;
    obj["configSpec"] = configSpec;
    obj["lastPushSignature"] = lastPushSignature;
    obj["iconSha1"] = iconSha1;
    obj["iconCheckedAt"] = static_cast<double>(iconCheckedAt);
    obj["autoPush"] = autoPush;
    obj["quickFingerprint"] = quickFingerprint;
    obj["lastPushEnvironment"] = lastPushEnvironment;
    obj["lastChangeLog"] = QJsonArray::fromStringList(lastChangeLog);
    obj["lastChangeVersion"] = lastChangeVersion;
    obj["lastInviteLink"] = lastInviteLink;
    QJsonArray files;
    for (const auto& mf : managedFiles) {
        QJsonObject fileObj;
        fileObj["rel"] = mf.rel;
        fileObj["sha1"] = mf.sha1;
        fileObj["size"] = static_cast<double>(mf.size);
        fileObj["source"] = mf.source;
        if (!mf.optionalKey.isEmpty())
            fileObj["optionalKey"] = mf.optionalKey;
        files.append(fileObj);
    }
    obj["managedFiles"] = files;
    obj["managedConfigs"] = QJsonArray::fromStringList(managedConfigs);
    obj["optionalProjects"] = QJsonArray::fromStringList(optionalProjects);
    obj["optionalFiles"] = QJsonArray::fromStringList(optionalFiles);
    obj["disabledOptional"] = QJsonArray::fromStringList(disabledOptional);
    QJsonArray servers;
    for (const auto& server : sharedServers) {
        QJsonObject serverObj;
        serverObj["name"] = server.name;
        serverObj["ip"] = server.address;
        servers.append(serverObj);
    }
    obj["sharedServers"] = servers;

    try {
        FS::write(filePath(instanceRoot), QJsonDocument(obj).toJson(QJsonDocument::Indented));
        return true;
    } catch (...) {
        return false;
    }
}

void Attachment::remove(const QString& instanceRoot)
{
    QFile::remove(filePath(instanceRoot));
}

ShareInfo cachedShareInfo(const QString& instanceRoot)
{
    struct CacheEntry {
        QDateTime mtime;
        qint64 size = -1;
        ShareInfo info;
        qint64 checkedAtMs = 0;
    };
    static QHash<QString, CacheEntry> cache;

    // This runs from paint code (instance-grid delegates) and tooltip
    // queries, so even the stat call is too expensive to repeat per frame;
    // trust a recent answer.
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    auto it = cache.find(instanceRoot);
    if (it != cache.end() && nowMs - it->checkedAtMs < 5000)
        return it->info;

    const QFileInfo info(Attachment::filePath(instanceRoot));
    if (!info.exists()) {
        CacheEntry entry;
        entry.checkedAtMs = nowMs;
        cache.insert(instanceRoot, entry);
        return {};
    }
    if (it != cache.end() && it->mtime == info.lastModified() && it->size == info.size()) {
        it->checkedAtMs = nowMs;
        return it->info;
    }

    const auto attachment = Attachment::load(instanceRoot);
    CacheEntry entry{ info.lastModified(), info.size(), {}, nowMs };
    if (attachment) {
        entry.info.role = attachment->role;
        entry.info.appliedVersion = attachment->appliedVersion;
    }
    cache.insert(instanceRoot, entry);
    return entry.info;
}

QString cachedRole(const QString& instanceRoot)
{
    return cachedShareInfo(instanceRoot).role;
}

QString quickContentFingerprint(const QString& gameRoot, bool includeConfigs)
{
    static const char* CONTENT_FOLDERS[] = { "mods", "resourcepacks", "shaderpacks", "datapacks" };
    QStringList parts;
    for (const char* folder : CONTENT_FOLDERS) {
        QDir dir(gameRoot + '/' + QString::fromUtf8(folder));
        for (const auto& info : dir.entryInfoList(QDir::Files, QDir::Name)) {
            parts.append(info.fileName() + '|' + QString::number(info.size()) + '|' +
                         QString::number(info.lastModified().toSecsSinceEpoch()));
        }
    }
    if (includeConfigs) {
        QDirIterator it(gameRoot + "/config", QDir::Files, QDirIterator::Subdirectories);
        QStringList configParts;
        while (it.hasNext()) {
            it.next();
            const auto info = it.fileInfo();
            configParts.append(info.filePath() + '|' + QString::number(info.size()) + '|' +
                               QString::number(info.lastModified().toSecsSinceEpoch()));
        }
        configParts.sort();
        parts += configParts;
    }
    return QString::fromLatin1(
        QCryptographicHash::hash(parts.join('\n').toUtf8(), QCryptographicHash::Sha1).toHex());
}

}  // namespace ModrinthShared
