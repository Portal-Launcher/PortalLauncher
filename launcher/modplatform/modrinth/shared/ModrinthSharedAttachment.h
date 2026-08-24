// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Prism Launcher fork - Modrinth Shared Instances support
 *
 *  Per-instance share state, stored as shared-instance.json in the instance
 *  root (next to instance.cfg).
 */
#pragma once

#include <QList>
#include <QString>
#include <QStringList>
#include <optional>

namespace ModrinthShared {

struct ManagedFile {
    QString rel;     // path relative to the game root, forward slashes
    QString sha1;    // may be empty for external files
    qint64 size = -1;
    QString source;       // "modrinth:<versionId>" or "external"
    QString optionalKey;  // non-empty when the owner marked this optional (project id or file name)
};

/** A multiplayer server the pack's owner shares with everyone in the pack. */
struct SharedServer {
    QString name;
    QString address;
};

class Attachment {
   public:
    QString id;                 // shared instance id on the service
    QString role;               // "owner" or "member"
    int appliedVersion = -1;    // last locally applied/pushed version, -1 = none
    QString configSpec;         // "", "all", or comma-separated prefixes
    QString lastPushSignature;  // owner only: content signature of last push
    QString iconSha1;           // sha1 of the last uploaded (owner) / adopted (member) icon
    qint64 iconCheckedAt = 0;   // member only: when the owner's icon was last checked (secs since epoch)
    bool autoPush = false;      // owner only: push automatically before launching
    QString quickFingerprint;   // owner only: fast content fingerprint at last push
    QString lastPushEnvironment;  // owner only: game/loader versions + config spec at last push
    QStringList lastChangeLog;  // member: human-readable summary of the last applied update
    int lastChangeVersion = -1;
    QString lastInviteLink;     // owner only: the invite link minted most recently
    QList<ManagedFile> managedFiles;
    QStringList managedConfigs;
    // Optional mods: the owner picks which mods guests may keep disabled. Keys
    // are Modrinth project ids (optionalProjects) or plain file names
    // (optionalFiles). Owners edit these; members mirror them from the share.
    QStringList optionalProjects;
    QStringList optionalFiles;
    // Member only: optional keys this user has turned off; sync keeps them off.
    QStringList disabledOptional;
    // The owner's shared server list, mirrored on sync. Lets "play along"
    // launch straight into the same server, not just the same pack.
    QList<SharedServer> sharedServers;

    bool isOwner() const { return role == QLatin1String("owner"); }
    bool isMember() const { return role == QLatin1String("member"); }

    static QString filePath(const QString& instanceRoot);
    static std::optional<Attachment> load(const QString& instanceRoot);
    bool save(const QString& instanceRoot) const;
    static void remove(const QString& instanceRoot);
};

/** What the paint/hover paths need to know about a share, without the cost of
 *  parsing the whole attachment (managed file list included) each time. */
struct ShareInfo {
    QString role;  // "owner", "member", or empty when not shared
    int appliedVersion = -1;
};

/**
 * Cheap, paint-safe lookup of the share state. Backed by an mtime-checked,
 * time-guarded cache so it can run from delegates and tooltip queries.
 */
ShareInfo cachedShareInfo(const QString& instanceRoot);

/** Shorthand for cachedShareInfo().role. */
QString cachedRole(const QString& instanceRoot);

/**
 * Fast fingerprint of shareable content (file names/sizes/mtimes only, no
 * reads). Used to hint owners that they have unpushed changes.
 */
QString quickContentFingerprint(const QString& gameRoot, bool includeConfigs);

}  // namespace ModrinthShared
