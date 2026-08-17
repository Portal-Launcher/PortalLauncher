// SPDX-License-Identifier: GPL-3.0-only
#include "DiscordPresence.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QJsonDocument>
#include <QtEndian>

#include "Application.h"
#include "settings/SettingsObject.h"

namespace {
constexpr int MAX_PIPES = 10;  // Discord tries discord-ipc-0 through -9

constexpr qint32 OP_HANDSHAKE = 0;
constexpr qint32 OP_FRAME = 1;
constexpr qint32 OP_CLOSE = 2;
constexpr qint32 OP_PING = 3;
constexpr qint32 OP_PONG = 4;
}  // namespace

DiscordPresence* DiscordPresence::get()
{
    static DiscordPresence* s_instance = new DiscordPresence(APPLICATION);
    return s_instance;
}

DiscordPresence::DiscordPresence(QObject* parent) : QObject(parent) {}

void DiscordPresence::setPlaying(const QString& instanceName)
{
    if (!APPLICATION->settings()->get("DiscordPresenceEnabled").toBool() ||
        APPLICATION->settings()->get("DiscordClientId").toString().trimmed().isEmpty()) {
        teardown();
        return;
    }

    if (instanceName.isEmpty()) {
        // Closing the connection clears the presence on Discord's side.
        m_playing.clear();
        teardown();
        return;
    }

    if (m_playing != instanceName) {
        m_playing = instanceName;
        m_startedAt = QDateTime::currentSecsSinceEpoch();
    }

    if (m_socket && m_ready)
        sendActivity();
    else if (!m_socket)
        connectPipe();
    // else: connection in progress, READY will push the activity
}

void DiscordPresence::connectPipe()
{
    m_pipeIndex = 0;
    m_ready = false;
    m_readBuffer.clear();
    m_socket = new QLocalSocket(this);
    connect(m_socket, &QLocalSocket::connected, this, [this]() {
        QJsonObject handshake;
        handshake["v"] = 1;
        handshake["client_id"] = APPLICATION->settings()->get("DiscordClientId").toString().trimmed();
        sendFrame(OP_HANDSHAKE, handshake);
    });
    connect(m_socket, &QLocalSocket::readyRead, this, &DiscordPresence::onReadyRead);
    connect(m_socket, &QLocalSocket::errorOccurred, this, [this](QLocalSocket::LocalSocketError) { tryNextPipe(); });
    m_socket->connectToServer(QStringLiteral("discord-ipc-0"));
}

void DiscordPresence::tryNextPipe()
{
    if (!m_socket)
        return;
    m_pipeIndex++;
    if (m_pipeIndex >= MAX_PIPES) {
        qDebug() << "Discord presence: no Discord client pipe found";
        teardown();
        return;
    }
    m_socket->abort();
    m_socket->connectToServer(QStringLiteral("discord-ipc-%1").arg(m_pipeIndex));
}

void DiscordPresence::sendFrame(qint32 opcode, const QJsonObject& payload)
{
    if (!m_socket || m_socket->state() != QLocalSocket::ConnectedState)
        return;
    const QByteArray body = QJsonDocument(payload).toJson(QJsonDocument::Compact);
    QByteArray frame(8, '\0');
    qToLittleEndian<qint32>(opcode, frame.data());
    qToLittleEndian<qint32>(static_cast<qint32>(body.size()), frame.data() + 4);
    frame.append(body);
    m_socket->write(frame);
}

void DiscordPresence::sendActivity()
{
    QJsonObject args;
    args["pid"] = static_cast<qint64>(QCoreApplication::applicationPid());
    if (!m_playing.isEmpty()) {
        QJsonObject timestamps;
        timestamps["start"] = m_startedAt;
        QJsonObject activity;
        activity["details"] = tr("Playing %1").arg(m_playing);
        activity["timestamps"] = timestamps;
        args["activity"] = activity;
    }
    QJsonObject payload;
    payload["cmd"] = "SET_ACTIVITY";
    payload["args"] = args;
    payload["nonce"] = QString::number(++m_nonce);
    sendFrame(OP_FRAME, payload);
}

void DiscordPresence::onReadyRead()
{
    if (!m_socket)
        return;
    m_readBuffer.append(m_socket->readAll());
    while (m_readBuffer.size() >= 8) {
        const qint32 opcode = qFromLittleEndian<qint32>(m_readBuffer.constData());
        const qint32 length = qFromLittleEndian<qint32>(m_readBuffer.constData() + 4);
        if (length < 0 || length > 1024 * 1024) {
            teardown();
            return;
        }
        if (m_readBuffer.size() < 8 + length)
            return;
        const QByteArray body = m_readBuffer.mid(8, length);
        m_readBuffer.remove(0, 8 + length);

        if (opcode == OP_PING) {
            sendFrame(OP_PONG, QJsonDocument::fromJson(body).object());
            continue;
        }
        if (opcode == OP_CLOSE) {
            teardown();
            return;
        }
        if (opcode == OP_FRAME && !m_ready) {
            const auto obj = QJsonDocument::fromJson(body).object();
            if (obj.value("evt").toString() == QLatin1String("READY")) {
                m_ready = true;
                if (!m_playing.isEmpty())
                    sendActivity();
            }
        }
    }
}

void DiscordPresence::teardown()
{
    if (m_socket) {
        m_socket->disconnect(this);
        m_socket->close();
        m_socket->deleteLater();
        m_socket = nullptr;
    }
    m_ready = false;
    m_readBuffer.clear();
}
