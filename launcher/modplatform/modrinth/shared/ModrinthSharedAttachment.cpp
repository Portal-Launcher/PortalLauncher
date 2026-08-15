// SPDX-License-Identifier: GPL-3.0-only
#include "ModrinthSharedAttachment.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

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
    att.autoPush = obj.value("autoPush").toBool(false);
    for (const auto& value : obj.value("lastChangeLog").toArray())
        att.lastChangeLog.append(value.toString());
    att.lastChangeVersion = obj.value("lastChangeVersion").toInt(-1);
    for (const auto& value : obj.value("managedFiles").toArray()) {
        auto fileObj = value.toObject();
        ManagedFile mf;
        mf.rel = fileObj.value("rel").toString();
        mf.sha1 = fileObj.value("sha1").toString();
        mf.size = static_cast<qint64>(fileObj.value("size").toDouble(-1));
        mf.source = fileObj.value("source").toString();
        if (!mf.rel.isEmpty())
            att.managedFiles.append(mf);
    }
    for (const auto& value : obj.value("managedConfigs").toArray())
        att.managedConfigs.append(value.toString());

    if (att.id.isEmpty() || att.role.isEmpty())
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
    obj["autoPush"] = autoPush;
    obj["lastChangeLog"] = QJsonArray::fromStringList(lastChangeLog);
    obj["lastChangeVersion"] = lastChangeVersion;
    QJsonArray files;
    for (const auto& mf : managedFiles) {
        QJsonObject fileObj;
        fileObj["rel"] = mf.rel;
        fileObj["sha1"] = mf.sha1;
        fileObj["size"] = static_cast<double>(mf.size);
        fileObj["source"] = mf.source;
        files.append(fileObj);
    }
    obj["managedFiles"] = files;
    obj["managedConfigs"] = QJsonArray::fromStringList(managedConfigs);

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

}  // namespace ModrinthShared
