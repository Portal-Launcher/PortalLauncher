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

#include "ProjectDetailPanel.h"

#include <QCryptographicHash>
#include <QHeaderView>
#include <QIcon>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QPixmapCache>
#include <QTabWidget>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "Application.h"
#include "BuildConfig.h"
#include "Markdown.h"
#include "StringUtils.h"

#include "net/ApiDownload.h"
#include "net/NetJob.h"

#include "ui/dialogs/ImageViewerDialog.h"
#include "ui/pages/modplatform/ResourceModel.h"
#include "ui/widgets/ProjectDescriptionPage.h"

namespace {

/** Lowers the text color of a label towards the background, palette-aware. */
void dimLabel(QLabel* label)
{
    auto palette = label->palette();
    auto color = palette.color(QPalette::WindowText);
    color.setAlphaF(color.alphaF() * 0.65F);
    palette.setColor(QPalette::WindowText, color);
    label->setPalette(palette);
}

QString joinDot(const QStringList& parts)
{
    return parts.join(QStringLiteral("&nbsp;·&nbsp; "));
}

QString sideToText(ModPlatform::Side side)
{
    switch (side) {
        case ModPlatform::Side::ClientSide:
            return QObject::tr("Client-side");
        case ModPlatform::Side::ServerSide:
            return QObject::tr("Server-side");
        case ModPlatform::Side::UniversalSide:
            return QObject::tr("Client + server");
        default:
            return {};
    }
}

QColor versionTypeColor(const ModPlatform::IndexedVersionType& type)
{
    switch (static_cast<int>(type)) {
        case static_cast<int>(ModPlatform::IndexedVersionType::Release):
            return { 0x2E, 0xA5, 0x60 };
        case static_cast<int>(ModPlatform::IndexedVersionType::Beta):
            return { 0xD6, 0x8F, 0x2E };
        case static_cast<int>(ModPlatform::IndexedVersionType::Alpha):
            return { 0xCB, 0x4A, 0x4A };
        default:
            return {};
    }
}

}  // namespace

