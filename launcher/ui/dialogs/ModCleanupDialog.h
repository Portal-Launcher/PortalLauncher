// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Prism Launcher - Minecraft Launcher
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 */

#pragma once

#include <QDialog>
#include <QList>
#include <QString>

class QTreeWidget;
class QTreeWidgetItem;
class QLabel;
class QPushButton;
class ModFolderModel;

/** Finds copies of the same mod in an instance (two versions installed at once,
 *  or a disabled leftover identical to an enabled file) and offers to disable
 *  or delete the redundant ones.
 */
class ModCleanupDialog : public QDialog {
    Q_OBJECT

   public:
    explicit ModCleanupDialog(ModFolderModel* model, QWidget* parent = nullptr);

    /** True if scanning found at least one group of duplicates. */
    bool hasDuplicates() const { return m_groupCount > 0; }

   private slots:
    void disableChecked();
    void deleteChecked();

   private:
    void scan();
    QModelIndexList checkedIndexes() const;

    ModFolderModel* m_model;
    QTreeWidget* m_tree;
    QLabel* m_summary;
    QPushButton* m_disableBtn;
    QPushButton* m_deleteBtn;
    int m_groupCount = 0;
};
