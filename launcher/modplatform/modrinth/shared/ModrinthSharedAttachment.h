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
    QList<ManagedFile> managedFiles;
    QStringList managedConfigs;

    bool isOwner() const { return role == QLatin1String("owner"); }
    bool isMember() const { return role == QLatin1String("member"); }

    static QString filePath(const QString& instanceRoot);
    static std::optional<Attachment> load(const QString& instanceRoot);
    bool save(const QString& instanceRoot) const;
    static void remove(const QString& instanceRoot);
};

}  // namespace ModrinthShared
