// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Prism Launcher - Minecraft Launcher
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "ModUpdateBackup.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

#include "BaseInstance.h"
#include "FileSystem.h"
#include "minecraft/mod/Mod.h"
#include "minecraft/mod/ModFolderModel.h"

namespace {

/** Finds the on-disk name of the packwiz index file for the given slug, tolerating case differences. */
QString findIndexFile(const QDir& indexDir, const QString& slug)
{
    if (slug.isEmpty()) {
        return {};
    }

    auto expected = QString("%1.pw.toml").arg(slug);
    if (indexDir.exists(expected)) {
        return expected;
    }

    for (const auto& fileName : indexDir.entryList(QDir::Files)) {
        if (QString::compare(fileName, expected, Qt::CaseInsensitive) == 0) {
            return fileName;
        }
    }

    return {};
}

QStringList snapshotNames(const QString& root)
{
    // Snapshot folder names are timestamps, so sorting by name sorts by age
    return QDir(root).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
}

}  // namespace

QString ModUpdateBackup::snapshotsRoot(const BaseInstance* instance)
{
    return FS::PathCombine(instance->instanceRoot(), "backups", "mod-updates");
}

bool ModUpdateBackup::createSnapshot(const BaseInstance* instance, ModFolderModel* model, const QList<ResourceDownloadTask::Ptr>& tasks)
{
    if (instance == nullptr || model == nullptr || tasks.isEmpty()) {
        return false;
    }

    auto root = snapshotsRoot(instance);
    auto name = QDateTime::currentDateTime().toString("yyyy-MM-dd-HHmmss");
    auto path = FS::PathCombine(root, name);
    for (int suffix = 1; QDir(path).exists(); ++suffix) {
        path = FS::PathCombine(root, QString("%1-%2").arg(name).arg(suffix));
    }

    auto filesPath = FS::PathCombine(path, "files");
    auto indexPath = FS::PathCombine(path, "index");
    if (!FS::ensureFolderPathExists(filesPath) || !FS::ensureFolderPathExists(indexPath)) {
        qWarning() << "Could not create mod update snapshot folder at" << path;
        return false;
    }

    auto indexDir = model->indexDir();
    auto allMods = model->allMods();

    QJsonArray entries;
    for (const auto& taskPtr : tasks) {
        auto* task = qobject_cast<ResourceDownloadTask*>(taskPtr.get());
        if (task == nullptr) {
            continue;
        }

        Entry entry;
        entry.name = task->getName();
        entry.newFile = task->getFilename();

        // Find the installed mod that this download is going to replace
        Mod* oldMod = nullptr;
        auto pack = task->getPack();
        for (auto* mod : allMods) {
            auto meta = mod->metadata();
            if (pack != nullptr && meta != nullptr && meta->provider == task->getProvider() &&
                meta->project_id.toString() == pack->addonId.toString()) {
                oldMod = mod;
                break;
            }
        }
        if (oldMod == nullptr) {
            // Fresh installs (e.g. dependencies) have no old mod; the name match below
            // covers mods whose metadata got lost between the check and the update
            for (auto* mod : allMods) {
                if (mod->name() == entry.name) {
                    oldMod = mod;
                    break;
                }
            }
        }

        if (oldMod != nullptr) {
            auto fileInfo = oldMod->fileinfo();
            if (fileInfo.exists() && fileInfo.isFile()) {
                if (QFile::copy(fileInfo.absoluteFilePath(), FS::PathCombine(filesPath, fileInfo.fileName()))) {
                    entry.oldFile = fileInfo.fileName();
                } else {
                    qWarning() << "Could not back up" << fileInfo.absoluteFilePath() << "before updating";
                }
            }

            if (auto meta = oldMod->metadata(); meta != nullptr) {
                auto indexFile = findIndexFile(indexDir, meta->slug);
                if (!indexFile.isEmpty() && QFile::copy(indexDir.absoluteFilePath(indexFile), FS::PathCombine(indexPath, indexFile))) {
                    entry.oldIndexFile = indexFile;
                }
            }
        }

        QJsonObject obj;
        obj.insert("name", entry.name);
        obj.insert("oldFile", entry.oldFile);
        obj.insert("newFile", entry.newFile);
        obj.insert("oldIndexFile", entry.oldIndexFile);
        entries.append(obj);
    }

    if (entries.isEmpty()) {
        FS::deletePath(path);
        return false;
    }

    QJsonObject manifest;
    manifest.insert("formatVersion", 1);
    manifest.insert("created", QDateTime::currentDateTime().toString(Qt::ISODate));
    manifest.insert("entries", entries);

    try {
        FS::write(FS::PathCombine(path, "manifest.json"), QJsonDocument(manifest).toJson());
    } catch (const FS::FileSystemException& e) {
        qWarning() << "Could not write mod update snapshot manifest:" << e.cause();
        FS::deletePath(path);
        return false;
    }

    pruneSnapshots(instance);
    return true;
}

std::optional<QString> ModUpdateBackup::latestSnapshot(const BaseInstance* instance)
{
    auto root = snapshotsRoot(instance);
    auto names = snapshotNames(root);
    for (auto it = names.crbegin(); it != names.crend(); ++it) {
        auto path = FS::PathCombine(root, *it);
        if (QFile::exists(FS::PathCombine(path, "manifest.json"))) {
            return path;
        }
    }
    return std::nullopt;
}

