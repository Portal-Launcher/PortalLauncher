// SPDX-License-Identifier: GPL-3.0-only
#include "ModrinthSharingPage.h"

#include <QApplication>
#include <QClipboard>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QMessageBox>
#include <QVBoxLayout>

#include "BaseInstance.h"
#include "modplatform/modrinth/shared/ModrinthSharedApi.h"
#include "modplatform/modrinth/shared/ModrinthSharedAttachment.h"
#include "modplatform/modrinth/shared/ModrinthSharedPublishTask.h"
#include "modplatform/modrinth/shared/ModrinthSharedSyncTask.h"
#include "modplatform/modrinth/shared/ModrinthSignInTask.h"
#include "ui/dialogs/ProgressDialog.h"

ModrinthSharingPage::ModrinthSharingPage(BaseInstance* inst, QWidget* parent) : QWidget(parent), m_instance(inst)
{
    auto* layout = new QVBoxLayout(this);

    auto* accountRow = new QHBoxLayout();
    m_accountLabel = new QLabel(this);
    m_signInButton = new QPushButton(this);
    accountRow->addWidget(m_accountLabel, 1);
    accountRow->addWidget(m_signInButton);
    layout->addLayout(accountRow);

    m_stateLabel = new QLabel(this);
    m_stateLabel->setWordWrap(true);
    layout->addWidget(m_stateLabel);

    // --- Not shared yet -----------------------------------------------------
    m_notSharedBox = new QGroupBox(tr("Share this instance"), this);
    {
        auto* box = new QVBoxLayout(m_notSharedBox);
        auto* info = new QLabel(
            tr("Invite friends and push updates as you change the pack. Friends can join from this launcher or the "
               "official Modrinth App, and their copies update automatically before they play."),
            m_notSharedBox);
        info->setWordWrap(true);
        box->addWidget(info);
        auto* row = new QHBoxLayout();
        m_shareConfigsCombo = new QComboBox(m_notSharedBox);
        m_shareConfigsCombo->addItem(tr("Don't share config files"), QString());
        m_shareConfigsCombo->addItem(tr("Share all config files"), QStringLiteral("all"));
        m_shareButton = new QPushButton(tr("Share this instance"), m_notSharedBox);
        row->addWidget(m_shareConfigsCombo, 1);
        row->addWidget(m_shareButton);
        box->addLayout(row);
    }
    layout->addWidget(m_notSharedBox);

    // --- Hosting ------------------------------------------------------------
    m_ownerBox = new QGroupBox(tr("Hosting"), this);
    {
        auto* box = new QVBoxLayout(m_ownerBox);

        auto* pushRow = new QHBoxLayout();
        m_pushButton = new QPushButton(tr("Push update now"), m_ownerBox);
        m_autoPushCheck = new QCheckBox(tr("Push my changes automatically when I launch this instance"), m_ownerBox);
        pushRow->addWidget(m_pushButton);
        pushRow->addWidget(m_autoPushCheck, 1);
        box->addLayout(pushRow);

        m_configsCombo = new QComboBox(m_ownerBox);
        m_configsCombo->addItem(tr("Don't share config files"), QString());
        m_configsCombo->addItem(tr("Share all config files"), QStringLiteral("all"));
        box->addWidget(m_configsCombo);

        auto* linkRow = new QHBoxLayout();
        m_inviteLinkEdit = new QLineEdit(m_ownerBox);
        m_inviteLinkEdit->setReadOnly(true);
        m_inviteLinkEdit->setPlaceholderText(tr("Invite link appears here"));
        m_newLinkButton = new QPushButton(tr("New invite link"), m_ownerBox);
        linkRow->addWidget(m_inviteLinkEdit, 1);
        linkRow->addWidget(m_newLinkButton);
        box->addLayout(linkRow);

        auto* userRow = new QHBoxLayout();
        m_usernameEdit = new QLineEdit(m_ownerBox);
        m_usernameEdit->setPlaceholderText(tr("Friend's Modrinth username (not their Minecraft name)"));
        m_inviteUserButton = new QPushButton(tr("Invite"), m_ownerBox);
        userRow->addWidget(m_usernameEdit, 1);
        userRow->addWidget(m_inviteUserButton);
        box->addLayout(userRow);

        box->addWidget(new QLabel(tr("Members:"), m_ownerBox));
        m_membersList = new QListWidget(m_ownerBox);
        m_membersList->setMaximumHeight(140);
        box->addWidget(m_membersList);
        auto* memberButtons = new QHBoxLayout();
        m_removeMemberButton = new QPushButton(tr("Remove selected member"), m_ownerBox);
        m_stopButton = new QPushButton(tr("Stop sharing"), m_ownerBox);
        memberButtons->addWidget(m_removeMemberButton);
        memberButtons->addStretch(1);
        memberButtons->addWidget(m_stopButton);
        box->addLayout(memberButtons);
    }
    layout->addWidget(m_ownerBox);

    // --- Joined pack ----------------------------------------------------------
    m_memberBox = new QGroupBox(tr("Joined shared pack"), this);
    {
        auto* box = new QVBoxLayout(m_memberBox);
        m_memberInfoLabel = new QLabel(m_memberBox);
        m_memberInfoLabel->setWordWrap(true);
        box->addWidget(m_memberInfoLabel);
        auto* row = new QHBoxLayout();
        m_syncButton = new QPushButton(tr("Check for updates now"), m_memberBox);
        m_changesButton = new QPushButton(tr("What changed last update?"), m_memberBox);
        m_leaveButton = new QPushButton(tr("Leave shared pack"), m_memberBox);
        row->addWidget(m_syncButton);
        row->addWidget(m_changesButton);
        row->addStretch(1);
        row->addWidget(m_leaveButton);
        box->addLayout(row);
    }
    layout->addWidget(m_memberBox);
    layout->addStretch(1);

    connect(m_signInButton, &QPushButton::clicked, this, &ModrinthSharingPage::signInOrOut);
    connect(m_shareButton, &QPushButton::clicked, this, &ModrinthSharingPage::shareInstance);
    connect(m_pushButton, &QPushButton::clicked, this, &ModrinthSharingPage::pushUpdate);
    connect(m_newLinkButton, &QPushButton::clicked, this, &ModrinthSharingPage::newInviteLink);
    connect(m_inviteUserButton, &QPushButton::clicked, this, &ModrinthSharingPage::inviteByUsername);
    connect(m_removeMemberButton, &QPushButton::clicked, this, &ModrinthSharingPage::removeSelectedMember);
    connect(m_autoPushCheck, &QCheckBox::toggled, this, &ModrinthSharingPage::toggleAutoPush);
    connect(m_configsCombo, QOverload<int>::of(&QComboBox::activated), this, &ModrinthSharingPage::configSpecChanged);
    connect(m_syncButton, &QPushButton::clicked, this, &ModrinthSharingPage::syncNow);
    connect(m_changesButton, &QPushButton::clicked, this, [this]() {
        auto attachment = ModrinthShared::Attachment::load(m_instance->instanceRoot());
        if (!attachment || attachment->lastChangeLog.isEmpty()) {
            QMessageBox::information(this, tr("Last update"), tr("No update has been applied yet."));
            return;
        }
        QMessageBox::information(this, tr("Changes in version %1").arg(attachment->lastChangeVersion),
                                 attachment->lastChangeLog.join('\n'));
    });
    connect(m_stopButton, &QPushButton::clicked, this, &ModrinthSharingPage::stopSharing);
    connect(m_leaveButton, &QPushButton::clicked, this, &ModrinthSharingPage::leaveShare);

    refresh();
}

