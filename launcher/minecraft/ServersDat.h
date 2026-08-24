// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Prism Launcher fork - servers.dat helpers
 *
 *  Minimal read/merge access to Minecraft's server list, used by shared
 *  instances to hand the owner's servers to everyone in the pack.
 */
#pragma once

#include <QList>
#include <QString>

namespace ServersDat {

struct Entry {
    QString name;
    QString address;
};

/** The server list of a servers.dat file; empty on missing or unreadable. */
QList<Entry> read(const QString& serversDatPath);

/** Append entries whose address is not in the file yet (case-insensitive);
 *  creates the file when missing, never reorders or removes anything the
 *  user already has. Returns how many entries were added, or -1 when the
 *  file could not be read or written. */
int mergeAppend(const QString& serversDatPath, const QList<Entry>& entries);

}  // namespace ServersDat
