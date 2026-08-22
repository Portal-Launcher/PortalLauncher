// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Prism Launcher - Minecraft Launcher
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 */

#pragma once

#include <QRegularExpression>
#include <QString>

/** Decides which GitHub release tags the updater may offer as updates.
 *
 *  Only plain dotted versions (optionally v-prefixed) qualify. The fork
 *  inherited upstream Prism's tags, whose 11.x numbers outrank Portal's 1.x
 *  versioning, so anything with a suffix ("11.0.3-shared", "1.0.6-beta1")
 *  is deliberately invisible to the updater.
 */
inline bool isUpdateCandidateTag(const QString& tag)
{
    static const QRegularExpression s_plainVersion("^v?\\d+(\\.\\d+)*$");
    return s_plainVersion.match(tag).hasMatch();
}
