// SPDX-License-Identifier: GPL-3.0-only
#include "SharedPackPage.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QMessageBox>
#include <QVBoxLayout>

#include "Application.h"
#include "InstanceList.h"
#include "modplatform/modrinth/shared/ModrinthJoinFlow.h"
#include "modplatform/modrinth/shared/ModrinthSharedApi.h"
#include "modplatform/modrinth/shared/ModrinthSharedAttachment.h"
#include "modplatform/modrinth/shared/ModrinthSignInTask.h"
#include "ui/dialogs/NewInstanceDialog.h"
#include "ui/dialogs/ProgressDialog.h"

SharedPackPage::SharedPackPage(NewInstanceDialog* dialog, QWidget* parent) : QWidget(parent), m_dialog(dialog)
{
    auto* layout = new QVBoxLayout(this);

    auto* info = new QLabel(
        tr("Join a modpack a friend shared with you. Paste the invite link they sent (it looks like "
           "<i>modrinth.com/share/…</i>). The pack installs as a normal instance and pulls the owner's updates "
           "automatically every time you press Play."),
        this);
    info->setWordWrap(true);
    layout->addWidget(info);

    auto* linkRow = new QHBoxLayout();
    m_linkEdit = new QLineEdit(this);
    m_linkEdit->setPlaceholderText(tr("https://modrinth.com/share/…"));
    m_joinButton = new QPushButton(tr("Join"), this);
    linkRow->addWidget(m_linkEdit, 1);
    linkRow->addWidget(m_joinButton);
    layout->addLayout(linkRow);

    m_invitesLabel = new QLabel(tr("Invites sent to your Modrinth account:"), this);
    m_invitesList = new QListWidget(this);
    m_acceptInviteButton = new QPushButton(tr("Accept selected invite"), this);
    layout->addWidget(m_invitesLabel);
    layout->addWidget(m_invitesList, 1);
    layout->addWidget(m_acceptInviteButton);
    m_invitesLabel->hide();
    m_invitesList->hide();
    m_acceptInviteButton->hide();

    m_statusLabel = new QLabel(this);
    m_statusLabel->setWordWrap(true);
    layout->addWidget(m_statusLabel);
    layout->addStretch(0);

    connect(m_joinButton, &QPushButton::clicked, this, &SharedPackPage::joinByLink);
    connect(m_acceptInviteButton, &QPushButton::clicked, this, &SharedPackPage::acceptSelectedInvite);
    connect(m_invitesList, &QListWidget::itemDoubleClicked, this, &SharedPackPage::acceptSelectedInvite);
}

void SharedPackPage::openedImpl()
{
    if (!m_loadedInvites && ModrinthShared::isSignedIn()) {
        m_loadedInvites = true;
        ModrinthShared::refreshSessionIfNeeded(this);
        loadPendingInvites();
    }
}

bool SharedPackPage::ensureSignedIn()
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

void SharedPackPage::loadPendingInvites()
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

void SharedPackPage::joinByLink()
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
                                     "not reviewed by Modrinth - only accept invites from people you trust.")
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
                                         finishJoin(instanceId, instanceName);
                                     });
    });
}

void SharedPackPage::acceptSelectedInvite()
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
                                 "reviewed by Modrinth - only accept invites from people you trust.")
                                  .arg(name)) != QMessageBox::Yes)
        return;

    m_acceptInviteButton->setEnabled(false);
    ModrinthShared::acceptPendingInvite(this, instanceId, [this, instanceId, name](const ModrinthShared::Response& res) {
        m_acceptInviteButton->setEnabled(true);
        if (!res.ok && res.status != 404) {
            m_statusLabel->setText(res.error);
            return;
        }
        finishJoin(instanceId, name);
    });
}

void SharedPackPage::finishJoin(const QString& instanceId, const QString& instanceName)
{
    m_statusLabel->setText(tr("Installing the shared pack…"));
    ModrinthShared::runJoinFlow(this, instanceId, instanceName, [this, instanceName](bool joined, const QString& message) {
        if (!joined) {
            m_statusLabel->setText(message);
            return;
        }
        QMessageBox::information(this, tr("Joined!"),
                                 tr("\"%1\" is now in your instance list. It checks for the owner's updates every "
                                    "time you press Play.")
                                     .arg(instanceName));
        m_dialog->reject();  // the instance was created by the join flow itself
    });
}
