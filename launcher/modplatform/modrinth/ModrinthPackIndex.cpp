// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Prism Launcher - Minecraft Launcher
 *  Copyright (c) 2022 flowln <flowlnlnln@gmail.com>
 *  Copyright (c) 2023 Trial97 <alexandru.tripon97@gmail.com>
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

#include "ModrinthPackIndex.h"
#include "FileSystem.h"
#include "ModrinthAPI.h"

#include <algorithm>

#include "Json.h"
#include "minecraft/MinecraftInstance.h"
#include "minecraft/PackProfile.h"
#include "modplatform/ModIndex.h"

bool shouldDownloadOnSide(const QString& side)
{
    return side == "required" || side == "optional";
}

// https://docs.modrinth.com/api/operations/getproject/
void Modrinth::loadIndexedPack(ModPlatform::IndexedPack& pack, QJsonObject& obj)
{
    pack.addonId = obj["project_id"].toString();
    if (pack.addonId.toString().isEmpty()) {
        pack.addonId = Json::requireString(obj, "id");
    }

    pack.provider = ModPlatform::ResourceProvider::MODRINTH;
    pack.name = Json::requireString(obj, "title");

    pack.slug = obj["slug"].toString("");
    if (!pack.slug.isEmpty()) {
        pack.websiteUrl = "https://modrinth.com/mod/" + pack.slug;
    } else {
        pack.websiteUrl = "";
    }

    pack.description = obj["description"].toString("");

    pack.logoUrl = obj["icon_url"].toString("");
    pack.logoName = QString("%1.%2").arg(obj["slug"].toString(), QFileInfo(QUrl(pack.logoUrl).fileName()).suffix());

    if (obj.contains("author")) {
        ModPlatform::ModpackAuthor modAuthor;
        modAuthor.name = obj["author"].toString();
        modAuthor.url = ModrinthAPI::getAuthorURL(modAuthor.name);
        pack.authors = { modAuthor };
    }

    auto client = shouldDownloadOnSide(obj["client_side"].toString());
    auto server = shouldDownloadOnSide(obj["server_side"].toString());

    if (server && client) {
        pack.side = ModPlatform::Side::UniversalSide;
    } else if (server) {
        pack.side = ModPlatform::Side::ServerSide;
    } else if (client) {
        pack.side = ModPlatform::Side::ClientSide;
    }

    // Rich info available straight from the search results. Only set what's present,
    // since this also runs on the full project object where some keys differ.
    if (obj.contains("downloads")) {
        pack.extraData.downloads = static_cast<qint64>(obj["downloads"].toDouble(-1));
    }
    if (obj.contains("follows")) {
        pack.extraData.followers = static_cast<qint64>(obj["follows"].toDouble(-1));
    }
    if (obj.contains("license") && obj["license"].isString()) {
        pack.extraData.license = obj["license"].toString();
    }
    if (obj.contains("date_created")) {
        pack.extraData.dateCreated = QDateTime::fromString(obj["date_created"].toString(), Qt::ISODateWithMs);
    }
    if (obj.contains("date_modified")) {
        pack.extraData.dateModified = QDateTime::fromString(obj["date_modified"].toString(), Qt::ISODateWithMs);
    }

    auto categories = obj["display_categories"].toArray();
    if (categories.isEmpty()) {
        categories = obj["categories"].toArray();
    }
    if (!categories.isEmpty()) {
        pack.extraData.categories.clear();
        for (auto c : categories) {
            auto cat = c.toString();
            if (!cat.isEmpty()) {
                pack.extraData.categories.append(cat);
            }
        }
    }

    // Search hits carry the gallery as a plain list of image URLs. Keep it as a preview
    // until the full project info (with titles and full-size URLs) is loaded.
    if (pack.extraData.gallery.isEmpty()) {
        for (auto img : obj["gallery"].toArray()) {
            if (auto url = img.toString(); !url.isEmpty()) {
                ModPlatform::GalleryImage image;
                image.url = url;
                image.thumbnailUrl = url;
                pack.extraData.gallery.append(image);
            }
        }
    }

    // Modrinth can have more data than what's provided by the basic search :)
    pack.extraDataLoaded = false;
}

