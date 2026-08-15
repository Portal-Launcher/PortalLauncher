// SPDX-License-Identifier: GPL-3.0-only
#include "ModrinthJoinFlow.h"

#include <QJsonObject>
#include <QObject>

#include "Application.h"
#include "InstanceList.h"
#include "ModrinthSharedApi.h"
#include "ModrinthSharedAttachment.h"
#include "ModrinthSharedJoinTask.h"
#include "ModrinthSharedSyncTask.h"
#include "ui/dialogs/ProgressDialog.h"

namespace ModrinthShared {

void runJoinFlow(QWidget* parent,
                 const QString& instanceId,
                 const QString& instanceName,
                 std::function<void(bool, const QString&)> done)
{
    getLatestVersion(parent, instanceId, [parent, instanceId, instanceName, done](const Response& res) {
        if (!res.ok || !res.json.isObject()) {
            done(false, res.error.isEmpty() ? QObject::tr("Could not fetch the shared pack.") : res.error);
            return;
        }
        const auto version = res.json.object();

        auto* instances = APPLICATION->instances();
        for (int i = 0; i < instances->count(); i++) {
            auto* existing = instances->at(i);
            auto att = Attachment::load(existing->instanceRoot());
            if (att && att->id == instanceId) {
                done(false, QObject::tr("You already joined this pack as \"%1\".").arg(existing->name()));
                return;
            }
        }

        auto* joinTask = new ModrinthSharedJoinTask(instanceId, instanceName, version);
        std::unique_ptr<Task> wrapped(APPLICATION->instances()->wrapInstanceTask(joinTask));
        ProgressDialog createDialog(parent);
        createDialog.setSkipButton(true, QObject::tr("Abort"));
        if (createDialog.execWithTask(wrapped.get()) != QDialog::Accepted) {
            done(false, wrapped->failReason().isEmpty() ? QObject::tr("Join canceled.") : wrapped->failReason());
            return;
        }

        BaseInstance* created = nullptr;
        for (int i = 0; i < instances->count(); i++) {
            auto* candidate = instances->at(i);
            auto att = Attachment::load(candidate->instanceRoot());
            if (att && att->id == instanceId) {
                created = candidate;
                break;
            }
        }
        if (created) {
            ModrinthSharedSyncTask syncTask(created, /*softFail*/ false);
            ProgressDialog syncDialog(parent);
            syncDialog.setSkipButton(true, QObject::tr("Abort"));
            syncDialog.execWithTask(&syncTask);
        }
        done(true, QString());
    });
}

}  // namespace ModrinthShared
