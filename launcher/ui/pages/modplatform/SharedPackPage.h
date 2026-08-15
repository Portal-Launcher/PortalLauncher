// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Prism Launcher fork - Modrinth Shared Instances support
 *
 *  "Shared Pack" page of the Add Instance dialog: join a pack a friend
 *  shared, from an invite link or a pending username invite.
 */
#pragma once

#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QWidget>

#include "ui/pages/BasePage.h"

class NewInstanceDialog;

class SharedPackPage : public QWidget, public BasePage {
    Q_OBJECT
   public:
    explicit SharedPackPage(NewInstanceDialog* dialog, QWidget* parent = nullptr);

    QString displayName() const override { return tr("Shared Pack"); }
    QIcon icon() const override
    {
        auto pageIcon = QIcon::fromTheme("server");
        if (pageIcon.isNull())
            pageIcon = QIcon::fromTheme("modrinth");
        return pageIcon;
    }
    QString id() const override { return "shared_pack"; }
    QString helpPage() const override { return "Sharing"; }
    void retranslate() override {}
    void openedImpl() override;

   private slots:
    void joinByLink();
    void acceptSelectedInvite();

   private:
    bool ensureSignedIn();
    void loadPendingInvites();
    void finishJoin(const QString& instanceId, const QString& instanceName);

    NewInstanceDialog* m_dialog;
    QLineEdit* m_linkEdit;
    QPushButton* m_joinButton;
    QLabel* m_statusLabel;
    QLabel* m_invitesLabel;
    QListWidget* m_invitesList;
    QPushButton* m_acceptInviteButton;
    bool m_loadedInvites = false;
};
