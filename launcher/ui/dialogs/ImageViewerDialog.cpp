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

#include "ImageViewerDialog.h"

#include <QApplication>
#include <QCryptographicHash>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QMovie>
#include <QPainter>
#include <QScreen>
#include <QTimer>
#include <QToolButton>
#include <QWheelEvent>
#include <QtMath>
#include <utility>

#include "Application.h"
#include "DesktopServices.h"
#include "MediaUtils.h"
#include "net/ApiDownload.h"
#include "net/NetJob.h"

// ---------------------------------------------------------------------------
// ImageViewport

ImageViewport::ImageViewport(QWidget* parent) : QWidget(parent)
{
    setMouseTracking(true);
}

void ImageViewport::setImage(const QImage& image)
{
    m_image = image;
    m_placeholder.clear();
    zoomToFit();
}

void ImageViewport::setFrame(const QImage& image)
{
    m_image = image;
    m_placeholder.clear();
    update();
}

void ImageViewport::setPlaceholderText(const QString& text)
{
    m_image = QImage();
    m_placeholder = text;
    update();
}

qreal ImageViewport::fitScale() const
{
    if (m_image.isNull() || width() <= 0 || height() <= 0)
        return 1.0;
    const qreal scaleX = qreal(width()) / m_image.width();
    const qreal scaleY = qreal(height()) / m_image.height();
    // fit inside the viewport, but never upscale past 100%
    return qMin(1.0, qMin(scaleX, scaleY));
}

qreal ImageViewport::currentScale() const
{
    return m_fit ? fitScale() : m_zoom;
}

QRectF ImageViewport::imageRect() const
{
    const qreal scale = currentScale();
    const QSizeF size = QSizeF(m_image.size()) * scale;
    const QPointF center = QPointF(width(), height()) / 2.0 + m_pan;
    return { center.x() - size.width() / 2.0, center.y() - size.height() / 2.0, size.width(), size.height() };
}

void ImageViewport::zoomToFit()
{
    m_fit = true;
    m_zoom = fitScale();
    m_pan = QPointF();
    updateCursorShape();
    update();
    emit zoomChanged();
}

void ImageViewport::zoomTo(qreal zoom, const QPointF& anchor)
{
    if (m_image.isNull())
        return;

    const qreal oldScale = currentScale();
    const qreal newScale = qBound(0.05, zoom, 8.0);
    if (qFuzzyCompare(oldScale, newScale))
        return;

    // Keep the point under the anchor stationary while the scale changes.
    const QPointF center = QPointF(width(), height()) / 2.0 + m_pan;
    const QPointF anchorFromCenter = anchor - center;
    m_pan += anchorFromCenter - anchorFromCenter * (newScale / oldScale);

    m_zoom = newScale;
    m_fit = false;
    clampPan();
    updateCursorShape();
    update();
    emit zoomChanged();
}

void ImageViewport::clampPan()
{
    if (m_image.isNull())
        return;
    const QSizeF size = QSizeF(m_image.size()) * currentScale();

    // Center any axis that fits; clamp the other so the image edge never
    // detaches from the viewport edge.
    if (size.width() <= width()) {
        m_pan.setX(0);
    } else {
        const qreal maxX = (size.width() - width()) / 2.0;
        m_pan.setX(qBound(-maxX, m_pan.x(), maxX));
    }
    if (size.height() <= height()) {
        m_pan.setY(0);
    } else {
        const qreal maxY = (size.height() - height()) / 2.0;
        m_pan.setY(qBound(-maxY, m_pan.y(), maxY));
    }
}

void ImageViewport::updateCursorShape()
{
    if (m_panning) {
        setCursor(Qt::ClosedHandCursor);
        return;
    }
    const QSizeF size = m_image.isNull() ? QSizeF() : QSizeF(m_image.size()) * currentScale();
    const bool pannable = size.width() > width() || size.height() > height();
    setCursor(pannable ? Qt::OpenHandCursor : Qt::ArrowCursor);
}

