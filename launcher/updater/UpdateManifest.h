// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Prism Launcher - Minecraft Launcher
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 */

#pragma once

#include <QDir>
#include <QRegularExpression>
#include <QString>
#include <QStringList>

/** Parses a release manifest.txt (one relative path per line) into the list
 *  of files it names, dropping anything that could point outside the folder
 *  the manifest belongs to. The updater deletes files by these names after
 *  an install, so a hostile or corrupted manifest must never reach past the
 *  install folder. */
inline QStringList parseUpdateManifest(const QString& contents)
{
    // Drive-letter and UNC forms are rejected on every platform, not just the
    // one whose QDir considers them absolute: a manifest is data that travels.
    static const QRegularExpression s_driveOrUnc(QStringLiteral("^([A-Za-z]:[/\\\\]|[/\\\\]{2})"));
    QStringList entries;
    for (const auto& line : contents.split(QChar::LineFeed)) {
        const auto entry = line.trimmed();
        if (entry.isEmpty())
            continue;
        if (entry.startsWith('/') || entry.startsWith('\\') || entry.contains(QStringLiteral("..")) || QDir::isAbsolutePath(entry) ||
            s_driveOrUnc.match(entry).hasMatch())
            continue;
        entries.append(entry);
    }
    return entries;
}
