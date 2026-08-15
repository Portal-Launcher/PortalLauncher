// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Prism Launcher fork - Modrinth Shared Instances support
 *
 *  Management dialog for sharing an instance: sign-in, share, push updates,
 *  invite links / usernames, members, and leaving/stopping the share.
 */
#pragma once

#include <QComboBox>
#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>

class BaseInstance;

class ModrinthSharingDialog : public QDialog {
    Q_OBJECT
   public:
    ModrinthSharingDialog(QWidget* parent, BaseInstance* instance);

   private slots:
    void refresh();
    void signIn();
    void shareOrPush();
    void copyInviteLink();
    void inviteByUsername();
    void showMembers();
    void stopSharingOrLeave();
    void syncNow();

   private:
    QString currentConfigSpec() const;

    BaseInstance* m_instance;

    QLabel* m_accountLabel;
    QPushButton* m_signInButton;
    QLabel* m_stateLabel;
    QComboBox* m_configsCombo;
    QLineEdit* m_inviteLinkEdit;
    QPushButton* m_copyLinkButton;
    QPushButton* m_shareButton;
    QPushButton* m_inviteUserButton;
    QPushButton* m_membersButton;
    QPushButton* m_syncNowButton;
    QPushButton* m_stopButton;
};
