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

/** The image surface of the gallery lightbox: fills its whole area, supports
 *  wheel zoom toward the cursor, drag panning, and double-click to toggle
 *  between fit and 100%. */
class ImageViewport final : public QWidget {
    Q_OBJECT

   public:
    explicit ImageViewport(QWidget* parent = nullptr);

    void setImage(const QImage& image);
    void setPlaceholderText(const QString& text);

    /** Scale-to-fit (never upscaling past 100%). */
    void zoomToFit();
    void zoomTo(qreal zoom, const QPointF& anchor);
    qreal currentScale() const;
    bool isFit() const { return m_fit; }

   signals:
    void zoomChanged();

   protected:
    void paintEvent(QPaintEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

   private:
    qreal fitScale() const;
    QRectF imageRect() const;
    void clampPan();
    void updateCursorShape();

    QImage m_image;
    QString m_placeholder;
    bool m_fit = true;
    qreal m_zoom = 1.0;
    QPointF m_pan;  // offset of the image center from the viewport center
    bool m_panning = false;
    QPointF m_lastDragPos;
};

/** A lightbox for project gallery images: the image fills the dialog, with
 *  navigation arrows, counter, close button, and caption overlaid on top.
 *  All images preload in the background when the dialog opens. */
class ImageViewerDialog final : public QDialog {
    Q_OBJECT

   public:
    ImageViewerDialog(QWidget* parent, QList<ModPlatform::GalleryImage> images, int startIndex, QString metaEntry);

   protected:
    void keyPressEvent(QKeyEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

   private:
    void preloadAll();
    void decodeIfNeeded(int index);
    void evictFarImages();
    void showImage(int index);
    void layoutOverlays();
    void updateOverlays();

    /** How far around the current index decoded frames are kept in memory. */
    static constexpr int EVICT_WINDOW = 2;

    QList<ModPlatform::GalleryImage> m_images;
    int m_index = 0;
    QString m_metaEntry;

    QHash<int, QImage> m_loaded;        // decoded images near the current index
    QHash<int, QString> m_cachedPaths;  // downloaded cache file per gallery index
    QHash<int, bool> m_failed;          // indices whose download or decode failed

    ImageViewport* m_viewport;
    QToolButton* m_prevButton;
    QToolButton* m_nextButton;
    QLabel* m_counterLabel;
    QLabel* m_zoomLabel;
    QWidget* m_captionBox;
    QLabel* m_titleLabel;
    QLabel* m_descriptionLabel;
};
