// SPDX-License-Identifier: GPL-3.0-only
#include "ForkUpdater.h"

#include <QDateTime>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QProcess>
#include <QVersionNumber>

#include "Application.h"
#include "BuildConfig.h"
#include "settings/Setting.h"
#include "settings/SettingsObject.h"

namespace ForkUpdater {

QString scriptPath()
{
    // Developer-only rebuild-from-source flow. There is deliberately no
    // default: the setting has to be pointed at an upgrade script by hand,
    // so released builds never run a script from a well-known path.
    return APPLICATION->settings()->getOrRegisterSetting("ForkUpgradeScript", QString())->get().toString();
}

bool available()
{
    const QString path = scriptPath();
    return !path.isEmpty() && QFileInfo(path).isFile();
}

static void launchUpgradeAndQuit(QWidget* parent)
{
    if (!QProcess::startDetached("powershell.exe", { "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", scriptPath(),
                                                     "-WaitForExit", "-Relaunch" })) {
        QMessageBox::warning(parent, QObject::tr("Update failed"),
                             QObject::tr("Could not start the upgrade script:\n%1").arg(scriptPath()));
        return;
    }
    QMetaObject::invokeMethod(APPLICATION, &QApplication::quit, Qt::QueuedConnection);
}

void check(QWidget* parent, bool silent)
{
    if (silent) {
        // At most one automatic check per day.
        auto setting = APPLICATION->settings()->getOrRegisterSetting("ForkUpdateLastCheck", "");
        const qint64 last = setting->get().toString().toLongLong();
        const qint64 now = QDateTime::currentSecsSinceEpoch();
        if (last != 0 && now - last < 24 * 3600)
            return;
        setting->set(QString::number(now));
    }

    // Check this fork's own releases, not upstream's.
    const QString repoPath = QUrl(BuildConfig.UPDATER_GITHUB_REPO).path();
    QNetworkRequest request(QUrl("https://api.github.com/repos" + repoPath + "/releases/latest"));
    request.setHeader(QNetworkRequest::UserAgentHeader, APPLICATION->getUserAgent().toUtf8());
    request.setTransferTimeout(20000);
    QNetworkReply* reply = APPLICATION->network()->get(request);
    QObject::connect(reply, &QNetworkReply::finished, reply, &QObject::deleteLater);
    QPointer<QWidget> guard(parent);

    QObject::connect(reply, &QNetworkReply::finished, parent, [reply, guard, parent, silent]() {
        if (guard.isNull())
            return;

        if (reply->error() != QNetworkReply::NoError) {
            if (!silent)
                QMessageBox::warning(parent, QObject::tr("Update check failed"),
                                     QObject::tr("Could not reach GitHub to check for new releases:\n%1").arg(reply->errorString()));
            return;
        }

        const auto release = QJsonDocument::fromJson(reply->readAll()).object();
        QString latestTag = release.value("tag_name").toString();
        if (latestTag.startsWith('v'))
            latestTag.remove(0, 1);
        const auto latest = QVersionNumber::fromString(latestTag);
        const auto current = QVersionNumber::fromString(BuildConfig.printableVersionString().section('-', 0, 0));
        if (latest.isNull()) {
            if (!silent)
                QMessageBox::warning(parent, QObject::tr("Update check failed"),
                                     QObject::tr("GitHub returned an unexpected response."));
            return;
        }

        if (latest <= current) {
            if (!silent)
                QMessageBox::information(parent, QObject::tr("No updates"),
                                         QObject::tr("You are running the newest %1 (%2).")
                                             .arg(BuildConfig.LAUNCHER_DISPLAYNAME, current.toString()));
            return;
        }

        const auto answer = QMessageBox::question(
            parent, QObject::tr("%1 %2 is available").arg(BuildConfig.LAUNCHER_DISPLAYNAME, latestTag),
            QObject::tr("You are running %1 %2. Version %3 is available.\n\n"
                        "Updating rebuilds the launcher from source with all of its features included. "
                        "This takes roughly 5-20 minutes; the launcher will close now and "
                        "reopen automatically when the update is done.\n\nUpdate now?")
                .arg(BuildConfig.LAUNCHER_DISPLAYNAME, current.toString(), latestTag),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
        if (answer == QMessageBox::Yes)
            launchUpgradeAndQuit(parent);
    });
}

}  // namespace ForkUpdater
