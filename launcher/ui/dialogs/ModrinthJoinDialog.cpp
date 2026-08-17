// SPDX-License-Identifier: GPL-3.0-only
#include "ModrinthJoinDialog.h"

#include <QClipboard>
#include <QDialogButtonBox>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QMessageBox>
#include <QShowEvent>
#include <QVBoxLayout>

#include "Application.h"
#include "InstanceList.h"
#include "modplatform/modrinth/shared/ModrinthJoinFlow.h"
#include "modplatform/modrinth/shared/ModrinthSharedApi.h"
#include "modplatform/modrinth/shared/ModrinthSharedAttachment.h"
#include "modplatform/modrinth/shared/ModrinthSignInTask.h"
#include "ui/dialogs/ProgressDialog.h"

namespace {
/** The trust prompt, in plain text so a remote-controlled pack name can never
 *  restyle the dialog that carries the warning. */
bool confirmJoin(QWidget* parent, const QString& instanceName)
{
    QMessageBox confirm(QMessageBox::Question, QObject::tr("Join \"%1\"?").arg(instanceName),
                        QObject::tr("You are about to install \"%1\" from a shared instance.\n\nShared instances are not "
                                    "reviewed by Modrinth - only accept invites from people you trust.")
                            .arg(instanceName),
                        QMessageBox::Yes | QMessageBox::No, parent);
    confirm.setTextFormat(Qt::PlainText);
    return confirm.exec() == QMessageBox::Yes;
}
}  // namespace

ModrinthJoinDialog::ModrinthJoinDialog(QWidget* parent) : QDialog(parent)
{
    setWindowTitle(tr("Join a shared pack"));
    setMinimumWidth(560);

    auto* layout = new QVBoxLayout(this);
    auto* info = new QLabel(
        tr("Paste the invite link a friend sent you (it looks like <i>modrinth.com/share/…</i>).<br>"
           "The modpack appears as a normal instance and updates itself whenever they push changes."),
        this);
    info->setWordWrap(true);
    layout->addWidget(info);

    auto* linkRow = new QHBoxLayout();
    m_linkEdit = new QLineEdit(this);
    m_linkEdit->setPlaceholderText(tr("https://modrinth.com/share/…"));
    m_joinButton = new QPushButton(tr("Join"), this);
    m_joinButton->setDefault(true);
    linkRow->addWidget(m_linkEdit, 1);
    linkRow->addWidget(m_joinButton);
    layout->addLayout(linkRow);

    m_invitesLabel = new QLabel(tr("Invites sent to your Modrinth account:"), this);
    m_invitesList = new QListWidget(this);
    m_invitesList->setMaximumHeight(180);
    m_acceptInviteButton = new QPushButton(tr("Accept selected invite"), this);
    m_acceptInviteButton->setEnabled(false);
    layout->addWidget(m_invitesLabel);
    layout->addWidget(m_invitesList);
    layout->addWidget(m_acceptInviteButton);
    m_invitesLabel->hide();
    m_invitesList->hide();
    m_acceptInviteButton->hide();
    connect(m_invitesList, &QListWidget::itemSelectionChanged, this,
            [this]() { m_acceptInviteButton->setEnabled(m_invitesList->currentItem() != nullptr &&
                                                        !m_invitesList->currentItem()->data(Qt::UserRole).toString().isEmpty()); });

    m_statusLabel = new QLabel(this);
    m_statusLabel->setWordWrap(true);
    layout->addWidget(m_statusLabel);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    connect(m_joinButton, &QPushButton::clicked, this, &ModrinthJoinDialog::joinByLink);
    connect(m_acceptInviteButton, &QPushButton::clicked, this, &ModrinthJoinDialog::acceptSelectedInvite);
    connect(m_invitesList, &QListWidget::itemDoubleClicked, this, &ModrinthJoinDialog::acceptSelectedInvite);
}

void ModrinthJoinDialog::showEvent(QShowEvent* event)
{
    QDialog::showEvent(event);
    // If an invite link is already on the clipboard, save the user the paste.
    if (m_linkEdit->text().isEmpty()) {
        const QString clip = QGuiApplication::clipboard()->text().trimmed();
        if (!clip.isEmpty() && clip.contains('/') && !ModrinthShared::parseInviteRef(clip).isEmpty())
            m_linkEdit->setText(clip);
    }
    if (!m_loadedInvites && ModrinthShared::isSignedIn()) {
        m_loadedInvites = true;
        ModrinthShared::refreshSessionIfNeeded(this);
        loadPendingInvites();
    }
}

bool ModrinthJoinDialog::ensureSignedIn()
{
    if (ModrinthShared::isSignedIn())
        return true;
    ModrinthSignInTask task;
    ProgressDialog dialog(this);
    dialog.setSkipButton(true, tr("Cancel"));
    if (dialog.execWithTask(&task) != QDialog::Accepted)
        return false;
    if (!m_loadedInvites) {
        m_loadedInvites = true;
        loadPendingInvites();
    }
    return true;
}

