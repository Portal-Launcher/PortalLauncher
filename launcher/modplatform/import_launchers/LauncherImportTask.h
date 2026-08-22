// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Prism Launcher - Minecraft Launcher
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 */

#pragma once

#include <QFuture>
#include <QFutureWatcher>

#include "InstanceTask.h"
#include "LauncherScanner.h"
#include "QObjectPtr.h"

namespace Technic {
class TechnicPackProcessor;
}

/** Turns an instance found in another launcher into one of ours. MultiMC-format
 *  instances are copied whole and keep their settings; everything else gets
 *  its game folder copied (minus the old launcher's own files) and a fresh
 *  component list for the game version and loader the scanner read. The
 *  source is never modified.
 */
class LauncherImportTask : public InstanceTask {
    Q_OBJECT

   public:
    explicit LauncherImportTask(LauncherImport::FoundInstance found) : m_found(std::move(found)) {}
    virtual ~LauncherImportTask() = default;

   protected:
    void executeTask() override;

   private slots:
    void copyFinished();

   private:
    void finishMultiMC();
    void finishGeneric();
    void finishTechnic();

    QFuture<bool> m_copyFuture;
    QFutureWatcher<bool> m_copyFutureWatcher;
    const LauncherImport::FoundInstance m_found;
    shared_qobject_ptr<Technic::TechnicPackProcessor> m_technicProcessor;
};
