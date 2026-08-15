// SPDX-License-Identifier: GPL-3.0-only
#include "ModrinthJoinDialog.h"

#include <QDialogButtonBox>
#include <QJsonArray>
#include <QJsonObject>
#include <QMessageBox>
#include <QVBoxLayout>

#include "Application.h"
#include "InstanceList.h"
#include "modplatform/modrinth/shared/ModrinthSharedApi.h"
#include "modplatform/modrinth/shared/ModrinthSharedAttachment.h"
#include "modplatform/modrinth/shared/ModrinthSharedJoinTask.h"
#include "modplatform/modrinth/shared/ModrinthSharedSyncTask.h"
#include "modplatform/modrinth/shared/ModrinthSignInTask.h"
#include "ui/dialogs/ProgressDialog.h"

ModrinthJoinDialog::ModrinthJoinDialog(QWidget* parent) : QDialog(parent)
{
    setWindowTitle(tr("Join a shared pack"));
    setMinimumWidth(520);

    auto* layout = new QVBoxLayout(this);
    auto* info = new QLabel(
        tr("Paste the invite link a friend sent you (it looks like <i>modrinth.com/share/…</i>).<br>"
           "The modpack will appear as a normal instance and update itself whenever they push changes."),
        this);
    info->setWordWrap(true);
    layout->addWidget(info);

    m_linkEdit = new QLineEdit(this);
    m_linkEdit->setPlaceholderText(tr("https://modrinth.com/share/…"));
    layout->addWidget(m_linkEdit);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setWordWrap(true);
    layout->addWidget(m_statusLabel);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    m_joinButton = buttons->addButton(tr("Join"), QDialogButtonBox::AcceptRole);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_joinButton, &QPushButton::clicked, this, &ModrinthJoinDialog::join);
    layout->addWidget(buttons);
}

void ModrinthJoinDialog::join()
{
    if (!ModrinthShared::isSignedIn()) {
        ModrinthSignInTask task;
        ProgressDialog dialog(this);
        dialog.setSkipButton(true, tr("Cancel"));
        if (dialog.execWithTask(&task) != QDialog::Accepted)
            return;
    }
    ModrinthShared::refreshSessionIfNeeded(this);

    const QString inviteId = ModrinthShared::parseInviteRef(m_linkEdit->text());
    if (inviteId.isEmpty()) {
        m_statusLabel->setText(tr("Please paste an invite link first."));
        return;
    }

    m_joinButton->setEnabled(false);
    m_statusLabel->setText(tr("Looking up the invite…"));

    ModrinthShared::getInviteInfo(this, inviteId, [this, inviteId](const ModrinthShared::Response& res) {
        if (!res.ok || !res.json.isObject()) {
            m_joinButton->setEnabled(true);
            m_statusLabel->setText(tr("That invite does not exist or has expired. Ask your friend for a new link."));
            return;
        }
        const auto invite = res.json.object();
        const QString instanceId = invite.value("instance_id").toString();
        QString instanceName = invite.value("instance_name").toString();
        if (instanceName.trimmed().isEmpty())
            instanceName = tr("Shared pack");

        const auto answer = QMessageBox::question(
            this, tr("Join \"%1\"?").arg(instanceName),
            tr("You are about to install \"%1\" from a shared instance.\n\nShared instances are not reviewed by "
               "Modrinth — only accept invites from people you trust.")
                .arg(instanceName));
        if (answer != QMessageBox::Yes) {
            m_joinButton->setEnabled(true);
            m_statusLabel->clear();
            return;
        }

        m_statusLabel->setText(tr("Accepting the invite…"));
        ModrinthShared::acceptInvite(this, instanceId, inviteId, [this, instanceId, instanceName](const ModrinthShared::Response& acceptRes) {
            if (!acceptRes.ok) {
                m_joinButton->setEnabled(true);
                m_statusLabel->setText(acceptRes.error);
                return;
            }
            ModrinthShared::getLatestVersion(this, instanceId, [this, instanceId, instanceName](const ModrinthShared::Response& versionRes) {
                m_joinButton->setEnabled(true);
                if (!versionRes.ok || !versionRes.json.isObject()) {
                    m_statusLabel->setText(versionRes.error.isEmpty() ? tr("Could not fetch the shared pack.") : versionRes.error);
                    return;
                }
                const auto version = versionRes.json.object();

                // Already joined?
                auto* instances = APPLICATION->instances();
                for (int i = 0; i < instances->count(); i++) {
                    auto* existing = instances->at(i);
                    auto att = ModrinthShared::Attachment::load(existing->instanceRoot());
                    if (att && att->id == instanceId) {
                        m_statusLabel->setText(tr("You already joined this pack as \"%1\".").arg(existing->name()));
                        return;
                    }
                }

                auto* joinTask = new ModrinthSharedJoinTask(instanceId, instanceName, version);
                std::unique_ptr<Task> wrapped(APPLICATION->instances()->wrapInstanceTask(joinTask));
                ProgressDialog createDialog(this);
                createDialog.setSkipButton(true, tr("Abort"));
                if (createDialog.execWithTask(wrapped.get()) != QDialog::Accepted) {
                    m_statusLabel->setText(wrapped->failReason().isEmpty() ? tr("Join canceled.") : wrapped->failReason());
                    return;
                }

                // Find the created instance and pull the content down.
                BaseInstance* created = nullptr;
                for (int i = 0; i < instances->count(); i++) {
                    auto* candidate = instances->at(i);
                    auto att = ModrinthShared::Attachment::load(candidate->instanceRoot());
                    if (att && att->id == instanceId) {
                        created = candidate;
                        break;
                    }
                }
                if (created) {
                    ModrinthSharedSyncTask syncTask(created, /*softFail*/ false);
                    ProgressDialog syncDialog(this);
                    syncDialog.setSkipButton(true, tr("Abort"));
                    syncDialog.execWithTask(&syncTask);
                }

                QMessageBox::information(this, tr("Joined!"),
                                         tr("\"%1\" is now in your instance list. It checks for the owner's updates "
                                            "every time you press Play.")
                                             .arg(instanceName));
                accept();
            });
        });
    });
}