ProjectDetailPanel::ProjectDetailPanel(QWidget* parent) : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);

    m_header = new QWidget(this);
    auto* headerLayout = new QHBoxLayout(m_header);
    headerLayout->setContentsMargins(8, 8, 8, 0);
    headerLayout->setSpacing(10);

    m_iconLabel = new QLabel(m_header);
    m_iconLabel->setFixedSize(64, 64);
    m_iconLabel->setScaledContents(true);
    headerLayout->addWidget(m_iconLabel, 0, Qt::AlignTop);

    auto* headerText = new QVBoxLayout();
    headerText->setContentsMargins(0, 0, 0, 0);
    headerText->setSpacing(2);

    m_titleLabel = new QLabel(m_header);
    m_titleLabel->setWordWrap(true);
    m_titleLabel->setTextInteractionFlags(Qt::TextBrowserInteraction);
    m_titleLabel->setOpenExternalLinks(false);
    connect(m_titleLabel, &QLabel::linkActivated, this, [this](const QString& link) { emit openUrlRequested(QUrl(link)); });
    headerText->addWidget(m_titleLabel);

    m_statsLabel = new QLabel(m_header);
    m_statsLabel->setWordWrap(true);
    headerText->addWidget(m_statsLabel);

    m_tagsLabel = new QLabel(m_header);
    m_tagsLabel->setWordWrap(true);
    dimLabel(m_tagsLabel);
    headerText->addWidget(m_tagsLabel);

    m_linksLabel = new QLabel(m_header);
    m_linksLabel->setWordWrap(true);
    m_linksLabel->setTextInteractionFlags(Qt::TextBrowserInteraction);
    m_linksLabel->setOpenExternalLinks(false);
    connect(m_linksLabel, &QLabel::linkActivated, this, [this](const QString& link) { emit openUrlRequested(QUrl(link)); });
    headerText->addWidget(m_linksLabel);

    headerText->addStretch();
    headerLayout->addLayout(headerText, 1);
    layout->addWidget(m_header);

    m_tabs = new QTabWidget(this);
    m_tabs->setDocumentMode(true);

    m_description = new ProjectDescriptionPage(m_tabs);
    m_description->setOpenExternalLinks(false);
    m_description->setOpenLinks(false);
    connect(m_description, &QTextBrowser::anchorClicked, this, &ProjectDetailPanel::openUrlRequested);
    m_tabs->addTab(m_description, tr("Description"));

    m_gallery = new QListWidget(m_tabs);
    m_gallery->setViewMode(QListView::IconMode);
    m_gallery->setIconSize(QSize(192, 108));
    m_gallery->setGridSize(QSize(208, 150));
    m_gallery->setResizeMode(QListView::Adjust);
    m_gallery->setMovement(QListView::Static);
    m_gallery->setSpacing(6);
    m_gallery->setWordWrap(true);
    m_gallery->setTextElideMode(Qt::ElideRight);
    m_gallery->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_gallery->setSelectionMode(QAbstractItemView::NoSelection);
    connect(m_gallery, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        if (item != nullptr) {
            openImageViewer(item->data(Qt::UserRole).toInt());
        }
    });
    m_tabs->addTab(m_gallery, tr("Gallery"));

    m_versions = new QTreeWidget(m_tabs);
    m_versions->setColumnCount(6);
    m_versions->setHeaderLabels({ tr("Version"), tr("Type"), tr("Loaders"), tr("Minecraft"), tr("Released"), tr("Downloads") });
    m_versions->setRootIsDecorated(false);
    m_versions->setAlternatingRowColors(true);
    m_versions->setUniformRowHeights(true);
    m_versions->setAllColumnsShowFocus(true);
    m_versions->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_versions->header()->setStretchLastSection(false);
    m_versions->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int col = 1; col < 6; col++) {
        m_versions->header()->setSectionResizeMode(col, QHeaderView::ResizeToContents);
    }
    connect(m_versions, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem* current, QTreeWidgetItem*) {
        if (m_syncingSelection || current == nullptr || current->isDisabled()) {
            return;
        }
        emit versionPicked(current->data(0, Qt::UserRole).toInt());
    });
    m_tabs->addTab(m_versions, tr("Versions"));

    // Room for descenders (the g in "Changelog" clips in document mode otherwise);
    // only vertical padding is set so the active theme keeps its own look.
    m_tabs->tabBar()->setStyleSheet(QStringLiteral("QTabBar::tab { padding-top: 4px; padding-bottom: 6px; }"));

    // Start fetching the full-size gallery images as soon as the gallery is
    // opened, so the image viewer is instant instead of loading per click.
    connect(m_tabs, &QTabWidget::currentChanged, this, [this](int index) {
        if (m_tabs->widget(index) == m_gallery)
            prefetchFullGallery();
    });

    m_changelog = new ProjectDescriptionPage(m_tabs);
    m_changelog->setOpenExternalLinks(false);
    m_changelog->setOpenLinks(false);
    connect(m_changelog, &QTextBrowser::anchorClicked, this, &ProjectDetailPanel::openUrlRequested);
    m_tabs->addTab(m_changelog, tr("Changelog"));

    layout->addWidget(m_tabs, 1);

    clear();
}

void ProjectDetailPanel::setMetaEntry(QString entry)
{
    m_metaEntry = entry;
    m_description->setMetaEntry(entry);
    m_changelog->setMetaEntry(entry);
}

void ProjectDetailPanel::setInfoText(const QString& text)
{
    clear();
    m_description->setPlainText(text);
}

