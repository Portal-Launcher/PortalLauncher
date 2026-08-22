// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Prism Launcher - Minecraft Launcher
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 */

#pragma once

#include <QString>

#include "tasks/Task.h"

/** A content-addressed pool of downloaded files (mods, mostly), keyed by
 *  hash. Installing the same mod into a second instance, or syncing a shared
 *  pack that friends already play, links from disk instead of redownloading.
 *
 *  Entries are hard links where the filesystem allows it, so a mod shared by
 *  ten instances occupies disk once; when linking is not possible (another
 *  volume, FAT, ...) the pool quietly falls back to copying. Files are
 *  re-verified against their hash every time they leave the pool, and a
 *  corrupted entry is dropped so the next attempt downloads fresh. Only
 *  entries no instance links to anymore count against the size cap; the
 *  least recently used of those are pruned when inserts push past it.
 */
namespace ContentCache {

/** Absolute path of the cached file for this hash, or empty when absent.
 *  Never modifies the entry. */
QString find(const QString& hashType, const QString& hash);

/** Adopt a verified downloaded file into the pool (no-op if present).
 *  Hard links to the source when possible, copies otherwise. */
void store(const QString& hashType, const QString& hash, const QString& filePath);

/** Materialize a pool entry at destPath: re-verify the hash, then hard link
 *  (or copy) it there. Returns false and drops the entry when it no longer
 *  matches its hash, so callers fall back to a fresh download. */
bool deploy(const QString& cachedPath, const QString& destPath, const QString& hashType, const QString& hash);

/** Task wrapper around deploy() for install pipelines. */
class CopyTask : public Task {
    Q_OBJECT

   public:
    CopyTask(QString cachedPath, QString destPath, QString hashType, QString hash)
        : m_cachedPath(std::move(cachedPath)), m_destPath(std::move(destPath)), m_hashType(std::move(hashType)), m_hash(std::move(hash))
    {}

   protected:
    void executeTask() override;

   private:
    QString m_cachedPath;
    QString m_destPath;
    QString m_hashType;
    QString m_hash;
};

}  // namespace ContentCache
