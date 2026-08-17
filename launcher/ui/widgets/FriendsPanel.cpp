// SPDX-License-Identifier: GPL-3.0-only
#include "FriendsPanel.h"

#include <QApplication>
#include <QEvent>
#include <QHBoxLayout>
#include <QMenu>
#include <QMessageBox>
#include <QVBoxLayout>

#include "Application.h"
#include "BaseInstance.h"
#include "InstanceList.h"
#include "modplatform/modrinth/shared/ModrinthFriends.h"
#include "modplatform/modrinth/shared/ModrinthJoinFlow.h"
#include "modplatform/modrinth/shared/ModrinthSharedApi.h"
#include "modplatform/modrinth/shared/ModrinthSignInTask.h"
#include "ui/dialogs/ProgressDialog.h"

namespace {
enum ItemRole {
    UserIdRole = Qt::UserRole,
    UsernameRole,
    IncomingRole,
    AcceptedRole,
    InviteIdRole,
    InviteNameRole,
    // Set on friend rows when what they are playing matches a pending invite
    // or an installed instance (the presence socket only carries the name).
    FriendInviteIdRole,
    FriendInviteNameRole,
    LocalInstanceIdRole,
    LocalInstanceNameRole,
};
}

FriendsPanel::FriendsPanel(QWidget* parent) : QDockWidget(tr("Friends"), parent)
{
    setObjectName("friendsPanel");  // required for window-state save/restore
    auto* body = new QWidget(this);
    auto* layout = new QVBoxLayout(body);
    layout->setContentsMargins(6, 6, 6, 6);

    auto* headerRow = new QHBoxLayout();
    m_headerLabel = new QLabel(body);
    m_signInButton = new QPushButton(tr("Sign in…"), body);
    headerRow->addWidget(m_headerLabel, 1);
    headerRow->addWidget(m_signInButton);
    layout->addLayout(headerRow);

    m_tree = new QTreeWidget(body);
    m_tree->setHeaderHidden(true);
    m_tree->setRootIsDecorated(true);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    layout->addWidget(m_tree, 1);

    auto* addRow = new QHBoxLayout();
    m_addEdit = new QLineEdit(body);
    m_addEdit->setPlaceholderText(tr("Modrinth username"));
    m_addButton = new QPushButton(tr("Add friend"), body);
    addRow->addWidget(m_addEdit, 1);
    addRow->addWidget(m_addButton);
    layout->addLayout(addRow);

    setWidget(body);

    connect(m_signInButton, &QPushButton::clicked, this, [this]() {
        if (ModrinthShared::isSignedIn())
            return;
        ModrinthSignInTask task;
        ProgressDialog dialog(this);
        dialog.setSkipButton(true, tr("Cancel"));
        dialog.execWithTask(&task);
        ModrinthFriends::get()->ensureConnected();
        ModrinthFriends::get()->refresh();
        rebuild();
    });
    connect(m_addButton, &QPushButton::clicked, this, &FriendsPanel::addFriendClicked);
    connect(m_addEdit, &QLineEdit::returnPressed, this, &FriendsPanel::addFriendClicked);
    connect(m_tree, &QTreeWidget::customContextMenuRequested, this, &FriendsPanel::showContextMenu);
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this, &FriendsPanel::itemDoubleClicked);
    connect(ModrinthFriends::get(), &ModrinthFriends::changed, this, &FriendsPanel::rebuild);
    connect(ModrinthFriends::get(), &ModrinthFriends::inviteNotification, this, &FriendsPanel::reloadInvites);

    rebuild();
}

void FriendsPanel::showEvent(QShowEvent* event)
{
    QDockWidget::showEvent(event);
    if (ModrinthShared::isSignedIn()) {
        ModrinthFriends::get()->ensureConnected();
        ModrinthFriends::get()->refresh();
        reloadInvites();
    }
}

