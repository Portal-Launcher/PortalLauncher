// SPDX-License-Identifier: GPL-3.0-only
#include "LauncherScanner.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTemporaryFile>

#ifdef LAUNCHER_HAS_QTSQL
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#endif

#include "FileSystem.h"
#include "icons/IconUtils.h"

namespace LauncherImport {

namespace {

QString homeDir()
{
    return QDir::homePath();
}

/** The per-OS "application data" folder most launchers put their data in. */
QString appDataDir()
{
#if defined(Q_OS_WIN)
    const QString roaming = qEnvironmentVariable("APPDATA");
    return roaming.isEmpty() ? FS::PathCombine(homeDir(), "AppData/Roaming") : roaming;
#elif defined(Q_OS_MACOS)
    return FS::PathCombine(homeDir(), "Library/Application Support");
#else
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);  // ~/.local/share
#endif
}

QJsonObject readJsonObject(const QString& path)
{
    QFile file(path);
    if (!file.exists() || !file.open(QIODevice::ReadOnly))
        return {};
    QJsonParseError err{};
    auto doc = QJsonDocument::fromJson(file.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return {};
    return doc.object();
}

QString existingIcon(const QString& path)
{
    return (!path.isEmpty() && QFileInfo(path).isFile()) ? path : QString();
}

/** Writes an inline data: image (vanilla launcher profile icons) to a temp
 *  file so it can be imported like any other icon. */
QString materializeDataUri(const QString& uri)
{
    if (!uri.startsWith(QLatin1String("data:image/")))
        return {};
    const int comma = uri.indexOf(',');
    if (comma < 0)
        return {};
    const QByteArray bytes = QByteArray::fromBase64(uri.mid(comma + 1).toLatin1());
    if (bytes.isEmpty())
        return {};
    QTemporaryFile temp(QDir::tempPath() + "/import-icon-XXXXXX.png");
    temp.setAutoRemove(false);
    if (!temp.open())
        return {};
    temp.write(bytes);
    temp.close();
    return temp.fileName();
}

/** Forge versions that still carry the game version ("1.16.5-36.2.0"). */
QString stripGameVersionPrefix(QString version, const QString& mcVersion)
{
    if (!mcVersion.isEmpty() && version.startsWith(mcVersion + '-'))
        version = version.mid(mcVersion.size() + 1);
    return version;
}

/** Try every single-instance parser on each subfolder of dir. */
void scanInstancesFolder(QList<FoundInstance>& out, const QString& dir, Source mmcSource = Source::MultiMC)
{
    QDir d(dir);
    if (!d.exists())
        return;
    const auto subdirs = d.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Readable);
    for (const auto& info : subdirs) {
        const QString path = info.absoluteFilePath();
        addUnique(out, parseMultiMCInstance(path, mmcSource));
        addUnique(out, parseCurseForgeInstance(path));
        addUnique(out, parseATLauncherInstance(path));
        addUnique(out, parseXMCLInstance(path));
        addUnique(out, parseGDLauncherInstance(path));
        addUnique(out, parseGDLauncherLegacyInstance(path));
    }
}

/** A MultiMC-family data root (instances/ + icons/). */
void scanMultiMCRoot(QList<FoundInstance>& out, const QString& root, Source source)
{
    if (root.isEmpty())
        return;
    // Never offer the launcher its own instances.
    const QString ownRoot = QDir::current().canonicalPath();
    if (!ownRoot.isEmpty() && QFileInfo(root).canonicalFilePath() == ownRoot)
        return;
    QDir d(FS::PathCombine(root, "instances"));
    if (!d.exists())
        return;
    const auto subdirs = d.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Readable);
    for (const auto& info : subdirs)
        addUnique(out, parseMultiMCInstance(info.absoluteFilePath(), source));
}

}  // namespace

QString sourceName(Source source)
{
    switch (source) {
        case Source::Vanilla:
            return QStringLiteral("Minecraft Launcher");
        case Source::CurseForge:
            return QStringLiteral("CurseForge");
        case Source::ModrinthApp:
            return QStringLiteral("Modrinth App");
        case Source::MultiMC:
            return QStringLiteral("MultiMC");
        case Source::PolyMC:
            return QStringLiteral("PolyMC");
        case Source::Prism:
            return QStringLiteral("Prism Launcher");
        case Source::GDLauncher:
            return QStringLiteral("GDLauncher");
        case Source::GDLauncherLegacy:
            return QStringLiteral("GDLauncher (legacy)");
        case Source::ATLauncher:
            return QStringLiteral("ATLauncher");
        case Source::XMCL:
            return QStringLiteral("XMCL");
    }
    return {};
}

