// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Prism Launcher fork - Modrinth Shared Instances support
 *
 *  Update path for this custom build: official Prism updates would erase the
 *  Shared Instances features, so "Check for Updates" instead rebases the
 *  feature branch onto the newest Prism release, rebuilds, and relaunches —
 *  via the upgrade script that ships with the fork.
 */
#pragma once

#include <QString>

class QWidget;

namespace ForkUpdater {

/** True when the upgrade script is present on this machine. */
bool available();

QString scriptPath();

/**
 * Check GitHub for a newer Prism release.
 * silent: only speak up when an update actually exists (startup check),
 * rate-limited to once per day.
 */
void check(QWidget* parent, bool silent);

}  // namespace ForkUpdater
