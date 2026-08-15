// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Prism Launcher fork - Modrinth Shared Instances support
 *
 *  Reusable join flow: fetch the shared pack, create the local instance,
 *  run the first sync. Used by the Join dialog and the Add Instance page.
 */
#pragma once

#include <QString>
#include <functional>

class QWidget;

namespace ModrinthShared {

/**
 * Runs the whole "install a shared pack I have access to" flow with modal
 * progress dialogs parented to `parent`.
 * done(joined, message): joined=false with a non-empty message on failure or
 * when already joined.
 */
void runJoinFlow(QWidget* parent,
                 const QString& instanceId,
                 const QString& instanceName,
                 std::function<void(bool joined, const QString& message)> done);

struct PendingInvite {
    QString instanceId;
    QString instanceName;
};

/** Pending shared-pack invites for this account, minus packs already joined. */
void fetchPendingInvites(QObject* ctx, std::function<void(const QList<PendingInvite>&)> done);

}  // namespace ModrinthShared
