// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Prism Launcher - Minecraft Launcher
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 */

#pragma once

#include <QJsonObject>
#include <QLocalSocket>
#include <QObject>
#include <QString>

/** Minimal Discord Rich Presence over Discord's local IPC pipe, no SDK
 *  involved. While a game runs, friends on Discord see what pack the user is
 *  playing and for how long. Controlled by the DiscordPresenceEnabled and
 *  DiscordClientId settings; every failure is silent and non-fatal.
 */
class DiscordPresence : public QObject {
    Q_OBJECT

   public:
    static DiscordPresence* get();

    /** Show the given instance name as playing, or clear with an empty name. */
    void setPlaying(const QString& instanceName);

   private:
    explicit DiscordPresence(QObject* parent = nullptr);

    void connectPipe();
    void tryNextPipe();
    void sendFrame(qint32 opcode, const QJsonObject& payload);
    void sendActivity();
    void onReadyRead();
    void teardown();

    QLocalSocket* m_socket = nullptr;
    QByteArray m_readBuffer;
    bool m_ready = false;  // Discord answered the handshake
    QString m_playing;
    qint64 m_startedAt = 0;  // unix seconds when the current game started
    int m_pipeIndex = 0;
    int m_nonce = 0;
};