void ProjectDetailPanel::clear()
{
    m_generation++;
    m_pack = nullptr;
    m_model = nullptr;
    m_selectedVersion = -1;
    m_galleryPrefetched = false;

    m_header->hide();
    m_description->flush();
    m_description->clear();
    m_changelog->flush();
    m_changelog->clear();
    m_gallery->clear();
    m_versions->clear();

    m_tabs->setCurrentIndex(0);
    updateTabStates();
}

void ProjectDetailPanel::setPack(ModPlatform::IndexedPack::Ptr pack, ResourceDownload::ResourceModel* model)
{
    if (!pack) {
        clear();
        return;
    }

    const bool samePack = m_pack == pack;
    const int previousTab = m_tabs->currentIndex();

    m_generation++;
    m_pack = pack;
    m_model = model;
    if (!samePack) {
        m_selectedVersion = -1;
        m_galleryPrefetched = false;
    }

    rebuildHeader();
    rebuildDescription();
    rebuildGallery();
    updateVersions();
    rebuildChangelog();
    updateTabStates();

    if (samePack && m_tabs->isTabEnabled(previousTab)) {
        m_tabs->setCurrentIndex(previousTab);
    } else if (!m_tabs->isTabEnabled(m_tabs->currentIndex())) {
        m_tabs->setCurrentIndex(0);
    }

    // If the gallery is already the visible tab, start its prefetch right away.
    if (m_tabs->currentWidget() == m_gallery)
        prefetchFullGallery();
}

void ProjectDetailPanel::rebuildHeader()
{
    m_header->show();

    // Icon
    m_iconLabel->clear();
    if (!m_pack->logoUrl.isEmpty()) {
        QPixmap cached;
        if (QPixmapCache::find(m_pack->logoUrl, &cached)) {
            m_iconLabel->setPixmap(cached);
        } else {
            fetchImage(QUrl(m_pack->logoUrl), m_generation,
                       [this](const QImage& image) { m_iconLabel->setPixmap(QPixmap::fromImage(image)); });
        }
    }

    // Title and authors
    QString title;
    const QString name = m_pack->name.toHtmlEscaped();
    if (m_pack->websiteUrl.isEmpty()) {
        title = name;
    } else {
        title = QString("<a href=\"%1\" style=\"text-decoration:none\">%2</a>").arg(m_pack->websiteUrl, name);
    }
    title = QString("<span style=\"font-size:%1pt; font-weight:600\">%2</span>").arg(font().pointSize() + 4).arg(title);
    if (!m_pack->authors.empty()) {
        QStringList authors;
        for (auto& author : m_pack->authors) {
            if (author.url.isEmpty()) {
                authors.append(author.name.toHtmlEscaped());
            } else {
                authors.append(QString("<a href=\"%1\">%2</a>").arg(author.url, author.name.toHtmlEscaped()));
            }
        }
        title += QString("&nbsp; %1").arg(tr("by %1").arg(authors.join(", ")));
    }
    m_titleLabel->setText(title);

    // Stats
    const auto& extra = m_pack->extraData;
    QStringList stats;
    if (extra.downloads >= 0) {
        stats.append(tr("<b>%1</b> downloads").arg(StringUtils::humanReadableCount(static_cast<double>(extra.downloads))));
    }
    if (extra.followers >= 0) {
        stats.append(tr("<b>%1</b> followers").arg(StringUtils::humanReadableCount(static_cast<double>(extra.followers))));
    }
    if (extra.dateModified.isValid()) {
        stats.append(tr("updated %1").arg(StringUtils::relativeTimeString(extra.dateModified)));
    }
    if (!extra.license.isEmpty()) {
        stats.append(extra.license.toHtmlEscaped());
    }
    if (auto side = sideToText(m_pack->side); !side.isEmpty()) {
        stats.append(side);
    }
    m_statsLabel->setText(joinDot(stats));
    m_statsLabel->setVisible(!stats.isEmpty());

    // Tags
    QStringList tags;
    for (const auto& category : extra.categories) {
        auto tag = category;
        if (!tag.isEmpty()) {
            tag[0] = tag[0].toUpper();
        }
        tags.append(tag);
    }
    m_tagsLabel->setText(tags.join(QStringLiteral(" · ")));
    m_tagsLabel->setVisible(!tags.isEmpty());

    // Links
    QStringList links;
    auto addLink = [&links](const QString& url, const QString& label) {
        if (!url.isEmpty()) {
            links.append(QString("<a href=\"%1\">%2</a>").arg(url, label));
        }
    };
    addLink(m_pack->websiteUrl, ModPlatform::ProviderCapabilities::readableName(m_pack->provider) + " ↗");
    addLink(extra.sourceUrl, tr("Source"));
    addLink(extra.issuesUrl, tr("Issues"));
    addLink(extra.wikiUrl, tr("Wiki"));
    addLink(extra.discordUrl, tr("Discord"));
    for (const auto& donate : extra.donate) {
        addLink(donate.url, tr("Donate (%1)").arg(donate.platform.toHtmlEscaped()));
    }
    m_linksLabel->setText(joinDot(links));
    m_linksLabel->setVisible(!links.isEmpty());
}

