#include "ResourceUpdateDialog.h"
#include "Application.h"
#include "ChooseProviderDialog.h"
#include "CustomMessageBox.h"
#include "ProgressDialog.h"
#include "StringUtils.h"
#include "minecraft/mod/tasks/GetModDependenciesTask.h"
#include "modplatform/ModIndex.h"
#include "modplatform/flame/FlameAPI.h"
#include "tasks/SequentialTask.h"
#include "ui_ReviewMessageBox.h"

#include "Markdown.h"

#include "tasks/ConcurrentTask.h"

#include "minecraft/MinecraftInstance.h"
#include "minecraft/PackProfile.h"

#include "modplatform/EnsureMetadataTask.h"
#include "modplatform/flame/FlameCheckUpdate.h"
#include "modplatform/modrinth/ModrinthCheckUpdate.h"

#include <QClipboard>
#include <QDateTime>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QSet>
#include <QShortcut>
#include <QTextBrowser>
#include <QTreeWidgetItem>

#include <optional>

namespace {
std::vector<Version> mcVersions(BaseInstance* inst)
{
    return { static_cast<MinecraftInstance*>(inst)->getPackProfile()->getComponent("net.minecraft")->getVersion() };
}
}  // namespace

ResourceUpdateDialog::ResourceUpdateDialog(QWidget* parent,
                                           BaseInstance* instance,
                                           ResourceFolderModel* resourceModel,
                                           QList<Resource*>& searchFor,
                                           bool includeDeps,
                                           QList<ModPlatform::ModLoaderType> loadersList)
    : ReviewMessageBox(parent, tr("Confirm resources to update"), "")
    , m_parent(parent)
    , m_resourceModel(resourceModel)
    , m_candidates(searchFor)
    , m_secondTryMetadata(new ConcurrentTask("Second Metadata Search", APPLICATION->settings()->get("NumberOfConcurrentTasks").toInt()))
    , m_instance(instance)
    , m_includeDeps(includeDeps)
    , m_loadersList(std::move(loadersList))
{
    ReviewMessageBox::setGeometry(0, 0, 800, 600);

    ui->explainLabel->setText(tr("You're about to update the following resources:"));
    ui->onlyCheckedLabel->setText(tr("Only resources with a check will be updated!"));
}

