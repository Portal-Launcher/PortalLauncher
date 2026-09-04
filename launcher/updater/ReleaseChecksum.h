// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QRegularExpression>
#include <QString>

/** Release descriptions carry the sha256 of every download as HTML comment
 *  lines, written by .github/scripts/write-release-body.py:
 *
 *      <!--
 *      sha256 PortalLauncher-Windows-MSVC-1.0.8.zip 3f2a...
 *      -->
 *
 *  This is how the updater verifies a download without a ".sha256" file
 *  cluttering the release page. Returns the lowercase hex digest, or an empty
 *  string when the body has no entry for that asset. Header-only so the
 *  updater executable and the tests share it. */
inline QString releaseBodyChecksum(const QString& body, const QString& assetName)
{
    static const QRegularExpression line(QStringLiteral("(?m)^\\s*sha256\\s+(\\S+)\\s+([0-9A-Fa-f]{64})\\s*$"));
    auto it = line.globalMatch(body);
    while (it.hasNext()) {
        const auto match = it.next();
        if (match.captured(1).compare(assetName, Qt::CaseInsensitive) == 0)
            return match.captured(2).toLower();
    }
    return {};
}
