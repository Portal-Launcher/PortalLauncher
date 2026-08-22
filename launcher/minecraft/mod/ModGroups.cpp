// SPDX-License-Identifier: GPL-3.0-only
#include "ModGroups.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include "FileSystem.h"
#include "minecraft/mod/Mod.h"
#include "modplatform/ModIndex.h"

namespace {
const char* FILE_NAME = "mod-groups.json";

QString fileKey(const QString& fileName)
{
    QString name = fileName;
    if (name.endsWith(QLatin1String(".disabled")))
        name.chop(9);
    return QStringLiteral("file:") + name;
}
}  // namespace

QString ModGroups::filePath(const QString& instanceRoot)
{
    return FS::PathCombine(instanceRoot, FILE_NAME);
}

void ModGroups::setInstanceRoot(const QString& instanceRoot)
{
    m_path = filePath(instanceRoot);
    m_groupByKey.clear();
}

bool ModGroups::load()
{
    m_groupByKey.clear();
    QFile file(m_path);
    if (!file.exists() || !file.open(QIODevice::ReadOnly))
        return false;
    QJsonParseError err{};
    const auto doc = QJsonDocument::fromJson(file.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return false;
    const auto groups = doc.object().value("groups").toObject();
    for (auto it = groups.constBegin(); it != groups.constEnd(); ++it) {
        for (const auto& key : it.value().toArray()) {
            const QString k = key.toString();
            if (!k.isEmpty())
                m_groupByKey.insert(k, it.key());
        }
    }
    return true;
}

bool ModGroups::save() const
{
    if (m_path.isEmpty())
        return false;
    if (m_groupByKey.isEmpty()) {
        QFile::remove(m_path);
        return true;
    }
    QHash<QString, QJsonArray> members;
    for (auto it = m_groupByKey.constBegin(); it != m_groupByKey.constEnd(); ++it)
        members[it.value()].append(it.key());
    QJsonObject groups;
    for (auto it = members.constBegin(); it != members.constEnd(); ++it)
        groups.insert(it.key(), it.value());
    QJsonObject root;
    root.insert("formatVersion", 1);
    root.insert("groups", groups);

    QSaveFile file(m_path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return file.commit();
}

QStringList ModGroups::keysFor(const Mod& mod)
{
    QStringList keys;
    if (auto meta = mod.metadata(); meta && meta->isValid())
        keys.append(QString::fromUtf8(ModPlatform::ProviderCapabilities::name(meta->provider)) + ':' + meta->project_id.toString());
    keys.append(fileKey(mod.fileinfo().fileName()));
    return keys;
}

QStringList ModGroups::groupNames() const
{
    QStringList names;
    for (const auto& group : m_groupByKey)
        if (!names.contains(group))
            names.append(group);
    names.sort(Qt::CaseInsensitive);
    return names;
}

QString ModGroups::groupOf(const Mod& mod) const
{
    return groupOf(keysFor(mod));
}

QString ModGroups::groupOf(const QStringList& keys) const
{
    for (const auto& key : keys) {
        auto it = m_groupByKey.constFind(key);
        if (it != m_groupByKey.constEnd())
            return *it;
    }
    return {};
}

int ModGroups::countIn(const QString& group) const
{
    int n = 0;
    for (const auto& g : m_groupByKey)
        if (g == group)
            n++;
    return n;
}

void ModGroups::assign(const Mod& mod, const QString& group)
{
    const auto keys = keysFor(mod);
    for (const auto& key : keys)
        m_groupByKey.remove(key);
    if (!group.trimmed().isEmpty())
        m_groupByKey.insert(keys.first(), group.trimmed());
}

void ModGroups::renameGroup(const QString& from, const QString& to)
{
    const QString target = to.trimmed();
    if (from == target || target.isEmpty())
        return;
    for (auto it = m_groupByKey.begin(); it != m_groupByKey.end(); ++it)
        if (it.value() == from)
            it.value() = target;
}

void ModGroups::deleteGroup(const QString& group)
{
    for (auto it = m_groupByKey.begin(); it != m_groupByKey.end();) {
        if (it.value() == group)
            it = m_groupByKey.erase(it);
        else
            ++it;
    }
}