void ModrinthSharingPage::openedImpl()
{
    ModrinthShared::refreshSessionIfNeeded(this);
    refresh();
}

QString ModrinthSharingPage::currentConfigSpec() const
{
    auto attachment = ModrinthShared::Attachment::load(m_instance->instanceRoot());
    return attachment ? m_configsCombo->currentData().toString() : m_shareConfigsCombo->currentData().toString();
}

void ModrinthSharingPage::refresh()
{
    const bool signedIn = ModrinthShared::isSignedIn();
    m_accountLabel->setText(signedIn ? tr("Modrinth account: <b>%1</b>").arg(ModrinthShared::username())
                                     : tr("Not signed in to Modrinth - sign in to share or join packs."));
    m_signInButton->setText(signedIn ? tr("Sign out") : tr("Sign in with browser…"));

    auto attachment = ModrinthShared::Attachment::load(m_instance->instanceRoot());
    const bool owner = attachment && attachment->isOwner();
    const bool member = attachment && attachment->isMember();

    m_notSharedBox->setVisible(!attachment);
    m_ownerBox->setVisible(owner);
    m_memberBox->setVisible(member);
    m_shareButton->setEnabled(signedIn);

    if (!attachment) {
        m_stateLabel->setText(QString());
    } else if (owner) {
        m_stateLabel->setText(tr("<b>Sharing is on.</b> Last pushed version: %1")
                                  .arg(attachment->appliedVersion < 0 ? tr("none yet") : QString::number(attachment->appliedVersion)));
        m_autoPushCheck->setChecked(attachment->autoPush);
        int idx = m_configsCombo->findData(attachment->configSpec);
        if (idx < 0 && !attachment->configSpec.isEmpty()) {
            m_configsCombo->addItem(tr("Custom (%1)").arg(attachment->configSpec), attachment->configSpec);
            idx = m_configsCombo->count() - 1;
        }
        m_configsCombo->setCurrentIndex(idx < 0 ? 0 : idx);
        for (auto* widget : { static_cast<QWidget*>(m_pushButton), static_cast<QWidget*>(m_newLinkButton),
                              static_cast<QWidget*>(m_inviteUserButton), static_cast<QWidget*>(m_removeMemberButton),
                              static_cast<QWidget*>(m_stopButton) })
            widget->setEnabled(signedIn);
        if (signedIn)
            loadMembers();
    } else if (member) {
        m_stateLabel->setText(tr("<b>This is a shared pack you joined.</b>"));
        m_memberInfoLabel->setText(tr("Applied version: %1. The pack checks for the owner's updates every time you "
                                      "press Play - including mods, shared configs, and the pack icon.")
                                       .arg(attachment->appliedVersion < 0 ? tr("none yet") : QString::number(attachment->appliedVersion)));
        m_syncButton->setEnabled(signedIn);
    }
}

