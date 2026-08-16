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
    QString source;  // "modrinth:<versionId>" or "external"
};

class Attachment {
   public:
    QString id;                 // shared instance id on the service
    QString role;               // "owner" or "member"
    int appliedVersion = -1;    // last locally applied/pushed version, -1 = none
    QString configSpec;         // "", "all", or comma-separated prefixes
    QString lastPushSignature;  // owner only: content signature of last push
    QString iconSha1;           // sha1 of the last uploaded (owner) / adopted (member) icon
    bool autoPush = false;      // owner only: push automatically before launching
    QString quickFingerprint;   // owner only: fast content fingerprint at last push
    QStringList lastChangeLog;  // member: human-readable summary of the last applied update
    int lastChangeVersion = -1;
    QList<ManagedFile> managedFiles;
    QStringList managedConfigs;

    bool isOwner() const { return role == QLatin1String("owner"); }
    bool isMember() const { return role == QLatin1String("member"); }

    static QString filePath(const QString& instanceRoot);
    static std::optional<Attachment> load(const QString& instanceRoot);
    bool save(const QString& instanceRoot) const;
    static void remove(const QString& instanceRoot);
};

/**
 * Cheap, paint-safe lookup of the share role ("owner", "member", or empty).
 * Backed by an mtime-checked cache so it can run from delegates.
 */
QString cachedRole(const QString& instanceRoot);

/**
 * Fast fingerprint of shareable content (file names/sizes/mtimes only, no
 * reads). Used to hint owners that they have unpushed changes.
 */
QString quickContentFingerprint(const QString& gameRoot, bool includeConfigs);

}  // namespace ModrinthShared
