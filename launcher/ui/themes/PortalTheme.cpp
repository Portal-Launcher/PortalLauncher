// SPDX-License-Identifier: GPL-3.0-only
#include "PortalTheme.h"

#include <QObject>

QString PortalTheme::id()
{
    return "portal";
}

QString PortalTheme::name()
{
    return QObject::tr("Portal");
}

QString PortalTheme::tooltip()
{
    return QObject::tr("The Portal look: obsidian purple with a violet glow.");
}

QPalette PortalTheme::colorScheme()
{
    // Drawn from the Portal logo: obsidian block purples, the violet vortex,
    // and the magenta crack glow.
    QPalette palette;
    palette.setColor(QPalette::Window, QColor(0x1b, 0x15, 0x23));
    palette.setColor(QPalette::WindowText, QColor(0xe6, 0xe0, 0xf0));
    palette.setColor(QPalette::Base, QColor(0x14, 0x10, 0x19));
    palette.setColor(QPalette::AlternateBase, QColor(0x22, 0x1a, 0x2e));
    palette.setColor(QPalette::ToolTipBase, QColor(0x24, 0x1c, 0x31));
    palette.setColor(QPalette::ToolTipText, QColor(0xe6, 0xe0, 0xf0));
    palette.setColor(QPalette::Text, QColor(0xe6, 0xe0, 0xf0));
    palette.setColor(QPalette::Button, QColor(0x2a, 0x21, 0x38));
    palette.setColor(QPalette::ButtonText, QColor(0xe6, 0xe0, 0xf0));
    palette.setColor(QPalette::BrightText, QColor(0xff, 0x2b, 0xd8));
    palette.setColor(QPalette::Link, QColor(0xb4, 0x6c, 0xff));
    palette.setColor(QPalette::Highlight, QColor(0x8a, 0x46, 0xe0));
    palette.setColor(QPalette::HighlightedText, Qt::white);
    palette.setColor(QPalette::PlaceholderText, QColor(0x8d, 0x84, 0xa0));
    return fadeInactive(palette, fadeAmount(), fadeColor());
}

double PortalTheme::fadeAmount()
{
    return 0.5;
}

QColor PortalTheme::fadeColor()
{
    return QColor(0x1b, 0x15, 0x23);
}

bool PortalTheme::hasStyleSheet()
{
    return true;
}

QString PortalTheme::appStyleSheet()
{
    return "QToolTip { color: #e6e0f0; background-color: #241c31; border: 1px solid #45356a; border-radius: 4px; }";
}