void ResourceUpdateDialog::checkCandidates()
{
    // Ensure mods have valid metadata
    auto wentWell = ensureMetadata();
    if (!wentWell) {
        m_aborted = true;
        return;
    }

    // Resources that are on neither Modrinth nor CurseForge (hand-built jars,
    // launcher clients, private mods) cannot be updated from here. That is
    // not a question to stop and ask about every time: they are simply left
    // alone and listed at the bottom of the review dialog.
    for (const auto& failed : m_failedMetadata) {
        const auto& mod = std::get<0>(failed);
        qDebug() << mod->name() << "is not on any mod provider, leaving it as is";
        m_skipped.append(tr("%1 (%2): not on Modrinth or CurseForge, left as is").arg(mod->name(), mod->fileinfo().fileName()));
    }

    QList<std::shared_ptr<GetModDependenciesTask::PackDependency>> selectedVers;
    QList<std::tuple<Resource*, QString, QUrl>> failed;
    if (!runUpdateCheck(m_modrinthToUpdate, m_flameToUpdate, failed, selectedVers)) {
        m_aborted = true;
        QMetaObject::invokeMethod(this, "reject", Qt::QueuedConnection);
        return;
    }

    // Second chance: a mod whose provider has no usable version (the author
    // moved to Modrinth, or the CurseForge listing stopped at an older game
    // version) is looked up on the other provider by file hash. When it is
    // there, its metadata switches over and the update check runs again.
    QList<Resource*> tryModrinth;
    QList<Resource*> tryFlame;
    for (const auto& entry : failed) {
        auto* resource = std::get<0>(entry);
        if (!resource->metadata())
            continue;
        if (resource->metadata()->provider == ModPlatform::ResourceProvider::FLAME)
            tryModrinth.append(resource);
        else
            tryFlame.append(resource);
    }
    if (!tryModrinth.isEmpty() || !tryFlame.isEmpty()) {
        QList<Resource*> secondModrinth;
        QList<Resource*> secondFlame;
        SequentialTask seq(tr("Looking on the other mod provider"));
        auto hookup = [this, &seq, &secondModrinth, &secondFlame](QList<Resource*>& list, ModPlatform::ResourceProvider provider) {
            if (list.isEmpty())
                return;
            auto task = makeShared<EnsureMetadataTask>(list, indexDir(), provider);
            connect(task.get(), &EnsureMetadataTask::metadataReady, this, [&secondModrinth, &secondFlame](Resource* resource) {
                if (!resource->metadata())
                    return;
                if (resource->metadata()->provider == ModPlatform::ResourceProvider::MODRINTH)
                    secondModrinth.append(resource);
                else
                    secondFlame.append(resource);
            });
            if (task->getHashingTask())
                seq.addTask(task->getHashingTask());
            seq.addTask(task);
        };
        hookup(tryModrinth, ModPlatform::ResourceProvider::MODRINTH);
        hookup(tryFlame, ModPlatform::ResourceProvider::FLAME);

        ProgressDialog secondDialog(m_parent);
        secondDialog.setSkipButton(true, tr("Abort"));
        secondDialog.setWindowTitle(tr("Checking the other mod provider..."));
        if (secondDialog.execWithTask(&seq) == QDialog::DialogCode::Rejected) {
            m_aborted = true;
            QMetaObject::invokeMethod(this, "reject", Qt::QueuedConnection);
            return;
        }

        QList<std::tuple<Resource*, QString, QUrl>> secondFailed;
        if (!runUpdateCheck(secondModrinth, secondFlame, secondFailed, selectedVers)) {
            m_aborted = true;
            QMetaObject::invokeMethod(this, "reject", Qt::QueuedConnection);
            return;
        }

        QSet<Resource*> found;
        for (auto* resource : secondModrinth)
            found.insert(resource);
        for (auto* resource : secondFlame)
            found.insert(resource);
        QSet<Resource*> stillFailed;
        for (const auto& entry : secondFailed)
            stillFailed.insert(std::get<0>(entry));

        QList<std::tuple<Resource*, QString, QUrl>> remaining;
        for (const auto& entry : failed) {
            auto* resource = std::get<0>(entry);
            if (!found.contains(resource)) {
                remaining.append(entry);
                continue;
            }
            const QString providerName = ModPlatform::ProviderCapabilities::readableName(resource->metadata()->provider);
            if (stillFailed.contains(resource)) {
                remaining.append({ resource, tr("no usable version on either Modrinth or CurseForge for this game version and loader"), {} });
            } else {
                m_switched.append(tr("%1: now tracked on %2, which has it for this game version").arg(resource->name(), providerName));
            }
        }
        failed = remaining;
    }
    m_failedCheckUpdate = failed;

    // Same treatment for resources the provider could not offer a version for
    // (nothing for this game version or loader, say): note it, move on.
    for (const auto& entry : m_failedCheckUpdate) {
        const auto& mod = std::get<0>(entry);
        const auto& reason = std::get<1>(entry);
        const auto& recoverUrl = std::get<2>(entry);
        qDebug() << mod->name() << "failed to check for updates:" << reason;
        QString line = reason.isEmpty() ? tr("%1: could not check for updates").arg(mod->name()) : tr("%1: %2").arg(mod->name(), reason);
        if (!recoverUrl.isEmpty())
            line += ' ' + tr("Get it by hand from %1").arg(recoverUrl.toString());
        m_skipped.append(line);
    }

    const bool depsDisabled = APPLICATION->settings()->get("ModDependenciesDisabled").toBool();
    bool includeDeps = m_includeDeps;

    // When not resolving dependencies anyway, check whether the new versions declare
    // required dependencies that are neither installed nor part of this update
    QStringList missingDeps;
    if (!depsDisabled && !includeDeps && !m_tasks.isEmpty()) {
        missingDeps = findMissingDependencies(selectedVers);
        if (!missingDeps.isEmpty()) {
            auto response =
                CustomMessageBox::selectable(m_parent, tr("Missing dependencies"),
                                             tr("The new versions of some mods require dependencies that are neither installed "
                                                "nor part of this update:\n\n%1\n\n"
                                                "Do you want to look up the missing dependencies and add them to this update?")
                                                 .arg(missingDeps.join('\n')),
                                             QMessageBox::Warning, QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes)
                    ->exec();
            includeDeps = response == QMessageBox::Yes;
        }
    }

    if (includeDeps && !depsDisabled) {  // dependencies
        auto* modModel = dynamic_cast<ModFolderModel*>(m_resourceModel);

        if (modModel != nullptr) {
            auto depTask = makeShared<GetModDependenciesTask>(m_instance, modModel, selectedVers);

            connect(depTask.get(), &Task::failed, this, [this](const QString& reason) {
                CustomMessageBox::selectable(this, tr("Error"), reason, QMessageBox::Critical)->exec();
            });
            auto weak = depTask.toWeakRef();
            connect(depTask.get(), &Task::succeeded, this, [this, weak]() {
                QStringList warnings;
                if (auto depTask = weak.lock()) {
                    warnings = depTask->warnings();
                }
                if (warnings.count()) {
                    CustomMessageBox::selectable(this, tr("Warnings"), warnings.join('\n'), QMessageBox::Warning)->exec();
                }
            });

            ProgressDialog progressDialogDeps(m_parent);
            progressDialogDeps.setSkipButton(true, tr("Abort"));
            progressDialogDeps.setWindowTitle(tr("Checking for dependencies..."));
            auto dret = progressDialogDeps.execWithTask(depTask.get());

            // If the dialog was skipped / some download error happened
            if (dret == QDialog::DialogCode::Rejected) {
                m_aborted = true;
                QMetaObject::invokeMethod(this, "reject", Qt::QueuedConnection);
                return;
            }
            static FlameAPI s_api;

            auto dependencyExtraInfo = depTask->getExtraInfo();

            for (const auto& dep : depTask->getDependecies()) {
                auto changelog = dep->version.changelog;
                if (dep->pack->provider == ModPlatform::ResourceProvider::FLAME) {
                    changelog = s_api.getModFileChangelog(dep->version.addonId.toInt(), dep->version.fileId.toInt());
                }
                auto downloadTask = makeShared<ResourceDownloadTask>(dep->pack, dep->version, m_resourceModel, true, "dependency");
                auto extraInfo = dependencyExtraInfo.value(dep->version.addonId.toString());
                CheckUpdateTask::Update updatable = {
                    dep->pack->name, dep->version.hash,   tr("Not installed"), dep->version.version,      dep->version.version_type,
                    changelog,       dep->pack->provider, downloadTask,        !extraInfo.maybe_installed
                };

                appendResource(updatable, extraInfo.required_by);
                m_tasks.insert(updatable.name, updatable.download);
            }
        }
    }

    // Surface a persistent warning about missing dependencies in the review dialog
    if (!missingDeps.isEmpty()) {
        auto* warningLabel = new QLabel(this);
        warningLabel->setWordWrap(true);
        if (includeDeps) {
            warningLabel->setText(tr("Warning: Some of the new versions had missing required dependencies. "
                                     "Any that could be resolved were added to the list above."));
        } else {
            warningLabel->setText(tr("Warning: The following required dependencies are missing:\n%1").arg(missingDeps.join('\n')));
        }
        // Always take a fresh row: a QGridLayout silently stacks widgets that
        // share a cell, and row 3 is not guaranteed to be free.
        ui->gridLayout->addWidget(warningLabel, ui->gridLayout->rowCount(), 0, 1, -1);
    }

    QStringList notes;
    if (!m_switched.isEmpty())
        notes << tr("Switched provider:") + '\n' + m_switched.join('\n');
    if (!m_skipped.isEmpty())
        notes << tr("Left alone (not updatable from here):") + '\n' + m_skipped.join('\n');
    if (!notes.isEmpty()) {
        auto* notesLabel = new QLabel(this);
        notesLabel->setWordWrap(true);
        notesLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        notesLabel->setText(notes.join("\n\n"));
        ui->gridLayout->addWidget(notesLabel, ui->gridLayout->rowCount(), 0, 1, -1);
    }

    // If there's no resource to be updated
    if (ui->modTreeWidget->topLevelItemCount() == 0) {
        m_noUpdates = true;
    } else {
        // FIXME: Find a more efficient way of doing this!

        // Sort major items in alphabetical order (also sorts the children unfortunately)
        ui->modTreeWidget->sortItems(0, Qt::SortOrder::AscendingOrder);

        // Re-sort the children
        auto* item = ui->modTreeWidget->topLevelItem(0);
        for (int i = 1; item != nullptr; ++i) {
            item->sortChildren(0, Qt::SortOrder::DescendingOrder);
            item = ui->modTreeWidget->topLevelItem(i);
        }
    }

    if (m_aborted || m_noUpdates) {
        QMetaObject::invokeMethod(this, "reject", Qt::QueuedConnection);
    }
}

