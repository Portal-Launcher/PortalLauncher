// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Prism Launcher fork - Modrinth Shared Instances support
 *
 *  Dockable friends panel, in the spirit of the Modrinth App's friends list:
 *  who is online, what they are playing, requests, and adding friends.
 */
#pragma once

#include <QAction>
#include <QDockWidget>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>

class FriendsPanel : public QDockWidget {
    Q_OBJECT
   public:
    explicit FriendsPanel(QWidget* parent = nullptr);

    /** Action that shows and hides the panel.
     *
     *  Not QDockWidget::toggleViewAction(): Qt disables that one unless the
     *  dock is closable, and this panel deliberately has no close or float
     *  button, which would leave the Friends button permanently greyed out.
     */
    QAction* viewAction() const { return m_viewAction; }

   protected:
    void showEvent(QShowEvent* event) override;
    bool event(QEvent* event) override;

   private slots:
    void rebuild();
    void scheduleRebuild();
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

    QAction* m_viewAction;
    QLabel* m_headerLabel;
    QToolButton* m_refreshButton;
    QPushButton* m_signInButton;
    QTreeWidget* m_tree;
    QLineEdit* m_addEdit;
    QPushButton* m_addButton;
    QTimer m_rebuildTimer;  // coalesces bursts of presence updates into one rebuild
};
