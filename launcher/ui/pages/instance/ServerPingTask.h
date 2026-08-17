#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QString>

#include <tasks/Task.h>

class ServerPingTask : public Task {
    Q_OBJECT
   public:
    explicit ServerPingTask(QString domain, int port) : Task(), m_domain(domain), m_port(port) {}
    ~ServerPingTask() override = default;
    int m_outputOnlinePlayers = -1;
    int m_outputMaxPlayers = -1;
    int m_outputLatencyMs = -1;
    QString m_outputVersion;
    QString m_outputMotd;

   private:
    QString m_domain;
    int m_port;
    QElapsedTimer m_pingTimer;

   protected:
    virtual void executeTask() override;
};
