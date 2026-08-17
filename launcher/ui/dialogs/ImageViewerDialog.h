// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Prism Launcher - Minecraft Launcher
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include <QDialog>
#include <QHash>
#include <QImage>

#include "modplatform/ModIndex.h"

class QLabel;
class QToolButton;

/** A lightbox for project gallery images: full-size image, caption, and
 *  previous / next navigation. All images preload in the background when the
 *  dialog opens, so navigation is instant once they have arrived. */
class ImageViewerDialog final : public QDialog {
    Q_OBJECT

   public:
    ImageViewerDialog(QWidget* parent, QList<ModPlatform::GalleryImage> images, int startIndex, QString metaEntry);

   protected:
    void keyPressEvent(QKeyEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

   private:
    void preloadAll();
    void showImage(int index);
    void updatePixmap();
    void updateNavState();

   private:
    QList<ModPlatform::GalleryImage> m_images;
    int m_index = 0;
    QString m_metaEntry;

    QHash<int, QImage> m_loaded;   // decoded full images by gallery index
    QHash<int, bool> m_failed;     // indices whose download or decode failed

    QLabel* m_imageLabel;
    QLabel* m_titleLabel;
    QLabel* m_descriptionLabel;
    QLabel* m_counterLabel;
    QToolButton* m_prevButton;
    QToolButton* m_nextButton;
};
