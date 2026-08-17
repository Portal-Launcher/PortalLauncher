// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Prism Launcher - Minecraft Launcher
 *  Copyright (c) 2022 Jamie Mansfield <jmansfield@cadixdev.org>
 *  Copyright (C) 2022 Sefa Eyeoglu <contact@scrumplex.net>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *      Copyright 2013-2021 MultiMC Contributors
 *
 *      Licensed under the Apache License, Version 2.0 (the "License");
 *      you may not use this file except in compliance with the License.
 *      You may obtain a copy of the License at
 *
 *          http://www.apache.org/licenses/LICENSE-2.0
 *
 *      Unless required by applicable law or agreed to in writing, software
 *      distributed under the License is distributed on an "AS IS" BASIS,
 *      WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *      See the License for the specific language governing permissions and
 *      limitations under the License.
 */

#include "ImportPage.h"

#include "ui/dialogs/ProgressDialog.h"
#include "ui_ImportPage.h"

#include <QFileDialog>
#include <QMimeDatabase>
#include <QPointer>
#include <QTimer>
#include <QValidator>
#include <utility>

#include "ui/dialogs/CustomMessageBox.h"
#include "ui/dialogs/NewInstanceDialog.h"

#include "Application.h"
#include "modplatform/flame/FlameAPI.h"

#include "Json.h"

#include "InstanceImportTask.h"
#include "modplatform/modrinth/shared/ModrinthJoinFlow.h"
#include "modplatform/modrinth/shared/ModrinthSharedApi.h"
#include "net/NetJob.h"

class UrlValidator : public QValidator {
   public:
    using QValidator::QValidator;

    State validate(QString& in, [[maybe_unused]] int& pos) const
    {
        const QUrl url(in);
        if (url.isValid() && !url.isRelative() && !url.isEmpty()) {
            return Acceptable;
        } else if (QFile::exists(in)) {
            return Acceptable;
        } else if (FlameAPI::isShareCode(in.trimmed())) {
            // A CurseForge share code, e.g. pasted from a friend.
            return Acceptable;
        } else {
            return Intermediate;
        }
    }
};

ImportPage::ImportPage(NewInstanceDialog* dialog, QWidget* parent) : QWidget(parent), ui(new Ui::ImportPage), dialog(dialog)
{
    ui->setupUi(this);
    ui->modpackEdit->setValidator(new UrlValidator(ui->modpackEdit));
    connect(ui->modpackEdit, &QLineEdit::textChanged, this, &ImportPage::updateState);

    m_cfCodeTimer = new QTimer(this);
    m_cfCodeTimer->setSingleShot(true);
    m_cfCodeTimer->setInterval(450);
    connect(m_cfCodeTimer, &QTimer::timeout, this, &ImportPage::lookupCurseForgeCode);
}

ImportPage::~ImportPage()
{
    if (m_cfMetaJob)
        m_cfMetaJob->abort();
    delete ui;
}

bool ImportPage::shouldDisplay() const
{
    return true;
}

void ImportPage::retranslate()
{
    ui->retranslateUi(this);
}

void ImportPage::openedImpl()
{
    updateState();
}