// Checks (without network access) whether the required dependencies declared by the new versions
// are satisfied by installed mods or by other mods in the update set, and lists the missing ones
QStringList ResourceUpdateDialog::findMissingDependencies(
    const QList<std::shared_ptr<GetModDependenciesTask::PackDependency>>& selectedVers)
{
    auto* modModel = dynamic_cast<ModFolderModel*>(m_resourceModel);
    if (modModel == nullptr) {
        return {};
    }

    auto projectKey = [](ModPlatform::ResourceProvider provider, const QString& id) {
        return QString("%1:%2").arg(ModPlatform::ProviderCapabilities::name(provider), id);
    };
    auto versionKey = [](ModPlatform::ResourceProvider provider, const QString& version) {
        return QString("%1@%2").arg(ModPlatform::ProviderCapabilities::name(provider), version);
    };

    QSet<QString> satisfied;

    for (auto* mod : modModel->allMods()) {
        if (auto meta = mod->metadata(); meta != nullptr) {
            satisfied.insert(projectKey(meta->provider, meta->project_id.toString()));
            satisfied.insert(versionKey(meta->provider, meta->file_id.toString()));
        }
    }
    for (const auto& sel : selectedVers) {
        satisfied.insert(projectKey(sel->pack->provider, sel->pack->addonId.toString()));
        satisfied.insert(versionKey(sel->pack->provider, sel->version.fileId.toString()));
        satisfied.insert(versionKey(sel->pack->provider, sel->version.version));
    }

    // Mods like Fabric API have Quilt counterparts that satisfy the same dependency
    for (const auto& over : ModPlatform::getOverrideDeps()) {
        if (satisfied.contains(projectKey(over.provider, over.fabric))) {
            satisfied.insert(projectKey(over.provider, over.quilt));
        } else if (satisfied.contains(projectKey(over.provider, over.quilt))) {
            satisfied.insert(projectKey(over.provider, over.fabric));
        }
    }

    QStringList missing;
    for (const auto& sel : selectedVers) {
        if (!m_tasks.contains(sel->pack->name)) {
            continue;  // only new versions that are actually part of this update matter
        }

        for (const auto& dep : sel->version.dependencies) {
            if (dep.type != ModPlatform::DependencyType::REQUIRED) {
                continue;
            }

            auto addonId = dep.addonId.toString();
            bool depSatisfied = false;
            if (addonId.isEmpty()) {
                // Modrinth dependencies may reference only a version instead of a project
                depSatisfied = dep.version.isEmpty() || satisfied.contains(versionKey(sel->pack->provider, dep.version));
            } else {
                depSatisfied = satisfied.contains(projectKey(sel->pack->provider, addonId));
            }

            if (!depSatisfied) {
                auto depName = addonId.isEmpty() ? tr("version %1").arg(dep.version) : tr("project %1").arg(addonId);
                //: %1 is the mod name, %2 the missing dependency, %3 the mod platform it comes from
                auto line = tr("%1 requires %2 from %3")
                                .arg(sel->pack->name, depName, ModPlatform::ProviderCapabilities::readableName(sel->pack->provider));
                if (!missing.contains(line)) {
                    missing.append(line);
                }
            }
        }
    }

    return missing;
}

