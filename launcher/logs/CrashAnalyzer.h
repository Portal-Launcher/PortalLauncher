// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Prism Launcher fork - Crash Doctor
 *
 *  Turns a crashed game's log into plain-language findings: what broke, which
 *  mod likely did it, and what to do about it. Pure functions over log text so
 *  the rules are unit-testable without a game.
 */
#pragma once

#include <QList>
#include <QString>
#include <QStringList>

namespace CrashAnalyzer {

struct Finding {
    QString title;        // one line: what happened
    QString explanation;  // 1-3 sentences: why, in user language
    QString suggestion;   // what to try first
    /** Mod ids or file-name fragments implicated by the log, best guess first.
     *  Callers can match these against the installed mod list to offer a
     *  "disable this mod" action. Empty when no specific mod is implicated. */
    QStringList culprits;
};

/** Scan a full log (or its tail) for known crash families. Findings arrive
 *  most-specific first; an empty list means nothing recognizable was found. */
QList<Finding> analyze(const QString& logText);

}  // namespace CrashAnalyzer
