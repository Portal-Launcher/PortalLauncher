// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Prism Launcher - Minecraft Launcher
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 */

#pragma once

#include <QFuture>
#include <QFutureWatcher>
#include <QStringList>
#include <atomic>

#include "tasks/Task.h"

/** Walks every instance's mods, resource packs, shader packs and data packs,
 *  finds files that are byte-for-byte duplicates, and replaces the copies
 *  with hard links through the content pool, so each distinct file occupies
 *  disk once. Files that cannot be linked (other volume, in use by a running
 *  game) are skipped, never broken. Purely additive: every instance keeps a
 *  path with the exact same bytes at the exact same name.
 *
 *  Runs in two steps so the user sees real numbers before anything changes:
 *  a planning task hashes and groups the files, an apply task links them.
 */
class ContentPoolDedupTask : public Task {
    Q_OBJECT

   public:
    struct Group {
        QString sha1;
        QStringList paths;  // every copy, first one is the pool donor
        qint64 size = 0;
    };
    struct Plan {
        QList<Group> groups;
        int files = 0;       // copies that would become links
        qint64 bytes = 0;    // disk that would come back
        bool isEmpty() const { return files == 0; }
    };
    struct Result {
        int filesPooled = 0;
        int filesSkipped = 0;
        qint64 bytesFreed = 0;
    };

    /** Planning: scan these instance roots and work out what could be pooled. */
    explicit ContentPoolDedupTask(QStringList instanceDirs, QObject* parent = nullptr);
    /** Applying: link the copies a planning run found. */
    explicit ContentPoolDedupTask(Plan plan, QObject* parent = nullptr);

    Plan plan() const { return m_plan; }
    Result result() const { return m_result; }

    bool abort() override;

   protected:
    void executeTask() override;

   private:
    Plan buildPlan();
    Result applyPlan();

    QStringList m_instanceDirs;
    bool m_applying = false;
    Plan m_plan;
    Result m_result;
    std::atomic_bool m_aborted = false;

    QFuture<void> m_future;
    QFutureWatcher<void> m_watcher;
};
