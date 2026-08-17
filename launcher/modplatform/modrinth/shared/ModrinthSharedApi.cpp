// SPDX-License-Identifier: GPL-3.0-only
#include "ModrinthSharedApi.h"

#include <QDateTime>
#include <QFile>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QRegularExpression>
#include <QUrlQuery>

#include "Application.h"
#include "BuildConfig.h"
#include "settings/Setting.h"
#include "settings/SettingsObject.h"

namespace ModrinthShared {

QString serviceBaseUrl()
{
    return QStringLiteral("https://shared-instances.modrinth.com/v1");
}

QString siteUrl()
{
    return QStringLiteral("https://modrinth.com");
}

static QString apiBase()
{
    return BuildConfig.MODRINTH_PROD_URL;  // https://api.modrinth.com/v2
}

// ---------------------------------------------------------------------------
// Session storage

static QVariant settingGet(const QString& key, const QVariant& def = QVariant(QString()))
{
    return APPLICATION->settings()->getOrRegisterSetting(key, def)->get();
}

static void settingSet(const QString& key, const QVariant& value)
{
    APPLICATION->settings()->getOrRegisterSetting(key, QVariant(QString()))->set(value);
}

QString token()
{
    return settingGet("ModrinthToken").toString();
}

bool isSignedIn()
{
    return !token().isEmpty() && !userId().isEmpty();
}

QString userId()
{
    return settingGet("ModrinthShareUserId").toString();
}

QString username()
{
    return settingGet("ModrinthShareUsername").toString();
}

void storeSession(const QString& tok, const QString& uid, const QString& uname, bool isSession)
{
    settingSet("ModrinthToken", tok);
    settingSet("ModrinthShareUserId", uid);
    settingSet("ModrinthShareUsername", uname);
    settingSet("ModrinthShareTokenIsSession", isSession ? "true" : "");
    settingSet("ModrinthShareRefreshAfter",
               isSession ? QString::number(QDateTime::currentSecsSinceEpoch() + 7 * 24 * 3600) : QString());
}

void clearSession()
{
    settingSet("ModrinthToken", QString());
    settingSet("ModrinthShareUserId", QString());
    settingSet("ModrinthShareUsername", QString());
    settingSet("ModrinthShareTokenIsSession", QString());
    settingSet("ModrinthShareRefreshAfter", QString());
}

void refreshSessionIfNeeded(QObject* ctx)
{
    if (settingGet("ModrinthShareTokenIsSession").toString().isEmpty() || token().isEmpty())
        return;
    qint64 after = settingGet("ModrinthShareRefreshAfter").toString().toLongLong();
    if (after != 0 && QDateTime::currentSecsSinceEpoch() < after)
        return;
    refreshSession(ctx, [](const Response&) { /* best-effort */ });
}

void refreshSession(QObject* ctx, Callback cb)
{
    request(ctx, "POST", QUrl(apiBase() + "/session/refresh"), QByteArray(), QByteArray(), Auth::Labrinth,
            [cb](const Response& res) {
                if (res.ok && res.json.isObject()) {
                    QString newToken = res.json.object().value("session").toString();
                    if (!newToken.isEmpty()) {
                        settingSet("ModrinthToken", newToken);
                        settingSet("ModrinthShareRefreshAfter",
                                   QString::number(QDateTime::currentSecsSinceEpoch() + 7 * 24 * 3600));
                    }
                }
                if (cb)
                    cb(res);
            });
}

// ---------------------------------------------------------------------------
// Generic request plumbing

/** The session token must never travel to a host we do not control. */
static bool isAuthorizedHost(const QUrl& url)
{
    const QString host = url.host().toLower();
    return host == QUrl(apiBase()).host().toLower() || host == QUrl(serviceBaseUrl()).host().toLower();
}

void request(QObject* ctx,
             const QByteArray& verb,
             const QUrl& url,
             const QByteArray& contentType,
             const QByteArray& body,
             Auth auth,
             Callback cb)
{
    if (auth != Auth::None && !isAuthorizedHost(url)) {
        Response bad;
        bad.error = QStringLiteral("Refusing to send credentials to %1").arg(url.host());
        if (cb)
            QMetaObject::invokeMethod(ctx ? ctx : qApp, [cb, bad]() { cb(bad); }, Qt::QueuedConnection);
        return;
    }

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, APPLICATION->getUserAgent().toUtf8());
    if (!contentType.isEmpty())
        req.setHeader(QNetworkRequest::ContentTypeHeader, contentType);
    switch (auth) {
        case Auth::None:
            break;
        case Auth::Labrinth:
            req.setRawHeader("Authorization", token().toUtf8());
            break;
        case Auth::ServiceBearer:
            req.setRawHeader("Authorization", "Bearer " + token().toUtf8());
            break;
    }
    req.setTransferTimeout(5 * 60 * 1000);