void ProjectDetailPanel::rebuildDescription()
{
    m_description->flush();

    QString text;
    if (m_pack->extraDataLoaded && m_pack->extraData.status == "archived") {
        text += tr("<b>This project has been archived. It will not receive any further updates unless the author decides "
                   "to unarchive the project.</b><hr>");
    }

    if (m_pack->extraData.body.isEmpty()) {
        text += m_pack->description.toHtmlEscaped();
    } else {
        text += markdownToHTML(m_pack->extraData.body);
    }

    m_description->setHtml(StringUtils::htmlListPatch(text));
}

void ProjectDetailPanel::rebuildGallery()
{
    m_gallery->clear();

    const auto& gallery = m_pack->extraData.gallery;
    for (int i = 0; i < gallery.size(); i++) {
        const auto& image = gallery[i];

        auto* item = new QListWidgetItem(image.title.isEmpty() ? tr("Screenshot %1").arg(i + 1) : image.title);
        item->setData(Qt::UserRole, i);
        if (!image.description.isEmpty()) {
            item->setToolTip(image.description);
        }
        item->setIcon(QIcon::fromTheme("screenshot-placeholder"));
        m_gallery->addItem(item);

        const int generation = m_generation;
        const QUrl thumbUrl(image.thumbnailUrl.isEmpty() ? image.url : image.thumbnailUrl);
        const QUrl fullUrl(image.url);
        auto applyThumb = [this, i](const QImage& img) {
            if (auto* thumbItem = m_gallery->item(i); thumbItem != nullptr && !img.isNull()) {
                thumbItem->setIcon(QIcon(QPixmap::fromImage(img.scaled(m_gallery->iconSize(), Qt::KeepAspectRatio,
                                                                       Qt::SmoothTransformation))));
            }
        };
        fetchImage(thumbUrl, generation, [this, generation, thumbUrl, fullUrl, applyThumb](const QImage& img) {
            if (!img.isNull()) {
                applyThumb(img);
            } else if (fullUrl != thumbUrl) {
                // The derived thumbnail URL may not exist; fall back to the full image
                fetchImage(fullUrl, generation, applyThumb);
            }
        });
    }
}

void ProjectDetailPanel::prefetchFullGallery()
{
    if (m_galleryPrefetched || !m_pack)
        return;
    m_galleryPrefetched = true;

    for (const auto& image : m_pack->extraData.gallery) {
        // cache-backed fetch; the result is discarded, the viewer reads the cache
        fetchImage(QUrl(image.url), m_generation, [](const QImage&) {});
    }
}

