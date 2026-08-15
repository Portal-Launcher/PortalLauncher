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

static const char* DEFAULT_SCRIPT = "C:/Users/tinsl/OneDrive/Documents/PrisimUPDATE/upgrade-prism.ps1";

QString scriptPath()
{
    return APPLICATION->settings()->getOrRegisterSetting("ForkUpgradeScript", QString::fromUtf8(DEFAULT_SCRIPT))->get().toString();
}

bool available()
{
    return QFileInfo(scriptPath()).isFile();
}

static void launchUpgradeAndQuit()
{
    QProcess::startDetached("powershell.exe", { "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", scriptPath(),
                                                "-WaitForExit", "-Relaunch" });
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

    QNetworkRequest request(QUrl("https://api.github.com/repos/PrismLauncher/PrismLauncher/releases/latest"));
    request.setHeader(QNetworkRequest::UserAgentHeader, APPLICATION->getUserAgent().toUtf8());
    request.setTransferTimeout(20000);
    QNetworkReply* reply = APPLICATION->network()->get(request);
    QPointer<QWidget> guard(parent);

    QObject::connect(reply, &QNetworkReply::finished, parent, [reply, guard, parent, silent]() {
        reply->deleteLater();
        if (guard.isNull())
            return;

        if (reply->error() != QNetworkReply::NoError) {
            if (!silent)
                QMessageBox::warning(parent, QObject::tr("Update check failed"),
                                     QObject::tr("Could not reach GitHub to check for Prism releases:\n%1").arg(reply->errorString()));
            return;
        }

        const auto release = QJsonDocument::fromJson(reply->readAll()).object();
        const QString latestTag = release.value("tag_name").toString();
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
                QMessageBox::information(
                    parent, QObject::tr("No updates"),
                    QObject::tr("You are running the newest Prism Launcher (%1) — with Shared Instances included.")
                        .arg(current.toString()));
            return;
        }

        const auto answer = QMessageBox::question(
            parent, QObject::tr("Prism Launcher %1 is available").arg(latestTag),
            QObject::tr("You are running Prism Launcher %1. Version %2 is available.\n\n"
                        "Updating rebuilds the launcher from the new Prism release with your Shared Instances "
                        "features included. This takes roughly 5–20 minutes; the launcher will close now and "
                        "reopen automatically when the update is done.\n\nUpdate now?")
                .arg(current.toString(), latestTag),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
        if (answer == QMessageBox::Yes)
            launchUpgradeAndQuit();
    });
}

}  // namespace ForkUpdater
