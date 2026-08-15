// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Prism Launcher fork - Modrinth Shared Instances support
 *
 *  "Join Shared Pack": paste a modrinth.com/share/... invite link, or accept
 *  a pending invite that a friend sent to your Modrinth username.
 */
#pragma once

#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>

class ModrinthJoinDialog : public QDialog {
    Q_OBJECT
   public:
    explicit ModrinthJoinDialog(QWidget* parent);

   protected:
    void showEvent(QShowEvent* event) override;

   private slots:
    void joinByLink();
    void acceptSelectedInvite();

   private:
    bool ensureSignedIn();
    void loadPendingInvites();
    /** Shared tail of both flows: fetch latest version, create + sync the instance. */
    void installShared(const QString& instanceId, const QString& instanceName);

    QLineEdit* m_linkEdit;
    QPushButton* m_joinButton;
    QLabel* m_statusLabel;
    QLabel* m_invitesLabel;
    QListWidget* m_invitesList;
    QPushButton* m_acceptInviteButton;
    bool m_loadedInvites = false;
};