void ImageViewport::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event)
    QPainter painter(this);
    painter.fillRect(rect(), QColor(0x0d, 0x0e, 0x10));

    if (m_image.isNull()) {
        painter.setPen(QColor(0x8f, 0x96, 0xa0));
        painter.drawText(rect(), Qt::AlignCenter, m_placeholder);
        return;
    }

    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    // Draw only the visible part of the image: scaling the whole frame on
    // every pan/zoom repaint makes dragging large screenshots choppy.
    const QRectF target = imageRect();
    const QRectF visibleTarget = target.intersected(QRectF(rect()));
    if (visibleTarget.isEmpty())
        return;
    const qreal scale = currentScale();
    const QRectF sourceRect((visibleTarget.left() - target.left()) / scale, (visibleTarget.top() - target.top()) / scale,
                            visibleTarget.width() / scale, visibleTarget.height() / scale);
    painter.drawImage(visibleTarget, m_image, sourceRect);
}

void ImageViewport::wheelEvent(QWheelEvent* event)
{
    if (m_image.isNull())
        return;
    const qreal steps = event->angleDelta().y() / 120.0;
    if (qFuzzyIsNull(steps))
        return;
    zoomTo(currentScale() * qPow(1.2, steps), event->position());
    event->accept();
}

void ImageViewport::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && !m_image.isNull()) {
        m_panning = true;
        m_lastDragPos = event->position();
        updateCursorShape();
    }
    QWidget::mousePressEvent(event);
}

void ImageViewport::mouseMoveEvent(QMouseEvent* event)
{
    if (m_panning) {
        m_pan += event->position() - m_lastDragPos;
        m_lastDragPos = event->position();
        clampPan();
        update();
    }
    QWidget::mouseMoveEvent(event);
}

void ImageViewport::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_panning = false;
        updateCursorShape();
    }
    QWidget::mouseReleaseEvent(event);
}

void ImageViewport::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (m_image.isNull())
        return;
    if (m_fit && fitScale() < 1.0) {
        zoomTo(1.0, event->position());
    } else {
        zoomToFit();
    }
}

void ImageViewport::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    if (m_fit) {
        m_zoom = fitScale();
    }
    clampPan();
    updateCursorShape();
    emit zoomChanged();
}

// ---------------------------------------------------------------------------
// ImageViewerDialog

namespace {
const char* OVERLAY_BUTTON_QSS =
    "QToolButton { background-color: rgba(12, 13, 16, 150); border: none; border-radius: 20px;"
    "              min-width: 40px; min-height: 40px; max-width: 40px; max-height: 40px; }"
    "QToolButton:hover { background-color: rgba(12, 13, 16, 220); }"
    "QToolButton:disabled { background-color: rgba(12, 13, 16, 80); }";

const char* OVERLAY_CHIP_QSS = "QLabel { background-color: rgba(12, 13, 16, 150); color: #c9ced6; border-radius: 10px; padding: 3px 10px; }";

/** A chevron drawn by hand so it is perfectly centered in the button,
 *  independent of any font's glyph metrics. */
QIcon makeChevronIcon(bool pointsRight)
{
    QIcon icon;
    for (const auto& [mode, color] : { std::pair{ QIcon::Normal, QColor(0xe8, 0xea, 0xee) },
                                       std::pair{ QIcon::Disabled, QColor(232, 234, 238, 60) } }) {
        QPixmap pixmap(36, 36);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing, true);
        QPen pen(color, 3.0);
        pen.setCapStyle(Qt::RoundCap);
        pen.setJoinStyle(Qt::RoundJoin);
        painter.setPen(pen);
        // apex at the horizontal center, arms spanning 7px each way
        const qreal cx = 18.0 + (pointsRight ? 3.5 : -3.5);
        const qreal dir = pointsRight ? -7.0 : 7.0;
        painter.drawPolyline(QPolygonF{ QPointF(cx + dir, 11.0), QPointF(cx, 18.0), QPointF(cx + dir, 25.0) });
        painter.end();
        icon.addPixmap(pixmap, mode);
    }
    return icon;
}
}  // namespace

