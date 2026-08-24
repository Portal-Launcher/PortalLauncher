// SPDX-License-Identifier: GPL-3.0-only
#include "ModLoadouts.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include "FileSystem.h"

QString ModLoadouts::filePath(const QString& instanceRoot)
{
    return FS::PathCombine(instanceRoot, "mod-loadouts.json");
}

void ModLoadouts::setInstanceRoot(const QString& instanceRoot)
{
    m_path = filePath(instanceRoot);
}

bool ModLoadouts::load()
{
    m_enabledKeysByName.clear();
    m_knownKeysByName.clear();
    QFile file(m_path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const auto root = QJsonDocument::fromJson(file.readAll()).object();
    const auto loadouts = root.value("loadouts").toObject();
    for (auto it = loadouts.constBegin(); it != loadouts.constEnd(); ++it) {
        const auto obj = it.value().toObject();
        QSet<QString> enabled;
        for (const auto& value : obj.value("enabled").toArray())
            enabled.insert(value.toString());
        QSet<QString> known;
        for (const auto& value : obj.value("known").toArray())
            known.insert(value.toString());
        m_enabledKeysByName.insert(it.key(), enabled);
        m_knownKeysByName.insert(it.key(), known);
    }
    return true;
}

bool ModLoadouts::save() const
{
    QJsonObject loadouts;
    for (auto it = m_enabledKeysByName.constBegin(); it != m_enabledKeysByName.constEnd(); ++it) {
        QJsonObject obj;
        QStringList enabled(it.value().begin(), it.value().end());
        enabled.sort();
        obj["enabled"] = QJsonArray::fromStringList(enabled);
        QStringList known(m_knownKeysByName.value(it.key()).begin(), m_knownKeysByName.value(it.key()).end());
        known.sort();
        obj["known"] = QJsonArray::fromStringList(known);
        loadouts[it.key()] = obj;
    }
    QJsonObject root;
    root["format"] = 1;
    root["loadouts"] = loadouts;
    QSaveFile file(m_path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return file.commit();
}

QStringList ModLoadouts::loadoutNames() const
{
    QStringList names = m_enabledKeysByName.keys();
    names.sort(Qt::CaseInsensitive);
    return names;
}

void ModLoadouts::capture(const QString& name, const QList<QStringList>& enabledModKeys, const QList<QStringList>& allModKeys)
{
    QSet<QString> enabled;
    for (const auto& keys : enabledModKeys)
        for (const auto& key : keys)
            enabled.insert(key);
    QSet<QString> known;
    for (const auto& keys : allModKeys)
        for (const auto& key : keys)
            known.insert(key);
    m_enabledKeysByName.insert(name, enabled);
    m_knownKeysByName.insert(name, known);
}

bool ModLoadouts::isEnabledIn(const QString& name, const QStringList& keys) const
{
    const auto& enabled = m_enabledKeysByName.value(name);
    for (const auto& key : keys)
        if (enabled.contains(key))
            return true;
    return false;
}

bool ModLoadouts::knowsMod(const QString& name, const QStringList& keys) const
{
    const auto& known = m_knownKeysByName.value(name);
    for (const auto& key : keys)
        if (known.contains(key))
            return true;
    return false;
}

void ModLoadouts::rename(const QString& from, const QString& to)
{
    if (!m_enabledKeysByName.contains(from) || from == to)
        return;
    m_enabledKeysByName.insert(to, m_enabledKeysByName.take(from));
    m_knownKeysByName.insert(to, m_knownKeysByName.take(from));
}

void ModLoadouts::remove(const QString& name)
{
    m_enabledKeysByName.remove(name);
    m_knownKeysByName.remove(name);
}
