// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Prism Launcher fork - Modrinth Shared Instances support
 *
 *  Client for Modrinth's shared-instances service (the backend behind the
 *  official Modrinth App's "Share instance with friends" feature) plus the
 *  handful of main-API (labrinth) calls the feature needs.
 */
#pragma once

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QUrl>
#include <functional>

class QObject;

namespace ModrinthShared {

QString serviceBaseUrl();  // https://shared-instances.modrinth.com/v1
QString siteUrl();         // https://modrinth.com

struct Response {
    bool ok = false;
    int status = 0;
    QString error;
    QJsonDocument json;
};
using Callback = std::function<void(const Response&)>;

enum class Auth {
    None,           // no Authorization header
    Labrinth,       // Authorization: <token>   (api.modrinth.com style)
    ServiceBearer,  // Authorization: Bearer <token> (shared-instances service)
};

// --- Session (stored in global settings, next to the existing ModrinthToken) ---
QString token();
bool isSignedIn();
QString userId();
QString username();
void storeSession(const QString& token, const QString& userId, const QString& username, bool isSession);
void clearSession();
/** Fire-and-forget: refreshes a browser-login session token when it is old. */
void refreshSessionIfNeeded(QObject* ctx);

// --- Generic plumbing ---
void request(QObject* ctx,
             const QByteArray& verb,
             const QUrl& url,
             const QByteArray& contentType,
             const QByteArray& body,
             Auth auth,
             Callback cb);
void sendJson(QObject* ctx, const QByteArray& verb, const QUrl& url, const QJsonObject& payload, Auth auth, Callback cb);
void getJson(QObject* ctx, const QUrl& url, Auth auth, Callback cb);

// --- Main API (labrinth) ---
void validateToken(QObject* ctx, const QString& explicitToken, Callback cb);  // GET /user
void refreshSession(QObject* ctx, Callback cb);                               // POST /session/refresh
void lookupUserByName(QObject* ctx, const QString& name, Callback cb);
void getUsersByIds(QObject* ctx, const QStringList& ids, Callback cb);
void lookupVersionFiles(QObject* ctx, const QStringList& sha1Hashes, Callback cb);  // POST /version_files
void getVersionsBulk(QObject* ctx, const QStringList& versionIds, Callback cb);
void getProjectsBulk(QObject* ctx, const QStringList& projectIds, Callback cb);

// --- Shared-instances service ---
void createRemoteInstance(QObject* ctx, const QString& name, Callback cb);
void renameRemoteInstance(QObject* ctx, const QString& id, const QString& name, Callback cb);
void deleteRemoteInstance(QObject* ctx, const QString& id, Callback cb);
void getMembers(QObject* ctx, const QString& id, Callback cb);
void addMembers(QObject* ctx, const QString& id, const QStringList& userIds, Callback cb);
void removeMembers(QObject* ctx, const QString& id, const QStringList& userIds, Callback cb);
void getLatestVersion(QObject* ctx, const QString& id, Callback cb);
void createVersion(QObject* ctx, const QString& id, const QJsonObject& payload, Callback cb);
void uploadBytes(QObject* ctx, const QUrl& uploadUrl, const QByteArray& bytes, Callback cb);
void uploadIcon(QObject* ctx, const QString& id, const QByteArray& bytes, Callback cb);
void createInvite(QObject* ctx, const QString& id, int maxAgeSeconds, int maxUses, Callback cb);
void getInviteInfo(QObject* ctx, const QString& inviteId, Callback cb);  // unauthenticated
void acceptInvite(QObject* ctx, const QString& instanceId, const QString& inviteId, Callback cb);
void acceptPendingInvite(QObject* ctx, const QString& instanceId, Callback cb);

QString inviteLink(const QString& inviteId);
/** Extracts the invite id from a modrinth.com/share/... link or returns the input unchanged. */
QString parseInviteRef(const QString& ref);

}  // namespace ModrinthShared