void ModrinthJoinDialog::loadPendingInvites()
{
    // Show the section immediately with a loading row, so a slow network does
    // not look like "no invites".
    auto showPlaceholder = [this](const QString& text) {
        m_invitesList->clear();
        auto* placeholder = new QListWidgetItem(text, m_invitesList);
        placeholder->setFlags(Qt::NoItemFlags);
        m_invitesLabel->show();
        m_invitesList->show();
        m_acceptInviteButton->show();
        m_acceptInviteButton->setEnabled(false);
    };
    showPlaceholder(tr("Checking for invites…"));

    ModrinthShared::getNotifications(this, [this, showPlaceholder](const ModrinthShared::Response& res) {
        if (!res.ok || !res.json.isArray()) {
            showPlaceholder(tr("Could not check for invites right now."));
            return;
        }
        QSet<QString> joined;
        auto* instances = APPLICATION->instances();
        for (int i = 0; i < instances->count(); i++) {
            if (auto att = ModrinthShared::Attachment::load(instances->at(i)->instanceRoot()))
                joined.insert(att->id);
        }
        m_invitesList->clear();
        for (const auto& value : res.json.array()) {
            const auto notification = value.toObject();
            const auto body = notification.value("body").toObject();
            const QString type = body.value("type").toString(notification.value("type").toString());
            if (type != QLatin1String("shared_instance_invite"))
                continue;
            const QString instanceId = body.value("shared_instance_id").toString();
            if (instanceId.isEmpty() || joined.contains(instanceId))
                continue;
            QString name = body.value("shared_instance_name").toString();
            if (name.trimmed().isEmpty())
                name = tr("Shared pack");
            auto* item = new QListWidgetItem(name, m_invitesList);
            item->setData(Qt::UserRole, instanceId);
            item->setData(Qt::UserRole + 1, name);
        }
        if (m_invitesList->count() == 0)
            showPlaceholder(tr("No pending invites right now."));
    });
}

void ModrinthJoinDialog::joinByLink()
{
    if (!ensureSignedIn())
        return;
    const QString inviteId = ModrinthShared::parseInviteRef(m_linkEdit->text());
    if (inviteId.isEmpty()) {
        m_statusLabel->setText(tr("Please paste an invite link first."));
        return;
    }

    m_joinButton->setEnabled(false);
    m_statusLabel->setText(tr("Looking up the invite…"));

    ModrinthShared::getInviteInfo(this, inviteId, [this, inviteId](const ModrinthShared::Response& res) {
        if (!res.ok || !res.json.isObject()) {
            m_joinButton->setEnabled(true);
            m_statusLabel->setText(tr("That invite does not exist or has expired. Ask your friend for a new link."));
            return;
        }
        const auto invite = res.json.object();
        const QString instanceId = invite.value("instance_id").toString();
        QString instanceName = invite.value("instance_name").toString();
        if (instanceName.trimmed().isEmpty())
            instanceName = tr("Shared pack");

        if (!confirmJoin(this, instanceName)) {
            m_joinButton->setEnabled(true);
            m_statusLabel->clear();
            return;
        }

        m_statusLabel->setText(tr("Accepting the invite…"));
        ModrinthShared::acceptInvite(this, instanceId, inviteId,
                                     [this, instanceId, instanceName](const ModrinthShared::Response& acceptRes) {
                                         m_joinButton->setEnabled(true);
                                         if (!acceptRes.ok) {
                                             m_statusLabel->setText(acceptRes.error);
                                             return;
                                         }
                                         installShared(instanceId, instanceName);
                                     });
    });
}

void ModrinthJoinDialog::acceptSelectedInvite()
{
    if (!ensureSignedIn())
        return;
    auto* item = m_invitesList->currentItem();
    if (!item || item->data(Qt::UserRole).toString().isEmpty())
        return;
    const QString instanceId = item->data(Qt::UserRole).toString();
    const QString name = item->data(Qt::UserRole + 1).toString();

    if (!confirmJoin(this, name))
        return;

    m_acceptInviteButton->setEnabled(false);
    m_statusLabel->setText(tr("Accepting the invite…"));
    ModrinthShared::acceptPendingInvite(this, instanceId, [this, instanceId, name](const ModrinthShared::Response& res) {
        m_acceptInviteButton->setEnabled(true);
        // 404 = the pending invite is gone but we may already have access.
        if (!res.ok && res.status != 404) {
            m_statusLabel->setText(res.error);
            return;
        }
        installShared(instanceId, name);
    });
}

void ModrinthJoinDialog::installShared(const QString& instanceId, const QString& instanceName)
{
    m_statusLabel->setText(tr("Fetching the shared pack…"));
    ModrinthShared::runJoinFlow(this, instanceId, instanceName, [this, instanceName](bool joined, const QString& message) {
        if (!joined) {
            m_statusLabel->setText(message);
            return;
        }
        QMessageBox::information(this, tr("Joined!"),
                                 tr("\"%1\" is now in your instance list. It checks for the owner's updates every "
                                    "time you press Play.")
                                     .arg(instanceName));
        accept();
    });
}