bool FriendsPanel::event(QEvent* event)
{
    // The dock can keep rendering with the previous palette when the theme
    // changes at runtime (launcher theme switch, or Windows flipping between
    // light and dark while the System theme is active). Re-polish the whole
    // panel so it always follows.
    if (event->type() == QEvent::ApplicationPaletteChange || event->type() == QEvent::ThemeChange) {
        auto* style = QApplication::style();
        const auto children = findChildren<QWidget*>();
        for (auto* child : children) {
            style->unpolish(child);
            style->polish(child);
            child->update();
        }
        update();
    }
    return QDockWidget::event(event);
}

void FriendsPanel::reloadInvites()
{
    ModrinthShared::fetchPendingInvites(this, [this](const QList<ModrinthShared::PendingInvite>& invites) {
        m_invites.clear();
        for (const auto& invite : invites)
            m_invites.append({ invite.instanceId, invite.instanceName });
        rebuild();
    });
}

void FriendsPanel::rebuild()
{
    const bool signedIn = ModrinthShared::isSignedIn();
    m_signInButton->setVisible(!signedIn);
    m_addEdit->setEnabled(signedIn);
    m_addButton->setEnabled(signedIn);

    m_tree->clear();
    if (!signedIn) {
        m_headerLabel->setText(tr("Sign in to see your Modrinth friends."));
        return;
    }

    const auto friends = ModrinthFriends::get()->friends();
    int onlineCount = 0;

    auto makeSection = [this](const QString& title) {
        auto* section = new QTreeWidgetItem(m_tree, { title });
        section->setFlags(Qt::ItemIsEnabled);
        auto font = section->font(0);
        font.setBold(true);
        section->setFont(0, font);
        section->setExpanded(true);
        return section;
    };

    QTreeWidgetItem* invites = nullptr;
    QTreeWidgetItem* online = nullptr;
    QTreeWidgetItem* offline = nullptr;
    QTreeWidgetItem* requests = nullptr;
    QTreeWidgetItem* sent = nullptr;

    for (const auto& invite : m_invites) {
        if (!invites)
            invites = makeSection(tr("Modpack invites"));
        auto* item = new QTreeWidgetItem(invites, { tr("%1 (double-click to join)").arg(invite.instanceName) });
        item->setData(0, InviteIdRole, invite.instanceId);
        item->setData(0, InviteNameRole, invite.instanceName);
    }

    for (const auto& f : friends) {
        QTreeWidgetItem* parent = nullptr;
        QString label = f.username;
        QString friendInviteId, friendInviteName;
        QString localInstanceId, localInstanceName;
        if (!f.accepted) {
            if (f.incoming) {
                if (!requests)
                    requests = makeSection(tr("Friend requests"));
                parent = requests;
                label = tr("%1 (double-click to accept)").arg(f.username);
            } else {
                if (!sent)
                    sent = makeSection(tr("Sent requests"));
                parent = sent;
                label = tr("%1 (pending)").arg(f.username);
            }
        } else if (f.online) {
            if (!online)
                online = makeSection(tr("Online"));
            parent = online;
            onlineCount++;
            label = f.playing.isEmpty() ? f.username : tr("%1 - playing %2").arg(f.username, f.playing);
            if (!f.playing.isEmpty()) {
                // The presence socket only tells us the pack's name, so match
                // pending invites and installed instances by that name.
                const QString playing = f.playing.trimmed();
                for (const auto& invite : m_invites) {
                    if (QString::compare(invite.instanceName.trimmed(), playing, Qt::CaseInsensitive) == 0) {
                        friendInviteId = invite.instanceId;
                        friendInviteName = invite.instanceName;
                        break;
                    }
                }
                auto* instances = APPLICATION->instances();
                for (int i = 0; i < instances->count(); i++) {
                    auto* inst = instances->at(i);
                    if (QString::compare(inst->name().trimmed(), playing, Qt::CaseInsensitive) == 0) {
                        localInstanceId = inst->id();
                        localInstanceName = inst->name();
                        break;
                    }
                }
                if (!localInstanceId.isEmpty()) {
                    // Already installed: launching beats joining.
                    friendInviteId.clear();
                    friendInviteName.clear();
                    label = tr("%1 - playing %2 (double-click to play too)").arg(f.username, f.playing);
                } else if (!friendInviteId.isEmpty()) {
                    label = tr("%1 - playing %2 (double-click to join)").arg(f.username, f.playing);
                }
            }
        } else {
            if (!offline)
                offline = makeSection(tr("Offline"));
            parent = offline;
        }
        auto* item = new QTreeWidgetItem(parent, { label });
        item->setData(0, UserIdRole, f.userId);
        item->setData(0, UsernameRole, f.username);
        item->setData(0, IncomingRole, f.incoming);
        item->setData(0, AcceptedRole, f.accepted);
        if (!friendInviteId.isEmpty()) {
            item->setData(0, FriendInviteIdRole, friendInviteId);
            item->setData(0, FriendInviteNameRole, friendInviteName);
            item->setToolTip(0, tr("You have a pending invite for \"%1\" - double-click to join and play along.")
                                    .arg(friendInviteName));
        }
        if (!localInstanceId.isEmpty()) {
            item->setData(0, LocalInstanceIdRole, localInstanceId);
            item->setData(0, LocalInstanceNameRole, localInstanceName);
            item->setToolTip(0, tr("You have \"%1\" installed - double-click to launch it and play along.")
                                    .arg(localInstanceName));
        }
        if (f.online)
            item->setForeground(0, QBrush(QColor(0x1b, 0xd9, 0x6a)));
    }

    // Keep section order: invites and requests first, then online, offline, sent.
    QList<QTreeWidgetItem*> order;
    for (auto* section : { invites, requests, online, offline, sent })
        if (section)
            order.append(section);
    for (int i = 0; i < order.size(); i++) {
        m_tree->takeTopLevelItem(m_tree->indexOfTopLevelItem(order[i]));
        m_tree->insertTopLevelItem(i, order[i]);
        order[i]->setExpanded(true);
    }

    m_headerLabel->setText(tr("<b>%1</b> - %2 online").arg(ModrinthShared::username()).arg(onlineCount));
}

