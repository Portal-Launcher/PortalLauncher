// SPDX-License-Identifier: GPL-3.0-only
#include "ModrinthJoinDialog.h"

#include <QDialogButtonBox>
#include <QJsonArray>
#include <QJsonObject>
#include <QMessageBox>
#include <QShowEvent>
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
    setMinimumWidth(560);

    auto* layout = new QVBoxLayout(this);
    auto* info = new QLabel(
        tr("Paste the invite link a friend sent you (it looks like <i>modrinth.com/share/…</i>).<br>"
           "The modpack appears as a normal instance and updates itself whenever they push changes."),
        this);
    info->setWordWrap(true);
    layout->addWidget(info);

    auto* linkRow = new QHBoxLayout();
    m_linkEdit = new QLineEdit(this);
    m_linkEdit->setPlaceholderText(tr("https://modrinth.com/share/…"));
    m_joinButton = new QPushButton(tr("Join"), this);
    m_joinButton->setDefault(true);
    linkRow->addWidget(m_linkEdit, 1);
    linkRow->addWidget(m_joinButton);
    layout->addLayout(linkRow);

    m_invitesLabel = new QLabel(tr("Invites sent to your Modrinth account:"), this);
    m_invitesList = new QListWidget(this);
    m_invitesList->setMaximumHeight(120);
    m_acceptInviteButton = new QPushButton(tr("Accept selected invite"), this);
    layout->addWidget(m_invitesLabel);
    layout->addWidget(m_invitesList);
    layout->addWidget(m_acceptInviteButton);
    m_invitesLabel->hide();
    m_invitesList->hide();
    m_acceptInviteButton->hide();

    m_statusLabel = new QLabel(this);
    m_statusLabel->setWordWrap(true);
    layout->addWidget(m_statusLabel);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    connect(m_joinButton, &QPushButton::clicked, this, &ModrinthJoinDialog::joinByLink);
    connect(m_acceptInviteButton, &QPushButton::clicked, this, &ModrinthJoinDialog::acceptSelectedInvite);
    connect(m_invitesList, &QListWidget::itemDoubleClicked, this, &ModrinthJoinDialog::acceptSelectedInvite);
}

void ModrinthJoinDialog::showEvent(QShowEvent* event)
{
    QDialog::showEvent(event);
    if (!m_loadedInvites && ModrinthShared::isSignedIn()) {
        m_loadedInvites = true;
        ModrinthShared::refreshSessionIfNeeded(this);
        loadPendingInvites();
    }
}

bool ModrinthJoinDialog::ensureSignedIn()
{
    if (ModrinthShared::isSignedIn())
        return true;
    ModrinthSignInTask task;
    ProgressDialog dialog(this);
    dialog.setSkipButton(true, tr("Cancel"));
    if (dialog.execWithTask(&task) != QDialog::Accepted)
        return false;
    if (!m_loadedInvites) {
        m_loadedInvites = true;
        loadPendingInvites();
    }
    return true;
}

void ModrinthJoinDialog::loadPendingInvites()
{
    ModrinthShared::getNotifications(this, [this](const ModrinthShared::Response& res) {
        if (!res.ok || !res.json.isArray())
            return;
        QSet<QString> joined;
        auto* instances = APPLICATION->instances();
        for (int i = 0; i < instances->count(); i++) {
            if (auto att = ModrinthShared::Attachment::load(instances->at(i)->instanceRoot()))
                joined.insert(att->id);
        }
        m_invitesList->clear();
        for (const auto& value : res.json.array()) {
            const auto notification = value.toObject();
            const auto body = notification.value("body").toObject();
            const QString type = body.value("type").toString(notification.value("type").toString());
            if (type != QLatin1String("shared_instance_invite"))
                continue;
            const QString instanceId = body.value("shared_instance_id").toString();
            if (instanceId.isEmpty() || joined.contains(instanceId))
                continue;
            QString name = body.value("shared_instance_name").toString();
            if (name.trimmed().isEmpty())
                name = tr("Shared pack");
            auto* item = new QListWidgetItem(name, m_invitesList);
            item->setData(Qt::UserRole, instanceId);
            item->setData(Qt::UserRole + 1, name);
        }
        const bool any = m_invitesList->count() > 0;
        m_invitesLabel->setVisible(any);
        m_invitesList->setVisible(any);
        m_acceptInviteButton->setVisible(any);
    });
}

void ModrinthJoinDialog::joinByLink()
{
    if (!ensureSignedIn())
        return;
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

        if (QMessageBox::question(this, tr("Join \"%1\"?").arg(instanceName),
                                  tr("You are about to install \"%1\" from a shared instance.\n\nShared instances are "
                                     "not reviewed by Modrinth — only accept invites from people you trust.")
                                      .arg(instanceName)) != QMessageBox::Yes) {
            m_joinButton->setEnabled(true);
            m_statusLabel->clear();
            return;
        }

        m_statusLabel->setText(tr("Accepting the invite…"));
        ModrinthShared::acceptInvite(this, instanceId, inviteId,
                                     [this, instanceId, instanceName](const ModrinthShared::Response& acceptRes) {
                                         m_joinButton->setEnabled(true);
                                         if (!acceptRes.ok) {
                                             m_statusLabel->setText(acceptRes.error);
                                             return;
                                         }
                                         installShared(instanceId, instanceName);
                                     });
    });
}

void ModrinthJoinDialog::acceptSelectedInvite()
{
    if (!ensureSignedIn())
        return;
    auto* item = m_invitesList->currentItem();
    if (!item)
        return;
    const QString instanceId = item->data(Qt::UserRole).toString();
    const QString name = item->data(Qt::UserRole + 1).toString();

    if (QMessageBox::question(this, tr("Join \"%1\"?").arg(name),
                              tr("You are about to install \"%1\" from a shared instance.\n\nShared instances are not "
                                 "reviewed by Modrinth — only accept invites from people you trust.")
                                  .arg(name)) != QMessageBox::Yes)
        return;

    m_acceptInviteButton->setEnabled(false);
    m_statusLabel->setText(tr("Accepting the invite…"));
    ModrinthShared::acceptPendingInvite(this, instanceId, [this, instanceId, name](const ModrinthShared::Response& res) {
        m_acceptInviteButton->setEnabled(true);
        // 404 = the pending invite is gone but we may already have access.
        if (!res.ok && res.status != 404) {
            m_statusLabel->setText(res.error);
            return;
        }
        installShared(instanceId, name);
    });
}

void ModrinthJoinDialog::installShared(const QString& instanceId, const QString& instanceName)
{
    m_statusLabel->setText(tr("Fetching the shared pack…"));
    ModrinthShared::getLatestVersion(this, instanceId, [this, instanceId, instanceName](const ModrinthShared::Response& res) {
        if (!res.ok || !res.json.isObject()) {
            m_statusLabel->setText(res.error.isEmpty() ? tr("Could not fetch the shared pack.") : res.error);
            return;
        }
        const auto version = res.json.object();

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
                                 tr("\"%1\" is now in your instance list. It checks for the owner's updates every "
                                    "time you press Play.")
                                     .arg(instanceName));
        accept();
    });
}
