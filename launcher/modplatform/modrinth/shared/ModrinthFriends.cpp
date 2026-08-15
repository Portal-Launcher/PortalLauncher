// SPDX-License-Identifier: GPL-3.0-only
#include "ModrinthFriends.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QUrlQuery>
#include <QWebSocket>

#include "Application.h"
#include "ModrinthSharedApi.h"

static QString v3Base()
{
    return QStringLiteral("https://api.modrinth.com/v3");
}

static QString socketUrl()
{
    return QStringLiteral("wss://api.modrinth.com/_internal/launcher_socket");
}

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
        if (m_socket && m_connected)
            m_socket->ping();
    });
    m_reconnectTimer.setInterval(30 * 1000);
    connect(&m_reconnectTimer, &QTimer::timeout, this, [this]() { ensureConnected(); });
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
    ModrinthShared::request(this, "GET", QUrl(v3Base() + "/friends"), QByteArray(), QByteArray(),
                            ModrinthShared::Auth::Labrinth, [this](const ModrinthShared::Response& res) {
                                if (!res.ok || !res.json.isArray())
                                    return;
                                const QString me = ModrinthShared::userId();
                                m_entries.clear();
                                QStringList unknown;
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
                                    if (!m_usernames.contains(entry.userId))
                                        unknown.append(entry.userId);
                                }
                                if (unknown.isEmpty())
                                    emit changed();
                                else
                                    resolveUsernames(unknown);
                            });
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
    if (m_connected || !ModrinthShared::isSignedIn())
        return;
    if (m_socket) {
        m_socket->deleteLater();
        m_socket = nullptr;
    }

    m_socket = new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this);
    connect(m_socket, &QWebSocket::connected, this, &ModrinthFriends::onSocketConnected);
    connect(m_socket, &QWebSocket::disconnected, this, &ModrinthFriends::onSocketDisconnected);
    connect(m_socket, &QWebSocket::textMessageReceived, this, &ModrinthFriends::onTextMessage);

    QUrl url(socketUrl());
    QUrlQuery query;
    query.addQueryItem("code", ModrinthShared::token());
    url.setQuery(query);
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, APPLICATION->getUserAgent().toUtf8());
    m_socket->open(request);
    m_reconnectTimer.start();
}

void ModrinthFriends::onSocketConnected()
{
    m_connected = true;
    m_pingTimer.start();
    if (!m_lastPlaying.isEmpty())
        setPlaying(m_lastPlaying);
    emit changed();
}

void ModrinthFriends::onSocketDisconnected()
{
    m_connected = false;
    m_pingTimer.stop();
    m_statuses.clear();
    emit changed();
    // m_reconnectTimer keeps running and will bring the socket back.
}

void ModrinthFriends::reset()
{
    m_reconnectTimer.stop();
    m_pingTimer.stop();
    if (m_socket) {
        m_socket->close();
        m_socket->deleteLater();
        m_socket = nullptr;
    }
    m_connected = false;
    m_entries.clear();
    m_statuses.clear();
    emit changed();
}

void ModrinthFriends::onTextMessage(const QString& message)
{
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
        m_statuses[idToString(status.value("user_id"))] = status.value("profile_name").toString();
        emit changed();
    } else if (type == QLatin1String("user_offline")) {
        m_statuses.remove(idToString(obj.value("id")));
        emit changed();
    } else if (type == QLatin1String("friend_request") || type == QLatin1String("friend_request_rejected")) {
        refresh();
    } else if (type.isEmpty() && obj.contains("body")) {
        // Notification payloads (e.g. shared-pack invites) are also pushed
        // over this socket by the server.
        const QString bodyType = obj.value("body").toObject().value("type").toString();
        if (bodyType == QLatin1String("shared_instance_invite"))
            emit inviteNotification();
    }
}

void ModrinthFriends::addFriend(const QString& usernameOrId, std::function<void(const QString&)> done)
{
    ModrinthShared::request(this, "POST", QUrl(v3Base() + "/friend/" + QString::fromUtf8(QUrl::toPercentEncoding(usernameOrId))),
                            QByteArray(), QByteArray(), ModrinthShared::Auth::Labrinth,
                            [this, done, usernameOrId](const ModrinthShared::Response& res) {
                                if (!res.ok) {
                                    done(res.status == 404
                                             ? tr("No Modrinth user named \"%1\" was found.").arg(usernameOrId)
                                             : res.error);
                                    return;
                                }
                                done(QString());
                                refresh();
                            });
}

void ModrinthFriends::removeFriend(const QString& userId, std::function<void(const QString&)> done)
{
    ModrinthShared::request(this, "DELETE", QUrl(v3Base() + "/friend/" + userId), QByteArray(), QByteArray(),
                            ModrinthShared::Auth::Labrinth, [this, done](const ModrinthShared::Response& res) {
                                done(res.ok ? QString() : res.error);
                                refresh();
                            });
}

void ModrinthFriends::setPlaying(const QString& instanceName)
{
    m_lastPlaying = instanceName;
    if (!m_socket || !m_connected)
        return;
    QJsonObject payload;
    payload["type"] = "status_update";
    payload["profile_name"] = instanceName.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(instanceName);
    m_socket->sendTextMessage(QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact)));
}