// Part 1: Ensure we have a valid metadata
auto ResourceUpdateDialog::ensureMetadata() -> bool
{
    auto indexDir2 = indexDir();

    SequentialTask seq(tr("Looking for metadata"));

    // A better use of data structures here could remove the need for this QHash
    QHash<QString, bool> shouldTryOthers;
    QList<Resource*> modrinthTmp;
    QList<Resource*> flameTmp;

    bool confirmRest = false;
    bool tryOthersRest = false;
    bool skipRest = false;
    ModPlatform::ResourceProvider providerRest = ModPlatform::ResourceProvider::MODRINTH;

    // adds resource to list based on provider
    auto addToTmp = [&modrinthTmp, &flameTmp](Resource* resource, ModPlatform::ResourceProvider p) {
        switch (p) {
            case ModPlatform::ResourceProvider::MODRINTH:
                modrinthTmp.push_back(resource);
                break;
            case ModPlatform::ResourceProvider::FLAME:
                flameTmp.push_back(resource);
                break;
        }
    };

    // ask the user on what provider to seach for the mod first
    for (auto* candidate : m_candidates) {
        if (candidate->status() != ResourceStatus::NO_METADATA) {
            onMetadataEnsured(candidate);
            continue;
        }

        if (skipRest) {
            continue;
        }

        if (candidate->type() == ResourceType::FOLDER) {
            continue;
        }

        if (recentlyUnresolved(candidate)) {
            m_failedMetadata.append({ candidate, tr("not found on any provider last week, tried again weekly") });
            continue;
        }

        if (confirmRest) {
            addToTmp(candidate, providerRest);
            shouldTryOthers.insert(candidate->internal_id(), tryOthersRest);
            continue;
        }

        ChooseProviderDialog chooser(this);
        chooser.setDescription(tr("The resource '%1' does not have a metadata yet. We need to generate it in order to track relevant "
                                  "information on how to update this mod. "
                                  "To do this, please select a mod provider which we can use to check for updates for this mod.")
                                   .arg(candidate->name()));
        auto confirmed = chooser.exec() == QDialog::DialogCode::Accepted;

        auto response = chooser.getResponse();

        if (response.skip_all) {
            skipRest = true;
        }
        if (response.confirm_all) {
            confirmRest = true;
            providerRest = response.chosen;
            tryOthersRest = response.try_others;
        }

        shouldTryOthers.insert(candidate->internal_id(), response.try_others);

        if (confirmed) {
            addToTmp(candidate, response.chosen);
        }
    }

    // prepare task for the modrinth mods
    if (!modrinthTmp.empty()) {
        auto modrinthTask = makeShared<EnsureMetadataTask>(modrinthTmp, indexDir2, ModPlatform::ResourceProvider::MODRINTH);
        connect(modrinthTask.get(), &EnsureMetadataTask::metadataReady, [this](Resource* candidate) { onMetadataEnsured(candidate); });
        connect(modrinthTask.get(), &EnsureMetadataTask::metadataFailed, [this, &shouldTryOthers](Resource* candidate) {
            onMetadataFailed(candidate, shouldTryOthers.find(candidate->internal_id()).value(), ModPlatform::ResourceProvider::MODRINTH);
        });
        connect(modrinthTask.get(), &EnsureMetadataTask::failed,
                [this](const QString& reason) { CustomMessageBox::selectable(this, tr("Error"), reason, QMessageBox::Critical)->exec(); });

        if (modrinthTask->getHashingTask()) {
            seq.addTask(modrinthTask->getHashingTask());
        }

        seq.addTask(modrinthTask);
    }

    // prepare task for the flame mods
    if (!flameTmp.empty()) {
        auto flameTask = makeShared<EnsureMetadataTask>(flameTmp, indexDir2, ModPlatform::ResourceProvider::FLAME);
        connect(flameTask.get(), &EnsureMetadataTask::metadataReady, [this](Resource* candidate) { onMetadataEnsured(candidate); });
        connect(flameTask.get(), &EnsureMetadataTask::metadataFailed, [this, &shouldTryOthers](Resource* candidate) {
            onMetadataFailed(candidate, shouldTryOthers.find(candidate->internal_id()).value(), ModPlatform::ResourceProvider::FLAME);
        });
        connect(flameTask.get(), &EnsureMetadataTask::failed,
                [this](const QString& reason) { CustomMessageBox::selectable(this, tr("Error"), reason, QMessageBox::Critical)->exec(); });

        if (flameTask->getHashingTask()) {
            seq.addTask(flameTask->getHashingTask());
        }

        seq.addTask(flameTask);
    }

    seq.addTask(m_secondTryMetadata);

    // execute all the tasks
    ProgressDialog checkingDialog(m_parent);
    checkingDialog.setSkipButton(true, tr("Abort"));
    checkingDialog.setWindowTitle(tr("Generating metadata..."));
    auto retMetadata = checkingDialog.execWithTask(&seq);

    return (retMetadata != QDialog::DialogCode::Rejected);
}