    QNetworkReply* reply = APPLICATION->network()->sendCustomRequest(req, verb, body);
    // Always reap the reply, even if the context object dies before it lands.
    QObject::connect(reply, &QNetworkReply::finished, reply, &QObject::deleteLater);
    QPointer<QObject> guard(ctx);
    QObject::connect(reply, &QNetworkReply::finished, ctx ? ctx : reply, [reply, guard, ctx, cb, verb, url]() {
        if (ctx && guard.isNull())
            return;

        Response out;
        out.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QByteArray data = reply->readAll();
        out.body = data;
        if (!data.isEmpty()) {
            QJsonParseError parseError{};
            auto doc = QJsonDocument::fromJson(data, &parseError);
            if (parseError.error == QJsonParseError::NoError)
                out.json = doc;
        }
        out.ok = (reply->error() == QNetworkReply::NoError) && out.status >= 200 && out.status < 300;
        if (!out.ok) {
            QString detail;
            if (out.json.isObject()) {
                auto obj = out.json.object();
                detail = obj.value("description").toString();
                if (detail.isEmpty())
                    detail = obj.value("error").toString();
            }
            if (detail.isEmpty() && !data.isEmpty())
                detail = QString::fromUtf8(data.left(200));
            if (detail.isEmpty())
                detail = reply->errorString();
            out.error = QStringLiteral("%1 %2 failed (HTTP %3): %4")
                            .arg(QString::fromUtf8(verb), url.path(), QString::number(out.status), detail);
        }
        if (cb)
            cb(out);
    });
}

void sendJson(QObject* ctx, const QByteArray& verb, const QUrl& url, const QJsonObject& payload, Auth auth, Callback cb)
{
    request(ctx, verb, url, "application/json", QJsonDocument(payload).toJson(QJsonDocument::Compact), auth, std::move(cb));
}

void getJson(QObject* ctx, const QUrl& url, Auth auth, Callback cb)
{
    request(ctx, "GET", url, QByteArray(), QByteArray(), auth, std::move(cb));
}

static QUrl serviceUrl(const QString& path)
{
    return QUrl(serviceBaseUrl() + path);
}

static QString idsQuery(const QStringList& ids)
{
    QJsonArray arr;
    for (const auto& id : ids)
        arr.append(id);
    return QString::fromUtf8(QUrl::toPercentEncoding(QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact))));
}

// ---------------------------------------------------------------------------
// Main API (labrinth)

void validateToken(QObject* ctx, const QString& explicitToken, Callback cb)
{
    QNetworkRequest req(QUrl(apiBase() + "/user"));
    req.setHeader(QNetworkRequest::UserAgentHeader, APPLICATION->getUserAgent().toUtf8());
    req.setRawHeader("Authorization", explicitToken.toUtf8());
    req.setTransferTimeout(30 * 1000);
    QNetworkReply* reply = APPLICATION->network()->get(req);
    QObject::connect(reply, &QNetworkReply::finished, reply, &QObject::deleteLater);
    QPointer<QObject> guard(ctx);
    QObject::connect(reply, &QNetworkReply::finished, ctx ? ctx : reply, [reply, guard, ctx, cb]() {
        if (ctx && guard.isNull())
            return;
        Response out;
        out.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QJsonParseError parseError{};
        auto doc = QJsonDocument::fromJson(reply->readAll(), &parseError);
        if (parseError.error == QJsonParseError::NoError)
            out.json = doc;
        out.ok = reply->error() == QNetworkReply::NoError && out.status == 200 && out.json.isObject();
        if (!out.ok) {
            const QString detail = reply->error() != QNetworkReply::NoError ? reply->errorString()
                                                                            : QStringLiteral("HTTP %1").arg(out.status);
            out.error = QStringLiteral("Token validation failed: %1").arg(detail);
        }
        if (cb)
            cb(out);
    });
}

