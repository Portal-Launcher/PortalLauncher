// SPDX-License-Identifier: GPL-3.0-only
#include "ContentCache.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>

#include <filesystem>

#include "FileSystem.h"
#include "StringUtils.h"
#include "modplatform/helpers/HashUtils.h"

namespace {

constexpr qint64 DEFAULT_CAP_BYTES = qint64(2048) * 1024 * 1024;

// The launcher runs with the data folder as its working directory, same
// convention the HTTP meta cache relies on.
QString cacheDir()
{
    return QDir("cache/content").absolutePath();
}

QString entryPath(const QString& hashType, const QString& hash)
{
    // hash values are hex/decimal strings from the platforms; refuse anything
    // that could escape the cache folder
    for (const QChar& c : hash) {
        if (!c.isLetterOrNumber())
            return {};
    }
    if (hash.size() < 8 || hashType.isEmpty())
        return {};
    return FS::PathCombine(cacheDir(), hashType.toLower() + '-' + hash.toLower());
}

/** Hard link src as dst. Returns false on filesystems or volume layouts that
 *  cannot link; callers fall back to copying. */
bool tryHardLink(const QString& src, const QString& dst)
{
    std::error_code err;
    std::filesystem::create_hard_link(StringUtils::toStdString(src), StringUtils::toStdString(dst), err);
    if (err)
        qDebug() << "Content pool could not link" << src << "->" << dst << ":" << QString::fromStdString(err.message());
    return !err;
}

void pruneToCap(qint64 addedBytes)
{
    // Called after every store(). A modpack install stores hundreds of files,
    // and a full sweep opens a file handle per pool entry (hardLinkCount), so
    // O(files x pool) adds whole seconds to installs. Two escape hatches keep
    // it honest instead: skip unless enough time passed or enough bytes
    // arrived, and skip the per-entry handle pass while the naive size total
    // already fits the cap.
    static QMutex pruneMutex;
    static QElapsedTimer sincePrune;  // invalid until the first sweep
    static qint64 bytesSincePrune = 0;
    constexpr qint64 PRUNE_BYTE_STEP = qint64(256) * 1024 * 1024;

    QMutexLocker locker(&pruneMutex);
    bytesSincePrune += addedBytes;
    if (sincePrune.isValid() && sincePrune.elapsed() < 30 * 1000 && bytesSincePrune < PRUNE_BYTE_STEP)
        return;
    sincePrune.restart();
    bytesSincePrune = 0;

    QDir dir(cacheDir());
    auto entries = dir.entryInfoList(QDir::Files, QDir::Time);  // newest first
    qint64 naiveTotal = 0;
    for (const auto& info : entries)
        naiveTotal += info.size();
    // Under the cap even when counting still-linked entries: nothing could be
    // evicted anyway, so do not pay for a link-count stat per entry.
    if (naiveTotal <= DEFAULT_CAP_BYTES)
        return;

    // An entry some instance still links to shares its bytes with that
    // instance, so it is free to keep and eviction would save nothing.
    // Only unlinked leftovers count against the cap.
    QList<QFileInfo> loose;
    qint64 total = 0;
    for (const auto& info : entries) {
        if (FS::hardLinkCount(info.absoluteFilePath()) > 1)
            continue;
        total += info.size();
        loose.append(info);
    }
    // drop the least recently touched loose entries until we fit
    for (int i = loose.size() - 1; i >= 0 && total > DEFAULT_CAP_BYTES; i--) {
        total -= loose[i].size();
        QFile::remove(loose[i].absoluteFilePath());
    }
}

}  // namespace

QString ContentCache::find(const QString& hashType, const QString& hash)
{
    const QString path = entryPath(hashType, hash);
    if (path.isEmpty())
        return {};
    QFileInfo info(path);
    if (!info.exists() || info.size() <= 0)
        return {};
    // Deliberately not touched: the entry shares its inode with the instance
    // files linked to it, so bumping its mtime would make every shared pack
    // that contains the file look modified. Pruning only ever considers
    // entries nothing links to, and for those "oldest download first" is a
    // fine enough order.
    return path;
}

void ContentCache::store(const QString& hashType, const QString& hash, const QString& filePath)
{
    const QString path = entryPath(hashType, hash);
    if (path.isEmpty() || QFile::exists(path))
        return;
    if (!FS::ensureFolderPathExists(cacheDir()))
        return;
    // Linking adopts the instance's own file into the pool for free. When the
    // volume cannot link, copy via a temp name so a half-written entry can
    // never be served.
    if (!tryHardLink(filePath, path)) {
        const QString tempPath = path + ".tmp" + QString::number(QCoreApplication::applicationPid());
        if (!QFile::copy(filePath, tempPath))
            return;
        if (!QFile::rename(tempPath, path)) {
            QFile::remove(tempPath);
            return;
        }
    }
    pruneToCap(QFileInfo(path).size());
}

bool ContentCache::deploy(const QString& cachedPath, const QString& destPath, const QString& hashType, const QString& hash)
{
    const QString computed = Hashing::hash(cachedPath, Hashing::algorithmFromString(hashType));
    if (computed.isEmpty() || computed.compare(hash, Qt::CaseInsensitive) != 0) {
        qWarning() << "Content cache entry failed verification, dropping:" << cachedPath;
        QFile::remove(cachedPath);
        return false;
    }

    if (QFile::exists(destPath))
        QFile::remove(destPath);
    if (!FS::ensureFilePathExists(destPath))
        return false;
    // Same bytes, shared on disk. Anything that later replaces the file in an
    // instance does so by remove + rename, which breaks the link cleanly.
    if (tryHardLink(cachedPath, destPath))
        return true;
    return QFile::copy(cachedPath, destPath);
}

void ContentCache::CopyTask::executeTask()
{
    setStatus(tr("Installing from the download cache…"));

    if (!deploy(m_cachedPath, m_destPath, m_hashType, m_hash)) {
        emitFailed(tr("The cached copy could not be installed to %1. Try the download again.").arg(m_destPath));
        return;
    }
    emitSucceeded();
}
