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
#include <QPushButton>
#include <QScreen>
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
        "ImageViewerDialog { background-color: #1a1b1e; }"
        "QLabel { color: #e6e8eb; background: transparent; }"
        "QPushButton { background-color: #34373d; color: #e6e8eb; border: 1px solid #43474e; border-radius: 4px; padding: 4px 14px; }"
        "QPushButton:hover { background-color: #3d4148; }"
        "QPushButton:disabled { color: #6f7681; }");

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);

    m_imageLabel = new QLabel(this);
    m_imageLabel->setAlignment(Qt::AlignCenter);
    m_imageLabel->setMinimumSize(320, 200);
    layout->addWidget(m_imageLabel, 1);

    m_titleLabel = new QLabel(this);
    m_titleLabel->setAlignment(Qt::AlignCenter);
    auto titleFont = m_titleLabel->font();
    titleFont.setBold(true);
    m_titleLabel->setFont(titleFont);
    layout->addWidget(m_titleLabel);

    m_descriptionLabel = new QLabel(this);
    m_descriptionLabel->setAlignment(Qt::AlignCenter);
    m_descriptionLabel->setWordWrap(true);
    layout->addWidget(m_descriptionLabel);

    auto* nav = new QHBoxLayout();
    nav->addStretch();
    m_prevButton = new QPushButton(tr("← Previous"), this);
    nav->addWidget(m_prevButton);
    m_counterLabel = new QLabel(this);
    m_counterLabel->setMinimumWidth(60);
    m_counterLabel->setAlignment(Qt::AlignCenter);
    nav->addWidget(m_counterLabel);
    m_nextButton = new QPushButton(tr("Next →"), this);
    nav->addWidget(m_nextButton);
    nav->addStretch();
    layout->addLayout(nav);

    connect(m_prevButton, &QPushButton::clicked, this, [this] { showImage(m_index - 1); });
    connect(m_nextButton, &QPushButton::clicked, this, [this] { showImage(m_index + 1); });

    if (auto* screen = parent != nullptr && parent->screen() != nullptr ? parent->screen() : QApplication::primaryScreen();
        screen != nullptr) {
        resize(screen->availableSize() * 0.7);
    } else {
        resize(1000, 700);
    }

    showImage(startIndex);
}

void ImageViewerDialog::showImage(int index)
{
    if (index < 0 || index >= m_images.size()) {
        return;
    }

    m_index = index;
    m_generation++;
    updateNavState();

    const auto& image = m_images[m_index];
    m_titleLabel->setText(image.title);
    m_titleLabel->setVisible(!image.title.isEmpty());
    m_descriptionLabel->setText(image.description);
    m_descriptionLabel->setVisible(!image.description.isEmpty());

    m_currentImage = QImage();
    m_imageLabel->setText(tr("Loading image..."));

    const QUrl url(image.url);
    if (url.isEmpty()) {
        m_imageLabel->setText(tr("Could not load the image."));
        return;
    }

    auto entry = APPLICATION->metacache()->resolveEntry(
        m_metaEntry,
        QString("images/%1").arg(QString(QCryptographicHash::hash(url.toEncoded(), QCryptographicHash::Algorithm::Sha1).toHex())));

    auto* job = new NetJob(QString("Load image: %1").arg(url.fileName()), APPLICATION->network());
    job->setAskRetry(false);
    job->addNetAction(Net::ApiDownload::makeCached(url, entry));

    const int generation = m_generation;
    auto fullPath = entry->getFullPath();
    connect(job, &NetJob::succeeded, this, [this, generation, fullPath] {
        if (generation != m_generation) {
            return;
        }
        m_currentImage = QImage(fullPath);
        if (m_currentImage.isNull()) {
            m_imageLabel->setText(tr("Could not load the image."));
        } else {
            updatePixmap();
        }
    });
    connect(job, &NetJob::failed, this, [this, generation](const QString&) {
        if (generation == m_generation) {
            m_imageLabel->setText(tr("Could not load the image."));
        }
    });
    connect(job, &NetJob::finished, job, &NetJob::deleteLater);
    job->start();
}

void ImageViewerDialog::updatePixmap()
{
    if (m_currentImage.isNull()) {
        return;
    }

    auto size = m_imageLabel->size();
    m_imageLabel->setPixmap(
        QPixmap::fromImage(m_currentImage.scaled(size, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
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