void ResourceUpdateDialog::onMetadataEnsured(Resource* resource)
{
    // When the mod is a folder, for instance
    if (!resource->metadata()) {
        return;
    }

    switch (resource->metadata()->provider) {
        case ModPlatform::ResourceProvider::MODRINTH:
            m_modrinthToUpdate.push_back(resource);
            break;
        case ModPlatform::ResourceProvider::FLAME:
            m_flameToUpdate.push_back(resource);
            break;
    }
}

QString ResourceUpdateDialog::skippedNote() const
{
    QString note;
    if (!m_switched.isEmpty())
        note += "\n\n" + tr("Switched provider:") + '\n' + m_switched.join('\n');
    if (!m_skipped.isEmpty())
        note += "\n\n" + tr("Left alone (not updatable from here):") + '\n' + m_skipped.join('\n');
    return note;
}

bool ResourceUpdateDialog::runUpdateCheck(QList<Resource*>& modrinth,
                                          QList<Resource*>& flame,
                                          QList<std::tuple<Resource*, QString, QUrl>>& failed,
                                          QList<std::shared_ptr<GetModDependenciesTask::PackDependency>>& selectedVers)
{
    if (modrinth.isEmpty() && flame.isEmpty())
        return true;

    auto versions = mcVersions(m_instance);
    SequentialTask checkTask(tr("Checking for updates"));
    shared_qobject_ptr<ModrinthCheckUpdate> modrinthTask;
    shared_qobject_ptr<FlameCheckUpdate> flameTask;

    if (!modrinth.isEmpty()) {
        modrinthTask.reset(new ModrinthCheckUpdate(modrinth, versions, m_loadersList, m_resourceModel));
        connect(modrinthTask.get(), &CheckUpdateTask::checkFailed, this,
                [&failed](Resource* resource, const QString& reason, const QUrl& recoverUrl) {
                    failed.append({ resource, reason, recoverUrl });
                });
        checkTask.addTask(modrinthTask);
    }
    if (!flame.isEmpty()) {
        flameTask.reset(new FlameCheckUpdate(flame, versions, m_loadersList, m_resourceModel));
        connect(flameTask.get(), &CheckUpdateTask::checkFailed, this,
                [&failed](Resource* resource, const QString& reason, const QUrl& recoverUrl) {
                    failed.append({ resource, reason, recoverUrl });
                });
        checkTask.addTask(flameTask);
    }

    connect(&checkTask, &Task::failed, this,
            [this](const QString& reason) { CustomMessageBox::selectable(this, tr("Error"), reason, QMessageBox::Critical)->exec(); });
    connect(&checkTask, &Task::succeeded, this, [this, &checkTask]() {
        QStringList warnings = checkTask.warnings();
        if (warnings.count()) {
            CustomMessageBox::selectable(this, tr("Warnings"), warnings.join('\n'), QMessageBox::Warning)->exec();
        }
    });

    ProgressDialog progressDialog(m_parent);
    progressDialog.setSkipButton(true, tr("Abort"));
    progressDialog.setWindowTitle(tr("Checking for updates..."));
    if (progressDialog.execWithTask(&checkTask) == QDialog::DialogCode::Rejected)
        return false;

    auto collect = [this, &selectedVers](CheckUpdateTask* task) {
        for (auto& updatable : task->getUpdates()) {
            qDebug() << QString("Mod %1 has an update available!").arg(updatable.name);
            appendResource(updatable);
            m_tasks.insert(updatable.name, updatable.download);
        }
        selectedVers.append(task->getDependencies());
    };
    if (modrinthTask)
        collect(modrinthTask.get());
    if (flameTask)
        collect(flameTask.get());
    return true;
}

