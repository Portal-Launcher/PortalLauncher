// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Prism Launcher fork - Modrinth Shared Instances support
 *
 *  Modrinth friends: friend list and requests over the main API, plus live
 *  presence (online / playing X) over the same WebSocket the official
 *  Modrinth App uses.
 */
#pragma once

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

    /** Reload the friend list from the API (usernames included). */
    void refresh();
    /** Connect the presence socket if signed in (safe to call repeatedly). */
    void ensureConnected();
    /** Drop the socket and cached data (used on sign-out). */
    void reset();

    void addFriend(const QString& usernameOrId, std::function<void(const QString& error)> done);
    void removeFriend(const QString& userId, std::function<void(const QString& error)> done);

    /** Tell friends what we are playing; empty means idle. */
    void setPlaying(const QString& instanceName);

   signals:
    void changed();

   private slots:
    void onTextMessage(const QString& message);
    void onSocketConnected();
    void onSocketDisconnected();

   private:
    explicit ModrinthFriends(QObject* parent = nullptr);
    void resolveUsernames(const QStringList& ids);

    struct Entry {
        QString userId;
        bool accepted = false;
        bool incoming = false;
    };

    QWebSocket* m_socket = nullptr;
    bool m_connected = false;
    QTimer m_pingTimer;
    QTimer m_reconnectTimer;

    QList<Entry> m_entries;
    QHash<QString, QString> m_usernames;  // userId -> username
    QHash<QString, QString> m_statuses;   // userId -> playing ("" = online, idle)
    QString m_lastPlaying;
};
