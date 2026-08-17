// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Prism Launcher fork - Modrinth Shared Instances support
 *
 *  The "Sharing" page of the instance window: share the instance, push
 *  updates, manage invites and members, or manage a joined shared pack.
 */
#pragma once

#include <QCheckBox>
#include <QComboBox>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QWidget>

#include "ui/pages/BasePage.h"

class BaseInstance;

class ModrinthSharingPage : public QWidget, public BasePage {
    Q_OBJECT

   public:
    explicit ModrinthSharingPage(BaseInstance* inst, QWidget* parent = nullptr);
    ~ModrinthSharingPage() override = default;

    QString displayName() const override { return tr("Sharing"); }
    QIcon icon() const override
    {
        auto pageIcon = QIcon::fromTheme("accounts");
        if (pageIcon.isNull())
            pageIcon = QIcon::fromTheme("server");
        return pageIcon;
    }
    QString id() const override { return "sharing"; }
    bool apply() override { return true; }
    void retranslate() override {}
    void openedImpl() override;

   protected:
    bool event(QEvent* event) override;

   private slots:
    void refresh();
    void signInOrOut();
    void shareInstance();
    void pushUpdate();
    void newInviteLink();
    void inviteByUsername();
    void removeSelectedMember();
    void toggleAutoPush(bool checked);
    void configSpecChanged(int index);
    void syncNow();
    void stopSharing();
    void leaveShare();

   private:
    void loadMembers();
    QString currentConfigSpec() const;

    BaseInstance* m_instance;

    QLabel* m_accountLabel;
    QPushButton* m_signInButton;
    QLabel* m_stateLabel;

    QGroupBox* m_notSharedBox;
    QPushButton* m_shareButton;
    QComboBox* m_shareConfigsCombo;

    QGroupBox* m_ownerBox;
    QPushButton* m_pushButton;
    QCheckBox* m_autoPushCheck;
    QComboBox* m_configsCombo;
    QLineEdit* m_inviteLinkEdit;
    QPushButton* m_copyLinkButton;
    QPushButton* m_newLinkButton;
    QLineEdit* m_usernameEdit;
    QPushButton* m_inviteUserButton;
    QPushButton* m_inviteFriendButton;
    QListWidget* m_membersList;
    QPushButton* m_removeMemberButton;
    QPushButton* m_stopButton;

    QGroupBox* m_memberBox;
    QLabel* m_memberInfoLabel;
    QPushButton* m_syncButton;
    QPushButton* m_changesButton;
    QPushButton* m_leaveButton;

    // The "unpushed changes" hint walks the whole config tree; cache it so
    // refresh() stays cheap when it runs several times in a row.
    qint64 m_fingerprintCheckedAtMs = 0;
    bool m_fingerprintDirty = false;
};
