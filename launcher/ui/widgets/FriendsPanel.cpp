// SPDX-License-Identifier: GPL-3.0-only
#include "FriendsPanel.h"

#include <QHBoxLayout>
#include <QMenu>
#include <QMessageBox>
#include <QVBoxLayout>

#include "modplatform/modrinth/shared/ModrinthFriends.h"
#include "modplatform/modrinth/shared/ModrinthSharedApi.h"
#include "modplatform/modrinth/shared/ModrinthSignInTask.h"
#include "ui/dialogs/ProgressDialog.h"

namespace {
enum ItemRole { UserIdRole = Qt::UserRole, UsernameRole, IncomingRole, AcceptedRole };
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

    rebuild();
}

void FriendsPanel::showEvent(QShowEvent* event)
{
    QDockWidget::showEvent(event);
    if (ModrinthShared::isSignedIn()) {
        ModrinthFriends::get()->ensureConnected();
        ModrinthFriends::get()->refresh();
    }
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

    QTreeWidgetItem* online = nullptr;
    QTreeWidgetItem* offline = nullptr;
    QTreeWidgetItem* requests = nullptr;
    QTreeWidgetItem* sent = nullptr;

    for (const auto& f : friends) {
        QTreeWidgetItem* parent = nullptr;
        QString label = f.username;
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
        if (f.online)
            item->setForeground(0, QBrush(QColor(0x1b, 0xd9, 0x6a)));
    }

    // Keep section order: requests first, then online, offline, sent.
    QList<QTreeWidgetItem*> order;
    for (auto* section : { requests, online, offline, sent })
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
    if (!item || item->data(0, UserIdRole).toString().isEmpty())
        return;
    if (item->data(0, IncomingRole).toBool())
        acceptRequest(item->data(0, UserIdRole).toString(), item->data(0, UsernameRole).toString());
}

void FriendsPanel::showContextMenu(const QPoint& pos)
{
    auto* item = m_tree->itemAt(pos);
    if (!item)
        return;
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