void ProjectDetailPanel::updateVersions()
{
    m_syncingSelection = true;
    m_versions->clear();
    m_syncingSelection = false;

    if (!m_pack || !m_pack->versionsLoaded) {
        updateTabStates();
        return;
    }

    QVariant installedVersion;
    if (m_model != nullptr) {
        installedVersion = m_model->getInstalledPackVersion(m_pack);
    }

    for (int i = 0; i < m_pack->versions.size(); i++) {
        const auto& version = m_pack->versions[i];

        auto name = version.version;
        if (installedVersion.isValid() && version.fileId == installedVersion) {
            name += tr(" [installed]", "Mod version select");
        }

        QStringList loaders;
        for (auto loader : ModPlatform::modLoaderTypesToList(version.loaders)) {
            loaders.append(ModPlatform::getModLoaderAsString(loader));
        }

        QStringList mcVersions;
        for (const auto& mcVersion : version.mcVersion) {
            if (!mcVersions.contains(mcVersion)) {
                mcVersions.append(mcVersion);
            }
        }
        if (mcVersions.size() > 4) {
            const int extras = mcVersions.size() - 4;
            mcVersions = mcVersions.mid(0, 4);
            mcVersions.append(tr("+%1 more").arg(extras));
        }

        auto* item = new QTreeWidgetItem(m_versions);
        item->setText(0, name);
        item->setToolTip(0, version.version_number);
        item->setText(1, version.version_type.isValid() ? version.version_type.toString() : QString());
        if (auto color = versionTypeColor(version.version_type); color.isValid()) {
            item->setForeground(1, color);
        }
        item->setText(2, loaders.join(", "));
        item->setText(3, mcVersions.join(", "));
        item->setText(4, StringUtils::relativeTimeString(QDateTime::fromString(version.date, Qt::ISODateWithMs)));
        item->setToolTip(4, version.date);
        if (version.downloads >= 0) {
            item->setText(5, StringUtils::humanReadableCount(static_cast<double>(version.downloads)));
        }
        item->setData(0, Qt::UserRole, i);

        if (m_model != nullptr && !m_model->checkVersionFilters(version)) {
            item->setDisabled(true);
            item->setToolTip(0, tr("Not compatible with this instance (loader or Minecraft version)"));
        }
    }

    setSelectedVersion(m_selectedVersion);
    updateTabStates();
}

void ProjectDetailPanel::setSelectedVersion(int versionIndex)
{
    m_selectedVersion = versionIndex;

    m_syncingSelection = true;
    QTreeWidgetItem* match = nullptr;
    for (int i = 0; i < m_versions->topLevelItemCount(); i++) {
        auto* item = m_versions->topLevelItem(i);
        if (item->data(0, Qt::UserRole).toInt() == versionIndex) {
            match = item;
            break;
        }
    }
    m_versions->setCurrentItem(match);
    m_syncingSelection = false;

    rebuildChangelog();
    updateTabStates();
}