void ImportPage::updateState()
{
    if (!isOpened) {
        return;
    }
    if (ui->modpackEdit->hasAcceptableInput()) {
        QString input = ui->modpackEdit->text().trimmed();
        // CurseForge share code (a bare token, not a URL or file). Check this before
        // anything else: QUrl::fromUserInput() would turn a bare code into http://<code>
        // and the generic URL branch would happily try to download that.
        if (FlameAPI::isShareCode(input)) {
            if (APPLICATION->capabilities() & Application::SupportsFlame) {
                startCurseForgeCode(input);
            } else {
                dialog->setSuggestedPack();
            }
            return;
        }
        auto url = QUrl::fromUserInput(input);
        // Modrinth shared pack invite links install through the join flow, not a regular import
        const bool isModrinthHost = url.host().compare("modrinth.com", Qt::CaseInsensitive) == 0 ||
                                    url.host().endsWith(".modrinth.com", Qt::CaseInsensitive);
        if (isModrinthHost && url.path().split('/', Qt::SkipEmptyParts).contains("share")) {
            // Wait for a complete link (this fires on every keystroke), and
            // never start the modal join flow from inside the textChanged
            // signal itself: it runs nested event loops, and this page can be
            // destroyed while they are up.
            if (ModrinthShared::parseInviteRef(input).isEmpty() || m_joiningShare)
                return;
            m_joiningShare = true;
            dialog->setSuggestedPack();
            QPointer<ImportPage> self(this);
            QTimer::singleShot(0, this, [self, input]() {
                if (!self)
                    return;
                ModrinthShared::joinFromInviteRef(self, input, [self](bool joined) {
                    if (!self)
                        return;
                    self->m_joiningShare = false;
                    if (joined)
                        self->dialog->reject();  // the join flow already created the instance
                });
            });
            return;
        }
        if (url.isLocalFile()) {
            // FIXME: actually do some validation of what's inside here... this is fake AF
            QFileInfo fi(input);

            // Allow non-latin people to use ZIP files!
            bool isZip = QMimeDatabase().mimeTypeForUrl(url).suffixes().contains("zip");
            // mrpack is a modrinth pack
            bool isMRPack = fi.suffix() == "mrpack";

            if (fi.exists() && (isZip || isMRPack)) {
                auto extra_info = QMap(m_extra_info);
                qDebug() << "Pack Extra Info" << extra_info << m_extra_info;
                dialog->setSuggestedPack(fi.completeBaseName(), new InstanceImportTask(url, this, std::move(extra_info)));
                dialog->setSuggestedIcon("default");
            }
        } else if (url.scheme() == "curseforge") {
            // need to find the download link for the modpack
            // format of url curseforge://install?addonId=IDHERE&fileId=IDHERE
            QUrlQuery query(url);
            if (query.allQueryItemValues("addonId").isEmpty() || query.allQueryItemValues("fileId").isEmpty()) {
                qDebug() << "Invalid curseforge link:" << url;
                return;
            }
            auto addonId = query.allQueryItemValues("addonId")[0];
            auto fileId = query.allQueryItemValues("fileId")[0];

            auto api = FlameAPI();
            auto [job, array] = api.getFile(addonId, fileId);

            connect(job.get(), &NetJob::failed, this,
                    [this](QString reason) { CustomMessageBox::selectable(this, tr("Error"), reason, QMessageBox::Critical)->show(); });
            connect(job.get(), &NetJob::succeeded, this, [this, array, addonId, fileId] {
                qDebug() << "Returned CFURL Json:\n" << array->toStdString().c_str();
                auto doc = Json::requireDocument(*array);
                auto data = doc.object()["data"].toObject();
                // No way to find out if it's a mod or a modpack before here
                // And also we need to check if it ends with .zip, instead of any better way
                auto fileName = data["fileName"].toString();
                if (fileName.endsWith(".zip")) {
                    // Have to use ensureString then use QUrl to get proper url encoding
                    auto dl_url = QUrl(data["downloadUrl"].toString(""));
                    if (!dl_url.isValid()) {
                        CustomMessageBox::selectable(
                            this, tr("Error"),
                            tr("The modpack %1 is blocked for third-parties! Please download it manually.").arg(fileName),
                            QMessageBox::Critical)
                            ->show();
                        return;
                    }

                    QFileInfo dl_file(dl_url.fileName());
                    QString pack_name = data["displayName"].toString(dl_file.completeBaseName());

                    QMap<QString, QString> extra_info;
                    extra_info.insert("pack_id", addonId);
                    extra_info.insert("pack_version_id", fileId);

                    dialog->setSuggestedPack(pack_name, new InstanceImportTask(dl_url, this, std::move(extra_info)));
                    dialog->setSuggestedIcon("default");

                } else {
                    CustomMessageBox::selectable(this, tr("Error"), tr("This url isn't a valid modpack !"), QMessageBox::Critical)->show();
                }
            });
            ProgressDialog dlUrlDialod(this);
            dlUrlDialod.setSkipButton(true, tr("Abort"));
            dlUrlDialod.execWithTask(job.get());
            return;
        } else {
            if (input.endsWith("?client=y")) {
                input.chop(9);
                input.append("/file");
                url = QUrl::fromUserInput(input);
            }
            // hook, line and sinker.
            QFileInfo fi(url.fileName());
            auto extra_info = QMap(m_extra_info);
            dialog->setSuggestedPack(fi.completeBaseName(), new InstanceImportTask(url, this, std::move(extra_info)));
            dialog->setSuggestedIcon("default");
        }
    } else {
        m_cfCodePending.clear();
        m_cfCodeAuthor.clear();
        dialog->setSuggestedPack();
    }
}

