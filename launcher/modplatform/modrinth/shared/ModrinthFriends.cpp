// SPDX-License-Identifier: GPL-3.0-only
#include "ModrinthFriends.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QNetworkInformation>
#include <QNetworkRequest>
#include <QPointer>
#include <QSet>
#include <QUrlQuery>
#include <QWebSocket>

#include "Application.h"
#include "ModrinthSharedApi.h"
#include "settings/SettingsObject.h"

Q_LOGGING_CATEGORY(friendsLog, "portal.friends")

static QString v3Base()
{
    return QStringLiteral("https://api.modrinth.com/v3");
}

static QString socketUrl()
{
    return QStringLiteral("wss://api.modrinth.com/_internal/launcher_socket");
}

// A socket that stops answering pings for this long is presumed dead. Pings go
// out every 10 s, so this allows a few losses before tearing down.
static constexpr qint64 PONG_DEADLINE_MS = 35 * 1000;
// A handshake that has not finished after this long never will.
static constexpr int CONNECT_TIMEOUT_MS = 20 * 1000;

ModrinthFriends* ModrinthFriends::get()
{
    static ModrinthFriends* instance = new ModrinthFriends(qApp);
    return instance;
}

ModrinthFriends::ModrinthFriends(QObject* parent) : QObject(parent)
{
    // Same cadence as the official app: ping every 10 s, retry every 30 s.
    m_pingTimer.setInterval(10 * 1000);
    connect(&m_pingTimer, &QTimer::timeout, this, [this]() {
        if (!m_socket || !m_connected)
            return;
        // A laptop resuming from sleep or switching networks leaves the TCP
        // connection half-open: Qt still says connected but nothing answers.
        // No pong (or any other traffic) within the deadline means the socket
        // is gone; rebuild it instead of pinging a black hole forever.
        if (m_sincePong.isValid() && m_sincePong.elapsed() > PONG_DEADLINE_MS) {
            qCDebug(friendsLog) << "presence socket stopped answering, reconnecting";
            teardownSocket();
            ensureConnected();
            return;
        }
        m_socket->ping();
    });
    m_reconnectTimer.setInterval(30 * 1000);
    connect(&m_reconnectTimer, &QTimer::timeout, this, [this]() { ensureConnected(); });

    m_connectWatchdog.setSingleShot(true);
    m_connectWatchdog.setInterval(CONNECT_TIMEOUT_MS);
    connect(&m_connectWatchdog, &QTimer::timeout, this, [this]() {
        if (!m_connecting)
            return;
        // A stalled handshake never fires connected(), disconnected() or
        // errorOccurred(), which would leave m_connecting stuck forever and
        // block every future reconnect attempt.
        qCDebug(friendsLog) << "presence socket handshake timed out";
        teardownSocket();
    });

    // Socket-triggered refreshes can arrive in bursts; one trip to the API is
    // plenty.
    m_refreshDebounce.setSingleShot(true);
    m_refreshDebounce.setInterval(1500);
    connect(&m_refreshDebounce, &QTimer::timeout, this, [this]() { refresh(); });

    connect(ModrinthShared::SessionNotifier::get(), &ModrinthShared::SessionNotifier::sessionChanged, this,
            &ModrinthFriends::onSessionChanged);

    // Reconnect right away when the network comes back instead of waiting for
    // the retry timer (best effort; not every platform has a backend).
    if (QNetworkInformation::loadBackendByFeatures(QNetworkInformation::Feature::Reachability)) {
        connect(QNetworkInformation::instance(), &QNetworkInformation::reachabilityChanged, this,
                [this](QNetworkInformation::Reachability reachability) {
                    if (reachability != QNetworkInformation::Reachability::Online)
                        return;
                    if (m_connected && m_socket)
                        m_socket->ping();  // dead sockets get caught by the pong deadline
                    else
                        ensureConnected();
                });
    }
}