void FriendsPanel::addFriendClicked()
{
    const QString name = m_addEdit->text().trimmed();
    if (name.isEmpty())
        return;
    m_addButton->setEnabled(false);
    ModrinthFriends::get()->addFriend(name, [this](const QString& error) {
        m_addButton->setEnabled(true);
        if (!error.isEmpty()) {
            QMessageBox::warning(this, tr("Add friend"), error);
            return;
        }
        m_addEdit->clear();
    });
}

void FriendsPanel::acceptRequest(const QString& userId, const QString& username)
{
    ModrinthFriends::get()->addFriend(userId, [this, username](const QString& error) {
        if (!error.isEmpty())
            QMessageBox::warning(this, tr("Accept request"), error);
    });
    Q_UNUSED(username);
}

void FriendsPanel::itemDoubleClicked(QTreeWidgetItem* item, int)
{
    if (!item)
        return;
    const QString inviteId = item->data(0, InviteIdRole).toString();
    if (!inviteId.isEmpty()) {
        joinInvite(inviteId, item->data(0, InviteNameRole).toString());
        return;
    }
    if (item->data(0, UserIdRole).toString().isEmpty())
        return;
    if (item->data(0, IncomingRole).toBool()) {
        acceptRequest(item->data(0, UserIdRole).toString(), item->data(0, UsernameRole).toString());
        return;
    }
    // Friend rows: join or launch what they are playing, if we matched it.
    const QString localInstanceId = item->data(0, LocalInstanceIdRole).toString();
    if (!localInstanceId.isEmpty()) {
        launchLocalInstance(localInstanceId);
        return;
    }
    const QString friendInviteId = item->data(0, FriendInviteIdRole).toString();
    if (!friendInviteId.isEmpty())
        joinInvite(friendInviteId, item->data(0, FriendInviteNameRole).toString());
}

void FriendsPanel::launchLocalInstance(const QString& instanceId)
{
    auto* instance = APPLICATION->instances()->getInstanceById(instanceId);
    if (!instance)
        return;
    if (instance->isRunning()) {
        QMessageBox::information(this, tr("Already running"), tr("\"%1\" is already running.").arg(instance->name()));
        return;
    }
    APPLICATION->launch(instance);
}