void lookupUserByName(QObject* ctx, const QString& name, Callback cb)
{
    getJson(ctx, QUrl(apiBase() + "/user/" + QString::fromUtf8(QUrl::toPercentEncoding(name))), Auth::None, std::move(cb));
}

/** Deliver a canned response the same way a real one would arrive: queued,
 *  guarded by the context's lifetime, and only if a callback exists. */
static void deliverEmptyArray(QObject* ctx, const Callback& cb)
{
    if (!cb)
        return;
    Response empty;
    empty.ok = true;
    empty.json = QJsonDocument(QJsonArray());
    QMetaObject::invokeMethod(ctx ? ctx : qApp, [cb, empty]() { cb(empty); }, Qt::QueuedConnection);
}

void getUsersByIds(QObject* ctx, const QStringList& ids, Callback cb)
{
    if (ids.isEmpty()) {
        deliverEmptyArray(ctx, cb);
        return;
    }
    getJson(ctx, QUrl(apiBase() + "/users?ids=" + idsQuery(ids)), Auth::None, std::move(cb));
}

void lookupVersionFiles(QObject* ctx, const QStringList& sha1Hashes, Callback cb)
{
    QJsonObject payload;
    QJsonArray hashes;
    for (const auto& h : sha1Hashes)
        hashes.append(h);
    payload["hashes"] = hashes;
    payload["algorithm"] = "sha1";
    sendJson(ctx, "POST", QUrl(apiBase() + "/version_files"), payload, Auth::None, std::move(cb));
}

void getVersionsBulk(QObject* ctx, const QStringList& versionIds, Callback cb)
{
    if (versionIds.isEmpty()) {
        deliverEmptyArray(ctx, cb);
        return;
    }
    getJson(ctx, QUrl(apiBase() + "/versions?ids=" + idsQuery(versionIds)), Auth::None, std::move(cb));
}

void getProjectsBulk(QObject* ctx, const QStringList& projectIds, Callback cb)
{
    if (projectIds.isEmpty()) {
        deliverEmptyArray(ctx, cb);
        return;
    }
    getJson(ctx, QUrl(apiBase() + "/projects?ids=" + idsQuery(projectIds)), Auth::None, std::move(cb));
}

void getNotifications(QObject* ctx, Callback cb)
{
    getJson(ctx, QUrl(apiBase() + "/user/" + userId() + "/notifications"), Auth::Labrinth, std::move(cb));
}

// ---------------------------------------------------------------------------
// Shared-instances service

void fetchBytes(QObject* ctx, const QUrl& url, Callback cb)
{
    request(ctx, "GET", url, QByteArray(), QByteArray(), Auth::None, std::move(cb));
}

void createRemoteInstance(QObject* ctx, const QString& name, Callback cb)
{
    QJsonObject payload;
    payload["name"] = name;
    sendJson(ctx, "POST", serviceUrl("/instances"), payload, Auth::ServiceBearer, std::move(cb));
}

void getInstanceInfo(QObject* ctx, const QString& id, Callback cb)
{
    getJson(ctx, serviceUrl("/instances/" + id), Auth::ServiceBearer, std::move(cb));
}

void renameRemoteInstance(QObject* ctx, const QString& id, const QString& name, Callback cb)
{
    QJsonObject payload;
    payload["name"] = name;
    sendJson(ctx, "PATCH", serviceUrl("/instances/" + id), payload, Auth::ServiceBearer,
             [cb](const Response& res) {
                 // 405 = service build without rename support; not fatal.
                 if (!res.ok && res.status == 405) {
                     Response tolerated = res;
                     tolerated.ok = true;
                     cb(tolerated);
                     return;
                 }
                 cb(res);
             });
}

