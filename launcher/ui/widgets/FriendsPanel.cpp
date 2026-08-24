// SPDX-License-Identifier: GPL-3.0-only
#include "FriendsPanel.h"

#include <QAction>
#include <QApplication>
#include <QEvent>
#include <QFont>
#include <QHBoxLayout>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include <algorithm>

#include "Application.h"
#include "BaseInstance.h"
#include "InstanceList.h"
#include "modplatform/modrinth/shared/ModrinthFriends.h"
#include "modplatform/modrinth/shared/ModrinthJoinFlow.h"
#include "modplatform/modrinth/shared/ModrinthSharedApi.h"
#include "modplatform/modrinth/shared/ModrinthSharedAttachment.h"
#include "modplatform/modrinth/shared/ModrinthSignInTask.h"
#include "ui/dialogs/ProgressDialog.h"

namespace {
enum ItemRole {
    UserIdRole = Qt::UserRole,
    UsernameRole,
    IncomingRole,
    AcceptedRole,
    InviteIdRole,  // the shared INSTANCE id of a pending invite row
    InviteNameRole,
    // Set on friend rows when what they are playing matches a pending invite
    // or an installed instance (the presence socket only carries the name).
    FriendInviteIdRole,
    FriendInviteNameRole,
    LocalInstanceIdRole,
    LocalInstanceNameRole,
};

/** Overlay a count bubble on the Friends icon so pending invites and requests
 *  are visible even while the panel is closed. Fixed red with white text reads
 *  fine on light and dark toolbars alike (the universal badge convention). */
QIcon badgedIcon(const QIcon& base, int count)
{
    QPixmap pm = base.pixmap(32, 32);
    if (pm.isNull()) {
        pm = QPixmap(32, 32);
        pm.fill(Qt::transparent);
    }
    QPainter painter(&pm);
    painter.setRenderHint(QPainter::Antialiasing);
    const int d = pm.width() * 7 / 16;  // pixmap may be DPR-scaled; derive from it
    const QRect bubble(pm.width() - d, 0, d, d);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0xe5, 0x48, 0x4d));
    painter.drawEllipse(bubble);
    QFont font = painter.font();
    font.setBold(true);
    font.setPixelSize(d * 7 / 10);
    painter.setFont(font);
    painter.setPen(Qt::white);
    painter.drawText(bubble, Qt::AlignCenter, count > 9 ? QStringLiteral("9+") : QString::number(count));
    painter.end();
    return QIcon(pm);
}
}  // namespace