void addUnique(QList<FoundInstance>& list, const FoundInstance& inst)
{
    if (!inst.isValid())
        return;
    // Several vanilla profiles legitimately share one game folder, so the
    // identity is folder + name. The same folder can be reached under
    // different spellings on case-insensitive filesystems, so compare the
    // way the filesystem does.
    const QString where = QDir::cleanPath(QFileInfo(inst.location()).absoluteFilePath());
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
    const Qt::CaseSensitivity cs = Qt::CaseInsensitive;
#else
    const Qt::CaseSensitivity cs = Qt::CaseSensitive;
#endif
    for (const auto& existing : list) {
        if (existing.name == inst.name && QDir::cleanPath(QFileInfo(existing.location()).absoluteFilePath()).compare(where, cs) == 0)
            return;
    }
    list.append(inst);
}

QString loaderUidFor(const QString& loaderName)
{
    const QString l = loaderName.trimmed().toLower();
    if (l == QLatin1String("forge"))
        return QStringLiteral("net.minecraftforge");
    if (l == QLatin1String("neoforge") || l == QLatin1String("neoforged"))
        return QStringLiteral("net.neoforged");
    if (l == QLatin1String("fabric") || l == QLatin1String("fabricloader") || l == QLatin1String("fabric-loader"))
        return QStringLiteral("net.fabricmc.fabric-loader");
    if (l == QLatin1String("quilt") || l == QLatin1String("quiltloader") || l == QLatin1String("quilt-loader"))
        return QStringLiteral("org.quiltmc.quilt-loader");
    if (l == QLatin1String("liteloader"))
        return QStringLiteral("com.mumfrey.liteloader");
    return {};
}

QString FoundInstance::loaderDescription() const
{
    if (loaderUid.isEmpty())
        return {};
    QString label;
    if (loaderUid == QLatin1String("net.minecraftforge"))
        label = QStringLiteral("Forge");
    else if (loaderUid == QLatin1String("net.neoforged"))
        label = QStringLiteral("NeoForge");
    else if (loaderUid == QLatin1String("net.fabricmc.fabric-loader"))
        label = QStringLiteral("Fabric");
    else if (loaderUid == QLatin1String("org.quiltmc.quilt-loader"))
        label = QStringLiteral("Quilt");
    else if (loaderUid == QLatin1String("com.mumfrey.liteloader"))
        label = QStringLiteral("LiteLoader");
    else
        label = loaderUid;
    return loaderVersion.isEmpty() ? label : label + ' ' + loaderVersion;
}

// ---------------------------------------------------------------- CurseForge

FoundInstance parseCurseForgeInstance(const QString& dir)
{
    const auto root = readJsonObject(FS::PathCombine(dir, "minecraftinstance.json"));
    if (root.isEmpty())
        return {};
    FoundInstance inst;
    inst.source = Source::CurseForge;
    inst.name = root.value("name").toString();
    inst.gameDir = dir;
    inst.mcVersion = root.value("gameVersion").toString();
    const auto loader = root.value("baseModLoader").toObject();
    if (!loader.isEmpty()) {
        // "forge-47.2.0", "fabric-0.15.3", "neoforge-21.1.65"
        const QString full = loader.value("name").toString();
        const int dash = full.indexOf('-');
        if (dash > 0) {
            inst.loaderUid = loaderUidFor(full.left(dash));
            inst.loaderVersion = stripGameVersionPrefix(full.mid(dash + 1), inst.mcVersion);
        }
        if (inst.mcVersion.isEmpty())
            inst.mcVersion = loader.value("minecraftVersion").toString();
    }
    for (const QString& candidate : { QStringLiteral("minecraftinstance.json"), QStringLiteral("modlist.html"),
                                      QStringLiteral(".curseclient"), QStringLiteral("profileImage.png") })
        inst.excludes.append(candidate);
    inst.iconPath = existingIcon(FS::PathCombine(dir, "profileImage.png"));
    if (inst.name.isEmpty())
        inst.name = QFileInfo(dir).fileName();
    return inst;
}