void deleteRemoteInstance(QObject* ctx, const QString& id, Callback cb)
{
    request(ctx, "DELETE", serviceUrl("/instances/" + id), QByteArray(), QByteArray(), Auth::ServiceBearer,
            [cb](const Response& res) {
                if (!res.ok && (res.status == 404 || res.status == 410)) {
                    Response tolerated = res;
                    tolerated.ok = true;
                    cb(tolerated);
                    return;
                }
                cb(res);
            });
}

void getMembers(QObject* ctx, const QString& id, Callback cb)
{
    getJson(ctx, serviceUrl("/instances/" + id + "/users"), Auth::ServiceBearer, std::move(cb));
}

void addMembers(QObject* ctx, const QString& id, const QStringList& userIds, Callback cb)
{
    QJsonObject payload;
    QJsonArray arr;
    for (const auto& uid : userIds)
        arr.append(uid);
    payload["user_ids"] = arr;
    sendJson(ctx, "POST", serviceUrl("/instances/" + id + "/users"), payload, Auth::ServiceBearer, std::move(cb));
}

void removeMembers(QObject* ctx, const QString& id, const QStringList& userIds, Callback cb)
{
    QJsonObject payload;
    QJsonArray arr;
    for (const auto& uid : userIds)
        arr.append(uid);
    payload["user_ids"] = arr;
    sendJson(ctx, "DELETE", serviceUrl("/instances/" + id + "/users"), payload, Auth::ServiceBearer, std::move(cb));
}

void getLatestVersion(QObject* ctx, const QString& id, Callback cb)
{
    getJson(ctx, serviceUrl("/instances/" + id + "/versions"), Auth::ServiceBearer, std::move(cb));
}

void createVersion(QObject* ctx, const QString& id, const QJsonObject& payload, Callback cb)
{
    sendJson(ctx, "POST", serviceUrl("/instances/" + id + "/versions"), payload, Auth::ServiceBearer, std::move(cb));
}

void uploadBytes(QObject* ctx, const QUrl& uploadUrl, const QByteArray& bytes, Callback cb)
{
    // Uploads must stay on the shared-instances service origin (same check as
    // the official app makes).
    if (QUrl(serviceBaseUrl()).host().compare(uploadUrl.host(), Qt::CaseInsensitive) != 0) {
        Response bad;
        bad.error = QStringLiteral("Upload URL has an unexpected origin: %1").arg(uploadUrl.host());
        if (cb)
            QMetaObject::invokeMethod(ctx ? ctx : qApp, [cb, bad]() { cb(bad); }, Qt::QueuedConnection);
        return;
    }
    request(ctx, "PUT", uploadUrl, "application/octet-stream", bytes, Auth::ServiceBearer, std::move(cb));
}

void uploadFile(QObject* ctx, const QUrl& uploadUrl, const QString& filePath, Callback cb)
{
    auto deliverError = [ctx, cb](const QString& error) {
        if (!cb)
            return;
        Response bad;
        bad.error = error;
        QMetaObject::invokeMethod(ctx ? ctx : qApp, [cb, bad]() { cb(bad); }, Qt::QueuedConnection);
    };

    if (QUrl(serviceBaseUrl()).host().compare(uploadUrl.host(), Qt::CaseInsensitive) != 0) {
        deliverError(QStringLiteral("Upload URL has an unexpected origin: %1").arg(uploadUrl.host()));
        return;
    }

    auto* file = new QFile(filePath);
    if (!file->open(QIODevice::ReadOnly)) {
        delete file;
        deliverError(QStringLiteral("Could not open %1 for upload").arg(filePath));
        return;
    }

    QNetworkRequest req(uploadUrl);
    req.setHeader(QNetworkRequest::UserAgentHeader, APPLICATION->getUserAgent().toUtf8());
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/octet-stream");
    req.setRawHeader("Authorization", "Bearer " + token().toUtf8());
    req.setTransferTimeout(5 * 60 * 1000);

    // Streaming from the QFile keeps memory flat regardless of file size.
    QNetworkReply* reply = APPLICATION->network()->sendCustomRequest(req, "PUT", file);
    file->setParent(reply);  // the stream is reaped with the reply
    QObject::connect(reply, &QNetworkReply::finished, reply, &QObject::deleteLater);
    QPointer<QObject> guard(ctx);
    QObject::connect(reply, &QNetworkReply::finished, ctx ? ctx : reply, [reply, guard, ctx, cb, uploadUrl]() {
        if (ctx && guard.isNull())
            return;
        Response out;
        out.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray data = reply->readAll();
        out.body = data;
        out.ok = (reply->error() == QNetworkReply::NoError) && out.status >= 200 && out.status < 300;
        if (!out.ok) {
            const QString detail = data.isEmpty() ? reply->errorString() : QString::fromUtf8(data.left(200));
            out.error = QStringLiteral("PUT %1 failed (HTTP %2): %3").arg(uploadUrl.path(), QString::number(out.status), detail);
        }
        if (cb)
            cb(out);
    });
}

