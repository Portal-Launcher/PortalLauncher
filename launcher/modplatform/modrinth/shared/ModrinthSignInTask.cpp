// SPDX-License-Identifier: GPL-3.0-only
#include "ModrinthSignInTask.h"

#include <QHostAddress>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <memory>

#include "BuildConfig.h"
#include "DesktopServices.h"
#include "ModrinthSharedApi.h"

static QByteArray responsePage()
{
    const QString body = QStringLiteral(
                             "<!doctype html><meta charset=\"utf-8\"><title>Signed in</title>"
                             "<body style=\"font-family:system-ui;background:#16181c;color:#ecf9fb;display:grid;"
                             "place-items:center;height:100vh;margin:0\">"
                             "<div style=\"text-align:center\"><h1 style=\"color:#1bd96a\">Signed in &#10004;</h1>"
                             "<p>%1 is now connected to your Modrinth account.<br>You can close this tab.</p></div>")
                             .arg(BuildConfig.LAUNCHER_DISPLAYNAME);
    return "HTTP/1.1 200 OK\r\n"
           "Content-Type: text/html;charset=utf-8\r\n"
           "Connection: close\r\n"
           "\r\n" +
           body.toUtf8();
}

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

    // The port only stays open while the dialog is up; give up after a while
    // so a forgotten dialog does not leave a listener running forever.
    auto* timeout = new QTimer(this);
    timeout->setSingleShot(true);
    timeout->setInterval(10 * 60 * 1000);
    connect(timeout, &QTimer::timeout, this, [this]() {
        if (m_done)
            return;
        m_done = true;
        m_server->close();
        emitFailed(tr("The Modrinth sign-in timed out. Please try again."));
    });
    timeout->start();

    QUrl url(ModrinthShared::siteUrl() + "/auth/sign-in");
    QUrlQuery query;
    query.addQueryItem("launcher", "true");
    query.addQueryItem("ipver", "4");
    query.addQueryItem("port", QString::number(m_server->serverPort()));
    url.setQuery(query);

    setStatus(tr("Waiting for you to sign in to Modrinth in your browser…"));
    if (!DesktopServices::openUrl(url)) {
        m_done = true;
        m_server->close();
        emitFailed(tr("Could not open your browser. Visit this URL manually:\n%1").arg(url.toString()));
    }
}

void ModrinthSignInTask::onNewConnection()
{
    while (auto* socket = m_server->nextPendingConnection()) {
        // A handful of requests is plenty for one redirect (plus a stray
        // favicon fetch); anything noisier is not the sign-in flow.
        if (++m_connectionCount > 5) {
            socket->abort();
            socket->deleteLater();
            continue;
        }
        // Buffer until the full request line has arrived; it is not
        // guaranteed to come in a single TCP segment.
        auto buffer = std::make_shared<QByteArray>();
        connect(socket, &QTcpSocket::readyRead, this, [this, socket, buffer]() {
            buffer->append(socket->readAll());
            const int lineEnd = buffer->indexOf("\r\n");
            if (lineEnd < 0) {
                if (buffer->size() > 8 * 1024)
                    socket->abort();
                return;
            }

            // e.g. "GET /?code=mra_... HTTP/1.1"
            QString code;
            const auto parts = QString::fromUtf8(buffer->left(lineEnd)).split(' ');
            if (parts.size() >= 2 && parts[0] == QLatin1String("GET")) {
                QUrl url("http://localhost" + parts[1]);
                code = QUrlQuery(url).queryItemValue("code");
            }
            socket->write(responsePage());
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
        if (m_aborted)
            return;
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
    m_aborted = true;
    m_done = true;
    if (m_server)
        m_server->close();
    emitAborted();
    return true;
}
