// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Prism Launcher - Minecraft Launcher
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 */

#pragma once

#include <QPointer>

#include "modplatform/flame/FlameAPI.h"
#include "modplatform/modrinth/ModrinthAPI.h"
#include "tasks/Task.h"

class BaseInstance;
class QWidget;

/** Updates a managed modpack instance (Modrinth / CurseForge) to its newest
 *  listed version without the Managed Pack page: looks the versions up, picks
 *  the newest, and runs the same import flow the page's Update button uses.
 *  Used by the pre-launch update prompt.
 */
class ManagedPackUpdateTask : public Task {
    Q_OBJECT

   public:
    /** parentWidget hosts any dialogs the import may open (e.g. blocked mods). */
    ManagedPackUpdateTask(BaseInstance* instance, QWidget* parentWidget);
    ~ManagedPackUpdateTask() override = default;

    bool abort() override;

   protected:
    void executeTask() override;

   private:
    void startImport(const QUrl& url, const QString& versionId, const QString& versionName);

    BaseInstance* m_instance;
    QPointer<QWidget> m_parentWidget;
    ModrinthAPI m_modrinthApi;
    FlameAPI m_flameApi;
    Task::Ptr m_versionsJob;
    Task::Ptr m_importTask;
};