void uploadIcon(QObject* ctx, const QString& id, const QByteArray& bytes, Callback cb)
{
    request(ctx, "PUT", serviceUrl("/instances/" + id + "/icon"), "application/octet-stream", bytes, Auth::ServiceBearer,
            std::move(cb));
}

void createInvite(QObject* ctx, const QString& id, int maxAgeSeconds, int maxUses, Callback cb)
{
    QJsonObject payload;
    payload["max_age"] = maxAgeSeconds;
    payload["max_uses"] = maxUses;
    sendJson(ctx, "POST", serviceUrl("/instances/" + id + "/invites"), payload, Auth::ServiceBearer, std::move(cb));
}

void getInviteInfo(QObject* ctx, const QString& inviteId, Callback cb)
{
    getJson(ctx, serviceUrl("/invites/" + QString::fromUtf8(QUrl::toPercentEncoding(inviteId))), Auth::None, std::move(cb));
}

void acceptInvite(QObject* ctx, const QString& instanceId, const QString& inviteId, Callback cb)
{
    request(ctx, "POST", serviceUrl("/instances/" + instanceId + "/invites/" + inviteId), QByteArray(), QByteArray(),
            Auth::ServiceBearer, [cb](const Response& res) {
                // "already has access" is success for our purposes.
                if (!res.ok && res.status == 400 && res.error.contains("already has access")) {
                    Response tolerated = res;
                    tolerated.ok = true;
                    cb(tolerated);
                    return;
                }
                cb(res);
            });
}

void acceptPendingInvite(QObject* ctx, const QString& instanceId, Callback cb)
{
    request(ctx, "POST", serviceUrl("/instances/" + instanceId + "/invites/pending"), QByteArray(), QByteArray(),
            Auth::ServiceBearer, std::move(cb));
}

void declinePendingInvite(QObject* ctx, const QString& instanceId, Callback cb)
{
    request(ctx, "DELETE", serviceUrl("/instances/" + instanceId + "/invites/pending"), QByteArray(), QByteArray(),
            Auth::ServiceBearer, [cb](const Response& res) {
                if (!res.ok && res.status == 404) {
                    Response tolerated = res;
                    tolerated.ok = true;
                    cb(tolerated);
                    return;
                }
                cb(res);
            });
}

QString inviteLink(const QString& inviteId)
{
    return siteUrl() + "/share/" + QString::fromUtf8(QUrl::toPercentEncoding(inviteId));
}

QString parseInviteRef(const QString& ref)
{
    // Invite ids are plain base62; anything else never reaches the API.
    static const QRegularExpression idPattern(QStringLiteral("^[0-9A-Za-z]{1,64}$"));

    QString candidate = ref.trimmed();
    // Accept links pasted without a scheme ("modrinth.com/share/abc"); a bare
    // invite id never contains a slash, so this cannot misparse one.
    QUrl url = candidate.contains('/') ? QUrl::fromUserInput(candidate) : QUrl(candidate);
    if (url.isValid() && !url.host().isEmpty()) {
        // Only trust share links that actually point at Modrinth; a link from
        // any other site must not be treated as an invite.
        const QString host = url.host().toLower();
        if (host != QLatin1String("modrinth.com") && !host.endsWith(QLatin1String(".modrinth.com")))
            return {};
        auto parts = url.path().split('/', Qt::SkipEmptyParts);
        int idx = parts.indexOf("share");
        if (idx == -1 || idx + 1 >= parts.size())
            return {};
        candidate = parts[idx + 1];
    }
    return idPattern.match(candidate).hasMatch() ? candidate : QString();
}

}  // namespace ModrinthShared