// ---------------------------------------------------------------- ATLauncher

FoundInstance parseATLauncherInstance(const QString& dir)
{
    const auto root = readJsonObject(FS::PathCombine(dir, "instance.json"));
    const auto launcher = root.value("launcher").toObject();
    if (root.isEmpty() || launcher.isEmpty())
        return {};
    FoundInstance inst;
    inst.source = Source::ATLauncher;
    inst.name = launcher.value("name").toString();
    inst.gameDir = dir;
    inst.mcVersion = root.value("id").toString();
    const auto loader = launcher.value("loaderVersion").toObject();
    if (!loader.isEmpty()) {
        inst.loaderUid = loaderUidFor(loader.value("type").toString());
        inst.loaderVersion = stripGameVersionPrefix(loader.value("version").toString(), inst.mcVersion);
        if (inst.mcVersion.isEmpty())
            inst.mcVersion = loader.value("minecraft").toString();
    }
    inst.excludes = { QStringLiteral("instance.json"), QStringLiteral("instance.png"), QStringLiteral("disabledmods"),
                      QStringLiteral("jarmods"), QStringLiteral("bin") };
    inst.iconPath = existingIcon(FS::PathCombine(dir, "instance.png"));
    if (inst.name.isEmpty())
        inst.name = QFileInfo(dir).fileName();
    return inst;
}

// ---------------------------------------------------------------- XMCL

FoundInstance parseXMCLInstance(const QString& dir)
{
    const auto root = readJsonObject(FS::PathCombine(dir, "instance.json"));
    const auto runtime = root.value("runtime").toObject();
    if (root.isEmpty() || runtime.isEmpty())
        return {};
    FoundInstance inst;
    inst.source = Source::XMCL;
    inst.name = root.value("name").toString();
    inst.gameDir = dir;
    inst.mcVersion = runtime.value("minecraft").toString();
    struct Loader {
        const char* key;
        const char* name;
    };
    for (const Loader& l : { Loader{ "neoForged", "neoforge" }, Loader{ "forge", "forge" }, Loader{ "fabricLoader", "fabric" },
                             Loader{ "quiltLoader", "quilt" }, Loader{ "liteloader", "liteloader" } }) {
        const QString version = runtime.value(QLatin1String(l.key)).toString();
        if (!version.isEmpty()) {
            inst.loaderUid = loaderUidFor(QLatin1String(l.name));
            inst.loaderVersion = stripGameVersionPrefix(version, inst.mcVersion);
            break;
        }
    }
    inst.excludes = { QStringLiteral("instance.json"), QStringLiteral(".xmcl") };
    const QString icon = root.value("icon").toString();
    if (icon.startsWith(QLatin1String("data:")))
        inst.iconPath = materializeDataUri(icon);
    else if (!icon.isEmpty() && !icon.contains(QLatin1String("://")))
        inst.iconPath = existingIcon(QDir(dir).absoluteFilePath(icon));
    if (inst.name.isEmpty())
        inst.name = QFileInfo(dir).fileName();
    return inst;
}

// ---------------------------------------------------------------- GDLauncher

FoundInstance parseGDLauncherInstance(const QString& dir)
{
    const auto root = readJsonObject(FS::PathCombine(dir, "instance.json"));
    const auto gameConfig = root.value("game_configuration").toObject();
    if (root.isEmpty() || gameConfig.isEmpty())
        return {};
    FoundInstance inst;
    inst.source = Source::GDLauncher;
    inst.name = root.value("name").toString();
    inst.gameDir = FS::PathCombine(dir, "instance");
    if (!QDir(inst.gameDir).exists())
        return {};
    // version is either {"Standard": {release, modloaders}} or {"Version": {...}}
    auto version = gameConfig.value("version").toObject();
    for (const QString& wrapper : { QStringLiteral("Standard"), QStringLiteral("Version") }) {
        if (version.contains(wrapper) && version.value(wrapper).isObject()) {
            version = version.value(wrapper).toObject();
            break;
        }
    }
    inst.mcVersion = version.value("release").toString();
    const auto loaders = version.value("modloaders").toArray();
    if (!loaders.isEmpty()) {
        const auto first = loaders.first().toObject();
        QString type = first.value("type_").toString();
        if (type.isEmpty())
            type = first.value("type").toString();
        inst.loaderUid = loaderUidFor(type);
        inst.loaderVersion = stripGameVersionPrefix(first.value("version").toString(), inst.mcVersion);
    }
    inst.iconPath = existingIcon(FS::PathCombine(dir, "icon.png"));
    if (inst.iconPath.isEmpty())
        inst.iconPath = existingIcon(FS::PathCombine(dir, "icon"));
    if (inst.name.isEmpty())
        inst.name = QFileInfo(dir).fileName();
    return inst;
}