namespace {
const char* const UNRESOLVED_FILE = "portal-unresolved.json";
constexpr qint64 UNRESOLVED_TTL_SECS = 7 * 24 * 60 * 60;

QString unresolvedKey(Resource* resource)
{
    return resource->fileinfo().fileName() + '|' + QString::number(resource->fileinfo().size());
}

QJsonObject readUnresolved(const QDir& indexDir)
{
    QFile file(indexDir.absoluteFilePath(UNRESOLVED_FILE));
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}
}  // namespace

bool ResourceUpdateDialog::recentlyUnresolved(Resource* resource) const
{
    const auto entries = readUnresolved(indexDir());
    const qint64 seen = static_cast<qint64>(entries.value(unresolvedKey(resource)).toDouble(0));
    return seen > 0 && QDateTime::currentSecsSinceEpoch() - seen < UNRESOLVED_TTL_SECS;
}

void ResourceUpdateDialog::rememberUnresolved(Resource* resource)
{
    auto dir = indexDir();
    if (!dir.exists() && !dir.mkpath("."))
        return;
    auto entries = readUnresolved(dir);
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    // Drop stale entries so the file does not grow with every renamed jar.
    for (const auto& key : entries.keys()) {
        if (now - static_cast<qint64>(entries.value(key).toDouble(0)) >= UNRESOLVED_TTL_SECS)
            entries.remove(key);
    }
    entries[unresolvedKey(resource)] = static_cast<double>(now);
    QFile file(dir.absoluteFilePath(UNRESOLVED_FILE));
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        file.write(QJsonDocument(entries).toJson(QJsonDocument::Compact));
}