QList<ModrinthFriends::Friend> ModrinthFriends::friends() const
{
    QList<Friend> out;
    for (const auto& entry : m_entries) {
        Friend f;
        f.userId = entry.userId;
        f.username = m_usernames.value(entry.userId, entry.userId);
        f.accepted = entry.accepted;
        f.incoming = entry.incoming;
        f.online = m_statuses.contains(entry.userId);
        f.playing = m_statuses.value(entry.userId);
        out.append(f);
    }
    return out;
}

void ModrinthFriends::refresh()
{
    if (!ModrinthShared::isSignedIn())
        return;
    if (m_refreshInFlight) {
        m_refreshQueued = true;
        return;
    }
    m_refreshInFlight = true;
    ModrinthShared::request(this, "GET", QUrl(v3Base() + "/friends"), QByteArray(), QByteArray(),
                            ModrinthShared::Auth::Labrinth, [this](const ModrinthShared::Response& res) {
                                m_refreshInFlight = false;
                                const bool rerun = m_refreshQueued;
                                m_refreshQueued = false;
                                if (res.status == 401) {
                                    handleAuthFailure();
                                    return;
                                }
                                if (!res.ok || !res.json.isArray()) {
                                    qCDebug(friendsLog) << "friend list load failed:" << res.error;
                                    m_lastError = tr("Could not load your friend list right now.");
                                    emit changed();
                                    return;
                                }
                                m_lastError.clear();
                                m_refreshRetried = false;
                                const QString me = ModrinthShared::userId();
                                m_entries.clear();
                                QStringList unknown;
                                QSet<QString> listed;
                                for (const auto& value : res.json.array()) {
                                    const auto obj = value.toObject();
                                    Entry entry;
                                    // Counter-intuitive but per labrinth's UserFriend model:
                                    // "id" is the RECIPIENT of the request, "friend_id" the SENDER.
                                    const QString recipientId = obj.value("id").toString();
                                    const QString senderId = obj.value("friend_id").toString();
                                    entry.userId = senderId == me ? recipientId : senderId;
                                    entry.accepted = obj.value("accepted").toBool();
                                    entry.incoming = !entry.accepted && recipientId == me;
                                    if (entry.userId.isEmpty())
                                        continue;
                                    m_entries.append(entry);
                                    listed.insert(entry.userId);
                                    if (!m_usernames.contains(entry.userId))
                                        unknown.append(entry.userId);
                                }
                                // Presence for people no longer on the list (removed friends)
                                // should not linger.
                                for (auto it = m_statuses.begin(); it != m_statuses.end();) {
                                    if (listed.contains(it.key()))
                                        ++it;
                                    else
                                        it = m_statuses.erase(it);
                                }
                                if (unknown.isEmpty())
                                    emit changed();
                                else
                                    resolveUsernames(unknown);
                                if (rerun)
                                    refresh();
                            });
}

void ModrinthFriends::handleAuthFailure()
{
    if (ModrinthShared::tokenIsSession() && !m_refreshRetried) {
        // Browser-login sessions can usually be refreshed; try once before
        // declaring the session dead.
        m_refreshRetried = true;
        qCDebug(friendsLog) << "session rejected, attempting a refresh";
        ModrinthShared::refreshSession(this, [this](const ModrinthShared::Response& res) {
            if (res.ok) {
                refresh();
                ensureConnected();
            } else {
                handleAuthFailure();
            }
        });
        return;
    }
    if (ModrinthShared::tokenIsSession()) {
        // Refresh failed too: the session is gone. Sign out so every part of
        // the UI shows the truth instead of a frozen friend list.
        qCDebug(friendsLog) << "session expired; signing out";
        m_authFailed = true;
        ModrinthShared::clearSession();  // triggers onSessionChanged -> reset()
    } else {
        // A hand-entered token is the user's to fix; do not delete it.
        qCDebug(friendsLog) << "personal token rejected";
        m_authFailed = true;
        m_lastError = tr("Modrinth rejected your token. Check it under Settings > APIs.");
        emit changed();
    }
}

void ModrinthFriends::resolveUsernames(const QStringList& ids)
{
    ModrinthShared::getUsersByIds(this, ids, [this](const ModrinthShared::Response& res) {
        if (res.ok) {
            for (const auto& value : res.json.array()) {
                const auto obj = value.toObject();
                m_usernames[obj.value("id").toString()] = obj.value("username").toString();
            }
        }
        emit changed();
    });
}

