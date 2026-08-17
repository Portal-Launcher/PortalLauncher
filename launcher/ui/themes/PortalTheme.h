// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Portal Launcher - the Portal theme.
 *  A dark theme in the launcher's own branding: obsidian purple surfaces,
 *  violet selection, magenta accents. Selectable in Settings > Appearance
 *  alongside the stock themes.
 */
#pragma once

#include "FusionTheme.h"

class PortalTheme : public FusionTheme {
   public:
    virtual ~PortalTheme() {}

    QString id() override;
    QString name() override;
    QString tooltip() override;
    bool hasStyleSheet() override;
    QString appStyleSheet() override;
    QPalette colorScheme() override;
    double fadeAmount() override;
    QColor fadeColor() override;
};