void ProjectDetailPanel::rebuildChangelog()
{
    m_changelog->flush();
    m_changelog->clear();

    if (!m_pack || !m_pack->versionsLoaded || m_selectedVersion < 0 || m_selectedVersion >= m_pack->versions.size()) {
        return;
    }

    const auto& version = m_pack->versions[m_selectedVersion];

    QString header = QString("<b>%1</b>").arg(version.version.toHtmlEscaped());
    if (auto date = QDateTime::fromString(version.date, Qt::ISODateWithMs); date.isValid()) {
        header += QString(" · %1").arg(tr("released %1").arg(StringUtils::relativeTimeString(date)));
    }
    header += "<hr>";

    if (!version.changelog.isEmpty()) {
        QString body = m_pack->provider == ModPlatform::ResourceProvider::MODRINTH ? markdownToHTML(version.changelog) : version.changelog;
        m_changelog->setHtml(StringUtils::htmlListPatch(header + body));
        return;
    }

    if (m_pack->provider == ModPlatform::ResourceProvider::FLAME) {
        // CurseForge changelogs are behind an extra request; fetch lazily
        m_changelog->setHtml(header + tr("<i>Loading changelog...</i>"));

        const int generation = m_generation;
        const int versionIndex = m_selectedVersion;
        const auto url = QString("%1/mods/%2/files/%3/changelog")
                             .arg(BuildConfig.FLAME_BASE_URL, m_pack->addonId.toString(), version.fileId.toString());

        auto* job = new NetJob(QString("Flame changelog: %1").arg(version.fileId.toString()), APPLICATION->network());
        job->setAskRetry(false);
        auto [action, response] = Net::ApiDownload::makeByteArray(QUrl(url));
        job->addNetAction(action);

        connect(job, &NetJob::succeeded, this, [this, response, generation, versionIndex] {
            if (generation != m_generation || !m_pack || versionIndex >= m_pack->versions.size()) {
                return;
            }

            auto changelog = QJsonDocument::fromJson(*response).object()["data"].toString();
            if (changelog.isEmpty()) {
                changelog = tr("<i>No changelog was provided.</i>");
            }
            m_pack->versions[versionIndex].changelog = changelog;

            if (versionIndex == m_selectedVersion) {
                rebuildChangelog();
            }
        });
        connect(job, &NetJob::failed, this, [this, generation, versionIndex](const QString&) {
            if (generation == m_generation && m_pack && versionIndex == m_selectedVersion) {
                m_changelog->setHtml(tr("<i>Could not load the changelog.</i>"));
            }
        });
        connect(job, &NetJob::finished, job, &NetJob::deleteLater);
        job->start();
        return;
    }

    m_changelog->setHtml(header + tr("<i>No changelog was provided.</i>"));
}

void ProjectDetailPanel::updateTabStates()
{
    const bool hasPack = m_pack != nullptr;
    const int galleryCount = hasPack ? static_cast<int>(m_pack->extraData.gallery.size()) : 0;

    m_tabs->setTabEnabled(1, galleryCount > 0);
    m_tabs->setTabText(1, galleryCount > 0 ? tr("Gallery (%1)").arg(galleryCount) : tr("Gallery"));

    m_tabs->setTabEnabled(2, hasPack && m_pack->versionsLoaded && !m_pack->versions.empty());
    m_tabs->setTabText(2, hasPack && m_pack->versionsLoaded ? tr("Versions (%1)").arg(m_pack->versions.size()) : tr("Versions"));

    m_tabs->setTabEnabled(3, hasPack && m_pack->versionsLoaded && m_selectedVersion >= 0);
}

void ProjectDetailPanel::openImageViewer(int galleryIndex)
{
    if (!m_pack || galleryIndex < 0 || galleryIndex >= m_pack->extraData.gallery.size()) {
        return;
    }

    ImageViewerDialog dialog(this, m_pack->extraData.gallery, galleryIndex, m_metaEntry);
    dialog.exec();
}

void ProjectDetailPanel::fetchImage(const QUrl& url, int generation, const std::function<void(const QImage&)>& onDone)
{
    if (url.isEmpty()) {
        return;
    }

    auto entry = APPLICATION->metacache()->resolveEntry(
        m_metaEntry,
        QString("images/%1").arg(QString(QCryptographicHash::hash(url.toEncoded(), QCryptographicHash::Algorithm::Sha1).toHex())));

    auto* job = new NetJob(QString("Load image: %1").arg(url.fileName()), APPLICATION->network());
    job->setAskRetry(false);
    job->addNetAction(Net::ApiDownload::makeCached(url, entry));

    auto fullPath = entry->getFullPath();
    connect(job, &NetJob::succeeded, this, [this, generation, fullPath, onDone] {
        if (generation == m_generation) {
            onDone(QImage(fullPath));
        }
    });
    connect(job, &NetJob::failed, this, [this, generation, onDone](const QString&) {
        if (generation == m_generation) {
            onDone(QImage());
        }
    });
    connect(job, &NetJob::finished, job, &NetJob::deleteLater);
    job->start();
}