ModPlatform::ResourceProvider next(ModPlatform::ResourceProvider p)
{
    switch (p) {
        case ModPlatform::ResourceProvider::MODRINTH:
            return ModPlatform::ResourceProvider::FLAME;
        case ModPlatform::ResourceProvider::FLAME:
            return ModPlatform::ResourceProvider::MODRINTH;
    }

    return ModPlatform::ResourceProvider::FLAME;
}

void ResourceUpdateDialog::onMetadataFailed(Resource* resource, bool tryOthers, ModPlatform::ResourceProvider firstChoice)
{
    if (tryOthers) {
        auto indexDir2 = indexDir();

        auto task = makeShared<EnsureMetadataTask>(resource, indexDir2, next(firstChoice));
        connect(task.get(), &EnsureMetadataTask::metadataReady, [this](Resource* candidate) { onMetadataEnsured(candidate); });
        connect(task.get(), &EnsureMetadataTask::metadataFailed, [this](Resource* candidate) { onMetadataFailed(candidate, false); });
        connect(task.get(), &EnsureMetadataTask::failed,
                [this](const QString& reason) { CustomMessageBox::selectable(this, tr("Error"), reason, QMessageBox::Critical)->exec(); });
        if (task->getHashingTask()) {
            auto seq = makeShared<SequentialTask>();
            seq->addTask(task->getHashingTask());
            seq->addTask(task);
            m_secondTryMetadata->addTask(seq);
        } else {
            m_secondTryMetadata->addTask(task);
        }
    } else {
        QString reason{ tr("Couldn't find a valid version on the selected mod provider(s)") };

        rememberUnresolved(resource);
        m_failedMetadata.append({ resource, reason });
    }
}

