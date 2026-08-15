// SPDX-License-Identifier: GPL-3.0-only
#include "ModrinthSharingDialog.h"

#include <QApplication>
#include <QClipboard>
#include <QDialogButtonBox>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonObject>
#include <QListWidget>
#include <QMessageBox>
#include <QVBoxLayout>

#include "BaseInstance.h"
#include "modplatform/modrinth/shared/ModrinthSharedApi.h"
#include "modplatform/modrinth/shared/ModrinthSharedAttachment.h"
#include "modplatform/modrinth/shared/ModrinthSharedPublishTask.h"
#include "modplatform/modrinth/shared/ModrinthSharedSyncTask.h"
#include "modplatform/modrinth/shared/ModrinthSignInTask.h"
#include "ui/dialogs/ProgressDialog.h"

ModrinthSharingDialog::ModrinthSharingDialog(QWidget* parent, BaseInstance* instance) : QDialog(parent), m_instance(instance)
{
    setWindowTitle(tr("Share \"%1\" with friends").arg(instance->name()));
    setMinimumWidth(520);

    auto* layout = new QVBoxLayout(this);

    auto* accountRow = new QHBoxLayout();
    m_accountLabel = new QLabel(this);
    m_signInButton = new QPushButton(this);
    accountRow->addWidget(m_accountLabel, 1);
    accountRow->addWidget(m_signInButton);
    layout->addLayout(accountRow);

    m_stateLabel = new QLabel(this);
    m_stateLabel->setWordWrap(true);
    QFont bold = m_stateLabel->font();
    bold.setBold(true);
    m_stateLabel->setFont(bold);
    layout->addWidget(m_stateLabel);

    m_configsCombo = new QComboBox(this);
    m_configsCombo->addItem(tr("Don't share config files"), QString());
    m_configsCombo->addItem(tr("Share all config files"), QStringLiteral("all"));
    layout->addWidget(m_configsCombo);

    auto* linkRow = new QHBoxLayout();
    m_inviteLinkEdit = new QLineEdit(this);
    m_inviteLinkEdit->setReadOnly(true);
    m_inviteLinkEdit->setPlaceholderText(tr("Invite link appears here"));
    m_copyLinkButton = new QPushButton(tr("New invite link"), this);
    linkRow->addWidget(m_inviteLinkEdit, 1);
    linkRow->addWidget(m_copyLinkButton);
    layout->addLayout(linkRow);

    auto* buttonRow = new QHBoxLayout();
    m_shareButton = new QPushButton(this);
    m_inviteUserButton = new QPushButton(tr("Invite by username…"), this);
    m_membersButton = new QPushButton(tr("Members…"), this);
    m_syncNowButton = new QPushButton(tr("Check for updates now"), this);
    buttonRow->addWidget(m_shareButton);
    buttonRow->addWidget(m_inviteUserButton);
    buttonRow->addWidget(m_membersButton);
    buttonRow->addWidget(m_syncNowButton);
    layout->addLayout(buttonRow);

    auto* bottomRow = new QHBoxLayout();
    m_stopButton = new QPushButton(this);
    auto* closeBox = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(closeBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    bottomRow->addWidget(m_stopButton);
    bottomRow->addStretch(1);
    bottomRow->addWidget(closeBox);
    layout->addLayout(bottomRow);

    connect(m_signInButton, &QPushButton::clicked, this, &ModrinthSharingDialog::signIn);
    connect(m_shareButton, &QPushButton::clicked, this, &ModrinthSharingDialog::shareOrPush);
    connect(m_copyLinkButton, &QPushButton::clicked, this, &ModrinthSharingDialog::copyInviteLink);
    connect(m_inviteUserButton, &QPushButton::clicked, this, &ModrinthSharingDialog::inviteByUsername);
    connect(m_membersButton, &QPushButton::clicked, this, &ModrinthSharingDialog::showMembers);
    connect(m_stopButton, &QPushButton::clicked, this, &ModrinthSharingDialog::stopSharingOrLeave);
    connect(m_syncNowButton, &QPushButton::clicked, this, &ModrinthSharingDialog::syncNow);

    ModrinthShared::refreshSessionIfNeeded(this);
    refresh();
}

QString ModrinthSharingDialog::currentConfigSpec() const
{
    return m_configsCombo->currentData().toString();
}

void ModrinthSharingDialog::refresh()
{
    const bool signedIn = ModrinthShared::isSignedIn();
    m_accountLabel->setText(signedIn ? tr("Modrinth account: <b>%1</b>").arg(ModrinthShared::username())
                                     : tr("Not signed in to Modrinth."));
    m_signInButton->setText(signedIn ? tr("Sign out") : tr("Sign in with browser…"));

    auto attachment = ModrinthShared::Attachment::load(m_instance->instanceRoot());
    const bool shared = attachment.has_value();
    const bool owner = shared && attachment->isOwner();
    const bool member = shared && attachment->isMember();

    if (!shared) {
        m_stateLabel->setText(tr("This instance is not shared yet. Share it to invite friends — when you push updates, "
                                 "their copies update automatically before they play."));
        m_shareButton->setText(tr("Share this instance"));
    } else if (owner) {
        m_stateLabel->setText(tr("Sharing enabled — last pushed version: %1. Push after changing mods so friends get "
                                 "the update when they play.")
                                  .arg(attachment->appliedVersion < 0 ? tr("none") : QString::number(attachment->appliedVersion)));
        m_shareButton->setText(tr("Push update now"));
        const int idx = m_configsCombo->findData(attachment->configSpec);
        if (idx >= 0)
            m_configsCombo->setCurrentIndex(idx);
    } else {
        m_stateLabel->setText(tr("You joined this shared pack (version %1). It checks for the owner's updates every "
                                 "time you press Play.")
                                  .arg(attachment->appliedVersion < 0 ? tr("none") : QString::number(attachment->appliedVersion)));
    }

    m_shareButton->setVisible(!member);
    m_shareButton->setEnabled(signedIn);
    m_configsCombo->setVisible(!member);
    m_inviteLinkEdit->setVisible(owner || !shared);
    m_copyLinkButton->setVisible(owner || !shared);
    m_copyLinkButton->setEnabled(owner && signedIn);
    m_inviteUserButton->setVisible(!member);
    m_inviteUserButton->setEnabled(owner && signedIn);
    m_membersButton->setVisible(owner);
    m_membersButton->setEnabled(owner && signedIn);
    m_syncNowButton->setVisible(member);
    m_syncNowButton->setEnabled(signedIn);
    m_stopButton->setVisible(shared);
    m_stopButton->setText(owner ? tr("Stop sharing") : tr("Leave shared pack"));
}

void ModrinthSharingDialog::signIn()
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

void ModrinthSharingDialog::shareOrPush()
{
    const bool firstShare = !ModrinthShared::Attachment::load(m_instance->instanceRoot()).has_value();
    ModrinthSharedPublishTask task(m_instance, /*force*/ firstShare, currentConfigSpec());
    ProgressDialog dialog(this);
    dialog.setSkipButton(true, tr("Abort"));
    if (dialog.execWithTask(&task) == QDialog::Accepted) {
        if (firstShare) {
            copyInviteLink();
            QMessageBox::information(this, tr("Instance shared"),
                                     tr("Your instance is now shared! Send the invite link to your friends.\n\n"
                                        "They can accept it in this fork of Prism Launcher (Join Shared Pack) or in "
                                        "the official Modrinth App."));
        } else if (task.pushed()) {
            QMessageBox::information(this, tr("Update pushed"),
                                     tr("Version %1 is live. Friends get it automatically the next time they play.")
                                         .arg(task.pushedVersion()));
        }
    } else if (!task.failReason().isEmpty()) {
        QMessageBox::warning(this, tr("Sharing failed"), task.failReason());
    }
    refresh();
}

void ModrinthSharingDialog::copyInviteLink()
{
    auto attachment = ModrinthShared::Attachment::load(m_instance->instanceRoot());
    if (!attachment)
        return;
    m_copyLinkButton->setEnabled(false);
    ModrinthShared::createInvite(this, attachment->id, 7 * 24 * 3600, 10, [this](const ModrinthShared::Response& res) {
        m_copyLinkButton->setEnabled(true);
        if (!res.ok) {
            QMessageBox::warning(this, tr("Invite link"), res.error);
            return;
        }
        const QString link = ModrinthShared::inviteLink(res.json.object().value("id").toString());
        m_inviteLinkEdit->setText(link);
        QApplication::clipboard()->setText(link);
        m_inviteLinkEdit->selectAll();
        m_stateLabel->setText(tr("Invite link copied to your clipboard! It lasts 7 days or 10 uses."));
    });
}

void ModrinthSharingDialog::inviteByUsername()
{
    auto attachment = ModrinthShared::Attachment::load(m_instance->instanceRoot());
    if (!attachment)
        return;
    bool ok = false;
    const QString username =
        QInputDialog::getText(this, tr("Invite by username"),
                              tr("Modrinth username (not the Minecraft name) of the friend to invite:"),
                              QLineEdit::Normal, QString(), &ok);
    if (!ok || username.trimmed().isEmpty())
        return;

    ModrinthShared::lookupUserByName(this, username.trimmed(), [this, attachment](const ModrinthShared::Response& res) {
        if (!res.ok || !res.json.isObject()) {
            QMessageBox::warning(this, tr("Invite by username"),
                                 tr("No Modrinth user with that name was found."));
            return;
        }
        const auto user = res.json.object();
        const QString id = user.value("id").toString();
        const QString name = user.value("username").toString();
        ModrinthShared::addMembers(this, attachment->id, { id }, [this, name](const ModrinthShared::Response& addRes) {
            if (!addRes.ok) {
                QMessageBox::warning(this, tr("Invite by username"), addRes.error);
                return;
            }
            QMessageBox::information(this, tr("Invite sent"),
                                     tr("%1 has been invited. They will get a Modrinth notification and can accept "
                                        "it from this launcher or the Modrinth App.")
                                         .arg(name));
        });
    });
}

void ModrinthSharingDialog::showMembers()
{
    auto attachment = ModrinthShared::Attachment::load(m_instance->instanceRoot());
    if (!attachment)
        return;
    ModrinthShared::getMembers(this, attachment->id, [this, attachment](const ModrinthShared::Response& res) {
        if (!res.ok) {
            QMessageBox::warning(this, tr("Members"), res.error);
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
        ModrinthShared::getUsersByIds(this, ids, [this, attachment, ids](const ModrinthShared::Response& usersRes) {
            QHash<QString, QString> nameById;
            if (usersRes.ok) {
                for (const auto& value : usersRes.json.array()) {
                    const auto obj = value.toObject();
                    nameById[obj.value("id").toString()] = obj.value("username").toString();
                }
            }

            QDialog membersDialog(this);
            membersDialog.setWindowTitle(tr("Members of \"%1\"").arg(m_instance->name()));
            auto* layout = new QVBoxLayout(&membersDialog);
            auto* list = new QListWidget(&membersDialog);
            for (const auto& id : ids) {
                QString label = nameById.value(id, id);
                if (id == ModrinthShared::userId())
                    label += tr(" (you, owner)");
                auto* item = new QListWidgetItem(label, list);
                item->setData(Qt::UserRole, id);
            }
            layout->addWidget(list);
            auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &membersDialog);
            auto* removeButton = buttons->addButton(tr("Remove selected"), QDialogButtonBox::ActionRole);
            connect(buttons, &QDialogButtonBox::rejected, &membersDialog, &QDialog::reject);
            connect(removeButton, &QPushButton::clicked, &membersDialog, [this, list, attachment, &membersDialog]() {
                auto* item = list->currentItem();
                if (!item)
                    return;
                const QString id = item->data(Qt::UserRole).toString();
                if (id == ModrinthShared::userId())
                    return;
                ModrinthShared::removeMembers(this, attachment->id, { id }, [item](const ModrinthShared::Response& r) {
                    if (r.ok)
                        delete item;
                });
            });
            layout->addWidget(buttons);
            membersDialog.exec();
        });
    });
}

void ModrinthSharingDialog::stopSharingOrLeave()
{
    auto attachment = ModrinthShared::Attachment::load(m_instance->instanceRoot());
    if (!attachment)
        return;
    if (attachment->isOwner()) {
        if (QMessageBox::question(this, tr("Stop sharing"),
                                  tr("Stop sharing \"%1\"? Your friends will lose access to updates.")
                                      .arg(m_instance->name())) != QMessageBox::Yes)
            return;
        ModrinthShared::deleteRemoteInstance(this, attachment->id, [this](const ModrinthShared::Response& res) {
            if (!res.ok) {
                QMessageBox::warning(this, tr("Stop sharing"), res.error);
                return;
            }
            ModrinthShared::Attachment::remove(m_instance->instanceRoot());
            refresh();
        });
    } else {
        if (QMessageBox::question(this, tr("Leave shared pack"),
                                  tr("Detach \"%1\" from the shared pack? The instance and its files stay, but it "
                                     "will no longer receive the owner's updates.")
                                      .arg(m_instance->name())) != QMessageBox::Yes)
            return;
        ModrinthShared::Attachment::remove(m_instance->instanceRoot());
        refresh();
    }
}

void ModrinthSharingDialog::syncNow()
{
    ModrinthSharedSyncTask task(m_instance, /*softFail*/ false);
    ProgressDialog dialog(this);
    dialog.setSkipButton(true, tr("Abort"));
    if (dialog.execWithTask(&task) != QDialog::Accepted && !task.failReason().isEmpty())
        QMessageBox::warning(this, tr("Update check failed"), task.failReason());
    refresh();
}