FriendsPanel::FriendsPanel(QWidget* parent) : QDockWidget(tr("Friends"), parent)
{
    setObjectName("friendsPanel");  // required for window-state save/restore
    // No close X or float button: the Friends toolbar button is the one way
    // to show and hide the panel, so it can never get lost as a stray window.
    setFeatures(QDockWidget::DockWidgetMovable);
    // Qt greys out toggleViewAction() for docks that are not closable, so this
    // panel needs its own action or the Friends button cannot be clicked.
    m_viewAction = new QAction(tr("Friends"), this);
    m_viewAction->setCheckable(true);
    m_viewAction->setIcon(QIcon::fromTheme("accounts"));
    m_viewAction->setToolTip(tr("Show your Modrinth friends, who is online, and what they are playing."));
    connect(m_viewAction, &QAction::toggled, this, [this](bool checked) {
        if (isVisible() != checked)
            setVisible(checked);
    });
    connect(this, &QDockWidget::visibilityChanged, this, [this](bool visible) {
        QSignalBlocker blocker(m_viewAction);  // do not bounce back into setVisible
        m_viewAction->setChecked(visible);
    });
    // Window states saved before the popout button was removed can restore the
    // panel floating (possibly offscreen, looking like the toggle is broken);
    // snap it back into the dock whenever anything tries to float it.
    connect(this, &QDockWidget::topLevelChanged, this, [this](bool floating) {
        if (floating)
            QMetaObject::invokeMethod(this, [this] { setFloating(false); }, Qt::QueuedConnection);
    });
    auto* body = new QWidget(this);
    auto* layout = new QVBoxLayout(body);
    layout->setContentsMargins(6, 6, 6, 6);

    auto* headerRow = new QHBoxLayout();
    m_headerLabel = new QLabel(body);
    m_refreshButton = new QToolButton(body);
    m_refreshButton->setIcon(QIcon::fromTheme("refresh"));
    m_refreshButton->setAutoRaise(true);
    m_refreshButton->setToolTip(tr("Refresh the friend list and invites"));
    m_menuButton = new QToolButton(body);
    m_menuButton->setIcon(QIcon::fromTheme("settings"));
    m_menuButton->setAutoRaise(true);
    m_menuButton->setPopupMode(QToolButton::InstantPopup);
    m_menuButton->setToolTip(tr("Friends options"));
    m_signInButton = new QPushButton(tr("Sign in…"), body);
    headerRow->addWidget(m_headerLabel, 1);
    headerRow->addWidget(m_refreshButton);
    headerRow->addWidget(m_menuButton);
    headerRow->addWidget(m_signInButton);
    layout->addLayout(headerRow);

    auto* optionsMenu = new QMenu(m_menuButton);
    auto* presenceAction = optionsMenu->addAction(tr("Share what I'm playing"));
    presenceAction->setCheckable(true);
    presenceAction->setToolTip(tr("When off, friends see you online but not which pack you are playing."));
    connect(presenceAction, &QAction::toggled, this, [](bool checked) {
        if (APPLICATION->settings()->get("ModrinthPresenceEnabled").toBool() == checked)
            return;
        APPLICATION->settings()->set("ModrinthPresenceEnabled", checked);
        ModrinthFriends::get()->presenceSettingChanged();
    });
    connect(optionsMenu, &QMenu::aboutToShow, this, [presenceAction]() {
        QSignalBlocker blocker(presenceAction);
        presenceAction->setChecked(APPLICATION->settings()->get("ModrinthPresenceEnabled").toBool());
    });
    optionsMenu->addSeparator();
    optionsMenu->addAction(tr("Sign out"), this, &FriendsPanel::signOutClicked);
    m_menuButton->setMenu(optionsMenu);

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
        // storeSession() notifies ModrinthFriends, which refreshes and
        // reconnects on its own; just repaint.
        rebuild();
    });
    connect(m_refreshButton, &QToolButton::clicked, this, [this]() {
        if (!ModrinthShared::isSignedIn())
            return;
        ModrinthFriends::get()->ensureConnected();
        ModrinthFriends::get()->refresh();
        reloadInvites();
    });
    connect(m_addButton, &QPushButton::clicked, this, &FriendsPanel::addFriendClicked);
    connect(m_addEdit, &QLineEdit::returnPressed, this, &FriendsPanel::addFriendClicked);
    connect(m_tree, &QTreeWidget::customContextMenuRequested, this, &FriendsPanel::showContextMenu);
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this, &FriendsPanel::itemDoubleClicked);
    // Presence updates can arrive in bursts (one message per friend); coalesce
    // them so the tree rebuilds once instead of flickering per message.
    m_rebuildTimer.setSingleShot(true);
    m_rebuildTimer.setInterval(150);
    connect(&m_rebuildTimer, &QTimer::timeout, this, &FriendsPanel::rebuild);
    connect(ModrinthFriends::get(), &ModrinthFriends::changed, this, &FriendsPanel::scheduleRebuild);
    connect(ModrinthFriends::get(), &ModrinthFriends::inviteNotification, this, &FriendsPanel::reloadInvites);

    rebuild();
}

void FriendsPanel::scheduleRebuild()
{
    m_rebuildTimer.start();
}

void FriendsPanel::showEvent(QShowEvent* event)
{
    QDockWidget::showEvent(event);
    if (isFloating())
        setFloating(false);  // stray float state from an old session
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
        // item colors (online green) are chosen per theme, so re-derive them
        rebuild();
    }
    return QDockWidget::event(event);
}

void FriendsPanel::reloadInvites()
{
    ModrinthShared::fetchPendingInvites(this, [this](bool ok, const QList<ModrinthShared::PendingInvite>& invites) {
        if (!ok) {
            // Network blip: keep the invites we already know about instead of
            // making the section vanish as if they were gone.
            return;
        }
        m_invites.clear();
        for (const auto& invite : invites)
            m_invites.append({ invite.instanceId, invite.instanceName });
        rebuild();
    });
}

void FriendsPanel::updateViewAction(int attentionCount)
{
    if (attentionCount == m_lastAttentionCount)
        return;
    m_lastAttentionCount = attentionCount;
    const QIcon base = QIcon::fromTheme("accounts");
    if (attentionCount > 0) {
        m_viewAction->setIcon(badgedIcon(base, attentionCount));
        m_viewAction->setText(tr("Friends (%1)").arg(attentionCount));
    } else {
        m_viewAction->setIcon(base);
        m_viewAction->setText(tr("Friends"));
    }
}