FoundInstance parseGDLauncherLegacyInstance(const QString& dir)
{
    const auto root = readJsonObject(FS::PathCombine(dir, "config.json"));
    const auto loader = root.value("loader").toObject();
    if (root.isEmpty() || loader.isEmpty())
        return {};
    FoundInstance inst;
    inst.source = Source::GDLauncherLegacy;
    inst.name = QFileInfo(dir).fileName();
    inst.gameDir = dir;
    inst.mcVersion = loader.value("mcVersion").toString();
    inst.loaderUid = loaderUidFor(loader.value("loaderType").toString());
    inst.loaderVersion = stripGameVersionPrefix(loader.value("loaderVersion").toString(), inst.mcVersion);
    inst.excludes = { QStringLiteral("config.json"), QStringLiteral("background.png"), QStringLiteral("thumbnail.png") };
    inst.iconPath = existingIcon(FS::PathCombine(dir, "thumbnail.png"));
    return inst;
}

// ---------------------------------------------------------------- MultiMC family

FoundInstance parseMultiMCInstance(const QString& dir, Source source)
{
    const QString cfgPath = FS::PathCombine(dir, "instance.cfg");
    QFile cfg(cfgPath);
    if (!cfg.exists() || !cfg.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    FoundInstance inst;
    inst.source = source;
    inst.instanceDir = dir;
    QString iconKey;
    while (!cfg.atEnd()) {
        const QString line = QString::fromUtf8(cfg.readLine()).trimmed();
        const int eq = line.indexOf('=');
        if (eq <= 0)
            continue;
        const QString key = line.left(eq).trimmed();
        const QString value = line.mid(eq + 1).trimmed();
        if (key == QLatin1String("name"))
            inst.name = value;
        else if (key == QLatin1String("iconKey"))
            iconKey = value;
    }
    if (inst.name.isEmpty())
        inst.name = QFileInfo(dir).fileName();

    const auto pack = readJsonObject(FS::PathCombine(dir, "mmc-pack.json"));
    for (const auto& value : pack.value("components").toArray()) {
        const auto component = value.toObject();
        const QString uid = component.value("uid").toString();
        const QString version = component.value("version").toString();
        if (uid == QLatin1String("net.minecraft")) {
            inst.mcVersion = version;
        } else if (uid == QLatin1String("net.minecraftforge") || uid == QLatin1String("net.neoforged") ||
                   uid == QLatin1String("net.fabricmc.fabric-loader") || uid == QLatin1String("org.quiltmc.quilt-loader") ||
                   uid == QLatin1String("com.mumfrey.liteloader")) {
            inst.loaderUid = uid;
            inst.loaderVersion = version;
        }
    }
    // MultiMC-format instances are copied whole, so mcVersion is informative only.
    if (inst.mcVersion.isEmpty())
        inst.mcVersion = QStringLiteral("?");

    if (!iconKey.isEmpty() && iconKey != QLatin1String("default")) {
        // the launcher's icons folder sits next to its instances folder
        const QString iconsDir = FS::PathCombine(QFileInfo(dir).dir().absolutePath(), "..", "icons");
        const QString found = IconUtils::findBestIconIn(QDir(iconsDir).absolutePath(), iconKey);
        inst.iconPath = existingIcon(found);
    }
    return inst;
}

// ---------------------------------------------------------------- Vanilla launcher

bool parseVanillaVersionId(const QString& id, QString& loaderUid, QString& loaderVersion)
{
    static const QRegularExpression s_fabric("^(fabric|quilt)-loader-([^-]+)-");
    static const QRegularExpression s_neoforge("^neoforge-(.+)$");
    static const QRegularExpression s_forge("(?:^|-)forge-?(\\d[\\w.+-]*)$", QRegularExpression::CaseInsensitiveOption);

    auto m = s_fabric.match(id);
    if (m.hasMatch()) {
        loaderUid = loaderUidFor(m.captured(1));
        loaderVersion = m.captured(2);
        return true;
    }
    m = s_neoforge.match(id);
    if (m.hasMatch()) {
        loaderUid = loaderUidFor(QStringLiteral("neoforge"));
        loaderVersion = m.captured(1);
        return true;
    }
    m = s_forge.match(id);
    if (m.hasMatch()) {
        loaderUid = loaderUidFor(QStringLiteral("forge"));
        loaderVersion = m.captured(1);
        return true;
    }
    return false;
}

QList<FoundInstance> parseVanillaRoot(const QString& dotMinecraft)
{
    QList<FoundInstance> out;
    const auto root = readJsonObject(FS::PathCombine(dotMinecraft, "launcher_profiles.json"));
    const auto profiles = root.value("profiles").toObject();
    if (profiles.isEmpty())
        return out;

    // Everything the vanilla launcher owns, as opposed to the game's own files.
    static const QStringList s_launcherOwned = {
        "versions", "libraries", "assets", "natives", "runtime", "logs", "crash-reports", "webcache", "webcache2",
        "launcher_profiles.json", "launcher_accounts.json", "launcher_settings.json", "launcher_ui_state.json",
        "launcher_msa_credentials.bin", "launcher_log.txt", "launcher_cef_log.txt", "launcher.log", "clientId.txt",
        "usercache.json", "usernamecache.json", "realms_persistence.json", "TelemetryData", "bin", ".mixin.out", ".fabric",
    };

    // The stock profiles point at "latest-release"/"latest-snapshot"; the
    // launcher's cached manifest says which version that is right now.
    const auto latest = readJsonObject(FS::PathCombine(dotMinecraft, "versions", "version_manifest_v2.json")).value("latest").toObject();

    for (auto it = profiles.constBegin(); it != profiles.constEnd(); ++it) {
        const auto profile = it.value().toObject();
        QString versionId = profile.value("lastVersionId").toString();
        QString defaultName;
        if (versionId == QLatin1String("latest-release")) {
            versionId = latest.value("release").toString();
            defaultName = QStringLiteral("Minecraft (latest release)");
        } else if (versionId == QLatin1String("latest-snapshot")) {
            versionId = latest.value("snapshot").toString();
            defaultName = QStringLiteral("Minecraft (latest snapshot)");
        }
        if (versionId.isEmpty())
            continue;

        FoundInstance inst;
        inst.source = Source::Vanilla;
        inst.name = profile.value("name").toString();
        if (inst.name.isEmpty())
            inst.name = defaultName.isEmpty() ? versionId : defaultName;
        const QString gameDir = profile.value("gameDir").toString();
        inst.gameDir = gameDir.isEmpty() ? dotMinecraft : gameDir;
        if (!QDir(inst.gameDir).exists())
            continue;
        inst.excludes = s_launcherOwned;

        // The version json says which vanilla version a loader build inherits from.
        const auto versionJson = readJsonObject(FS::PathCombine(dotMinecraft, "versions", versionId, versionId + ".json"));
        const QString inherits = versionJson.value("inheritsFrom").toString();
        if (parseVanillaVersionId(versionId, inst.loaderUid, inst.loaderVersion)) {
            inst.mcVersion = inherits;
            if (inst.mcVersion.isEmpty()) {
                // fabric-loader-X-<mc> and <mc>-forge-X carry the game version in the id
                static const QRegularExpression s_trailingMc("-(\\d+\\.\\d+(?:\\.\\d+)?)$");
                static const QRegularExpression s_leadingMc("^(\\d+\\.\\d+(?:\\.\\d+)?)-");
                auto m = s_trailingMc.match(versionId);
                if (!m.hasMatch())
                    m = s_leadingMc.match(versionId);
                if (m.hasMatch())
                    inst.mcVersion = m.captured(1);
            }
        } else {
            inst.mcVersion = inherits.isEmpty() ? versionId : inherits;
        }
        if (inst.mcVersion.isEmpty())
            continue;

        inst.iconPath = materializeDataUri(profile.value("icon").toString());
        addUnique(out, inst);
    }
    return out;
}

// ---------------------------------------------------------------- Modrinth App

QList<FoundInstance> parseModrinthAppRoot(const QString& root)
{
    QList<FoundInstance> out;
#ifdef LAUNCHER_HAS_QTSQL
    const QString dbPath = FS::PathCombine(root, "app.db");
    const QString profilesDir = FS::PathCombine(root, "profiles");
    if (!QFileInfo(dbPath).isFile() || !QDir(profilesDir).exists())
        return out;

    // Work on a copy: the app may have the database open in WAL mode, and we
    // must never touch its files.
    QTemporaryDir temp;
    if (!temp.isValid())
        return out;
    const QString copy = FS::PathCombine(temp.path(), "app.db");
    if (!QFile::copy(dbPath, copy))
        return out;
    if (QFile::exists(dbPath + "-wal"))
        QFile::copy(dbPath + "-wal", copy + "-wal");

    const QString connection = QStringLiteral("portal-import-modrinth-%1").arg(QCoreApplication::applicationPid());
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
        db.setDatabaseName(copy);
        if (!db.open()) {
            qWarning() << "Could not open Modrinth App database:" << db.lastError().text();
        } else {
            struct Row {
                QString path, name, icon, mc, loader, loaderVersion;
            };
            QList<Row> rows;
            // Mid-2026 schema: instances + content sets. Older: a flat profiles table.
            QSqlQuery query(db);
            bool ok = query.exec(QStringLiteral("SELECT i.path, i.name, i.icon_path, c.game_version, c.loader, c.loader_version "
                                                "FROM instances i LEFT JOIN instance_content_sets c ON c.id = i.applied_content_set_id"));
            if (!ok)
                ok = query.exec(QStringLiteral("SELECT path, name, icon_path, game_version, mod_loader, mod_loader_version FROM profiles"));
            if (ok) {
                while (query.next())
                    rows.append({ query.value(0).toString(), query.value(1).toString(), query.value(2).toString(),
                                  query.value(3).toString(), query.value(4).toString(), query.value(5).toString() });
            } else {
                qWarning() << "Modrinth App database has an unknown layout:" << query.lastError().text();
            }
            db.close();

            for (const auto& row : rows) {
                FoundInstance inst;
                inst.source = Source::ModrinthApp;
                inst.name = row.name.isEmpty() ? row.path : row.name;
                inst.gameDir = FS::PathCombine(profilesDir, row.path);
                if (!QDir(inst.gameDir).exists())
                    continue;
                inst.mcVersion = row.mc;
                if (row.loader != QLatin1String("vanilla")) {
                    inst.loaderUid = loaderUidFor(row.loader);
                    inst.loaderVersion = row.loaderVersion;
                }
                inst.excludes = { QStringLiteral("profile.json"), QStringLiteral(".modrinth"), QStringLiteral("logs"), QStringLiteral("crash-reports") };
                if (!row.icon.isEmpty()) {
                    inst.iconPath = existingIcon(row.icon);
                    if (inst.iconPath.isEmpty())
                        inst.iconPath = existingIcon(FS::PathCombine(root, "caches", "icons", row.icon));
                    if (inst.iconPath.isEmpty())
                        inst.iconPath = existingIcon(FS::PathCombine(inst.gameDir, row.icon));
                }
                addUnique(out, inst);
            }
        }
    }
    QSqlDatabase::removeDatabase(connection);
#else
    Q_UNUSED(root);
#endif
    return out;
}