ImageViewerDialog::ImageViewerDialog(QWidget* parent, QList<ModPlatform::GalleryImage> images, int startIndex, QString metaEntry)
    : QDialog(parent), m_images(std::move(images)), m_metaEntry(std::move(metaEntry))
{
    setWindowTitle(tr("Gallery"));
    setModal(true);

    // The viewport IS the dialog; everything else floats above it.
    m_viewport = new ImageViewport(this);
    m_viewport->lower();

    m_prevButton = new QToolButton(this);
    m_prevButton->setIcon(makeChevronIcon(false));
    m_prevButton->setIconSize(QSize(36, 36));
    m_prevButton->setToolTip(tr("Previous (Left arrow)"));
    m_prevButton->setCursor(Qt::PointingHandCursor);
    m_prevButton->setStyleSheet(OVERLAY_BUTTON_QSS);
    m_prevButton->setFocusPolicy(Qt::NoFocus);
    connect(m_prevButton, &QToolButton::clicked, this, [this] { showImage(m_index - 1); });

    m_nextButton = new QToolButton(this);
    m_nextButton->setIcon(makeChevronIcon(true));
    m_nextButton->setIconSize(QSize(36, 36));
    m_nextButton->setToolTip(tr("Next (Right arrow)"));
    m_nextButton->setCursor(Qt::PointingHandCursor);
    m_nextButton->setStyleSheet(OVERLAY_BUTTON_QSS);
    m_nextButton->setFocusPolicy(Qt::NoFocus);
    connect(m_nextButton, &QToolButton::clicked, this, [this] { showImage(m_index + 1); });

    m_counterLabel = new QLabel(this);
    m_counterLabel->setStyleSheet(OVERLAY_CHIP_QSS);

    m_zoomLabel = new QLabel(this);
    m_zoomLabel->setStyleSheet(OVERLAY_CHIP_QSS);
    m_zoomLabel->setToolTip(tr("Scroll to zoom, drag to pan, double-click to toggle 100%"));

    m_playButton = new QToolButton(this);
    m_playButton->setText(tr("▶  Play video"));
    m_playButton->setCursor(Qt::PointingHandCursor);
    m_playButton->setStyleSheet("QToolButton { background-color: rgba(12, 13, 16, 210); color: #f0f2f5; border: none;"
                                "              border-radius: 22px; padding: 11px 20px; font-weight: 600; }"
                                "QToolButton:hover { background-color: rgba(35, 38, 43, 240); }");
    m_playButton->setFocusPolicy(Qt::NoFocus);
    m_playButton->hide();
    connect(m_playButton, &QToolButton::clicked, this, &ImageViewerDialog::playCurrentVideo);

    m_captionBox = new QWidget(this);
    m_captionBox->setStyleSheet("QWidget { background-color: rgba(12, 13, 16, 170); border-radius: 8px; }"
                                "QLabel { background: transparent; }");
    auto* captionLayout = new QVBoxLayout(m_captionBox);
    captionLayout->setContentsMargins(14, 8, 14, 8);
    captionLayout->setSpacing(2);
    m_titleLabel = new QLabel(m_captionBox);
    m_titleLabel->setStyleSheet("color: #f0f2f5; font-weight: 600;");
    m_titleLabel->setAlignment(Qt::AlignCenter);
    m_descriptionLabel = new QLabel(m_captionBox);
    m_descriptionLabel->setStyleSheet("color: #aeb6bf;");
    m_descriptionLabel->setAlignment(Qt::AlignCenter);
    m_descriptionLabel->setWordWrap(true);
    captionLayout->addWidget(m_titleLabel);
    captionLayout->addWidget(m_descriptionLabel);

    connect(m_viewport, &ImageViewport::zoomChanged, this, &ImageViewerDialog::updateOverlays);

    if (auto* screen = parent != nullptr && parent->screen() != nullptr ? parent->screen() : QApplication::primaryScreen();
        screen != nullptr) {
        resize(screen->availableSize() * 0.8);
    } else {
        resize(1100, 750);
    }

    if (m_images.isEmpty()) {
        // Nothing to show; close as soon as the event loop runs (qBound with
        // an inverted range would assert in debug builds).
        QTimer::singleShot(0, this, &QDialog::reject);
        return;
    }
    m_index = qBound(0, startIndex, int(m_images.size()) - 1);
    preloadAll();
    showImage(m_index);
}

