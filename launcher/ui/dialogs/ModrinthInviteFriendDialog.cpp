// SPDX-License-Identifier: GPL-3.0-only
#include "ModrinthInviteFriendDialog.h"

#include <QApplication>
#include <QClipboard>
#include <QDialogButtonBox>
#include <QJsonArray>
#include <QJsonObject>
#include <QVBoxLayout>

#include "modplatform/modrinth/shared/ModrinthFriends.h"
#include "modplatform/modrinth/shared/ModrinthSharedApi.h"

namespace {
enum ItemRole { UserIdRole = Qt::UserRole, UsernameRole };
}

ModrinthInviteFriendDialog::ModrinthInviteFriendDialog(QWidget* parent,
                                                       const QString& sharedInstanceId,
                                                       const QString& instanceName)
    : QDialog(parent), m_sharedInstanceId(sharedInstanceId)
{
    setWindowTitle(tr("Invite a friend to \"%1\"").arg(instanceName));
    setMinimumWidth(440);

    auto* layout = new QVBoxLayout(this);
    auto* info = new QLabel(tr("Pick a Modrinth friend to invite to \"%1\". They get a Modrinth notification right "
                               "away and can accept it from this launcher or the Modrinth App - no link needed.")
                                .arg(instanceName),
                            this);
    info->setWordWrap(true);
    layout->addWidget(info);

    m_friendsList = new QListWidget(this);
    layout->addWidget(m_friendsList, 1);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setWordWrap(true);
    layout->addWidget(m_statusLabel);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    m_inviteButton = buttons->addButton(tr("Send invite"), QDialogButtonBox::ActionRole);
    m_inviteButton->setDefault(true);
    m_copyLinkButton = buttons->addButton(tr("Copy invite link instead"), QDialogButtonBox::ActionRole);
    m_copyLinkButton->setToolTip(
        tr("For people who are not on your friends list: creates a fresh invite link (7 days, 10 uses) and copies it "
           "to the clipboard so you can send it over any chat."));
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_inviteButton, &QPushButton::clicked, this, &ModrinthInviteFriendDialog::inviteSelected);
    connect(m_copyLinkButton, &QPushButton::clicked, this, &ModrinthInviteFriendDialog::copyLinkInstead);
    connect(m_friendsList, &QListWidget::itemDoubleClicked, this, &ModrinthInviteFriendDialog::inviteSelected);
    connect(ModrinthFriends::get(), &ModrinthFriends::changed, this, [this]() {
        m_friendsLoaded = true;
        rebuildList();
    });

    // Reuse the friends data the Friends panel already maintains.
    ModrinthFriends::get()->ensureConnected();
    ModrinthFriends::get()->refresh();
    rebuildList();
    loadMembers();
}

void ModrinthInviteFriendDialog::loadMembers()
{
    ModrinthShared::getMembers(this, m_sharedInstanceId, [this](const ModrinthShared::Response& res) {
        if (!res.ok) {
            // Not fatal, but without this list "already has access" greying
            // cannot work; say so instead of failing silently.
            m_statusLabel->setText(tr("Could not check who already has access - inviting may report an error."));
            return;
        }
        m_memberIds.clear();
        if (res.json.isObject()) {
            for (const auto& value : res.json.object().value("users").toArray())
                m_memberIds.insert(value.toObject().value("id").toString());
        } else if (res.json.isArray()) {
            for (const auto& value : res.json.array())
                m_memberIds.insert(value.toString());
        }
        rebuildList();
    });
}

void ModrinthInviteFriendDialog::rebuildList()
{
    const QString selected =
        m_friendsList->currentItem() ? m_friendsList->currentItem()->data(UserIdRole).toString() : QString();
    m_friendsList->clear();

    for (const auto& f : ModrinthFriends::get()->friends()) {
        if (!f.accepted)
            continue;
        QString label = f.username;
        if (m_invitedIds.contains(f.userId))
            label = tr("%1 (invited)").arg(f.username);
        else if (m_memberIds.contains(f.userId))
            label = tr("%1 (already has access)").arg(f.username);
        else if (f.online)
            label = f.playing.isEmpty() ? tr("%1 (online)").arg(f.username)
                                        : tr("%1 (playing %2)").arg(f.username, f.playing);
        auto* item = new QListWidgetItem(label, m_friendsList);
        item->setData(UserIdRole, f.userId);
        item->setData(UsernameRole, f.username);
        if (m_invitedIds.contains(f.userId) || m_memberIds.contains(f.userId))
            item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
        else if (f.userId == selected)
            m_friendsList->setCurrentItem(item);
    }

    if (m_friendsList->count() == 0) {
        m_statusLabel->setText(m_friendsLoaded
                                   ? tr("You have no Modrinth friends yet. Add some in the Friends panel, or use "
                                        "\"Copy invite link instead\" and send the link over any chat.")
                                   : tr("Loading your friends…"));
        m_statusIsPlaceholder = true;
    } else if (m_statusIsPlaceholder) {
        m_statusLabel->clear();
        m_statusIsPlaceholder = false;
    }
}

void ModrinthInviteFriendDialog::inviteSelected()
{
    auto* item = m_friendsList->currentItem();
    if (!item || !(item->flags() & Qt::ItemIsEnabled)) {
        m_statusLabel->setText(tr("Select a friend from the list first."));
        return;
    }
    const QString userId = item->data(UserIdRole).toString();
    const QString username = item->data(UsernameRole).toString();
    if (userId.isEmpty())
        return;

    m_inviteButton->setEnabled(false);
    m_statusLabel->setText(tr("Inviting %1…").arg(username));
    ModrinthShared::addMembers(this, m_sharedInstanceId, { userId },
                               [this, userId, username](const ModrinthShared::Response& res) {
                                   m_inviteButton->setEnabled(true);
                                   if (!res.ok) {
                                       m_statusLabel->setText(tr("Could not invite %1: %2").arg(username, res.error));
                                       return;
                                   }
                                   m_invitedIds.insert(userId);
                                   m_statusLabel->setText(
                                       tr("<b>Invite sent to %1</b> - they'll get a Modrinth notification and can "
                                          "accept it in this launcher or the Modrinth App.")
                                           .arg(username));
                                   emit inviteSent();
                                   rebuildList();
                               });
}

void ModrinthInviteFriendDialog::copyLinkInstead()
{
    m_copyLinkButton->setEnabled(false);
    ModrinthShared::createInvite(this, m_sharedInstanceId, 7 * 24 * 3600, 10,
                                 [this](const ModrinthShared::Response& res) {
                                     m_copyLinkButton->setEnabled(true);
                                     if (!res.ok) {
                                         m_statusLabel->setText(tr("Could not create an invite link: %1").arg(res.error));
                                         return;
                                     }
                                     const QString link =
                                         ModrinthShared::inviteLink(res.json.object().value("id").toString());
                                     QApplication::clipboard()->setText(link);
                                     m_statusLabel->setText(
                                         tr("<b>Invite link copied to your clipboard</b> (lasts 7 days or 10 uses) - "
                                            "send it to anyone, friend list or not."));
                                 });
}