// ---------------------------------------------------------------- discovery

QList<FoundInstance> scanDefaultLocations()
{
    QList<FoundInstance> out;
    const QString home = homeDir();
    const QString appData = appDataDir();
#if defined(Q_OS_WIN)
    QString localAppData = qEnvironmentVariable("LOCALAPPDATA");
    if (localAppData.isEmpty())
        localAppData = FS::PathCombine(home, "AppData/Local");
#endif

    // Minecraft Launcher
#if defined(Q_OS_WIN)
    out += parseVanillaRoot(FS::PathCombine(appData, ".minecraft"));
#elif defined(Q_OS_MACOS)
    out += parseVanillaRoot(FS::PathCombine(appData, "minecraft"));
#else
    out += parseVanillaRoot(FS::PathCombine(home, ".minecraft"));
    out += parseVanillaRoot(FS::PathCombine(home, ".var/app/com.mojang.Minecraft/.minecraft"));
#endif

    // CurseForge app
#if defined(Q_OS_MACOS)
    scanInstancesFolder(out, FS::PathCombine(home, "Documents/curseforge/minecraft/Instances"));
#endif
    scanInstancesFolder(out, FS::PathCombine(home, "curseforge/minecraft/Instances"));

    // Modrinth App (current and older data folder names)
    for (const QString& name : { QStringLiteral("ModrinthApp"), QStringLiteral("com.modrinth.theseus"), QStringLiteral("theseus") }) {
        out += parseModrinthAppRoot(FS::PathCombine(appData, name));
#if !defined(Q_OS_WIN) && !defined(Q_OS_MACOS)
        out += parseModrinthAppRoot(FS::PathCombine(home, ".var/app/com.modrinth.ModrinthApp/data", name));
#endif
    }

    // MultiMC family
    scanMultiMCRoot(out, FS::PathCombine(appData, "PrismLauncher"), Source::Prism);
    scanMultiMCRoot(out, FS::PathCombine(appData, "PolyMC"), Source::PolyMC);
    scanMultiMCRoot(out, FS::PathCombine(appData, "MultiMC"), Source::MultiMC);
    scanMultiMCRoot(out, FS::PathCombine(appData, "multimc"), Source::MultiMC);
#if defined(Q_OS_WIN)
    scanMultiMCRoot(out, FS::PathCombine(localAppData, "Programs", "MultiMC"), Source::MultiMC);
    scanMultiMCRoot(out, FS::PathCombine(home, "MultiMC"), Source::MultiMC);
#elif !defined(Q_OS_MACOS)
    scanMultiMCRoot(out, FS::PathCombine(home, ".var/app/org.prismlauncher.PrismLauncher/data/PrismLauncher"), Source::Prism);
    scanMultiMCRoot(out, FS::PathCombine(home, ".var/app/org.polymc.PolyMC/data/PolyMC"), Source::PolyMC);
#endif

    // GDLauncher
    scanInstancesFolder(out, FS::PathCombine(appData, "gdlauncher_carbon/data/instances"));
    scanInstancesFolder(out, FS::PathCombine(appData, "gdlauncher_next/instances"));

    // ATLauncher
    scanInstancesFolder(out, FS::PathCombine(appData, "ATLauncher/instances"));
#if defined(Q_OS_WIN)
    scanInstancesFolder(out, FS::PathCombine(localAppData, "Programs", "ATLauncher", "instances"));
#endif
    scanInstancesFolder(out, FS::PathCombine(home, "ATLauncher/instances"));

    // XMCL
    scanInstancesFolder(out, FS::PathCombine(appData, "xmcl/instances"));
    scanInstancesFolder(out, FS::PathCombine(home, ".xmcl/instances"));

    return out;
}

QList<FoundInstance> scanPath(const QString& path)
{
    QList<FoundInstance> out;
    if (path.isEmpty() || !QDir(path).exists())
        return out;

    // a launcher data root
    out += parseVanillaRoot(path);
    out += parseModrinthAppRoot(path);
    scanMultiMCRoot(out, path, Source::MultiMC);
    // an instances folder, or a root that has one under a common name
    scanInstancesFolder(out, path);
    for (const QString& sub : { QStringLiteral("instances"), QStringLiteral("Instances"), QStringLiteral("data/instances"),
                                QStringLiteral("minecraft/Instances"), QStringLiteral("profiles") })
        scanInstancesFolder(out, FS::PathCombine(path, sub));
    // a single instance
    addUnique(out, parseMultiMCInstance(path));
    addUnique(out, parseCurseForgeInstance(path));
    addUnique(out, parseATLauncherInstance(path));
    addUnique(out, parseXMCLInstance(path));
    addUnique(out, parseGDLauncherInstance(path));
    addUnique(out, parseGDLauncherLegacyInstance(path));
    return out;
}

}  // namespace LauncherImport
