// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Prism Launcher fork - mod loadouts
 *
 *  Named enabled/disabled presets for the mods of one instance ("Everything",
 *  "Performance only", "Server testing"...), stored as mod-loadouts.json in
 *  the instance root next to mod-groups.json.
 *
 *  A loadout records the set of ENABLED mods by durable identity (the same
 *  keys mod groups use), so it survives updates that rename jars. Applying a
 *  loadout enables exactly the recorded mods and disables the rest; mods that
 *  appeared after the loadout was saved are left enabled (new content should
 *  never silently vanish).
 */
#pragma once

#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>

class Mod;

class ModLoadouts {
   public:
    ModLoadouts() = default;
    explicit ModLoadouts(const QString& instanceRoot) { setInstanceRoot(instanceRoot); }

    void setInstanceRoot(const QString& instanceRoot);
    bool load();
    bool save() const;

    static QString filePath(const QString& instanceRoot);

    QStringList loadoutNames() const;
    bool has(const QString& name) const { return m_enabledKeysByName.contains(name); }

    /** Record the current state under 'name' (replaces): enabledModKeys are
     *  the key sets of enabled mods, allModKeys the key sets of every mod
     *  present, so applying later can tell "disabled then" from "unknown". */
    void capture(const QString& name, const QList<QStringList>& enabledModKeys, const QList<QStringList>& allModKeys);

    /** Whether the mod (identified by its keys) is enabled in the loadout. */
    bool isEnabledIn(const QString& name, const QStringList& keys) const;

    /** True when the loadout knew about this mod when it was captured; mods
     *  added later are not disabled by applying an older loadout. */
    bool knowsMod(const QString& name, const QStringList& keys) const;

    void rename(const QString& from, const QString& to);
    void remove(const QString& name);

   private:
    QString m_path;
    QHash<QString, QSet<QString>> m_enabledKeysByName;  // loadout -> enabled mod keys
    QHash<QString, QSet<QString>> m_knownKeysByName;    // loadout -> all keys seen at capture
};
