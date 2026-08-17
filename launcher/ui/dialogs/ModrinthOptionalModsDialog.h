// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Prism Launcher fork - Modrinth Shared Instances support
 *
 *  Lets the owner of a shared pack mark mods as optional: members may disable
 *  optional mods and sync keeps their choice instead of re-enabling them.
 */
#pragma once

#include <QDialog>

class QTreeWidget;
class QLabel;
class MinecraftInstance;

class ModrinthOptionalModsDialog : public QDialog {
    Q_OBJECT

   public:
    explicit ModrinthOptionalModsDialog(MinecraftInstance* instance, QWidget* parent = nullptr);

    void accept() override;

   private:
    void populate();

    MinecraftInstance* m_instance;
    QTreeWidget* m_tree;
    QLabel* m_hint;
};
