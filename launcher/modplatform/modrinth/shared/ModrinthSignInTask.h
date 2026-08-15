// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Prism Launcher fork - Modrinth Shared Instances support
 *
 *  Signs in to a Modrinth account with the same browser flow the official
 *  Modrinth App uses: listen on localhost, open modrinth.com/auth/sign-in
 *  with ?launcher=true&port=..., and receive the session token via redirect.
 */
#pragma once

#include <QTcpServer>
#include <QTcpSocket>

#include "tasks/Task.h"

class ModrinthSignInTask : public Task {
    Q_OBJECT
   public:
    explicit ModrinthSignInTask(QObject* parent = nullptr);
    ~ModrinthSignInTask() override = default;

    bool abort() override;

   protected:
    void executeTask() override;

   private slots:
    void onNewConnection();

   private:
    void handleCode(const QString& code);

    QTcpServer* m_server = nullptr;
    bool m_done = false;
};