void ImportPage::startCurseForgeCode(const QString& code)
{
    // Suggest the pack immediately so the OK button works without waiting on the
    // network. The download URL streams a standard CurseForge modpack zip, which the
    // regular Flame import path (InstanceImportTask -> FlameCreationTask) understands.
    // The real pack name comes from the manifest during import; this suggested name is
    // just the editable default shown in the dialog.
    auto extra_info = QMap(m_extra_info);
    const QString name = m_cfCodeAuthor.isEmpty() ? tr("CurseForge modpack") : tr("%1's modpack").arg(m_cfCodeAuthor);
    dialog->setSuggestedPack(name, new InstanceImportTask(QUrl(FlameAPI::shareProfileDownloadUrl(code)), this, std::move(extra_info)));
    dialog->setSuggestedIcon("default");

    // Only hit the network when the code actually changes, and debounce it so we do
    // not fire on every keystroke.
    if (m_cfCodePending != code) {
        m_cfCodePending = code;
        m_cfCodeAuthor.clear();
        m_cfCodeTimer->start();
    }
}

void ImportPage::lookupCurseForgeCode()
{
    const QString code = m_cfCodePending;
    if (code.isEmpty())
        return;

    if (m_cfMetaJob)
        m_cfMetaJob->abort();

    auto api = FlameAPI();
    auto [job, response] = api.getSharedProfileMetadata(code);
    m_cfMetaJob = job;

    QPointer<ImportPage> self(this);
    connect(job.get(), &NetJob::succeeded, this, [self, response, code]() {
        if (!self)
            return;
        // Ignore stale replies: the field may have moved on to another code.
        if (self->ui->modpackEdit->text().trimmed() != code)
            return;

        QString author;
        try {
            auto doc = Json::requireDocument(*response);
            author = doc.object()["data"].toObject()["sharedBy"].toString();
        } catch (const Json::JsonException&) {
        }

        if (!author.isEmpty() && author != self->m_cfCodeAuthor) {
            self->m_cfCodeAuthor = author;
            auto extra_info = QMap(self->m_extra_info);
            self->dialog->setSuggestedPack(tr("%1's modpack").arg(author),
                                           new InstanceImportTask(QUrl(FlameAPI::shareProfileDownloadUrl(code)), self, std::move(extra_info)));
            self->dialog->setSuggestedIcon("default");
        }
    });
    // On failure we keep the provisional suggestion; if the code is really invalid or
    // expired, the import itself will surface a clear download error.

    m_cfMetaJob->start();
}

void ImportPage::setUrl(const QString& url)
{
    ui->modpackEdit->setText(url);
    updateState();
}

void ImportPage::setExtraInfo(const QMap<QString, QString>& extra_info)
{
    m_extra_info = extra_info;
    updateState();
}

void ImportPage::on_modpackBtn_clicked()
{
    const QMimeType zip = QMimeDatabase().mimeTypeForName("application/zip");
    auto filter = tr("Supported files") + QString(" (%1 *.mrpack)").arg(zip.globPatterns().join(" "));
    filter += ";;" + zip.filterString();
    //: Option for filtering for *.mrpack files when importing
    filter += ";;" + tr("Modrinth pack") + " (*.mrpack)";
    const QUrl url = QFileDialog::getOpenFileUrl(this, tr("Choose modpack"), modpackUrl(), filter);
    if (url.isValid()) {
        if (url.isLocalFile()) {
            ui->modpackEdit->setText(url.toLocalFile());
        } else {
            ui->modpackEdit->setText(url.toString());
        }
    }
}

QUrl ImportPage::modpackUrl() const
{
    const QUrl url(ui->modpackEdit->text());
    if (url.isValid() && !url.isRelative() && !url.host().isEmpty()) {
        return url;
    } else {
        return QUrl::fromLocalFile(ui->modpackEdit->text());
    }
}