void FriendsPanel::signOutClicked()
{
    if (!ModrinthShared::isSignedIn())
        return;
    if (QMessageBox::question(this, tr("Sign out"),
                              tr("Sign out of Modrinth? Shared packs stop syncing and your friends list goes away "
                                 "until you sign in again.")) != QMessageBox::Yes)
        return;
    ModrinthShared::clearSession();  // ModrinthFriends resets itself off this
}

void FriendsPanel::rebuild()
{
    auto* service = ModrinthFriends::get();
    const bool signedIn = ModrinthShared::isSignedIn();
    m_signInButton->setVisible(!signedIn);
    m_refreshButton->setVisible(signedIn);
    m_menuButton->setVisible(signedIn);
    m_addEdit->setEnabled(signedIn);
    m_addButton->setEnabled(signedIn);

    const int scrollPos = m_tree->verticalScrollBar() ? m_tree->verticalScrollBar()->value() : 0;
    m_tree->clear();
    if (!signedIn) {
        m_headerLabel->setText(service->authFailed() ? tr("Your Modrinth session expired - sign in again.")
                                                     : tr("Sign in to see your Modrinth friends."));
        updateViewAction(0);
        return;
    }

    auto friends = service->friends();
    int onlineCount = 0;
    int incomingCount = 0;

    // Stable, meaningful order: online friends cluster by what they play (so a
    // pack several friends share reads as one group), everyone else is alpha.
    std::sort(friends.begin(), friends.end(), [](const ModrinthFriends::Friend& a, const ModrinthFriends::Friend& b) {
        if (a.online != b.online)
            return a.online;
        if (a.online) {
            const bool aPlays = !a.playing.isEmpty();
            const bool bPlays = !b.playing.isEmpty();
            if (aPlays != bPlays)
                return aPlays;
            const int byPack = QString::compare(a.playing, b.playing, Qt::CaseInsensitive);
            if (byPack != 0)
                return byPack < 0;
        }
        return QString::compare(a.username, b.username, Qt::CaseInsensitive) < 0;
    });

    // The presence socket only carries pack names; build the lookup tables
    // once instead of scanning every instance per online friend.
    QHash<QString, QPair<QString, QString>> inviteByName;   // lowercased name -> (id, name)
    for (const auto& invite : m_invites)
        inviteByName.insert(invite.instanceName.trimmed().toLower(), { invite.instanceId, invite.instanceName });
    QHash<QString, QPair<QString, QString>> instanceByName;  // lowercased name -> (id, name)
    auto* instances = APPLICATION->instances();
    for (int i = 0; i < instances->count(); i++) {
        auto* inst = instances->at(i);
        instanceByName.insert(inst->name().trimmed().toLower(), { inst->id(), inst->name() });
    }

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
                incomingCount++;
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
                const QString playing = f.playing.trimmed().toLower();
                if (const auto invite = inviteByName.constFind(playing); invite != inviteByName.constEnd()) {
                    friendInviteId = invite->first;
                    friendInviteName = invite->second;
                }
                if (const auto inst = instanceByName.constFind(playing); inst != instanceByName.constEnd()) {
                    localInstanceId = inst->first;
                    localInstanceName = inst->second;
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
            if (!offline) {
                // With the presence socket down we cannot tell who is online,
                // so do not claim everyone is offline.
                offline = makeSection(service->socketConnected() ? tr("Offline") : tr("Friends"));
            }
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
                                    .arg(friendInviteName.toHtmlEscaped()));
        }
        if (!localInstanceId.isEmpty()) {
            item->setData(0, LocalInstanceIdRole, localInstanceId);
            item->setData(0, LocalInstanceNameRole, localInstanceName);
            item->setToolTip(0, tr("You have \"%1\" installed - double-click to launch it and play along.")
                                    .arg(localInstanceName.toHtmlEscaped()));
        }
        if (f.online) {
            // Online green, picked per theme so it stays readable on light and
            // dark backgrounds alike.
            const bool darkBase = m_tree->palette().color(QPalette::Base).lightness() < 128;
            item->setForeground(0, QBrush(darkBase ? QColor(0x1b, 0xd9, 0x6a) : QColor(0x0f, 0x7d, 0x3e)));
        }
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

    if (order.isEmpty()) {
        auto* empty = new QTreeWidgetItem(m_tree, { tr("No friends yet - add one below.") });
        empty->setFlags(Qt::NoItemFlags);
    }

    if (m_tree->verticalScrollBar())
        m_tree->verticalScrollBar()->setValue(scrollPos);

    if (!service->lastError().isEmpty())
        m_headerLabel->setText(service->lastError().toHtmlEscaped());
    else if (!service->socketConnected())
        m_headerLabel->setText(tr("<b>%1</b> - reconnecting…").arg(ModrinthShared::username().toHtmlEscaped()));
    else
        m_headerLabel->setText(
            tr("<b>%1</b> - %2 online").arg(ModrinthShared::username().toHtmlEscaped()).arg(onlineCount));

    updateViewAction(m_invites.size() + incomingCount);
}

