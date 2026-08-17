// SPDX-License-Identifier: GPL-3.0-only
#include "ModrinthOptionalModsDialog.h"

#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "minecraft/MinecraftInstance.h"
#include "minecraft/mod/Mod.h"
#include "minecraft/mod/ModFolderModel.h"
#include "modplatform/modrinth/shared/ModrinthSharedAttachment.h"

namespace {
constexpr int KeyRole = Qt::UserRole;
constexpr int IsProjectRole = Qt::UserRole + 1;
}  // namespace

ModrinthOptionalModsDialog::ModrinthOptionalModsDialog(MinecraftInstance* instance, QWidget* parent)
    : QDialog(parent), m_instance(instance)
{
    setWindowTitle(tr("Optional Mods"));
    resize(560, 440);

    auto* layout = new QVBoxLayout(this);

    m_hint = new QLabel(
        tr("Checked mods are optional: members can disable them on their Mods page and "
           "updates will keep them disabled instead of turning them back on.\n"
           "Takes effect with your next push."),
        this);
    m_hint->setWordWrap(true);
    layout->addWidget(m_hint);

    m_tree = new QTreeWidget(this);
    m_tree->setColumnCount(2);
    m_tree->setHeaderLabels({ tr("Mod"), tr("File") });
    m_tree->setRootIsDecorated(false);
    m_tree->setSelectionMode(QAbstractItemView::NoSelection);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    layout->addWidget(m_tree, 1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    auto* model = m_instance->loaderModList();
    // Show what is already loaded right away; refresh once the folder rescan
    // lands (kept edits survive the refresh).
    connect(model, &ResourceFolderModel::updateFinished, this, &ModrinthOptionalModsDialog::populate);
    model->update();
    populate();
}

void ModrinthOptionalModsDialog::populate()
{
    auto attachment = ModrinthShared::Attachment::load(m_instance->instanceRoot());
    const QStringList optionalProjects = attachment ? attachment->optionalProjects : QStringList();
    const QStringList optionalFiles = attachment ? attachment->optionalFiles : QStringList();

    // Keep any checkmarks the user already flipped in this dialog session.
    QHash<QString, bool> editedState;
    for (int i = 0; i < m_tree->topLevelItemCount(); i++) {
        auto* item = m_tree->topLevelItem(i);
        editedState[item->data(0, KeyRole).toString()] = item->checkState(0) == Qt::Checked;
    }

    m_tree->clear();
    auto model = m_instance->loaderModList();
    for (int row = 0; row < static_cast<int>(model->size()); row++) {
        auto& mod = static_cast<Mod&>(model->at(row));
        if (!mod.enabled())
            continue;  // disabled mods are not pushed at all

        QString fileName = mod.fileinfo().fileName();
        auto meta = mod.metadata();
        const bool hasProject = meta && meta->isValid();
        const QString key = hasProject ? meta->project_id.toString() : fileName;

        auto* item = new QTreeWidgetItem(m_tree);
        item->setText(0, mod.name().isEmpty() ? fileName : mod.name());
        item->setText(1, fileName);
        item->setData(0, KeyRole, key);
        item->setData(0, IsProjectRole, hasProject);
        bool optional = hasProject ? optionalProjects.contains(key) : optionalFiles.contains(key);
        if (editedState.contains(key))
            optional = editedState.value(key);
        item->setCheckState(0, optional ? Qt::Checked : Qt::Unchecked);
    }
    m_tree->sortItems(0, Qt::AscendingOrder);
}

void ModrinthOptionalModsDialog::accept()
{
    auto attachment = ModrinthShared::Attachment::load(m_instance->instanceRoot());
    if (attachment) {
        QStringList projects;
        QStringList files;
        for (int i = 0; i < m_tree->topLevelItemCount(); i++) {
            auto* item = m_tree->topLevelItem(i);
            if (item->checkState(0) != Qt::Checked)
                continue;
            if (item->data(0, IsProjectRole).toBool())
                projects.append(item->data(0, KeyRole).toString());
            else
                files.append(item->data(0, KeyRole).toString());
        }
        projects.sort();
        files.sort();
        attachment->optionalProjects = projects;
        attachment->optionalFiles = files;
        attachment->save(m_instance->instanceRoot());
    }
    QDialog::accept();
}