void ModUpdateBackup::pruneSnapshots(const BaseInstance* instance, int keep)
{
    auto root = snapshotsRoot(instance);
    auto names = snapshotNames(root);
    for (qsizetype i = 0; i < names.size() - keep; ++i) {
        auto path = FS::PathCombine(root, names.at(i));
        if (!FS::deletePath(path)) {
            qWarning() << "Could not prune old mod update snapshot at" << path;
        }
    }
}

std::optional<ModUpdateBackup> ModUpdateBackup::load(const QString& snapshotPath)
{
    auto manifestPath = FS::PathCombine(snapshotPath, "manifest.json");

    QFile file(manifestPath);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "Could not open mod update snapshot manifest at" << manifestPath;
        return std::nullopt;
    }

    QJsonParseError parseError{};
    auto doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        qWarning() << "Could not parse mod update snapshot manifest at" << manifestPath << ":" << parseError.errorString();
        return std::nullopt;
    }

    auto obj = doc.object();

    ModUpdateBackup backup;
    backup.m_path = snapshotPath;
    backup.m_created = QDateTime::fromString(obj.value("created").toString(), Qt::ISODate);

    // Be lenient here: a partially written manifest should still allow reverting whatever it does describe
    for (const auto& value : obj.value("entries").toArray()) {
        if (!value.isObject()) {
            continue;
        }

        auto entryObj = value.toObject();

        Entry entry;
        entry.name = entryObj.value("name").toString();
        entry.oldFile = entryObj.value("oldFile").toString();
        entry.newFile = entryObj.value("newFile").toString();
        entry.oldIndexFile = entryObj.value("oldIndexFile").toString();

        if (entry.oldFile.isEmpty() && entry.newFile.isEmpty()) {
            continue;  // nothing usable in this entry
        }

        backup.m_entries.append(entry);
    }

    if (backup.m_entries.isEmpty()) {
        qWarning() << "Mod update snapshot manifest at" << manifestPath << "contains no usable entries";
        return std::nullopt;
    }

    return backup;
}

bool ModUpdateBackup::revert(ModFolderModel* model, QStringList& summary) const
{
    bool clean = true;
    auto warn = [&summary, &clean](const QString& line) {
        summary.append(tr("Warning: %1").arg(line));
        clean = false;
    };

    auto modsDir = model->dir();
    auto indexDir = model->indexDir();

    for (const auto& entry : m_entries) {
        auto displayName = entry.name.isEmpty() ? entry.newFile : entry.name;

        // Remove the file the update installed (unless it kept the same file name,
        // in which case restoring the old file below overwrites it anyway)
        if (!entry.newFile.isEmpty() && entry.newFile != entry.oldFile) {
            if (model->uninstallResource(entry.newFile)) {
                summary.append(tr("%1: removed the updated file '%2'").arg(displayName, entry.newFile));
            } else {
                // The model may not know about the file (e.g. the update failed halfway), so check the disk directly
                bool removedAny = false;
                const QString disabledNewFile = entry.newFile + ".disabled";
                for (const auto& candidate : { entry.newFile, disabledNewFile }) {
                    auto candidatePath = modsDir.absoluteFilePath(candidate);
                    if (QFile::exists(candidatePath) && FS::deletePath(candidatePath)) {
                        removedAny = true;
                    }
                }
                if (removedAny) {
                    summary.append(tr("%1: removed the updated file '%2'").arg(displayName, entry.newFile));
                } else {
                    warn(tr("%1: the updated file '%2' was not found, skipping its removal").arg(displayName, entry.newFile));
                }
            }
        }

        // Restore the old mod file, keeping its enabled or disabled state
        if (!entry.oldFile.isEmpty()) {
            auto backupPath = FS::PathCombine(m_path, "files", entry.oldFile);
            if (!QFile::exists(backupPath)) {
                warn(tr("%1: the backup of '%2' is missing from the snapshot, skipping its restore").arg(displayName, entry.oldFile));
            } else {
                // Clean up both the enabled and the disabled variant before restoring
                auto baseName = entry.oldFile;
                if (baseName.endsWith(".disabled")) {
                    baseName.chop(9);
                }
                const QString disabledBaseName = baseName + ".disabled";
                for (const auto& candidate : { baseName, disabledBaseName }) {
                    auto candidatePath = modsDir.absoluteFilePath(candidate);
                    if (QFile::exists(candidatePath)) {
                        FS::deletePath(candidatePath);
                    }
                }

                if (QFile::copy(backupPath, modsDir.absoluteFilePath(entry.oldFile))) {
                    summary.append(tr("%1: restored '%2'").arg(displayName, entry.oldFile));
                } else {
                    warn(tr("%1: could not copy '%2' back into the mods folder").arg(displayName, entry.oldFile));
                }
            }
        }

        // Restore the old update metadata
        if (!entry.oldIndexFile.isEmpty()) {
            auto backupIndexPath = FS::PathCombine(m_path, "index", entry.oldIndexFile);
            if (QFile::exists(backupIndexPath)) {
                auto targetPath = indexDir.absoluteFilePath(entry.oldIndexFile);
                if (QFile::exists(targetPath)) {
                    FS::deletePath(targetPath);
                }
                if (!QFile::copy(backupIndexPath, targetPath)) {
                    warn(tr("%1: could not restore the update metadata").arg(displayName));
                }
            } else {
                warn(tr("%1: the backup of the update metadata is missing from the snapshot").arg(displayName));
            }
        }
    }

    model->update();

    return clean;
}

void ModUpdateBackup::remove() const
{
    if (!m_path.isEmpty() && !FS::deletePath(m_path)) {
        qWarning() << "Could not delete mod update snapshot at" << m_path;
    }
}