void FriendsPanel::addFriendClicked()
{
    const QString name = m_addEdit->text().trimmed();
    if (name.isEmpty())
        return;
    m_addButton->setEnabled(false);
    ModrinthFriends::get()->addFriend(this, name, [this](const QString& error) {
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
    ModrinthFriends::get()->addFriend(
        this, userId,
        [this](const QString& error) {
            if (!error.isEmpty())
                QMessageBox::warning(this, tr("Accept request"), error);
        },
        username);
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
    // Plain text on purpose: the pack name is remote-controlled and must never
    // be able to restyle the dialog that carries the trust warning.
    QMessageBox confirm(QMessageBox::Question, tr("Join \"%1\"?").arg(instanceName),
                        tr("You are about to install \"%1\" from a shared instance.\n\nShared instances are not "
                           "reviewed by Modrinth - only accept invites from people you trust.")
                            .arg(instanceName),
                        QMessageBox::Yes | QMessageBox::No, this);
    confirm.setTextFormat(Qt::PlainText);
    if (confirm.exec() != QMessageBox::Yes)
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
            ModrinthShared::declinePendingInvite(this, inviteId, [this](const ModrinthShared::Response& res) {
                if (!res.ok)
                    QMessageBox::warning(this, tr("Decline invite"), res.error);
                reloadInvites();
            });
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
        menu.addAction(tr("Ignore request"), this, [this, userId]() {
            ModrinthFriends::get()->removeFriend(this, userId, [this](const QString& error) {
                if (!error.isEmpty())
                    QMessageBox::warning(this, tr("Ignore request"), error);
            });
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
        // Invite this friend straight into a pack you own - the same call the
        // Sharing page's invite dialog makes, minus the detour.
        QMenu* inviteToMenu = nullptr;
        auto* instances = APPLICATION->instances();
        for (int i = 0; i < instances->count(); i++) {
            auto* inst = instances->at(i);
            auto att = ModrinthShared::Attachment::load(inst->instanceRoot());
            if (!att || !att->isOwner())
                continue;
            if (!inviteToMenu)
                inviteToMenu = menu.addMenu(tr("Invite to"));
            const QString sharedId = att->id;
            const QString packName = inst->name();
            inviteToMenu->addAction(packName, this, [this, sharedId, userId, username, packName]() {
                ModrinthShared::addMembers(this, sharedId, { userId },
                                           [this, username, packName](const ModrinthShared::Response& res) {
                                               if (!res.ok) {
                                                   QMessageBox::warning(
                                                       this, tr("Invite"),
                                                       tr("Could not invite %1: %2").arg(username, res.error));
                                                   return;
                                               }
                                               QMessageBox::information(
                                                   this, tr("Invite sent"),
                                                   tr("%1 was invited to \"%2\" - they'll get a Modrinth "
                                                      "notification and can accept it from their launcher.")
                                                       .arg(username, packName));
                                           });
            });
        }
        if (inviteToMenu)
            menu.addSeparator();
        menu.addAction(tr("Remove friend"), this, [this, userId, username]() {
            if (QMessageBox::question(this, tr("Remove friend"), tr("Remove %1 from your friends?").arg(username)) ==
                QMessageBox::Yes)
                ModrinthFriends::get()->removeFriend(this, userId, [this](const QString& error) {
                    if (!error.isEmpty())
                        QMessageBox::warning(this, tr("Remove friend"), error);
                });
        });
    } else {
        menu.addAction(tr("Cancel request"), this, [this, userId]() {
            ModrinthFriends::get()->removeFriend(this, userId, [this](const QString& error) {
                if (!error.isEmpty())
                    QMessageBox::warning(this, tr("Cancel request"), error);
            });
        });
    }
    menu.exec(m_tree->viewport()->mapToGlobal(pos));
}