void FriendsPanel::joinInvite(const QString& instanceId, const QString& instanceName)
{
    if (QMessageBox::question(this, tr("Join \"%1\"?").arg(instanceName),
                              tr("You are about to install \"%1\" from a shared instance.\n\nShared instances are not "
                                 "reviewed by Modrinth - only accept invites from people you trust.")
                                  .arg(instanceName)) != QMessageBox::Yes)
        return;
    ModrinthShared::acceptPendingInvite(this, instanceId, [this, instanceId, instanceName](const ModrinthShared::Response& res) {
        if (!res.ok && res.status != 404) {
            QMessageBox::warning(this, tr("Join failed"), res.error);
            return;
        }
        ModrinthShared::runJoinFlow(this, instanceId, instanceName,
                                    [this, instanceName](bool joined, const QString& message) {
                                        if (!joined) {
                                            QMessageBox::warning(this, tr("Join failed"), message);
                                        } else {
                                            QMessageBox::information(
                                                this, tr("Joined!"),
                                                tr("\"%1\" is now in your instance list. It checks for the owner's "
                                                   "updates every time you press Play.")
                                                    .arg(instanceName));
                                        }
                                        reloadInvites();
                                    });
    });
}

void FriendsPanel::showContextMenu(const QPoint& pos)
{
    auto* item = m_tree->itemAt(pos);
    if (!item)
        return;
    const QString inviteId = item->data(0, InviteIdRole).toString();
    if (!inviteId.isEmpty()) {
        const QString inviteName = item->data(0, InviteNameRole).toString();
        QMenu inviteMenu(this);
        inviteMenu.addAction(tr("Join"), this, [this, inviteId, inviteName]() { joinInvite(inviteId, inviteName); });
        inviteMenu.addAction(tr("Decline"), this, [this, inviteId]() {
            ModrinthShared::declinePendingInvite(this, inviteId,
                                                 [this](const ModrinthShared::Response&) { reloadInvites(); });
        });
        inviteMenu.exec(m_tree->viewport()->mapToGlobal(pos));
        return;
    }
    const QString userId = item->data(0, UserIdRole).toString();
    if (userId.isEmpty())
        return;
    const QString username = item->data(0, UsernameRole).toString();
    const bool incoming = item->data(0, IncomingRole).toBool();
    const bool accepted = item->data(0, AcceptedRole).toBool();

    QMenu menu(this);
    if (incoming) {
        menu.addAction(tr("Accept request"), this, [this, userId, username]() { acceptRequest(userId, username); });
        menu.addAction(tr("Ignore request"), this, [userId]() {
            ModrinthFriends::get()->removeFriend(userId, [](const QString&) {});
        });
    } else if (accepted) {
        const QString localInstanceId = item->data(0, LocalInstanceIdRole).toString();
        const QString localInstanceName = item->data(0, LocalInstanceNameRole).toString();
        const QString friendInviteId = item->data(0, FriendInviteIdRole).toString();
        const QString friendInviteName = item->data(0, FriendInviteNameRole).toString();
        if (!localInstanceId.isEmpty()) {
            menu.addAction(tr("Launch \"%1\" and play along").arg(localInstanceName), this,
                           [this, localInstanceId]() { launchLocalInstance(localInstanceId); });
            menu.addSeparator();
        } else if (!friendInviteId.isEmpty()) {
            menu.addAction(tr("Join their pack \"%1\"").arg(friendInviteName), this,
                           [this, friendInviteId, friendInviteName]() { joinInvite(friendInviteId, friendInviteName); });
            menu.addSeparator();
        }
        menu.addAction(tr("Remove friend"), this, [this, userId, username]() {
            if (QMessageBox::question(this, tr("Remove friend"), tr("Remove %1 from your friends?").arg(username)) ==
                QMessageBox::Yes)
                ModrinthFriends::get()->removeFriend(userId, [](const QString&) {});
        });
    } else {
        menu.addAction(tr("Cancel request"), this, [userId]() {
            ModrinthFriends::get()->removeFriend(userId, [](const QString&) {});
        });
    }
    menu.exec(m_tree->viewport()->mapToGlobal(pos));
}
