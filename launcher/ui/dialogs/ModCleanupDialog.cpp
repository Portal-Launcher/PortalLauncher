// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Prism Launcher - Minecraft Launcher
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 */

#include "ModCleanupDialog.h"

#include <QDateTime>
#include <QDialogButtonBox>
#include <QHash>
#include <QHeaderView>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QSet>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "minecraft/mod/Mod.h"
#include "minecraft/mod/ModFolderModel.h"
#include "modplatform/ModIndex.h"
#include "ui/dialogs/CustomMessageBox.h"

namespace {

constexpr int InternalIdRole = Qt::UserRole;

// One installed file inside a duplicate group
struct Entry {
    int row;
    QString internalId;
    QString fileName;
    QString version;
    QString versionKey;  // what we compare to call two files "the same version"
    bool enabled;
    QDateTime changed;
};

// A stable identity for "this is the same mod": platform project when we have
// metadata, else the loader's mod id, else the display name.
QString identityKey(const Mod& mod)
{
    auto meta = mod.metadata();
    if (meta && meta->isValid())
        return QString("project:%1:%2").arg(ModPlatform::ProviderCapabilities::name(meta->provider), meta->project_id.toString());
    if (!mod.mod_id().isEmpty())
        return QString("modid:%1").arg(mod.mod_id().toLower());
    return QString("name:%1").arg(mod.name().toLower());
}

QString versionKey(const Mod& mod)
{
    auto meta = mod.metadata();
    if (meta && meta->isValid() && !meta->file_id.isNull())
        return QString("file:%1").arg(meta->file_id.toString());
    if (!mod.version().isEmpty())
        return QString("version:%1").arg(mod.version());
    // fall back to the filename with any .disabled suffix stripped
    QString name = mod.fileinfo().fileName();
    if (name.endsWith(".disabled"))
        name.chop(QStringLiteral(".disabled").length());
    return QString("filename:%1").arg(name);
}

}  // namespace

ModCleanupDialog::ModCleanupDialog(ModFolderModel* model, QWidget* parent) : QDialog(parent), m_model(model)
{
    setWindowTitle(tr("Find Duplicates"));
    resize(640, 420);

    auto* layout = new QVBoxLayout(this);

    m_summary = new QLabel(this);
    m_summary->setWordWrap(true);
    layout->addWidget(m_summary);

    m_tree = new QTreeWidget(this);
    m_tree->setColumnCount(4);
    m_tree->setHeaderLabels({ tr("Mod"), tr("Version"), tr("State"), tr("Modified") });
    m_tree->setRootIsDecorated(true);
    m_tree->setSelectionMode(QAbstractItemView::NoSelection);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int i = 1; i < 4; i++)
        m_tree->header()->setSectionResizeMode(i, QHeaderView::ResizeToContents);
    layout->addWidget(m_tree, 1);

    auto* buttons = new QDialogButtonBox(this);
    m_disableBtn = buttons->addButton(tr("Disable Checked"), QDialogButtonBox::ActionRole);
    m_deleteBtn = buttons->addButton(tr("Delete Checked"), QDialogButtonBox::DestructiveRole);
    buttons->addButton(QDialogButtonBox::Close);
    connect(m_disableBtn, &QPushButton::clicked, this, &ModCleanupDialog::disableChecked);
    connect(m_deleteBtn, &QPushButton::clicked, this, &ModCleanupDialog::deleteChecked);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    scan();
}

