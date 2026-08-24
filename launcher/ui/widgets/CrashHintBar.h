// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Prism Launcher fork - Crash Doctor
 *
 *  Banner above the log view: what crashed, in plain language, with a
 *  one-click way to disable the implicated mod and to copy a report.
 */
#pragma once

#include <QFrame>
#include <QLabel>
#include <QPushButton>

#include "logs/CrashAnalyzer.h"

class CrashHintBar : public QFrame {
    Q_OBJECT
   public:
    explicit CrashHintBar(QWidget* parent = nullptr);

    /** Show the findings (first one prominently). culpritModName is the
     *  display name of the installed mod matched to the first finding's
     *  culprit list, or empty when none matched. */
    void showFindings(const QList<CrashAnalyzer::Finding>& findings, const QString& culpritModName);
    void clearFindings();

   signals:
    /** The user clicked "Disable <mod>" for the matched culprit. */
    void disableCulpritRequested();

   private:
    QLabel* m_title;
    QLabel* m_body;
    QPushButton* m_disableButton;
    QPushButton* m_copyButton;
    QPushButton* m_dismissButton;
    QList<CrashAnalyzer::Finding> m_findings;
};