void ModrinthSharingPage::loadMembers()
{
    auto attachment = ModrinthShared::Attachment::load(m_instance->instanceRoot());
    if (!attachment)
        return;
    m_membersList->clear();
    m_membersList->addItem(tr("Loading members…"));
    ModrinthShared::getMembers(this, attachment->id, [this](const ModrinthShared::Response& res) {
        if (!res.ok) {
            m_membersList->clear();
            m_membersList->addItem(tr("Could not load members."));
            return;
        }
        QStringList ids;
        if (res.json.isObject()) {
            for (const auto& value : res.json.object().value("users").toArray())
                ids.append(value.toObject().value("id").toString());
        } else if (res.json.isArray()) {
            for (const auto& value : res.json.array())
                ids.append(value.toString());
        }
        ModrinthShared::getUsersByIds(this, ids, [this, ids](const ModrinthShared::Response& usersRes) {
            QHash<QString, QString> nameById;
            if (usersRes.ok) {
                for (const auto& value : usersRes.json.array()) {
                    const auto obj = value.toObject();
                    nameById[obj.value("id").toString()] = obj.value("username").toString();
                }
            }
            m_membersList->clear();
            for (const auto& id : ids) {
                QString label = nameById.value(id, id);
                if (id == ModrinthShared::userId())
                    label += tr(" (you - owner)");
                auto* item = new QListWidgetItem(label, m_membersList);
                item->setData(Qt::UserRole, id);
            }
        });
    });
}

void ModrinthSharingPage::signInOrOut()
{
    if (ModrinthShared::isSignedIn()) {
        ModrinthShared::clearSession();
        refresh();
        return;
    }
    ModrinthSignInTask task;
    ProgressDialog dialog(this);
    dialog.setSkipButton(true, tr("Cancel"));
    dialog.execWithTask(&task);
    refresh();
}

void ModrinthSharingPage::shareInstance()
{
    ModrinthSharedPublishTask task(m_instance, /*force*/ true, m_shareConfigsCombo->currentData().toString());
    ProgressDialog dialog(this);
    dialog.setSkipButton(true, tr("Abort"));
    if (dialog.execWithTask(&task) == QDialog::Accepted) {
        refresh();
        newInviteLink();
    } else if (!task.failReason().isEmpty()) {
        QMessageBox::warning(this, tr("Sharing failed"), task.failReason());
    }
    refresh();
}

void ModrinthSharingPage::pushUpdate()
{
    ModrinthSharedPublishTask task(m_instance, /*force*/ false, m_configsCombo->currentData().toString());
    ProgressDialog dialog(this);
    dialog.setSkipButton(true, tr("Abort"));
    if (dialog.execWithTask(&task) != QDialog::Accepted && !task.failReason().isEmpty())
        QMessageBox::warning(this, tr("Push failed"), task.failReason());
    refresh();
}

void ModrinthSharingPage::newInviteLink()
{
    auto attachment = ModrinthShared::Attachment::load(m_instance->instanceRoot());
    if (!attachment)
        return;
    m_newLinkButton->setEnabled(false);
    ModrinthShared::createInvite(this, attachment->id, 7 * 24 * 3600, 10, [this](const ModrinthShared::Response& res) {
        m_newLinkButton->setEnabled(true);
        if (!res.ok) {
            QMessageBox::warning(this, tr("Invite link"), res.error);
            return;
        }
        const QString link = ModrinthShared::inviteLink(res.json.object().value("id").toString());
        m_inviteLinkEdit->setText(link);
        m_inviteLinkEdit->selectAll();
        QApplication::clipboard()->setText(link);
        m_stateLabel->setText(tr("<b>Invite link copied to your clipboard!</b> It lasts 7 days or 10 uses."));
    });
}

