// SPDX-License-Identifier: GPL-3.0-only
#include "WorldBackup.h"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>

#include "FileSystem.h"
#include "MMCZip.h"
#include "archive/ArchiveWriter.h"

namespace WorldBackup {

namespace {

/** The newest content change of a world. level.dat is rewritten on every save
 *  and world exit, so it is a reliable, cheap proxy; the folder mtime catches
 *  worlds without one (corrupt or mid-creation). */
QDateTime worldChangedAt(const QString& worldDir)
{
    const QFileInfo levelDat(FS::PathCombine(worldDir, "level.dat"));
    if (levelDat.exists())
        return levelDat.lastModified();
    return QFileInfo(worldDir).lastModified();
}

bool zipWorld(const QString& worldDir, const QString& zipPath, QString* error)
{
    const QString worldName = QFileInfo(worldDir).fileName();
    // Write under a temp name first so a half-written zip is never listed as
    // a valid backup.
    const QString tempPath = zipPath + ".part";
    {
        MMCZip::ArchiveWriter writer(tempPath);
        if (!writer.open()) {
            if (error)
                *error = QObject::tr("Could not create %1").arg(tempPath);
            return false;
        }
        QDirIterator it(worldDir, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            it.next();
            const auto info = it.fileInfo();
            // The lock file is transient and Windows refuses to read it while
            // the game holds it.
            if (info.fileName() == QLatin1String("session.lock"))
                continue;
            const QString rel = QDir(worldDir).relativeFilePath(info.absoluteFilePath());
            if (!writer.addFile(info.absoluteFilePath(), worldName + '/' + rel)) {
                if (error)
                    *error = QObject::tr("Could not add %1 to the backup").arg(rel);
                writer.close();
                QFile::remove(tempPath);
                return false;
            }
        }
        if (!writer.close()) {
            if (error)
                *error = QObject::tr("Could not finish writing %1").arg(tempPath);
            QFile::remove(tempPath);
            return false;
        }
    }
    QFile::remove(zipPath);
    if (!QFile::rename(tempPath, zipPath)) {
        if (error)
            *error = QObject::tr("Could not move the finished backup into place");
        QFile::remove(tempPath);
        return false;
    }
    return true;
}

void pruneBackups(const QString& worldBackupDir, int keep)
{
    QDir dir(worldBackupDir);
    auto zips = dir.entryInfoList({ "*.zip" }, QDir::Files, QDir::Time);  // newest first
    for (int i = zips.size() - 1; i >= keep && i >= 0; i--)
        QFile::remove(zips[i].absoluteFilePath());
}

}  // namespace

QString backupsRoot(const QString& instanceRoot)
{
    return FS::PathCombine(instanceRoot, "backups", "worlds");
}

QFileInfoList listBackups(const QString& instanceRoot, const QString& worldFolderName)
{
    QDir dir(FS::PathCombine(backupsRoot(instanceRoot), worldFolderName));
    return dir.entryInfoList({ "*.zip" }, QDir::Files, QDir::Time);  // newest first
}

bool backupOneWorld(const QString& instanceRoot, const QString& worldDir, int keep, QString* error)
{
    const QString worldName = QFileInfo(worldDir).fileName();
    const QString backupDir = FS::PathCombine(backupsRoot(instanceRoot), worldName);
    if (!FS::ensureFolderPathExists(backupDir)) {
        if (error)
            *error = QObject::tr("Could not create the backup folder for %1").arg(worldName);
        return false;
    }
    const QString zipPath =
        FS::PathCombine(backupDir, QDateTime::currentDateTime().toString("yyyy-MM-dd-HHmmss") + ".zip");
    if (!zipWorld(worldDir, zipPath, error))
        return false;
    pruneBackups(backupDir, qMax(1, keep));
    return true;
}

Result backupChangedWorlds(const QString& instanceRoot, const QString& worldsDir, int keep)
{
    Result result;
    QDir dir(worldsDir);
    for (const auto& info : dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        // Only real worlds; anything without a level.dat is not restorable
        // and probably not a world at all.
        if (!QFile::exists(FS::PathCombine(info.absoluteFilePath(), "level.dat")))
            continue;
        const auto backups = listBackups(instanceRoot, info.fileName());
        if (!backups.isEmpty() && backups.first().lastModified() >= worldChangedAt(info.absoluteFilePath())) {
            result.skipped++;
            continue;
        }
        QString error;
        if (backupOneWorld(instanceRoot, info.absoluteFilePath(), keep, &error))
            result.backedUp++;
        else
            result.errors.append(QObject::tr("%1: %2").arg(info.fileName(), error));
    }
    return result;
}

bool restoreBackup(const QString& instanceRoot, const QString& worldsDir, const QString& worldFolderName, const QString& zipPath, QString* error)
{
    const QString worldDir = FS::PathCombine(worldsDir, worldFolderName);
    // Zip the current state first, so restoring is never a one-way door.
    if (QFile::exists(FS::PathCombine(worldDir, "level.dat"))) {
        const auto backups = listBackups(instanceRoot, worldFolderName);
        if (backups.isEmpty() || backups.first().lastModified() < worldChangedAt(worldDir)) {
            // keep+1 so this safety snapshot cannot evict the one being restored
            if (!backupOneWorld(instanceRoot, worldDir, backups.size() + 2, error))
                return false;
        }
    }
    if (QFile::exists(worldDir) && !FS::deletePath(worldDir)) {
        if (error)
            *error = QObject::tr("Could not remove the current world folder (is the game running?)");
        return false;
    }
    // The zip stores everything under "<world folder>/", so extracting into
    // the worlds folder recreates it.
    if (!MMCZip::extractDir(zipPath, worldsDir)) {
        if (error)
            *error = QObject::tr("Could not extract the backup zip");
        return false;
    }
    return true;
}

}  // namespace WorldBackup