void ModrinthFriends::ensureConnected()
{
    // m_connecting keeps the 30 s retry timer from tearing down a socket that
    // is still mid-handshake and never letting it finish; the connect watchdog
    // is the escape hatch when the handshake itself hangs.
    if (m_connected || m_connecting || !ModrinthShared::isSignedIn())
        return;
    if (m_socket) {
        m_socket->deleteLater();
        m_socket = nullptr;
    }

    m_socket = new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this);
    connect(m_socket, &QWebSocket::connected, this, &ModrinthFriends::onSocketConnected);
    connect(m_socket, &QWebSocket::disconnected, this, &ModrinthFriends::onSocketDisconnected);
    connect(m_socket, &QWebSocket::textMessageReceived, this, &ModrinthFriends::onTextMessage);
    connect(m_socket, &QWebSocket::pong, this, [this](quint64, const QByteArray&) { m_sincePong.restart(); });
    connect(m_socket, &QWebSocket::errorOccurred, this, [this](QAbstractSocket::SocketError error) {
        qCDebug(friendsLog) << "presence socket error" << error;
        m_connecting = false;
        m_connectWatchdog.stop();
        // disconnected() may not fire after a connection error; the retry
        // timer picks it up from here.
    });

    QUrl url(socketUrl());
    QUrlQuery query;
    query.addQueryItem("code", ModrinthShared::token());
    url.setQuery(query);
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, APPLICATION->getUserAgent().toUtf8());
    m_connecting = true;
    m_socket->open(request);
    m_connectWatchdog.start();
    m_reconnectTimer.start();
    qCDebug(friendsLog) << "presence socket connecting";
}

void ModrinthFriends::onSocketConnected()
{
    qCDebug(friendsLog) << "presence socket connected";
    m_connected = true;
    m_connecting = false;
    m_connectWatchdog.stop();
    m_sincePong.restart();
    m_pingTimer.start();
    if (!m_lastPlaying.isEmpty())
        setPlaying(m_lastPlaying);
    emit changed();
}

void ModrinthFriends::onSocketDisconnected()
{
    qCDebug(friendsLog) << "presence socket disconnected";
    m_connected = false;
    m_connecting = false;
    m_connectWatchdog.stop();
    m_pingTimer.stop();
    m_statuses.clear();
    emit changed();
    // m_reconnectTimer keeps running and will bring the socket back.
}

void ModrinthFriends::teardownSocket()
{
    if (m_socket) {
        // The socket is presumed dead; do not wait for a close handshake, and
        // do not let its queued signals re-enter our disconnect handling.
        m_socket->disconnect(this);
        m_socket->abort();
        m_socket->deleteLater();
        m_socket = nullptr;
    }
    m_connected = false;
    m_connecting = false;
    m_connectWatchdog.stop();
    m_pingTimer.stop();
    if (!m_statuses.isEmpty()) {
        m_statuses.clear();
        emit changed();
    }
}

void ModrinthFriends::onSessionChanged()
{
    if (ModrinthShared::isSignedIn()) {
        // Signed in, or the token changed: start over with the new identity.
        m_authFailed = false;
        m_refreshRetried = false;
        m_lastError.clear();
        teardownSocket();
        m_entries.clear();
        m_usernames.clear();
        refresh();
        ensureConnected();
    } else {
        reset();
    }
}

void ModrinthFriends::reset()
{
    m_reconnectTimer.stop();
    m_pingTimer.stop();
    m_connectWatchdog.stop();
    m_refreshDebounce.stop();
    if (m_socket) {
        m_socket->disconnect(this);
        m_socket->close();
        m_socket->deleteLater();
        m_socket = nullptr;
    }
    m_connected = false;
    m_connecting = false;
    m_refreshInFlight = false;
    m_refreshQueued = false;
    m_entries.clear();
    m_usernames.clear();
    m_statuses.clear();
    m_lastError.clear();
    emit changed();
}

