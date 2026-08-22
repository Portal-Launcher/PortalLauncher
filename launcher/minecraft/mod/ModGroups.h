// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Prism Launcher - Minecraft Launcher
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 */

#pragma once

#include <QHash>
#include <QString>
#include <QStringList>

class Mod;

/** User-defined groups for the mods of one instance ("Performance",
 *  "Optional", "Library"...), stored as mod-groups.json in the instance root.
 *
 *  Mods are keyed by their metadata identity (provider + project id) when the
 *  launcher installed them, so a group survives updates that rename the jar,
 *  and by file name otherwise. */
class ModGroups {
   public:
    ModGroups() = default;
    explicit ModGroups(const QString& instanceRoot) { setInstanceRoot(instanceRoot); }

    void setInstanceRoot(const QString& instanceRoot);
    bool load();
    bool save() const;

    static QString filePath(const QString& instanceRoot);

    /** Identity keys for a mod, most durable first. */
    static QStringList keysFor(const Mod& mod);

    QStringList groupNames() const;
    QString groupOf(const Mod& mod) const;
    QString groupOf(const QStringList& keys) const;
    int countIn(const QString& group) const;

    /** File the mod under group; an empty group removes it from any group.
     *  Older keys for the same mod are cleaned up. */
    void assign(const Mod& mod, const QString& group);
    void renameGroup(const QString& from, const QString& to);
    void deleteGroup(const QString& group);

   private:
    QString m_path;
    QHash<QString, QString> m_groupByKey;
};
