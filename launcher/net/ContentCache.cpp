// SPDX-License-Identifier: GPL-3.0-only
#include "ContentCache.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#include "FileSystem.h"
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

void pruneToCap()
{
    QDir dir(cacheDir());
    auto entries = dir.entryInfoList(QDir::Files, QDir::Time);  // newest first
    qint64 total = 0;
    for (const auto& info : entries)
        total += info.size();
    // drop the least recently touched entries until we fit
    for (int i = entries.size() - 1; i >= 0 && total > DEFAULT_CAP_BYTES; i--) {
        total -= entries[i].size();
        QFile::remove(entries[i].absoluteFilePath());
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
    // touch, so pruning sees this entry as fresh
    QFile file(path);
    if (file.open(QIODevice::ReadWrite))
        file.setFileTime(QDateTime::currentDateTime(), QFileDevice::FileModificationTime);
    return path;
}

void ContentCache::store(const QString& hashType, const QString& hash, const QString& filePath)
{
    const QString path = entryPath(hashType, hash);
    if (path.isEmpty() || QFile::exists(path))
        return;
    if (!FS::ensureFolderPathExists(cacheDir()))
        return;
    // copy via a temp name so a half-written entry can never be served
    const QString tempPath = path + ".tmp" + QString::number(QCoreApplication::applicationPid());
    if (!QFile::copy(filePath, tempPath))
        return;
    if (!QFile::rename(tempPath, path)) {
        QFile::remove(tempPath);
        return;
    }
    pruneToCap();
}

void ContentCache::CopyTask::executeTask()
{
    setStatus(tr("Copying from the download cache…"));

    const QString computed = Hashing::hash(m_cachedPath, Hashing::algorithmFromString(m_hashType));
    if (computed.isEmpty() || computed.toLower() != m_hash.toLower()) {
        qWarning() << "Content cache entry failed verification, dropping:" << m_cachedPath;
        QFile::remove(m_cachedPath);
        emitFailed(tr("The cached copy was corrupted and has been removed. Try the download again."));
        return;
    }

    if (QFile::exists(m_destPath))
        QFile::remove(m_destPath);
    if (!FS::ensureFilePathExists(m_destPath) || !QFile::copy(m_cachedPath, m_destPath)) {
        emitFailed(tr("Could not copy the cached file to %1.").arg(m_destPath));
        return;
    }
    emitSucceeded();
}
