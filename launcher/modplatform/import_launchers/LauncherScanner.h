// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Prism Launcher - Minecraft Launcher
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 */

#pragma once

#include <QList>
#include <QMetaType>
#include <QString>
#include <QStringList>

/** Finds instances other launchers installed on this machine, so they can be
 *  imported without rebuilding them by hand. Every scanner reads only the
 *  launcher's own metadata files; nothing is modified.
 */
namespace LauncherImport {

enum class Source { Vanilla, CurseForge, ModrinthApp, MultiMC, PolyMC, Prism, GDLauncher, GDLauncherLegacy, ATLauncher, XMCL };

QString sourceName(Source source);

struct FoundInstance {
    Source source = Source::Vanilla;
    QString name;
    /** Directory whose contents become the instance's minecraft/ folder. */
    QString gameDir;
    /** MultiMC-format instance root (instance.cfg + mmc-pack.json); copied
     *  as-is when set, and gameDir is ignored. */
    QString instanceDir;
    QString mcVersion;
    /** Component uid of the loader ("net.fabricmc.fabric-loader"), empty for vanilla. */
    QString loaderUid;
    QString loaderVersion;
    /** An image file on disk for the instance icon, when the launcher has one. */
    QString iconPath;
    /** Top-level names inside gameDir that belong to the old launcher, not the game. */
    QStringList excludes;

    bool isValid() const { return !name.isEmpty() && (!instanceDir.isEmpty() || (!gameDir.isEmpty() && !mcVersion.isEmpty())); }
    bool isMultiMCFormat() const { return !instanceDir.isEmpty(); }
    QString launcherName() const { return sourceName(source); }
    /** "Fabric 0.15.3", "Forge 47.2.0", or empty for vanilla. */
    QString loaderDescription() const;
    /** Where this instance lives, for tooltips. */
    QString location() const { return isMultiMCFormat() ? instanceDir : gameDir; }
};

/** Component uid for a loader name as other launchers spell it
 *  ("forge", "neoforge", "fabric", "quilt", "liteloader"); empty if unknown. */
QString loaderUidFor(const QString& loaderName);

/** Append inst unless the list already has it (same folder and name). */
void addUnique(QList<FoundInstance>& list, const FoundInstance& inst);

/** Scan every launcher at its default location for this OS. */
QList<FoundInstance> scanDefaultLocations();

/** Scan a folder the user picked, working out what lives there: a launcher's
 *  data root, its instances folder, or a single instance. */
QList<FoundInstance> scanPath(const QString& path);

// Per-launcher parsers, exposed for tests. Each returns an invalid instance
// when the directory is not what it expects.
FoundInstance parseCurseForgeInstance(const QString& dir);
FoundInstance parseATLauncherInstance(const QString& dir);
FoundInstance parseXMCLInstance(const QString& dir);
FoundInstance parseGDLauncherInstance(const QString& dir);
FoundInstance parseGDLauncherLegacyInstance(const QString& dir);
FoundInstance parseMultiMCInstance(const QString& dir, Source source = Source::MultiMC);
QList<FoundInstance> parseVanillaRoot(const QString& dotMinecraft);
QList<FoundInstance> parseModrinthAppRoot(const QString& root);

/** Parses a vanilla launcher version id ("1.20.1-forge-47.2.0",
 *  "fabric-loader-0.15.3-1.20.1", "neoforge-21.1.65") into loader uid +
 *  version. Returns false when the id carries no loader. */
bool parseVanillaVersionId(const QString& id, QString& loaderUid, QString& loaderVersion);

}  // namespace LauncherImport

Q_DECLARE_METATYPE(LauncherImport::FoundInstance)
