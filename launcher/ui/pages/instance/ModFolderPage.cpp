// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Prism Launcher - Minecraft Launcher
 *  Copyright (c) 2022 Jamie Mansfield <jmansfield@cadixdev.org>
 *  Copyright (C) 2022 Sefa Eyeoglu <contact@scrumplex.net>
 *  Copyright (C) 2022 TheKodeToad <TheKodeToad@proton.me>
 *  Copyright (c) 2023 Trial97 <alexandru.tripon97@gmail.com>
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
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *      Copyright 2013-2021 MultiMC Contributors
 *
 *      Licensed under the Apache License, Version 2.0 (the "License");
 *      you may not use this file except in compliance with the License.
 *      You may obtain a copy of the License at
 *
 *          http://www.apache.org/licenses/LICENSE-2.0
 *
 *      Unless required by applicable law or agreed to in writing, software
 *      distributed under the License is distributed on an "AS IS" BASIS,
 *      WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *      See the License for the specific language governing permissions and
 *      limitations under the License.
 */

#include "ModFolderPage.h"
#include "minecraft/mod/Resource.h"
#include "ui/dialogs/ExportToModListDialog.h"
#include "ui/dialogs/InstallLoaderDialog.h"
#include "ui_ExternalResourcesPage.h"

#include <QAbstractItemModel>
#include <QAction>
#include <QEvent>
#include <QKeyEvent>
#include <QFileInfo>
#include <QLocale>
#include <QMenu>
#include <QMessageBox>
#include <QSortFilterProxyModel>
#include <algorithm>
#include <memory>

#include "Application.h"

#include "ui/dialogs/CustomMessageBox.h"
#include "ui/dialogs/ModCleanupDialog.h"
#include "ui/dialogs/ResourceDownloadDialog.h"
#include "ui/dialogs/ResourceUpdateDialog.h"
#include "ui/dialogs/ScrollMessageBox.h"

#include "minecraft/PackProfile.h"
#include "minecraft/VersionFilterData.h"
#include "minecraft/mod/Mod.h"
#include "minecraft/mod/ModFolderModel.h"
#include "minecraft/mod/ModUpdateBackup.h"

#include "tasks/ConcurrentTask.h"
#include "tasks/Task.h"
#include "ui/dialogs/ProgressDialog.h"

ModFolderPage::ModFolderPage(BaseInstance* inst, ModFolderModel* model, QWidget* parent)
    : ExternalResourcesPage(inst, model, parent), m_model(model)
{
    ui->actionDownloadItem->setText(tr("Download Mods"));
    ui->actionDownloadItem->setToolTip(tr("Download mods from online mod platforms"));
    ui->actionDownloadItem->setEnabled(true);
    ui->actionsToolbar->insertActionBefore(ui->actionAddItem, ui->actionDownloadItem);

    connect(ui->actionDownloadItem, &QAction::triggered, this, &ModFolderPage::downloadMods);

    ui->actionUpdateItem->setToolTip(tr("Try to check or update all selected mods (all mods if none are selected)"));
    connect(ui->actionUpdateItem, &QAction::triggered, this, &ModFolderPage::updateMods);
    ui->actionsToolbar->insertActionBefore(ui->actionAddItem, ui->actionUpdateItem);

    auto* updateMenu = new QMenu(this);

    auto* update = updateMenu->addAction(tr("Check for Updates"));
    connect(update, &QAction::triggered, this, &ModFolderPage::updateMods);

    updateMenu->addAction(ui->actionVerifyItemDependencies);
    connect(ui->actionVerifyItemDependencies, &QAction::triggered, this, [this] { updateMods(true); });

    auto depsDisabled = APPLICATION->settings()->getSetting("ModDependenciesDisabled");
    ui->actionVerifyItemDependencies->setVisible(!depsDisabled->get().toBool());
    connect(depsDisabled.get(), &Setting::SettingChanged, this,
            [this](const Setting&, const QVariant& value) { ui->actionVerifyItemDependencies->setVisible(!value.toBool()); });

    updateMenu->addAction(ui->actionResetItemMetadata);
    connect(ui->actionResetItemMetadata, &QAction::triggered, this, &ModFolderPage::deleteModMetadata);

    auto* revertUpdate = updateMenu->addAction(tr("Revert Last Mod Update"));
    revertUpdate->setToolTip(tr("Restore the mod files that were replaced by the most recent mod update."));
    connect(revertUpdate, &QAction::triggered, this, &ModFolderPage::revertLastUpdate);
    // Grey the action out when there is nothing to revert, and say which
    // update it would revert when there is.
    connect(updateMenu, &QMenu::aboutToShow, this, [this, revertUpdate]() {
        const auto snapshotPath = ModUpdateBackup::latestSnapshot(m_instance);
        if (snapshotPath.has_value()) {
            revertUpdate->setEnabled(true);
            const QDateTime when = QFileInfo(snapshotPath.value()).lastModified();
            revertUpdate->setText(when.isValid() ? tr("Revert Mod Update from %1").arg(QLocale().toString(when, QLocale::ShortFormat))
                                                 : tr("Revert Last Mod Update"));
        } else {
            revertUpdate->setEnabled(false);
            revertUpdate->setText(tr("Revert Last Mod Update"));
        }
    });

    ui->actionUpdateItem->setMenu(updateMenu);

    ui->actionChangeVersion->setToolTip(tr("Change a mod's version."));
    connect(ui->actionChangeVersion, &QAction::triggered, this, &ModFolderPage::changeModVersion);
    ui->actionsToolbar->insertActionAfter(ui->actionUpdateItem, ui->actionChangeVersion);

    auto* findDuplicates = updateMenu->addAction(tr("Find Duplicates"));
    findDuplicates->setToolTip(tr("Look for mods installed more than once and clean up the redundant copies."));
    connect(findDuplicates, &QAction::triggered, this, &ModFolderPage::findDuplicates);

    ui->actionViewHomepage->setToolTip(tr("View the homepages of all selected mods."));

    ui->actionExportMetadata->setToolTip(tr("Export mod's metadata to text."));
    connect(ui->actionExportMetadata, &QAction::triggered, this, &ModFolderPage::exportModMetadata);
    ui->actionsToolbar->insertActionAfter(ui->actionViewHomepage, ui->actionExportMetadata);

    ui->actionsToolbar->insertActionAfter(ui->actionViewFolder, ui->actionViewConfigs);
}

