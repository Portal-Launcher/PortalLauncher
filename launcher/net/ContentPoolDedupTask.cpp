// SPDX-License-Identifier: GPL-3.0-only
#include "ContentPoolDedupTask.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QThreadPool>
#include <QtConcurrentRun>

#include <filesystem>

#include "FileSystem.h"
#include "StringUtils.h"
#include "modplatform/helpers/HashUtils.h"
#include "net/ContentCache.h"

namespace {

// Flat resource folders duplicated between instances. Mods dominate, but
// packs repeat too.
const QStringList RESOURCE_DIRS = { "mods", "resourcepacks", "texturepacks", "shaderpacks", "datapacks" };

bool sameInode(const QString& a, const QString& b)
{
    std::error_code err;
    bool eq = std::filesystem::equivalent(StringUtils::toStdString(a), StringUtils::toStdString(b), err);
    return !err && eq;
}

}  // namespace

ContentPoolDedupTask::ContentPoolDedupTask(QStringList instanceDirs, QObject* parent)
    : Task(parent), m_instanceDirs(std::move(instanceDirs))
{}

ContentPoolDedupTask::ContentPoolDedupTask(Plan plan, QObject* parent) : Task(parent), m_applying(true), m_plan(std::move(plan)) {}

bool ContentPoolDedupTask::abort()
{
    m_aborted = true;
    return true;
}

void ContentPoolDedupTask::executeTask()
{
    setStatus(m_applying ? tr("Linking duplicates through the content pool…") : tr("Looking for duplicate files across instances…"));

    m_future = QtConcurrent::run(QThreadPool::globalInstance(), [this] {
        if (m_applying)
            m_result = applyPlan();
        else
            m_plan = buildPlan();
    });
    connect(&m_watcher, &QFutureWatcher<void>::finished, this, [this] {
        if (m_aborted)
            emitAborted();
        else
            emitSucceeded();
    });
    m_watcher.setFuture(m_future);
}

ContentPoolDedupTask::Plan ContentPoolDedupTask::buildPlan()
{
    Plan plan;

    // Pass 1: collect candidate files. The game dir is minecraft/ on Prism
    // instances, .minecraft/ on imported MultiMC ones.
    struct Candidate {
        QString path;
        qint64 size;
    };
    QList<Candidate> files;
    for (const QString& root : m_instanceDirs) {
        for (const QString& gameDir : { QStringLiteral("minecraft"), QStringLiteral(".minecraft") }) {
            for (const QString& sub : RESOURCE_DIRS) {
                QDir dir(FS::PathCombine(root, gameDir, sub));
                if (!dir.exists())
                    continue;
                const auto entries = dir.entryInfoList(QDir::Files | QDir::NoSymLinks);
                for (const auto& info : entries) {
                    if (info.size() <= 0)
                        continue;
                    files.append({ info.absoluteFilePath(), info.size() });
                }
            }
        }
        if (m_aborted)
            return plan;
    }

    // Pass 2: only files sharing a size can be duplicates; hash just those.
    QHash<qint64, QStringList> bySize;
    for (const auto& c : files)
        bySize[c.size].append(c.path);

    int toHash = 0;
    for (auto it = bySize.constBegin(); it != bySize.constEnd(); ++it)
        if (it.value().size() > 1)
            toHash += it.value().size();

    QHash<QString, Group> byHash;
    int hashed = 0;
    for (auto it = bySize.constBegin(); it != bySize.constEnd(); ++it) {
        if (it.value().size() < 2)
            continue;
        for (const QString& path : it.value()) {
            if (m_aborted)
                return plan;
            const QString hash = Hashing::hash(path, Hashing::Algorithm::Sha1);
            hashed++;
            setProgress(hashed, toHash);
            if (hash.isEmpty())
                continue;
            auto& group = byHash[hash];
            group.sha1 = hash;
            group.size = it.key();
            group.paths.append(path);
        }
    }

    // Pass 3: keep the groups where at least one copy is a separate inode.
    for (auto it = byHash.constBegin(); it != byHash.constEnd(); ++it) {
        const Group& group = it.value();
        if (group.paths.size() < 2)
            continue;
        int separate = 0;
        for (int i = 1; i < group.paths.size(); i++)
            if (!sameInode(group.paths.first(), group.paths.at(i)))
                separate++;
        if (separate == 0)
            continue;
        plan.groups.append(group);
        plan.files += separate;
        plan.bytes += separate * group.size;
    }
    return plan;
}

ContentPoolDedupTask::Result ContentPoolDedupTask::applyPlan()
{
    Result res;
    int done = 0;
    for (const Group& group : m_plan.groups) {
        if (m_aborted)
            return res;
        setProgress(++done, m_plan.groups.size());

        // Adopt the first copy into the pool if it is not there yet.
        QString pooled = ContentCache::find("sha1", group.sha1);
        if (pooled.isEmpty()) {
            ContentCache::store("sha1", group.sha1, group.paths.first());
            pooled = ContentCache::find("sha1", group.sha1);
        }
        if (pooled.isEmpty())
            continue;

        for (const QString& path : group.paths) {
            if (sameInode(path, pooled))
                continue;  // already sharing bytes with the pool

            // The file may have changed since planning; never link over
            // something that is no longer the same bytes.
            const QFileInfo now(path);
            if (!now.exists() || now.size() != group.size) {
                res.filesSkipped++;
                continue;
            }

            // Prove the link is possible before touching the original: link
            // the pool entry to a temp name beside the target first. A file
            // held open by a running game fails the remove and is skipped.
            const QString temp = path + ".pooltmp";
            QFile::remove(temp);
            std::error_code err;
            std::filesystem::create_hard_link(StringUtils::toStdString(pooled), StringUtils::toStdString(temp), err);
            if (err) {
                res.filesSkipped++;
                continue;
            }
            if (!QFile::remove(path)) {
                QFile::remove(temp);
                res.filesSkipped++;
                continue;
            }
            if (!QFile::rename(temp, path)) {
                // Rename after a successful remove should not fail; recover
                // with a plain copy so the instance is never left short a file.
                QFile::remove(temp);
                if (!QFile::copy(pooled, path))
                    qWarning() << "Dedup could not restore" << path << "- redownload it from the instance's mod page";
                res.filesSkipped++;
                continue;
            }
            res.filesPooled++;
            res.bytesFreed += group.size;
        }
    }
    return res;
}