void ImageViewerDialog::preloadAll()
{
    // Fetch every gallery image up front (cache-backed) in ONE job, so the
    // concurrent-downloads setting applies instead of everything at once.
    // Images are only decoded near the current index; a full gallery of
    // decoded 4K frames is easily hundreds of megabytes.
    auto* job = new NetJob(QStringLiteral("Preload gallery images"), APPLICATION->network());
    job->setAskRetry(false);
    bool queuedAny = false;
    for (int i = 0; i < m_images.size(); i++) {
        const QUrl url(m_images[i].url);
        if (url.isEmpty()) {
            m_failed[i] = true;
            continue;
        }

        const auto urlKind = MediaUtils::kindFromUrl(url);
        m_mediaKinds[i] = urlKind;
        if (urlKind == MediaUtils::Kind::Video)
            continue;

        auto entry = APPLICATION->metacache()->resolveEntry(
            m_metaEntry,
            QString("images/%1").arg(QString(QCryptographicHash::hash(url.toEncoded(), QCryptographicHash::Algorithm::Sha1).toHex())));

        auto action = Net::ApiDownload::makeCached(url, entry);
        const QString fullPath = entry->getFullPath();
        connect(action.get(), &Task::succeeded, this, [this, i, fullPath] {
            m_cachedPaths[i] = fullPath;
            if (qAbs(i - m_index) <= EVICT_WINDOW)
                decodeIfNeeded(i);
            if (i == m_index)
                showImage(m_index);
        });
        connect(action.get(), &Task::failed, this, [this, i](const QString&) {
            m_failed[i] = true;
            if (i == m_index)
                showImage(m_index);
        });
        job->addNetAction(action);
        queuedAny = true;
    }
    connect(job, &NetJob::finished, job, &NetJob::deleteLater);
    if (queuedAny)
        job->start();
    else
        job->deleteLater();
}

void ImageViewerDialog::decodeIfNeeded(int index)
{
    if (m_loaded.contains(index) || m_failed.value(index, false) || !m_cachedPaths.contains(index))
        return;

    const auto kind = MediaUtils::kindFromFile(m_cachedPaths.value(index), QUrl(m_images[index].url));
    m_mediaKinds[index] = kind;
    if (kind == MediaUtils::Kind::Video)
        return;

    QImage image(m_cachedPaths.value(index));
    if (image.isNull())
        m_failed[index] = true;
    else
        m_loaded[index] = image;
}

void ImageViewerDialog::evictFarImages()
{
    // Anything outside the window re-decodes from the disk cache on demand.
    for (auto it = m_loaded.begin(); it != m_loaded.end();) {
        if (qAbs(it.key() - m_index) > EVICT_WINDOW && m_cachedPaths.contains(it.key()))
            it = m_loaded.erase(it);
        else
            ++it;
    }
}

void ImageViewerDialog::startAnimation(int index)
{
    if (m_movie != nullptr && m_movieIndex == index)
        return;
    stopAnimation();

    if (!m_cachedPaths.contains(index))
        return;
    auto* movie = new QMovie(m_cachedPaths.value(index), QByteArray(), this);
    if (!movie->isValid()) {
        delete movie;
        return;
    }

    m_movie = movie;
    m_movieIndex = index;
    movie->setCacheMode(QMovie::CacheNone);
    connect(movie, &QMovie::frameChanged, this, [this, movie, index] {
        if (index != m_index || movie != m_movie)
            return;
        const QImage frame = movie->currentImage();
        if (!frame.isNull())
            m_viewport->setFrame(frame);
    });
    movie->start();
}

void ImageViewerDialog::stopAnimation()
{
    if (m_movie != nullptr) {
        m_movie->stop();
        m_movie->deleteLater();
    }
    m_movie = nullptr;
    m_movieIndex = -1;
}

void ImageViewerDialog::playCurrentVideo()
{
    if (m_index < 0 || m_index >= m_images.size())
        return;
    DesktopServices::openUrl(MediaUtils::watchableUrl(QUrl(m_images[m_index].url)));
}