bool ModFolderPage::shouldDisplay() const
{
    return true;
}

void ModFolderPage::updateFrame(const QModelIndex& current, [[maybe_unused]] const QModelIndex& previous)
{
    auto sourceCurrent = m_filterModel->mapToSource(current);
    int row = sourceCurrent.row();
    const Mod& mod = m_model->at(row);
    ui->frame->updateWithMod(mod);
}

void ModFolderPage::removeItems(const QItemSelection& selection)
{
    if (m_instance != nullptr && m_instance->isRunning()) {
        auto response = CustomMessageBox::selectable(this, tr("Confirm Delete"),
                                                     tr("If you remove mods while the game is running it may crash your game.\n"
                                                        "Are you sure you want to do this?"),
                                                     QMessageBox::Warning, QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
                            ->exec();

        if (response != QMessageBox::Yes) {
            return;
        }
    }

    auto indexes = selection.indexes();
    auto affected = m_model->getAffectedMods(indexes, EnableAction::DISABLE);
    if (!affected.isEmpty()) {
        auto response = CustomMessageBox::selectable(this, tr("Confirm Disable"),
                                                     tr("The mods you are trying to delete are required by %1 mods.\n"
                                                        "Do you want to disable them?")
                                                         .arg(affected.length()),
                                                     QMessageBox::Warning, QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel,
                                                     QMessageBox::Cancel)
                            ->exec();

        if (response == QMessageBox::Cancel) {
            return;
        }
        if (response == QMessageBox::Yes) {
            m_model->setResourceEnabled(affected, EnableAction::DISABLE);
        }
    }
    m_model->deleteResources(indexes);
}

void ModFolderPage::downloadMods()
{
    if (m_instance->typeName() != "Minecraft") {
        return;  // this is a null instance or a legacy instance
    }

    auto* profile = static_cast<MinecraftInstance*>(m_instance)->getPackProfile();
    if (!profile->getModLoaders().has_value()) {
        if (handleNoModLoader()) {
            return;
        }
    }

    m_downloadDialog = new ResourceDownload::ModDownloadDialog(this, m_model, m_instance);
    connect(this, &QObject::destroyed, m_downloadDialog, &QDialog::close);
    connect(m_downloadDialog, &QDialog::finished, this, &ModFolderPage::downloadDialogFinished);

    m_downloadDialog->open();
}

void ModFolderPage::downloadDialogFinished(int result)
{
    if (result != 0) {
        auto* tasks = new ConcurrentTask(tr("Download Mods"), APPLICATION->settings()->get("NumberOfConcurrentDownloads").toInt());
        connect(tasks, &Task::failed, [this, tasks](const QString& reason) {
            CustomMessageBox::selectable(this, tr("Error"), reason, QMessageBox::Critical)->show();
            tasks->deleteLater();
        });
        connect(tasks, &Task::aborted, [this, tasks]() {
            CustomMessageBox::selectable(this, tr("Aborted"), tr("Download stopped by user."), QMessageBox::Information)->show();
            tasks->deleteLater();
        });
        connect(tasks, &Task::succeeded, [this, tasks]() {
            QStringList warnings = tasks->warnings();
            if (warnings.count()) {
                CustomMessageBox::selectable(this, tr("Warnings"), warnings.join('\n'), QMessageBox::Warning)->show();
            }

            tasks->deleteLater();
        });

        if (m_downloadDialog) {
            for (auto& task : m_downloadDialog->getTasks()) {
                tasks->addTask(task);
            }
        } else {
            qWarning() << "ResourceDownloadDialog vanished before we could collect tasks!";
        }

        ProgressDialog loadDialog(this);
        loadDialog.setSkipButton(true, tr("Abort"));
        loadDialog.execWithTask(tasks);

        m_model->update();
    }
    if (m_downloadDialog) {
        m_downloadDialog->deleteLater();
    }
}

void ModFolderPage::updateMods(bool includeDeps)
{
    if (m_instance->typeName() != "Minecraft") {
        return;  // this is a null instance or a legacy instance
    }

    auto* profile = static_cast<MinecraftInstance*>(m_instance)->getPackProfile();
    if (!profile->getModLoaders().has_value()) {
        if (handleNoModLoader()) {
            return;
        }
    }
    if (APPLICATION->settings()->get("ModMetadataDisabled").toBool()) {
        QMessageBox::critical(this, tr("Error"), tr("Mod updates are unavailable when metadata is disabled!"));
        return;
    }
    if (m_instance != nullptr && m_instance->isRunning()) {
        auto response =
            CustomMessageBox::selectable(this, tr("Confirm Update"),
                                         tr("Updating mods while the game is running may cause mod duplication and game crashes.\n"
                                            "The old files may not be deleted as they are in use.\n"
                                            "Are you sure you want to do this?"),
                                         QMessageBox::Warning, QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
                ->exec();

        if (response != QMessageBox::Yes) {
            return;
        }
    }
    auto selection = m_filterModel->mapSelectionToSource(ui->treeView->selectionModel()->selection()).indexes();

    auto modsList = m_model->selectedResources(selection);
    bool useAll = modsList.empty();
    if (useAll) {
        modsList = m_model->allResources();
    }

    ResourceUpdateDialog updateDialog(this, m_instance, m_model, modsList, includeDeps, profile->getModLoadersList());
    updateDialog.checkCandidates();

    if (updateDialog.aborted()) {
        CustomMessageBox::selectable(this, tr("Aborted"), tr("The mod updater was aborted!"), QMessageBox::Warning)->show();
        return;
    }
    if (updateDialog.noUpdates()) {
        QString message{ tr("'%1' is up-to-date! :)").arg(modsList.front()->name()) };
        if (modsList.size() > 1) {
            if (useAll) {
                message = tr("All mods are up-to-date! :)");
            } else {
                message = tr("All selected mods are up-to-date! :)");
            }
        }
        CustomMessageBox::selectable(this, tr("Update checker"), message)->exec();
        return;
    }

    if (updateDialog.exec() != 0) {
        const auto updateTasks = updateDialog.getTasks();

        // Snapshot the outgoing files so this update can be reverted later
        if (!ModUpdateBackup::createSnapshot(m_instance, m_model, updateTasks)) {
            qWarning() << "Could not create a mod update snapshot; 'Revert Last Mod Update' will not cover this update.";
        }

        auto* tasks = new ConcurrentTask("Download Mods", APPLICATION->settings()->get("NumberOfConcurrentDownloads").toInt());
        connect(tasks, &Task::failed, [this, tasks](const QString& reason) {
            CustomMessageBox::selectable(this, tr("Error"), reason, QMessageBox::Critical)->show();
            tasks->deleteLater();
        });
        connect(tasks, &Task::aborted, [this, tasks]() {
            CustomMessageBox::selectable(this, tr("Aborted"), tr("Download stopped by user."), QMessageBox::Information)->show();
            tasks->deleteLater();
        });
        connect(tasks, &Task::succeeded, [this, tasks]() {
            QStringList warnings = tasks->warnings();
            if (warnings.count()) {
                CustomMessageBox::selectable(this, tr("Warnings"), warnings.join('\n'), QMessageBox::Warning)->show();
            }
            tasks->deleteLater();
        });

        for (const auto& task : updateTasks) {
            tasks->addTask(task);
        }

        ProgressDialog loadDialog(this);
        loadDialog.setSkipButton(true, tr("Abort"));
        loadDialog.execWithTask(tasks);

        m_model->update();
    }
}

void ModFolderPage::findDuplicates()
{
    ModCleanupDialog dialog(m_model, this);
    if (!dialog.hasDuplicates()) {
        CustomMessageBox::selectable(this, tr("Find Duplicates"), tr("No duplicate mods were found. Nice and tidy."),
                                     QMessageBox::Information)
            ->exec();
        return;
    }
    dialog.exec();
}

void ModFolderPage::revertLastUpdate()
{
    auto snapshotPath = ModUpdateBackup::latestSnapshot(m_instance);
    if (!snapshotPath.has_value()) {
        CustomMessageBox::selectable(this, tr("Revert Mod Update"),
                                     tr("There is no mod update snapshot to revert for this instance."), QMessageBox::Information)
            ->exec();
        return;
    }

    auto backup = ModUpdateBackup::load(snapshotPath.value());
    if (!backup.has_value()) {
        CustomMessageBox::selectable(this, tr("Revert Mod Update"),
                                     tr("The most recent mod update snapshot could not be read. It may be incomplete or corrupted."),
                                     QMessageBox::Warning)
            ->exec();
        return;
    }

    QStringList modNames;
    for (const auto& entry : backup->entries()) {
        modNames.append(entry.name.isEmpty() ? entry.newFile : entry.name);
    }

    auto when = backup->created().isValid() ? backup->created().toString(Qt::TextDate) : tr("an unknown time");
    auto question = tr("This will revert the mod update from %1, affecting the following mods:\n\n%2\n\n"
                       "The updated files will be removed and the previous files will be restored.\n"
                       "Are you sure you want to continue?")
                        .arg(when, modNames.join('\n'));
    if (m_instance != nullptr && m_instance->isRunning()) {
        question += "\n\n" + tr("Warning: The game is currently running. Files that are in use may fail to be replaced.");
    }

    auto response = CustomMessageBox::selectable(this, tr("Confirm Revert"), question, QMessageBox::Question,
                                                 QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
                        ->exec();
    if (response != QMessageBox::Yes) {
        return;
    }

    QStringList summary;
    bool clean = backup->revert(m_model, summary);
    if (clean) {
        backup->remove();
        summary.append(QString());
        summary.append(tr("The snapshot was removed after the successful revert."));
    } else {
        summary.append(QString());
        summary.append(tr("The snapshot was kept because some steps could not be completed."));
    }

    QString text;
    for (const auto& line : summary) {
        text += line.toHtmlEscaped() + "<br>";
    }

    ScrollMessageBox messageDialog(this, tr("Revert Mod Update"), tr("Reverting the last mod update gave the following results:"), text);
    messageDialog.setModal(true);
    messageDialog.exec();
}

void ModFolderPage::deleteModMetadata()
{
    auto selection = m_filterModel->mapSelectionToSource(ui->treeView->selectionModel()->selection()).indexes();
    auto selectionCount = m_model->selectedMods(selection).length();
    if (selectionCount == 0) {
        return;
    }
    if (selectionCount > 1) {
        auto response = CustomMessageBox::selectable(this, tr("Confirm Removal"),
                                                     tr("You are about to remove the metadata for %1 mods.\n"
                                                        "Are you sure?")
                                                         .arg(selectionCount),
                                                     QMessageBox::Warning, QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
                            ->exec();

        if (response != QMessageBox::Yes) {
            return;
        }
    }

    m_model->deleteMetadata(selection);
}

void ModFolderPage::changeModVersion()
{
    if (m_instance->typeName() != "Minecraft") {
        return;  // this is a null instance or a legacy instance
    }

    auto* profile = static_cast<MinecraftInstance*>(m_instance)->getPackProfile();
    if (!profile->getModLoaders().has_value()) {
        if (handleNoModLoader()) {
            return;
        }
    }
    if (APPLICATION->settings()->get("ModMetadataDisabled").toBool()) {
        QMessageBox::critical(this, tr("Error"), tr("Mod updates are unavailable when metadata is disabled!"));
        return;
    }
    auto selection = m_filterModel->mapSelectionToSource(ui->treeView->selectionModel()->selection()).indexes();
    auto modsList = m_model->selectedMods(selection);
    if (modsList.length() != 1 || modsList[0]->metadata() == nullptr) {
        return;
    }

    m_downloadDialog = new ResourceDownload::ModDownloadDialog(this, m_model, m_instance, true);
    connect(this, &QObject::destroyed, m_downloadDialog, &QDialog::close);
    connect(m_downloadDialog, &QDialog::finished, this, &ModFolderPage::downloadDialogFinished);

    m_downloadDialog->setResourceMetadata((*modsList.begin())->metadata());
    m_downloadDialog->open();
}

void ModFolderPage::exportModMetadata()
{
    auto selection = m_filterModel->mapSelectionToSource(ui->treeView->selectionModel()->selection()).indexes();
    auto selectedMods = m_model->selectedMods(selection);
    if (selectedMods.length() == 0) {
        selectedMods = m_model->allMods();
    }

    std::ranges::sort(selectedMods, [](const Mod* a, const Mod* b) { return a->name() < b->name(); });
    ExportToModListDialog dlg(m_instance->name(), selectedMods, this);
    dlg.exec();
}

CoreModFolderPage::CoreModFolderPage(BaseInstance* inst, ModFolderModel* mods, QWidget* parent) : ModFolderPage(inst, mods, parent)
{
    auto* mcInst = dynamic_cast<MinecraftInstance*>(m_instance);
    if (mcInst) {
        auto* version = mcInst->getPackProfile();
        if ((version != nullptr) && version->getComponent("net.minecraftforge") && version->getComponent("net.minecraft")) {
            auto minecraftCmp = version->getComponent("net.minecraft");
            if (!minecraftCmp->m_loaded) {
                version->reload(Net::Mode::Offline);
                auto update = version->getCurrentTask();
                if (update) {
                    connect(update.get(), &Task::finished, this, [this] {
                        if (m_container) {
                            m_container->refreshContainer();
                        }
                    });
                    if (!update->isRunning()) {
                        update->start();
                    }
                }
            }
        }
    }
}

bool CoreModFolderPage::shouldDisplay() const
{
    if (ModFolderPage::shouldDisplay()) {
        auto* inst = dynamic_cast<MinecraftInstance*>(m_instance);
        if (!inst) {
            return true;
        }

        auto* version = inst->getPackProfile();
        if ((version == nullptr) || !version->getComponent("net.minecraftforge") || !version->getComponent("net.minecraft")) {
            return false;
        }
        auto minecraftCmp = version->getComponent("net.minecraft");
        return minecraftCmp->m_loaded && minecraftCmp->getReleaseDateTime() < g_VersionFilterData.legacyCutoffDate;
    }
    return false;
}

NilModFolderPage::NilModFolderPage(BaseInstance* inst, ModFolderModel* mods, QWidget* parent) : ModFolderPage(inst, mods, parent) {}

bool NilModFolderPage::shouldDisplay() const
{
    return m_model->dir().exists();
}

// Helper function so this doesn't need to be duplicated 3 times
inline bool ModFolderPage::handleNoModLoader()
{
    int resp = QMessageBox::question(
        this, ModFolderPage::tr("Missing Mod Loader"),
        ModFolderPage::tr("You need to install a compatible mod loader before installing mods. Would you like to do so?"),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
    if (resp == QMessageBox::Yes) {
        // Should be safe
        auto* profile = static_cast<MinecraftInstance*>(this->m_instance)->getPackProfile();
        InstallLoaderDialog dialog(profile, QString(), this);
        bool ret = dialog.exec() != 0;
        this->m_container->refreshContainer();

        // returning negation of dialog.exec which'll be true if the install loader dialog got canceled/closed
        // and false if the user went through and installed a loader
        return !ret;
    }
    // Nothing happens the dialog is already closing
    // returning true so the caller doesn't go and continue with opening it's dialog without a mod loader
    return true;
}
