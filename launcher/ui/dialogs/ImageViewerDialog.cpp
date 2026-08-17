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
#include <QScreen>
#include <QToolButton>
#include <QVBoxLayout>

#include "Application.h"
#include "net/ApiDownload.h"
#include "net/NetJob.h"

ImageViewerDialog::ImageViewerDialog(QWidget* parent, QList<ModPlatform::GalleryImage> images, int startIndex, QString metaEntry)
    : QDialog(parent), m_images(std::move(images)), m_metaEntry(std::move(metaEntry))
{
    setWindowTitle(tr("Gallery"));
    setModal(true);

    // A committed lightbox look, independent of the theme
    setStyleSheet(
        "ImageViewerDialog { background-color: #101114; }"
        "QLabel { color: #e8eaee; background: transparent; }"
        "QLabel#viewerDescription { color: #9aa3ad; }"
        "QLabel#viewerCounter { color: #9aa3ad; }"
        "QToolButton { background-color: #24262b; color: #e8eaee; border: 1px solid #3a3d44; border-radius: 17px;"
        "              min-width: 34px; min-height: 34px; max-width: 34px; max-height: 34px; font-size: 17px; }"
        "QToolButton:hover { background-color: #2f3238; border-color: #4b4f58; }"
        "QToolButton:pressed { background-color: #1c1e22; }"
        "QToolButton:disabled { color: #565b64; border-color: #2a2c31; }");

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 16, 16, 12);
    layout->setSpacing(6);

    m_imageLabel = new QLabel(this);
    m_imageLabel->setAlignment(Qt::AlignCenter);
    m_imageLabel->setMinimumSize(360, 220);
    layout->addWidget(m_imageLabel, 1);

    m_titleLabel = new QLabel(this);
    m_titleLabel->setAlignment(Qt::AlignCenter);
    auto titleFont = m_titleLabel->font();
    titleFont.setBold(true);
    titleFont.setPointSizeF(titleFont.pointSizeF() + 1.0);
    m_titleLabel->setFont(titleFont);
    layout->addWidget(m_titleLabel);

    m_descriptionLabel = new QLabel(this);
    m_descriptionLabel->setObjectName(QStringLiteral("viewerDescription"));
    m_descriptionLabel->setAlignment(Qt::AlignCenter);
    m_descriptionLabel->setWordWrap(true);
    layout->addWidget(m_descriptionLabel);

    auto* nav = new QHBoxLayout();
    nav->setSpacing(14);
    nav->addStretch();
    m_prevButton = new QToolButton(this);
    m_prevButton->setText(QStringLiteral("‹"));
    m_prevButton->setToolTip(tr("Previous (Left arrow)"));
    m_prevButton->setCursor(Qt::PointingHandCursor);
    nav->addWidget(m_prevButton);
    m_counterLabel = new QLabel(this);
    m_counterLabel->setObjectName(QStringLiteral("viewerCounter"));
    m_counterLabel->setMinimumWidth(64);
    m_counterLabel->setAlignment(Qt::AlignCenter);
    nav->addWidget(m_counterLabel);
    m_nextButton = new QToolButton(this);
    m_nextButton->setText(QStringLiteral("›"));
    m_nextButton->setToolTip(tr("Next (Right arrow)"));
    m_nextButton->setCursor(Qt::PointingHandCursor);
    nav->addWidget(m_nextButton);
    nav->addStretch();
    layout->addLayout(nav);

    connect(m_prevButton, &QToolButton::clicked, this, [this] { showImage(m_index - 1); });
    connect(m_nextButton, &QToolButton::clicked, this, [this] { showImage(m_index + 1); });

    if (auto* screen = parent != nullptr && parent->screen() != nullptr ? parent->screen() : QApplication::primaryScreen();
        screen != nullptr) {
        resize(screen->availableSize() * 0.7);
    } else {
        resize(1000, 700);
    }

    m_index = qBound(0, startIndex, int(m_images.size()) - 1);
    preloadAll();
    showImage(m_index);
}

void ImageViewerDialog::preloadAll()
{
    // Fetch every gallery image up front (cache-backed), decoding as each
    // arrives, so previous/next never waits on the network.
    for (int i = 0; i < m_images.size(); i++) {
        const QUrl url(m_images[i].url);
        if (url.isEmpty()) {
            m_failed[i] = true;
            continue;
        }

        auto entry = APPLICATION->metacache()->resolveEntry(
            m_metaEntry,
            QString("images/%1").arg(QString(QCryptographicHash::hash(url.toEncoded(), QCryptographicHash::Algorithm::Sha1).toHex())));

        auto* job = new NetJob(QString("Preload gallery image %1").arg(i + 1), APPLICATION->network());
        job->setAskRetry(false);
        job->addNetAction(Net::ApiDownload::makeCached(url, entry));

        auto fullPath = entry->getFullPath();
        connect(job, &NetJob::succeeded, this, [this, i, fullPath] {
            QImage image(fullPath);
            if (image.isNull()) {
                m_failed[i] = true;
            } else {
                m_loaded[i] = image;
            }
            if (i == m_index) {
                showImage(m_index);
            }
        });
        connect(job, &NetJob::failed, this, [this, i](const QString&) {
            m_failed[i] = true;
            if (i == m_index) {
                showImage(m_index);
            }
        });
        connect(job, &NetJob::finished, job, &NetJob::deleteLater);
        job->start();
    }
}

void ImageViewerDialog::showImage(int index)
{
    if (index < 0 || index >= m_images.size()) {
        return;
    }

    m_index = index;
    updateNavState();

    const auto& image = m_images[m_index];
    m_titleLabel->setText(image.title);
    m_titleLabel->setVisible(!image.title.isEmpty());
    m_descriptionLabel->setText(image.description);
    m_descriptionLabel->setVisible(!image.description.isEmpty());

    if (m_loaded.contains(m_index)) {
        updatePixmap();
    } else if (m_failed.value(m_index, false)) {
        m_imageLabel->setText(tr("Could not load the image."));
    } else {
        m_imageLabel->setText(QStringLiteral("…"));
    }
}

void ImageViewerDialog::updatePixmap()
{
    const auto found = m_loaded.constFind(m_index);
    if (found == m_loaded.constEnd()) {
        return;
    }
    const QImage& image = found.value();

    // Render at the physical resolution for crisp results on scaled displays,
    // and never upscale past the image's native size.
    const qreal ratio = devicePixelRatioF();
    QSize target = (QSizeF(m_imageLabel->size()) * ratio).toSize();
    if (image.width() < target.width() && image.height() < target.height()) {
        target = image.size();
    }
    QPixmap pixmap = QPixmap::fromImage(image.scaled(target, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    pixmap.setDevicePixelRatio(ratio);
    m_imageLabel->setPixmap(pixmap);
}

void ImageViewerDialog::updateNavState()
{
    m_prevButton->setEnabled(m_index > 0);
    m_nextButton->setEnabled(m_index < m_images.size() - 1);
    m_counterLabel->setText(QString("%1 / %2").arg(m_index + 1).arg(m_images.size()));
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
        default:
            QDialog::keyPressEvent(event);
    }
}

void ImageViewerDialog::resizeEvent(QResizeEvent* event)
{
    QDialog::resizeEvent(event);
    updatePixmap();
}