void ModrinthFriends::onTextMessage(const QString& message)
{
    m_sincePong.restart();  // any traffic proves the socket is alive
    const auto doc = QJsonDocument::fromJson(message.toUtf8());
    if (!doc.isObject())
        return;
    const auto obj = doc.object();
    const QString type = obj.value("type").toString();

    auto idToString = [](const QJsonValue& value) { return value.toVariant().toString(); };

    if (type == QLatin1String("friend_statuses")) {
        m_statuses.clear();
        for (const auto& value : obj.value("statuses").toArray()) {
            const auto status = value.toObject();
            m_statuses[idToString(status.value("user_id"))] = status.value("profile_name").toString();
        }
        emit changed();
    } else if (type == QLatin1String("status_update")) {
        const auto status = obj.value("status").toObject();
        const QString userId = idToString(status.value("user_id"));
        const bool wasOnline = m_statuses.contains(userId);
        m_statuses[userId] = status.value("profile_name").toString();
        if (!wasOnline && userId != ModrinthShared::userId()) {
            for (const auto& entry : m_entries) {
                if (entry.userId == userId && entry.accepted) {
                    emit friendOnline(m_usernames.value(userId, userId));
                    break;
                }
            }
        }
        emit changed();
    } else if (type == QLatin1String("user_offline")) {
        m_statuses.remove(idToString(obj.value("id")));
        emit changed();
    } else if (type == QLatin1String("friend_request") || type == QLatin1String("friend_request_rejected")) {
        m_refreshDebounce.start();
    } else {
        // Notification payloads (e.g. shared-pack invites) are also pushed
        // over this socket. Route on the body's type so a wrapped envelope
        // ({"type":"notification","body":{...}}) is not silently dropped.
        const QString bodyType = obj.value("body").toObject().value("type").toString();
        if (bodyType == QLatin1String("shared_instance_invite"))
            emit inviteNotification();
        else
            qCDebug(friendsLog) << "unhandled socket message type" << (type.isEmpty() ? bodyType : type);
    }
}

void ModrinthFriends::addFriend(QObject* ctx,
                                const QString& usernameOrId,
                                std::function<void(const QString&)> done,
                                const QString& displayName)
{
    QPointer<QObject> guard(ctx);
    const QString shown = displayName.isEmpty() ? usernameOrId : displayName;
    ModrinthShared::request(this, "POST", QUrl(v3Base() + "/friend/" + QString::fromUtf8(QUrl::toPercentEncoding(usernameOrId))),
                            QByteArray(), QByteArray(), ModrinthShared::Auth::Labrinth,
                            [this, guard, ctx, done, shown](const ModrinthShared::Response& res) {
                                if (done && !(ctx && guard.isNull())) {
                                    done(res.ok ? QString()
                                         : res.status == 404
                                             ? tr("No Modrinth user named \"%1\" was found.").arg(shown)
                                             : res.error);
                                }
                                if (res.ok)
                                    refresh();
                            });
}

void ModrinthFriends::removeFriend(QObject* ctx, const QString& userId, std::function<void(const QString&)> done)
{
    QPointer<QObject> guard(ctx);
    ModrinthShared::request(this, "DELETE", QUrl(v3Base() + "/friend/" + QString::fromUtf8(QUrl::toPercentEncoding(userId))),
                            QByteArray(), QByteArray(), ModrinthShared::Auth::Labrinth,
                            [this, guard, ctx, done](const ModrinthShared::Response& res) {
                                if (done && !(ctx && guard.isNull()))
                                    done(res.ok ? QString() : res.error);
                                refresh();
                            });
}

void ModrinthFriends::setPlaying(const QString& instanceName)
{
    m_lastPlaying = instanceName;
    if (!m_socket || !m_connected)
        return;
    const bool share = APPLICATION->settings()->get("ModrinthPresenceEnabled").toBool();
    QJsonObject payload;
    payload["type"] = "status_update";
    payload["profile_name"] =
        (instanceName.isEmpty() || !share) ? QJsonValue(QJsonValue::Null) : QJsonValue(instanceName);
    m_socket->sendTextMessage(QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact)));
}

void ModrinthFriends::presenceSettingChanged()
{
    setPlaying(m_lastPlaying);
}