void ModrinthSharingPage::inviteByUsername()
{
    auto attachment = ModrinthShared::Attachment::load(m_instance->instanceRoot());
    const QString username = m_usernameEdit->text().trimmed();
    if (!attachment || username.isEmpty())
        return;
    m_inviteUserButton->setEnabled(false);
    ModrinthShared::lookupUserByName(this, username, [this, attachment](const ModrinthShared::Response& res) {
        if (!res.ok || !res.json.isObject()) {
            m_inviteUserButton->setEnabled(true);
            QMessageBox::warning(this, tr("Invite"), tr("No Modrinth user with that name was found."));
            return;
        }
        const auto user = res.json.object();
        ModrinthShared::addMembers(this, attachment->id, { user.value("id").toString() },
                                   [this, user](const ModrinthShared::Response& addRes) {
                                       m_inviteUserButton->setEnabled(true);
                                       if (!addRes.ok) {
                                           QMessageBox::warning(this, tr("Invite"), addRes.error);
                                           return;
                                       }
                                       m_usernameEdit->clear();
                                       m_stateLabel->setText(
                                           tr("<b>%1 has been invited</b> - they'll get a Modrinth notification.")
                                               .arg(user.value("username").toString()));
                                       loadMembers();
                                   });
    });
}

void ModrinthSharingPage::removeSelectedMember()
{
    auto attachment = ModrinthShared::Attachment::load(m_instance->instanceRoot());
    auto* item = m_membersList->currentItem();
    if (!attachment || !item)
        return;
    const QString id = item->data(Qt::UserRole).toString();
    if (id.isEmpty() || id == ModrinthShared::userId())
        return;
    if (QMessageBox::question(this, tr("Remove member"), tr("Remove %1 from this shared pack?").arg(item->text())) !=
        QMessageBox::Yes)
        return;
    ModrinthShared::removeMembers(this, attachment->id, { id }, [this](const ModrinthShared::Response& res) {
        if (!res.ok)
            QMessageBox::warning(this, tr("Remove member"), res.error);
        loadMembers();
    });
}

void ModrinthSharingPage::toggleAutoPush(bool checked)
{
    auto attachment = ModrinthShared::Attachment::load(m_instance->instanceRoot());
    if (!attachment || attachment->autoPush == checked)
        return;
    attachment->autoPush = checked;
    attachment->save(m_instance->instanceRoot());
}

void ModrinthSharingPage::configSpecChanged(int)
{
    auto attachment = ModrinthShared::Attachment::load(m_instance->instanceRoot());
    if (!attachment)
        return;
    attachment->configSpec = m_configsCombo->currentData().toString();
    attachment->save(m_instance->instanceRoot());
}

void ModrinthSharingPage::syncNow()
{
    ModrinthSharedSyncTask task(m_instance, /*softFail*/ false);
    ProgressDialog dialog(this);
    dialog.setSkipButton(true, tr("Abort"));
    if (dialog.execWithTask(&task) != QDialog::Accepted && !task.failReason().isEmpty())
        QMessageBox::warning(this, tr("Update check failed"), task.failReason());
    refresh();
}

void ModrinthSharingPage::stopSharing()
{
    auto attachment = ModrinthShared::Attachment::load(m_instance->instanceRoot());
    if (!attachment)
        return;
    if (QMessageBox::question(this, tr("Stop sharing"),
                              tr("Stop sharing \"%1\"? Your friends will lose access to updates.").arg(m_instance->name())) !=
        QMessageBox::Yes)
        return;
    ModrinthShared::deleteRemoteInstance(this, attachment->id, [this](const ModrinthShared::Response& res) {
        if (!res.ok) {
            QMessageBox::warning(this, tr("Stop sharing"), res.error);
            return;
        }
        ModrinthShared::Attachment::remove(m_instance->instanceRoot());
        refresh();
    });
}

void ModrinthSharingPage::leaveShare()
{
    if (QMessageBox::question(this, tr("Leave shared pack"),
                              tr("Detach \"%1\" from the shared pack? The instance and its files stay, but it will no "
                                 "longer receive the owner's updates.")
                                  .arg(m_instance->name())) != QMessageBox::Yes)
        return;
    ModrinthShared::Attachment::remove(m_instance->instanceRoot());
    refresh();
}