void ResourceUpdateDialog::appendResource(const CheckUpdateTask::Update& info, QStringList requiredBy)
{
    auto* itemTop = new QTreeWidgetItem(ui->modTreeWidget);
    itemTop->setCheckState(0, info.enabled ? Qt::CheckState::Checked : Qt::CheckState::Unchecked);
    if (!info.enabled) {
        itemTop->setToolTip(0, tr("Mod was disabled as it may be already installed."));
    }
    itemTop->setText(0, info.name);
    itemTop->setExpanded(true);

    auto* providerItem = new QTreeWidgetItem(itemTop);
    QString providerName = ModPlatform::ProviderCapabilities::readableName(info.provider);
    providerItem->setText(0, tr("Provider: %1").arg(providerName));
    providerItem->setData(0, Qt::UserRole, providerName);

    auto* oldVersionItem = new QTreeWidgetItem(itemTop);
    oldVersionItem->setText(0, tr("Old version: %1").arg(info.oldVersion));
    oldVersionItem->setData(0, Qt::UserRole, info.oldVersion);

    auto* newVersionItem = new QTreeWidgetItem(itemTop);
    newVersionItem->setText(0, tr("New version: %1").arg(info.newVersion));
    newVersionItem->setData(0, Qt::UserRole, info.newVersion);

    if (info.newVersionType.has_value()) {
        auto* newVersionTypeItem = new QTreeWidgetItem(itemTop);
        newVersionTypeItem->setText(0, tr("New Version Type: %1").arg(info.newVersionType.value().toString()));
        newVersionTypeItem->setData(0, Qt::UserRole, info.newVersionType.value().toString());
    }

    if (!requiredBy.isEmpty()) {
        auto* requiredByItem = new QTreeWidgetItem(itemTop);
        if (requiredBy.length() == 1) {
            requiredByItem->setText(0, tr("Required by: %1").arg(requiredBy.back()));
            requiredByItem->setData(0, Qt::UserRole, requiredBy.back());
        } else {
            requiredByItem->setText(0, tr("Required by:"));
            for (const auto& req : requiredBy) {
                auto* reqItem = new QTreeWidgetItem(requiredByItem);
                reqItem->setText(0, req);
            }
        }

        ui->toggleDepsButton->show();
        m_deps << itemTop;
    }

    auto* changelogItem = new QTreeWidgetItem(itemTop);
    changelogItem->setText(0, tr("Changelog of the latest version"));

    auto* changelog = new QTreeWidgetItem(changelogItem);
    auto* changelogArea = new QTextBrowser();

    QString text = info.changelog;
    changelog->setData(0, Qt::UserRole, text);
    if (info.provider == ModPlatform::ResourceProvider::MODRINTH) {
        text = markdownToHTML(info.changelog.toUtf8());
    }

    changelogArea->setHtml(StringUtils::htmlListPatch(text));
    changelogArea->setOpenExternalLinks(true);
    changelogArea->setLineWrapMode(QTextBrowser::LineWrapMode::WidgetWidth);
    changelogArea->setVerticalScrollBarPolicy(Qt::ScrollBarPolicy::ScrollBarAsNeeded);

    ui->modTreeWidget->setItemWidget(changelog, 0, changelogArea);

    ui->modTreeWidget->addTopLevelItem(itemTop);
}

auto ResourceUpdateDialog::getTasks() -> const QList<ResourceDownloadTask::Ptr>
{
    QList<ResourceDownloadTask::Ptr> list;

    auto* item = ui->modTreeWidget->topLevelItem(0);

    for (int i = 1; item != nullptr; ++i) {
        if (item->checkState(0) == Qt::CheckState::Checked) {
            list.push_back(m_tasks.find(item->text(0)).value());
        }

        item = ui->modTreeWidget->topLevelItem(i);
    }

    return list;
}
