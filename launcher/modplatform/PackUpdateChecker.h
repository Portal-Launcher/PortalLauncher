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

#pragma once

#include <QObject>
#include <QQueue>

#include "modplatform/ModIndex.h"
#include "modplatform/flame/FlameAPI.h"
#include "modplatform/modrinth/ModrinthAPI.h"
#include "tasks/Task.h"

/** Checks managed modpack instances (Modrinth / CurseForge) for newer pack
 *  versions and marks the ones that can update, so the instance grid can show
 *  an update badge without the user digging through the Managed Pack page.
 */
class PackUpdateChecker : public QObject {
    Q_OBJECT

   public:
    /// Queue a check of every managed instance. Respects the CheckPackUpdates
    /// setting and is safe to call repeatedly; a few checks run in parallel.
    static void checkAll();

   private:
    explicit PackUpdateChecker(QObject* parent = nullptr);
    static PackUpdateChecker* get();

    void enqueueAll();
    void pump();
    void startOne(const QString& instanceId);
    void evaluate(const QString& instanceId, const QString& type, const QVector<ModPlatform::IndexedVersion>& versions);

    QQueue<QString> m_queue;
    QList<Task::Ptr> m_jobs;  // in-flight version lookups
    ModrinthAPI m_modrinthApi;
    FlameAPI m_flameApi;
};
