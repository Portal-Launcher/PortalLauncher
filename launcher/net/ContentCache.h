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

/** A content-addressed cache of downloaded files (mods, mostly), keyed by
 *  hash. Installing the same mod into a second instance, or syncing a shared
 *  pack that friends already play, copies from disk instead of redownloading.
 *  Copies (not links) keep every instance self-contained. Size-capped; least
 *  recently used entries are pruned when inserts push past the cap.
 */
namespace ContentCache {

/** Absolute path of the cached file for this hash, or empty when absent.
 *  Touches the entry so pruning treats it as recently used. */
QString find(const QString& hashType, const QString& hash);

/** Copy a verified downloaded file into the cache (no-op if present). */
void store(const QString& hashType, const QString& hash, const QString& filePath);

/** Copies a cache hit to its destination, re-verifying the hash on the way
 *  out. A corrupted entry is dropped from the cache and fails the task, so
 *  the next attempt downloads fresh. */
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
