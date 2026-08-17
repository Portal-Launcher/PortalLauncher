// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Prism Launcher fork - Modrinth Shared Instances support
 *
 *  Dockable friends panel, in the spirit of the Modrinth App's friends list:
 *  who is online, what they are playing, requests, and adding friends.
 */
#pragma once

#include <QDockWidget>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTreeWidget>

class FriendsPanel : public QDockWidget {
    Q_OBJECT
   public:
    explicit FriendsPanel(QWidget* parent = nullptr);

   protected:
    void showEvent(QShowEvent* event) override;
    bool event(QEvent* event) override;

   private slots:
    void rebuild();
    void addFriendClicked();
    void showContextMenu(const QPoint& pos);
    void itemDoubleClicked(QTreeWidgetItem* item, int column);
    void reloadInvites();

   private:
    void acceptRequest(const QString& userId, const QString& username);
    void joinInvite(const QString& instanceId, const QString& instanceName);
    void launchLocalInstance(const QString& instanceId);

    struct Invite {
        QString instanceId;
        QString instanceName;
    };
    QList<Invite> m_invites;

    QLabel* m_headerLabel;
    QPushButton* m_signInButton;
    QTreeWidget* m_tree;
    QLineEdit* m_addEdit;
    QPushButton* m_addButton;
};