void ImageViewerDialog::showImage(int index)
{
    if (index < 0 || index >= m_images.size()) {
        return;
    }

    m_index = index;
    decodeIfNeeded(m_index);
    evictFarImages();

    auto kind = m_mediaKinds.value(m_index, MediaUtils::kindFromUrl(QUrl(m_images[m_index].url)));
    const bool video = kind == MediaUtils::Kind::Video;
    if (video) {
        stopAnimation();
        m_viewport->setPlaceholderText(tr("This video will open in your default player."));
    } else if (m_loaded.contains(m_index)) {
        m_viewport->setImage(m_loaded.value(m_index));
        if (kind == MediaUtils::Kind::AnimatedImage)
            startAnimation(m_index);
        else
            stopAnimation();
    } else if (m_failed.value(m_index, false)) {
        stopAnimation();
        m_viewport->setPlaceholderText(tr("Could not load the image."));
    } else {
        stopAnimation();
        m_viewport->setPlaceholderText(tr("Loading media %1 of %2…").arg(m_index + 1).arg(m_images.size()));
    }

    const auto& image = m_images[m_index];
    m_titleLabel->setText(image.title);
    m_titleLabel->setVisible(!image.title.isEmpty());
    m_descriptionLabel->setText(image.description);
    m_descriptionLabel->setVisible(!image.description.isEmpty());
    m_captionBox->setVisible(!image.title.isEmpty() || !image.description.isEmpty());

    updateOverlays();
    layoutOverlays();
}

void ImageViewerDialog::updateOverlays()
{
    m_prevButton->setEnabled(m_index > 0);
    m_nextButton->setEnabled(m_index < m_images.size() - 1);
    const bool multiple = m_images.size() > 1;
    m_prevButton->setVisible(multiple);
    m_nextButton->setVisible(multiple);
    m_counterLabel->setVisible(multiple);
    m_counterLabel->setText(QString("%1 / %2").arg(m_index + 1).arg(m_images.size()));
    m_counterLabel->adjustSize();

    const bool video = m_mediaKinds.value(m_index, MediaUtils::kindFromUrl(QUrl(m_images[m_index].url))) == MediaUtils::Kind::Video;
    m_playButton->setVisible(video);
    m_playButton->adjustSize();
    const int percent = qRound(m_viewport->currentScale() * 100.0);
    m_zoomLabel->setText(m_viewport->isFit() ? tr("Fit") : QStringLiteral("%1%").arg(percent));
    m_zoomLabel->adjustSize();
    m_zoomLabel->setVisible(!video);
    layoutOverlays();
}

void ImageViewerDialog::layoutOverlays()
{
    const QRect area = rect();
    m_viewport->setGeometry(area);

    const int margin = 14;
    m_prevButton->move(area.left() + margin, area.center().y() - m_prevButton->height() / 2);
    m_nextButton->move(area.right() - margin - m_nextButton->width(), area.center().y() - m_nextButton->height() / 2);
    m_counterLabel->move(area.left() + margin, area.top() + margin);
    m_zoomLabel->move(m_counterLabel->geometry().right() + 8, area.top() + margin);
    m_playButton->move(area.center().x() - m_playButton->width() / 2, area.center().y() - m_playButton->height() / 2);

    if (m_captionBox->isVisible()) {
        const int captionWidth = qMin(int(area.width() * 0.7), 720);
        const int captionHeight = m_captionBox->sizeHint().height();
        m_captionBox->setGeometry((area.width() - captionWidth) / 2, area.bottom() - margin - captionHeight, captionWidth, captionHeight);
    }

    for (QWidget* overlay : { static_cast<QWidget*>(m_prevButton), static_cast<QWidget*>(m_nextButton),
                              static_cast<QWidget*>(m_counterLabel), static_cast<QWidget*>(m_zoomLabel),
                              static_cast<QWidget*>(m_playButton), m_captionBox }) {
        overlay->raise();
    }
}

void ImageViewerDialog::keyPressEvent(QKeyEvent* event)
{
    switch (event->key()) {
        case Qt::Key_Left:
            showImage(m_index - 1);
            return;
        case Qt::Key_Right:
            showImage(m_index + 1);
            return;
        case Qt::Key_Plus:
        case Qt::Key_Equal:
            m_viewport->zoomTo(m_viewport->currentScale() * 1.2, m_viewport->rect().center());
            return;
        case Qt::Key_Minus:
            m_viewport->zoomTo(m_viewport->currentScale() / 1.2, m_viewport->rect().center());
            return;
        case Qt::Key_0:
            m_viewport->zoomToFit();
            return;
        case Qt::Key_Space:
        case Qt::Key_Return:
        case Qt::Key_Enter:
            if (m_playButton->isVisible()) {
                playCurrentVideo();
                return;
            }
            break;
        default:
            break;
    }
    QDialog::keyPressEvent(event);
}

void ImageViewerDialog::resizeEvent(QResizeEvent* event)
{
    QDialog::resizeEvent(event);
    layoutOverlays();
}
