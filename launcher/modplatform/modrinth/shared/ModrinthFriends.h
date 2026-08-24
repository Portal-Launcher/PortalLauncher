// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Prism Launcher fork - Modrinth Shared Instances support
 *
 *  Modrinth friends: friend list and requests over the main API, plus live
 *  presence (online / playing X) over the same WebSocket the official
 *  Modrinth App uses.
 */
#pragma once

#include <QElapsedTimer>
#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QTimer>
#include <functional>

class QWebSocket;

class ModrinthFriends : public QObject {
    Q_OBJECT
   public:
    struct Friend {
        QString userId;
        QString username;
        bool accepted = false;
        bool incoming = false;  // pending request sent TO me
        bool online = false;
        QString playing;  // instance name they are playing, if any
    };

    static ModrinthFriends* get();

    QList<Friend> friends() const;
    bool socketConnected() const { return m_connected; }
    /** The session was rejected and could not be refreshed; the user needs to
     *  sign in again (or fix their token in settings). */
    bool authFailed() const { return m_authFailed; }
    /** Human-readable reason the last friend list load failed, if it did. */
    QString lastError() const { return m_lastError; }

    /** Reload the friend list from the API (usernames included). */
    void refresh();
    /** Connect the presence socket if signed in (safe to call repeatedly). */
    void ensureConnected();
    /** Drop the socket and cached data (used on sign-out). */
    void reset();

    /** ctx guards the callback: if it is destroyed before the reply lands,
     *  done is never invoked (the refresh still happens). displayName is used
     *  in error messages when usernameOrId is an opaque user id. */
    void addFriend(QObject* ctx,
                   const QString& usernameOrId,
                   std::function<void(const QString& error)> done,
                   const QString& displayName = QString());
    void removeFriend(QObject* ctx, const QString& userId, std::function<void(const QString& error)> done);

    /** Tell friends what we are playing; empty means idle. Honors the
     *  ModrinthPresenceEnabled setting (broadcasts idle when it is off). */
    void setPlaying(const QString& instanceName);
    /** Re-broadcast the current state after the presence setting changed. */
    void presenceSettingChanged();

   signals:
    void changed();
    /** A shared-pack invite (or other Modrinth notification) arrived live. */
    void inviteNotification();
    /** An accepted friend just went from offline to online. Not emitted for
     *  the initial bulk status list, so reconnects stay quiet. */
    void friendOnline(const QString& username);

   private slots:
    void onTextMessage(const QString& message);
    void onSocketConnected();
    void onSocketDisconnected();
    void onSessionChanged();

   private:
    explicit ModrinthFriends(QObject* parent = nullptr);
    void resolveUsernames(const QStringList& ids);
    /** Kill the socket without waiting for a close handshake (used when it is
     *  presumed dead) and let the retry timer or caller bring it back. */
    void teardownSocket();
    void handleAuthFailure();

    struct Entry {
        QString userId;
        bool accepted = false;
        bool incoming = false;
    };

    QWebSocket* m_socket = nullptr;
    bool m_connected = false;
    bool m_connecting = false;
    QTimer m_pingTimer;
    QTimer m_reconnectTimer;
    QTimer m_connectWatchdog;   // aborts a handshake that never finishes
    QTimer m_refreshDebounce;   // coalesces socket-triggered refresh bursts
    QElapsedTimer m_sincePong;  // liveness: restarted on any traffic

    bool m_refreshInFlight = false;
    bool m_refreshQueued = false;
    bool m_refreshRetried = false;  // one 401 -> session refresh -> retry cycle
    bool m_authFailed = false;
    QString m_lastError;

    QList<Entry> m_entries;
    QHash<QString, QString> m_usernames;  // userId -> username
    QHash<QString, QString> m_statuses;   // userId -> playing ("" = online, idle)
    QString m_lastPlaying;
};
