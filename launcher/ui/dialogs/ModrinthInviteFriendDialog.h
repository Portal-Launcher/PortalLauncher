// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Prism Launcher fork - Modrinth Shared Instances support
 *
 *  "Invite a Friend": pick one of your Modrinth friends and send them a
 *  shared-pack invite directly - they get a Modrinth notification, no link
 *  needed. Also offers a copy-to-clipboard invite link for people who are
 *  not on the friends list.
 */
#pragma once

#include <QDialog>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSet>
#include <QTimer>

class ModrinthInviteFriendDialog : public QDialog {
    Q_OBJECT
   public:
    ModrinthInviteFriendDialog(QWidget* parent, const QString& sharedInstanceId, const QString& instanceName);

   signals:
    /** At least one invite was delivered while the dialog was open. */
    void inviteSent();

   private slots:
    void rebuildList();
    void inviteSelected();
    void copyLinkInstead();

   private:
    void loadMembers();

    QString m_sharedInstanceId;
    QSet<QString> m_memberIds;   // friends who already have access
    QSet<QString> m_invitedIds;  // invited from this dialog
    bool m_friendsLoaded = false;       // first friends payload has arrived
    bool m_statusIsPlaceholder = false; // status label shows loading/empty text

    QListWidget* m_friendsList;
    QPushButton* m_inviteButton;
    QPushButton* m_copyLinkButton;
    QLabel* m_statusLabel;
    QTimer m_rebuildTimer;  // coalesces presence-update bursts so the list
                            // does not drop the user's selection mid-click
};
