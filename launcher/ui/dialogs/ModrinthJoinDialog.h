// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Prism Launcher fork - Modrinth Shared Instances support
 *
 *  "Join Shared Pack": paste a modrinth.com/share/... invite link, and the
 *  shared instance is created locally and kept up to date automatically.
 */
#pragma once

#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>

class ModrinthJoinDialog : public QDialog {
    Q_OBJECT
   public:
    explicit ModrinthJoinDialog(QWidget* parent);

   private slots:
    void join();

   private:
    QLineEdit* m_linkEdit;
    QPushButton* m_joinButton;
    QLabel* m_statusLabel;
};
