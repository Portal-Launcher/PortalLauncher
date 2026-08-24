// SPDX-License-Identifier: GPL-3.0-only
#include "CrashHintBar.h"

#include <QApplication>
#include <QClipboard>
#include <QHBoxLayout>
#include <QStyle>
#include <QVBoxLayout>

CrashHintBar::CrashHintBar(QWidget* parent) : QFrame(parent)
{
    setFrameShape(QFrame::StyledPanel);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 6, 8, 6);

    auto* titleRow = new QHBoxLayout();
    auto* icon = new QLabel(this);
    // Palette-driven standard icon, readable on every theme.
    icon->setPixmap(style()->standardIcon(QStyle::SP_MessageBoxWarning).pixmap(20, 20));
    m_title = new QLabel(this);
    auto font = m_title->font();
    font.setBold(true);
    m_title->setFont(font);
    m_title->setWordWrap(true);
    m_title->setTextFormat(Qt::PlainText);
    m_dismissButton = new QPushButton(tr("Hide"), this);
    m_dismissButton->setFlat(true);
    titleRow->addWidget(icon);
    titleRow->addWidget(m_title, 1);
    titleRow->addWidget(m_dismissButton);
    layout->addLayout(titleRow);

    m_body = new QLabel(this);
    m_body->setWordWrap(true);
    m_body->setTextFormat(Qt::PlainText);
    layout->addWidget(m_body);

    auto* buttonRow = new QHBoxLayout();
    m_disableButton = new QPushButton(this);
    m_disableButton->hide();
    m_copyButton = new QPushButton(tr("Copy report"), this);
    m_copyButton->setToolTip(tr("Copies the diagnosis, handy when asking for help."));
    buttonRow->addWidget(m_disableButton);
    buttonRow->addWidget(m_copyButton);
    buttonRow->addStretch(1);
    layout->addLayout(buttonRow);

    connect(m_dismissButton, &QPushButton::clicked, this, &CrashHintBar::clearFindings);
    connect(m_disableButton, &QPushButton::clicked, this, &CrashHintBar::disableCulpritRequested);
    connect(m_copyButton, &QPushButton::clicked, this, [this]() {
        QStringList lines;
        for (const auto& finding : m_findings) {
            lines.append(finding.title);
            lines.append(finding.explanation);
            lines.append(tr("Suggestion: %1").arg(finding.suggestion));
            if (!finding.culprits.isEmpty())
                lines.append(tr("Possibly involved: %1").arg(finding.culprits.join(", ")));
            lines.append(QString());
        }
        QApplication::clipboard()->setText(lines.join('\n').trimmed());
    });

    hide();
}

void CrashHintBar::showFindings(const QList<CrashAnalyzer::Finding>& findings, const QString& culpritModName)
{
    m_findings = findings;
    if (findings.isEmpty()) {
        clearFindings();
        return;
    }
    const auto& first = findings.first();
    m_title->setText(findings.size() > 1 ? tr("%1 (and %n more finding(s))", "", findings.size() - 1).arg(first.title)
                                         : first.title);
    m_body->setText(first.explanation + '\n' + first.suggestion);
    if (!culpritModName.isEmpty()) {
        m_disableButton->setText(tr("Disable \"%1\"").arg(culpritModName));
        m_disableButton->show();
    } else {
        m_disableButton->hide();
    }
    show();
}

void CrashHintBar::clearFindings()
{
    m_findings.clear();
    hide();
}
