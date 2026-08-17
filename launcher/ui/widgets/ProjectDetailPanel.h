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

#include <QUrl>
#include <QWidget>

#include <functional>

#include "modplatform/ModIndex.h"

class QLabel;
class QTabWidget;
class QListWidget;
class QListWidgetItem;
class QTreeWidget;
class ProjectDescriptionPage;

namespace ResourceDownload {
class ResourceModel;
}

/** The rich project panel shown next to the search results in the resource
 *  download dialog: a header with icon, stats, tags and links, then
 *  Description / Gallery / Versions / Changelog tabs.
 */
class ProjectDetailPanel final : public QWidget {
    Q_OBJECT

   public:
    explicit ProjectDetailPanel(QWidget* parent = nullptr);

    /** Namespace used to cache downloaded images. */
    void setMetaEntry(QString entry);

    /** Shows a plain message instead of any project content. */
    void setInfoText(const QString& text);
    void setText(const QString& text) { setInfoText(text); }
    void clear();

    /** Rebuilds the panel for the given pack. The model is used for version
     *  filters and the installed-version marker; it may be null. */
    void setPack(ModPlatform::IndexedPack::Ptr pack, ResourceDownload::ResourceModel* model);

    /** Rebuilds the versions tab, e.g. after the version list finished loading. */
    void updateVersions();

    /** Highlights the given index of the pack's version list (-1 for none). */
    void setSelectedVersion(int versionIndex);

   signals:
    void openUrlRequested(const QUrl& url);
    /** The user picked a version row; the index points into the pack's version list. */
    void versionPicked(int versionIndex);

   protected:
    bool event(QEvent* event) override;

   private:
    void rebuildHeader();
    void rebuildDescription();
    void rebuildGallery();
    void rebuildChangelog();
    void prefetchFullGallery();
    void updateTabStates();
    void openImageViewer(int galleryIndex);

    /** Downloads an image through the meta cache and hands it back if the panel
     *  hasn't switched to another pack meanwhile. */
    void fetchImage(const QUrl& url, int generation, const std::function<void(const QImage&)>& onDone);

   private:
    QString m_metaEntry = QStringLiteral("ResourceImages");

    ModPlatform::IndexedPack::Ptr m_pack;
    ResourceDownload::ResourceModel* m_model = nullptr;
    int m_selectedVersion = -1;
    int m_generation = 0;
    bool m_syncingSelection = false;
    bool m_galleryPrefetched = false;

    QWidget* m_header = nullptr;
    QLabel* m_iconLabel = nullptr;
    QLabel* m_titleLabel = nullptr;
    QLabel* m_statsLabel = nullptr;
    QLabel* m_tagsLabel = nullptr;
    QLabel* m_linksLabel = nullptr;

    QTabWidget* m_tabs;
    ProjectDescriptionPage* m_description;
    QListWidget* m_gallery;
    QTreeWidget* m_versions;
    ProjectDescriptionPage* m_changelog;
};
