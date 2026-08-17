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

#include "PackUpdateChecker.h"

#include <QDateTime>
#include <QDebug>
#include <QTimer>

#include "Application.h"
#include "BaseInstance.h"
#include "InstanceList.h"

PackUpdateChecker* PackUpdateChecker::get()
{
    static PackUpdateChecker* s_instance = new PackUpdateChecker(APPLICATION);
    return s_instance;
}

PackUpdateChecker::PackUpdateChecker(QObject* parent) : QObject(parent)
{
    // The launcher tends to stay open for a long time, so re-check now and then.
    auto timer = new QTimer(this);
    timer->setInterval(6 * 60 * 60 * 1000);
    connect(timer, &QTimer::timeout, this, [] { PackUpdateChecker::checkAll(); });
    timer->start();
}

void PackUpdateChecker::checkAll()
{
    if (!APPLICATION->settings()->get("CheckPackUpdates").toBool())
        return;
    get()->enqueueAll();
}

void PackUpdateChecker::enqueueAll()
{
    auto instances = APPLICATION->instances();
    for (int i = 0; i < instances->count(); i++) {
        auto inst = instances->at(i);
        if (!inst->isManagedPack() || inst->getManagedPackID().isEmpty())
            continue;
        const QString type = inst->getManagedPackType();
        if (type != "modrinth" && type != "flame")
            continue;
        if (type == "flame" && !(APPLICATION->capabilities() & Application::SupportsFlame))
            continue;
        if (!m_queue.contains(inst->id()))
            m_queue.enqueue(inst->id());
    }
    pump();
}

void PackUpdateChecker::pump()
{
    // A handful of checks in parallel: 20 managed packs at one serialized
    // round-trip each was several seconds of wall-clock for no reason.
    constexpr int MAX_CONCURRENT_CHECKS = 4;
    while (m_jobs.size() < MAX_CONCURRENT_CHECKS && !m_queue.isEmpty())
        startOne(m_queue.dequeue());
}

void PackUpdateChecker::startOne(const QString& instanceId)
{
    auto inst = APPLICATION->instances()->getInstanceById(instanceId);
    if (!inst || !inst->isManagedPack())
        return;
    const QString type = inst->getManagedPackType();

    ResourceAPI::Callback<QVector<ModPlatform::IndexedVersion>> callbacks{};
    callbacks.on_succeed = [this, instanceId, type](QVector<ModPlatform::IndexedVersion>& versions) {
        evaluate(instanceId, type, versions);
    };
    callbacks.on_fail = [instanceId](const QString& reason, int) {
        qDebug() << "Pack update check for" << instanceId << "failed:" << reason;
    };
    callbacks.on_abort = []() {};

    ModPlatform::IndexedPack pack;
    pack.addonId = inst->getManagedPackID();

    ResourceAPI* api = type == "modrinth" ? static_cast<ResourceAPI*>(&m_modrinthApi) : static_cast<ResourceAPI*>(&m_flameApi);
    auto job = api->getProjectVersions({ .pack = std::make_shared<ModPlatform::IndexedPack>(pack),
                                         .mcVersions = {},
                                         .loaders = {},
                                         .resourceType = ModPlatform::ResourceType::Modpack,
                                         .includeChangelog = false },
                                       std::move(callbacks));
    m_jobs.append(job);
    Task* raw = job.get();
    connect(raw, &Task::finished, this, [this, raw]() {
        for (int i = 0; i < m_jobs.size(); i++) {
            if (m_jobs[i].get() == raw) {
                m_jobs.removeAt(i);
                break;
            }
        }
        pump();
    });
    job->start();
}

void PackUpdateChecker::evaluate(const QString& instanceId, const QString& type, const QVector<ModPlatform::IndexedVersion>& versions)
{
    auto inst = APPLICATION->instances()->getInstanceById(instanceId);
    if (!inst || versions.isEmpty())
        return;

    int currentIndex = -1;
    for (int i = 0; i < versions.size(); i++) {
        // Same matching the Managed Pack page uses: Modrinth by version name
        // (the version id in the modpack index is unreliable), Flame by file id.
        bool matches = type == "modrinth" ? versions[i].version == inst->getManagedPackVersionName()
                                          : versions[i].fileId.toString() == inst->getManagedPackVersionID();
        if (matches) {
            currentIndex = i;
            break;
        }
    }
    if (currentIndex < 0) {
        // Custom or unlisted version, don't guess.
        inst->setUpdateAvailableVersion(QString());
        inst->setUpdateAvailable(false);
        return;
    }

    auto parseDate = [](const ModPlatform::IndexedVersion& v) {
        auto date = QDateTime::fromString(v.date, Qt::ISODateWithMs);
        if (!date.isValid())
            date = QDateTime::fromString(v.date, Qt::ISODate);
        return date;
    };

    int newestIndex = currentIndex;
    for (int i = 0; i < versions.size(); i++) {
        auto date = parseDate(versions[i]);
        auto newestDate = parseDate(versions[newestIndex]);
        if (date.isValid() && newestDate.isValid() ? date > newestDate : i < newestIndex)
            newestIndex = i;
    }

    if (newestIndex != currentIndex) {
        qDebug() << "Pack update available for" << inst->name() << "-" << versions[newestIndex].version;
        inst->setUpdateAvailableVersion(versions[newestIndex].version);
        inst->setUpdateAvailable(true);
    } else {
        inst->setUpdateAvailableVersion(QString());
        inst->setUpdateAvailable(false);
    }
}