/** Best-effort thumbnail variant of a Modrinth CDN gallery image (the CDN serves
 *  a downscaled copy with a _350 suffix). Callers fall back to the full URL. */
static QString modrinthThumbnail(const QString& url)
{
    if (!url.contains("cdn.modrinth.com") || url.contains("_350.")) {
        return url;
    }
    auto dot = url.lastIndexOf('.');
    if (dot <= url.lastIndexOf('/')) {
        return url;
    }
    return url.left(dot) + "_350" + url.mid(dot);
}

void Modrinth::loadExtraPackData(ModPlatform::IndexedPack& pack, QJsonObject& obj)
{
    pack.extraData.issuesUrl = obj["issues_url"].toString();
    if (pack.extraData.issuesUrl.endsWith('/'))
        pack.extraData.issuesUrl.chop(1);

    pack.extraData.sourceUrl = obj["source_url"].toString();
    if (pack.extraData.sourceUrl.endsWith('/'))
        pack.extraData.sourceUrl.chop(1);

    pack.extraData.wikiUrl = obj["wiki_url"].toString();
    if (pack.extraData.wikiUrl.endsWith('/'))
        pack.extraData.wikiUrl.chop(1);

    pack.extraData.discordUrl = obj["discord_url"].toString();
    if (pack.extraData.discordUrl.endsWith('/')) {
        pack.extraData.discordUrl.chop(1);
    }

    auto donate_arr = obj["donation_urls"].toArray();
    for (auto d : donate_arr) {
        auto d_obj = Json::requireObject(d);

        ModPlatform::DonationData donate;

        donate.id = d_obj["id"].toString();
        donate.platform = d_obj["platform"].toString();
        donate.url = d_obj["url"].toString();

        pack.extraData.donate.append(donate);
    }

    pack.extraData.status = obj["status"].toString();

    pack.extraData.body = obj["body"].toString().remove("<br>");

    // The full project object is authoritative for the rich info
    if (obj.contains("downloads")) {
        pack.extraData.downloads = static_cast<qint64>(obj["downloads"].toDouble(-1));
    }
    if (obj.contains("followers")) {
        pack.extraData.followers = static_cast<qint64>(obj["followers"].toDouble(-1));
    }
    if (auto license = obj["license"].toObject(); !license.isEmpty()) {
        pack.extraData.license = license["name"].toString();
        if (pack.extraData.license.isEmpty()) {
            pack.extraData.license = license["id"].toString();
        }
    }
    if (obj.contains("published")) {
        pack.extraData.dateCreated = QDateTime::fromString(obj["published"].toString(), Qt::ISODateWithMs);
    }
    if (obj.contains("updated")) {
        pack.extraData.dateModified = QDateTime::fromString(obj["updated"].toString(), Qt::ISODateWithMs);
    }

    auto gallery = obj["gallery"].toArray();
    if (!gallery.isEmpty()) {
        pack.extraData.gallery.clear();

        QList<QPair<double, ModPlatform::GalleryImage>> ordered;
        for (auto img : gallery) {
            auto img_obj = img.toObject();

            ModPlatform::GalleryImage image;
            image.url = img_obj["url"].toString();
            image.thumbnailUrl = modrinthThumbnail(image.url);
            image.title = img_obj["title"].toString();
            image.description = img_obj["description"].toString();

            if (!image.url.isEmpty()) {
                ordered.append({ img_obj["ordering"].toDouble(0), image });
            }
        }
        std::stable_sort(ordered.begin(), ordered.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        for (auto& [ordering, image] : ordered) {
            pack.extraData.gallery.append(image);
        }
    }

    pack.extraDataLoaded = true;
}

ModPlatform::IndexedVersion Modrinth::loadIndexedPackVersion(QJsonObject& obj,
                                                             const QString& preferred_hash_type,
                                                             const QString& preferred_file_name)
{
    ModPlatform::IndexedVersion file;

    file.addonId = Json::requireString(obj, "project_id");
    file.fileId = Json::requireString(obj, "id");
    file.date = Json::requireString(obj, "date_published");
    auto versionArray = Json::requireArray(obj, "game_versions");
    if (versionArray.empty()) {
        return {};
    }
    for (auto mcVer : versionArray) {
        file.mcVersion.append({ ModrinthAPI::mapMCVersionFromModrinth(mcVer.toString()),
                                mcVer.toString() });  // double this so we can check both strings when filtering
    }
    auto loaders = Json::requireArray(obj, "loaders");
    for (auto loader : loaders) {
        if (loader == "neoforge") {
            file.loaders |= ModPlatform::NeoForge;
        } else if (loader == "forge") {
            file.loaders |= ModPlatform::Forge;
        } else if (loader == "cauldron") {
            file.loaders |= ModPlatform::Cauldron;
        } else if (loader == "liteloader") {
            file.loaders |= ModPlatform::LiteLoader;
        } else if (loader == "fabric") {
            file.loaders |= ModPlatform::Fabric;
        } else if (loader == "quilt") {
            file.loaders |= ModPlatform::Quilt;
        }
    }
    file.version = Json::requireString(obj, "name");
    file.version_number = Json::requireString(obj, "version_number");
    file.version_type = ModPlatform::IndexedVersionType::fromString(Json::requireString(obj, "version_type"));
    if (obj.contains("downloads")) {
        file.downloads = static_cast<qint64>(obj["downloads"].toDouble(-1));
    }

    if (obj.contains("changelog")) {
        file.changelog = Json::requireString(obj, "changelog");
    }

    auto dependencies = obj["dependencies"].toArray();
    for (auto d : dependencies) {
        auto dep = d.toObject();
        ModPlatform::Dependency dependency;
        dependency.addonId = dep["project_id"].toString();
        dependency.version = dep["version_id"].toString();
        auto depType = Json::requireString(dep, "dependency_type");

        if (depType == "required") {
            dependency.type = ModPlatform::DependencyType::REQUIRED;
        } else if (depType == "optional") {
            dependency.type = ModPlatform::DependencyType::OPTIONAL;
        } else if (depType == "incompatible") {
            dependency.type = ModPlatform::DependencyType::INCOMPATIBLE;
        } else if (depType == "embedded") {
            dependency.type = ModPlatform::DependencyType::EMBEDDED;
        } else {
            dependency.type = ModPlatform::DependencyType::UNKNOWN;
        }

        file.dependencies.append(dependency);
    }

    auto files = Json::requireArray(obj, "files");
    int i = 0;

    if (files.empty()) {
        // This should not happen normally, but check just in case
        qWarning() << "Modrinth returned an unexpected empty list of files:" << obj;
        return {};
    }

    // Find correct file (needed in cases where one version may have multiple files)
    // Will default to the last one if there's no primary (though I think Modrinth requires that
    // at least one file is primary, idk)
    // NOTE: files.count() is 1-indexed, so we need to subtract 1 to become 0-indexed
    while (i < files.count() - 1) {
        auto parent = files[i].toObject();
        auto fileName = Json::requireString(parent, "filename");

        if (!preferred_file_name.isEmpty() && fileName.contains(preferred_file_name)) {
            file.is_preferred = true;
            break;
        }

        // Grab the primary file, if available
        if (Json::requireBoolean(parent, "primary")) {
            break;
        }

        i++;
    }

    auto parent = files[i].toObject();
    if (parent.contains("url")) {
        file.downloadUrl = Json::requireString(parent, "url");
        file.fileName = Json::requireString(parent, "filename");
        file.fileName = FS::RemoveInvalidPathChars(file.fileName);
        file.is_preferred = Json::requireBoolean(parent, "primary") || (files.count() == 1);
        auto hash_list = Json::requireObject(parent, "hashes");

        if (hash_list.contains(preferred_hash_type)) {
            file.hash = Json::requireString(hash_list, preferred_hash_type);
            file.hash_type = preferred_hash_type;
        } else {
            auto hash_types = ModPlatform::ProviderCapabilities::hashType(ModPlatform::ResourceProvider::MODRINTH);
            for (auto& hash_type : hash_types) {
                if (hash_list.contains(hash_type)) {
                    file.hash = Json::requireString(hash_list, hash_type);
                    file.hash_type = hash_type;
                    break;
                }
            }
        }

        return file;
    }

    return {};
}