void ModCleanupDialog::scan()
{
    QHash<QString, QList<Entry>> groups;
    QHash<QString, QString> displayNames;

    for (int row = 0; row < static_cast<int>(m_model->size()); row++) {
        auto& mod = static_cast<Mod&>(m_model->at(row));
        auto key = identityKey(mod);
        groups[key].append(Entry{ row, mod.internal_id(), mod.fileinfo().fileName(), mod.version(), versionKey(mod), mod.enabled(),
                                  mod.dateTimeChanged() });
        if (!displayNames.contains(key))
            displayNames[key] = mod.name().isEmpty() ? mod.fileinfo().fileName() : mod.name();
    }

    int extraCopies = 0;
    for (auto it = groups.constBegin(); it != groups.constEnd(); ++it) {
        auto entries = it.value();
        if (entries.size() < 2)
            continue;

        // newest enabled copy is the keeper; with none enabled, the newest overall
        int keeper = 0;
        for (int i = 1; i < entries.size(); i++) {
            bool better = (entries[i].enabled && !entries[keeper].enabled) ||
                          (entries[i].enabled == entries[keeper].enabled && entries[i].changed > entries[keeper].changed);
            if (better)
                keeper = i;
        }

        m_groupCount++;
        auto* group = new QTreeWidgetItem(m_tree);
        group->setText(0, tr("%1 (%2 copies)").arg(displayNames[it.key()]).arg(entries.size()));
        group->setFirstColumnSpanned(true);
        group->setExpanded(true);
        group->setFlags(group->flags() & ~Qt::ItemIsUserCheckable);

        for (int i = 0; i < entries.size(); i++) {
            const auto& entry = entries[i];
            auto* item = new QTreeWidgetItem(group);
            item->setText(0, entry.fileName);
            item->setText(1, entry.version);
            item->setText(2, entry.enabled ? tr("Enabled") : tr("Disabled"));
            item->setText(3, QLocale().toString(entry.changed, QLocale::ShortFormat));
            item->setData(0, InternalIdRole, entry.internalId);
            if (i == keeper) {
                item->setFlags(item->flags() & ~Qt::ItemIsUserCheckable);
                item->setText(2, tr("Keep (newest)"));
                continue;
            }
            // preselect enabled extra copies; a disabled copy is only preselected
            // when it duplicates the keeper's exact version (a pure leftover)
            bool preselect = entry.enabled || entry.versionKey == entries[keeper].versionKey;
            item->setCheckState(0, preselect ? Qt::Checked : Qt::Unchecked);
            extraCopies++;
        }
    }

    if (m_groupCount > 0) {
        m_summary->setText(tr("Found %n mod(s) installed more than once, %1 redundant file(s) in total.\n"
                              "The newest copy of each is kept; review the checkmarks and pick an action.",
                              nullptr, m_groupCount)
                               .arg(extraCopies));
    }
    m_disableBtn->setEnabled(m_groupCount > 0);
    m_deleteBtn->setEnabled(m_groupCount > 0);
}

QModelIndexList ModCleanupDialog::checkedIndexes() const
{
    QSet<QString> ids;
    for (int g = 0; g < m_tree->topLevelItemCount(); g++) {
        auto* group = m_tree->topLevelItem(g);
        for (int c = 0; c < group->childCount(); c++) {
            auto* item = group->child(c);
            if ((item->flags() & Qt::ItemIsUserCheckable) && item->checkState(0) == Qt::Checked)
                ids.insert(item->data(0, InternalIdRole).toString());
        }
    }

    QModelIndexList indexes;
    for (int row = 0; row < static_cast<int>(m_model->size()); row++) {
        if (ids.contains(m_model->at(row).internal_id()))
            indexes.append(m_model->index(row, 0));
    }
    return indexes;
}

void ModCleanupDialog::disableChecked()
{
    auto indexes = checkedIndexes();
    if (indexes.isEmpty())
        return;
    m_model->setResourceEnabled(indexes, EnableAction::DISABLE);
    accept();
}

void ModCleanupDialog::deleteChecked()
{
    auto indexes = checkedIndexes();
    if (indexes.isEmpty())
        return;

    auto response = CustomMessageBox::selectable(this, tr("Confirm Delete"),
                                                 tr("You are about to delete %n file(s).\nThis may be permanent.", nullptr, indexes.size()),
                                                 QMessageBox::Warning, QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
                        ->exec();
    if (response != QMessageBox::Yes)
        return;

    m_model->deleteResources(indexes);
    accept();
}
