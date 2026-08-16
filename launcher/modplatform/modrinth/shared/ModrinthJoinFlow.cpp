// SPDX-License-Identifier: GPL-3.0-only
#include "ModrinthJoinFlow.h"

#include <QJsonObject>
#include <QMessageBox>
#include <QObject>

#include "Application.h"
#include "InstanceList.h"
#include "ModrinthSharedApi.h"
#include "ModrinthSharedAttachment.h"
#include "ModrinthSharedJoinTask.h"
#include "ModrinthSharedSyncTask.h"
#include "ModrinthSignInTask.h"
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

void joinFromInviteRef(QWidget* parent, const QString& inviteRef, std::function<void(bool joined)> done)
{
    const QString inviteId = parseInviteRef(inviteRef);
    if (inviteId.isEmpty()) {
        done(false);
        return;
    }

    if (!isSignedIn()) {
        ModrinthSignInTask task;
        ProgressDialog signInDialog(parent);
        signInDialog.setSkipButton(true, QObject::tr("Cancel"));
        if (signInDialog.execWithTask(&task) != QDialog::Accepted) {
            done(false);
            return;
        }
    } else {
        refreshSessionIfNeeded(parent);
    }

    getInviteInfo(parent, inviteId, [parent, inviteId, done](const Response& res) {
        if (!res.ok || !res.json.isObject()) {
            QMessageBox::warning(parent, QObject::tr("Invite not found"),
                                 QObject::tr("That invite does not exist or has expired. Ask your friend for a new link."));
            done(false);
            return;
        }
        const auto invite = res.json.object();
        const QString instanceId = invite.value("instance_id").toString();
        QString instanceName = invite.value("instance_name").toString();
        if (instanceName.trimmed().isEmpty())
            instanceName = QObject::tr("Shared pack");

        if (QMessageBox::question(parent, QObject::tr("Join \"%1\"?").arg(instanceName),
                                  QObject::tr("You are about to install \"%1\" from a shared instance.\n\nShared instances are "
                                              "not reviewed by Modrinth - only accept invites from people you trust.")
                                      .arg(instanceName)) != QMessageBox::Yes) {
            done(false);
            return;
        }

        acceptInvite(parent, instanceId, inviteId, [parent, instanceId, instanceName, done](const Response& acceptRes) {
            if (!acceptRes.ok) {
                QMessageBox::warning(parent, QObject::tr("Could not accept the invite"), acceptRes.error);
                done(false);
                return;
            }
            runJoinFlow(parent, instanceId, instanceName, [parent, instanceName, done](bool joined, const QString& message) {
                if (!joined) {
                    QMessageBox::warning(parent, QObject::tr("Could not join"), message);
                    done(false);
                    return;
                }
                QMessageBox::information(parent, QObject::tr("Joined!"),
                                         QObject::tr("\"%1\" is now in your instance list. It checks for the owner's updates "
                                                     "every time you press Play.")
                                             .arg(instanceName));
                done(true);
            });
        });
    });
}

void fetchPendingInvites(QObject* ctx, std::function<void(const QList<PendingInvite>&)> done)
{
    if (!isSignedIn()) {
        done({});
        return;
    }
    getNotifications(ctx, [done](const Response& res) {
        if (!res.ok || !res.json.isArray()) {
            done({});
            return;
        }
        QSet<QString> joined;
        auto* instances = APPLICATION->instances();
        for (int i = 0; i < instances->count(); i++) {
            if (auto att = Attachment::load(instances->at(i)->instanceRoot()))
                joined.insert(att->id);
        }
        QList<PendingInvite> invites;
        QSet<QString> seen;
        for (const auto& value : res.json.array()) {
            const auto notification = value.toObject();
            const auto body = notification.value("body").toObject();
            const QString type = body.value("type").toString(notification.value("type").toString());
            if (type != QLatin1String("shared_instance_invite"))
                continue;
            PendingInvite invite;
            invite.instanceId = body.value("shared_instance_id").toString();
            invite.instanceName = body.value("shared_instance_name").toString();
            if (invite.instanceName.trimmed().isEmpty())
                invite.instanceName = QObject::tr("Shared pack");
            if (invite.instanceId.isEmpty() || joined.contains(invite.instanceId) || seen.contains(invite.instanceId))
                continue;
            seen.insert(invite.instanceId);
            invites.append(invite);
        }
        done(invites);
    });
}

}  // namespace ModrinthShared
