// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Prism Launcher - Minecraft Launcher
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 */

#pragma once

#include <QWidget>

#include "ImportLaunchersModel.h"
#include "modplatform/import_launchers/LauncherScanner.h"
#include "ui/pages/modplatform/ModpackProviderBasePage.h"

class NewInstanceDialog;

namespace LauncherImport {

namespace Ui {
class ImportLaunchersPage;
}

/** "Other Launchers" page of the New Instance dialog: lists instances found
 *  in the Minecraft Launcher, CurseForge, the Modrinth App, MultiMC/PolyMC/
 *  Prism, GDLauncher, ATLauncher and XMCL, and imports the selected one. */
class ImportLaunchersPage : public QWidget, public ModpackProviderBasePage {
    Q_OBJECT

   public:
    explicit ImportLaunchersPage(NewInstanceDialog* dialog, QWidget* parent = nullptr);
    ~ImportLaunchersPage() override;

    QString displayName() const override { return tr("Other Launchers"); }
    QIcon icon() const override { return QIcon::fromTheme("launcher"); }
    QString id() const override { return "import_launchers"; }
    QString helpPage() const override { return "Launcher-import"; }
    bool shouldDisplay() const override { return true; }
    void openedImpl() override;
    void retranslate() override;

    void setSearchTerm(QString term) override;
    QString getSerachTerm() const override;

   private slots:
    void onSelectionChanged(const QModelIndex& now, const QModelIndex& previous);
    void onScanFinished(int count);

   private:
    void suggestCurrent();

    bool m_initialized = false;
    FoundInstance m_selected;
    ListModel* m_model = nullptr;
    FilterModel* m_filter = nullptr;
    NewInstanceDialog* m_dialog = nullptr;
    Ui::ImportLaunchersPage* ui = nullptr;
};

}  // namespace LauncherImport
