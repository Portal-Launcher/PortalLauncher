// SPDX-License-Identifier: GPL-3.0-only
#include "ModrinthSignInTask.h"

#include <QHostAddress>
#include <QUrl>
#include <QUrlQuery>

#include "DesktopServices.h"
#include "ModrinthSharedApi.h"

static const char* RESPONSE_PAGE =
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: text/html;charset=utf-8\r\n"
    "Connection: close\r\n"
    "\r\n"
    "<!doctype html><meta charset=\"utf-8\"><title>Signed in</title>"
    "<body style=\"font-family:system-ui;background:#16181c;color:#ecf9fb;display:grid;place-items:center;height:100vh;margin:0\">"
    "<div style=\"text-align:center\"><h1 style=\"color:#1bd96a\">Signed in &#10004;</h1>"
    "<p>Prism Launcher is now connected to your Modrinth account.<br>You can close this tab.</p></div>";

ModrinthSignInTask::ModrinthSignInTask(QObject* parent) : Task()
{
    Q_UNUSED(parent);
}

void ModrinthSignInTask::executeTask()
{
    setAbortable(true);
    m_server = new QTcpServer(this);
    connect(m_server, &QTcpServer::newConnection, this, &ModrinthSignInTask::onNewConnection);
    if (!m_server->listen(QHostAddress::LocalHost, 0)) {
        emitFailed(tr("Could not open a local port for the Modrinth sign-in: %1").arg(m_server->errorString()));
        return;
    }

    QUrl url(ModrinthShared::siteUrl() + "/auth/sign-in");
    QUrlQuery query;
    query.addQueryItem("launcher", "true");
    query.addQueryItem("ipver", "4");
    query.addQueryItem("port", QString::number(m_server->serverPort()));
    url.setQuery(query);

    setStatus(tr("Waiting for you to sign in to Modrinth in your browser…"));
    if (!DesktopServices::openUrl(url)) {
        emitFailed(tr("Could not open your browser. Visit this URL manually:\n%1").arg(url.toString()));
    }
}

void ModrinthSignInTask::onNewConnection()
{
    while (auto* socket = m_server->nextPendingConnection()) {
        connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
            QByteArray requestLine = socket->readLine(8 * 1024);
            // e.g. "GET /?code=mra_... HTTP/1.1"
            QString code;
            auto parts = QString::fromUtf8(requestLine).split(' ');
            if (parts.size() >= 2) {
                QUrl url("http://localhost" + parts[1]);
                code = QUrlQuery(url).queryItemValue("code");
            }
            socket->write(RESPONSE_PAGE);
            socket->flush();
            socket->disconnectFromHost();
            if (!code.isEmpty() && !m_done) {
                m_done = true;
                handleCode(code);
            }
        });
        connect(socket, &QTcpSocket::disconnected, socket, &QTcpSocket::deleteLater);
    }
}

void ModrinthSignInTask::handleCode(const QString& code)
{
    setStatus(tr("Checking the Modrinth session…"));
    if (m_server)
        m_server->close();

    ModrinthShared::validateToken(this, code, [this, code](const ModrinthShared::Response& res) {
        if (!res.ok || !res.json.isObject()) {
            emitFailed(tr("Modrinth returned an invalid session. Please try signing in again."));
            return;
        }
        auto user = res.json.object();
        ModrinthShared::storeSession(code, user.value("id").toString(), user.value("username").toString(), true);
        emitSucceeded();
    });
}

bool ModrinthSignInTask::abort()
{
    if (m_server)
        m_server->close();
    emitAborted();
    return true;
}
