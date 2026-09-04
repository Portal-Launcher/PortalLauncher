// SPDX-FileCopyrightText: 2023 flowln <flowlnlnln@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#include "ModModel.h"

#include "minecraft/MinecraftInstance.h"
#include "minecraft/PackProfile.h"
#include "minecraft/mod/ModFolderModel.h"
#include "modplatform/ModIndex.h"

#include <QMessageBox>
#include <algorithm>

namespace ResourceDownload {

ModModel::ModModel(BaseInstance& base_inst, ResourceAPI* api, QString debugName, QString metaEntryBase)
    : ResourceModel(api), m_base_instance(base_inst), m_debugName(debugName + " (Model)"), m_metaEntryBase(metaEntryBase)
{}

/******** Make data requests ********/

ResourceAPI::SearchArgs ModModel::createSearchArguments()
{
    auto profile = static_cast<MinecraftInstance const&>(m_base_instance).getPackProfile();

    Q_ASSERT(profile);
    Q_ASSERT(m_filter);

    std::optional<std::vector<Version>> versions{};
    std::optional<QStringList> categories{};
    auto loaders = profile->getSupportedModLoaders();

    // Version filter
    if (!m_filter->versions.empty())
        versions = m_filter->versions;
    if (m_filter->loaders)
        loaders = m_filter->loaders;
    if (!m_filter->categoryIds.empty())
        categories = m_filter->categoryIds;
    auto side = m_filter->side;

    auto sort = getCurrentSortingMethodByIndex();

    return {
        ModPlatform::ResourceType::Mod, m_next_search_offset, m_search_term, sort, loaders, versions, side, categories, m_filter->openSource
    };
}

ResourceAPI::VersionSearchArgs ModModel::createVersionsArguments(const QModelIndex& entry)
{
    auto pack = m_packs[entry.row()];
    auto profile = static_cast<MinecraftInstance const&>(m_base_instance).getPackProfile();

    Q_ASSERT(profile);
    Q_ASSERT(m_filter);

    std::optional<std::vector<Version>> versions{};
    auto loaders = profile->getSupportedModLoaders();
    if (!m_filter->versions.empty())
        versions = m_filter->versions;
    if (m_filter->loaders)
        loaders = m_filter->loaders;

    return { pack, versions, loaders, ModPlatform::ResourceType::Mod };
}

ResourceAPI::ProjectInfoArgs ModModel::createInfoArguments(const QModelIndex& entry)
{
    auto pack = m_packs[entry.row()];
    return { pack };
}

void ModModel::searchWithTerm(const QString& term, unsigned int sort, bool filter_changed)
{
    if (m_search_term == term && m_search_term.isNull() == term.isNull() && m_current_sort_index == sort && !filter_changed) {
        return;
    }

    setSearchTerm(term);
    m_current_sort_index = sort;

    refresh();
}

namespace {
/** "Sodium Extra", "sodium-extra" and "SodiumExtra" are the same listing. */
QString normalisedModName(const QString& name)
{
    QString out;
    for (const QChar c : name) {
        if (c.isLetterOrNumber())
            out += c.toLower();
    }
    return out;
}

/** The same mod listed on Modrinth and on CurseForge has different project ids,
 *  so an id match only works within one provider. Across providers the slug
 *  or the name has to do: those are the same on both sites in practice, and
 *  a mod you installed from Modrinth should not show as "not installed" when
 *  you come across it on CurseForge. */
bool metadataMatchesPack(const Metadata::ModStruct& meta, const ModPlatform::IndexedPack& pack)
{
    if (meta.provider == pack.provider)
        return meta.project_id == pack.addonId;
    if (!meta.slug.isEmpty() && !pack.slug.isEmpty() && meta.slug.compare(pack.slug, Qt::CaseInsensitive) == 0)
        return true;
    const QString ours = normalisedModName(meta.name);
    return !ours.isEmpty() && ours == normalisedModName(pack.name);
}
}  // namespace

bool ModModel::isPackInstalled(ModPlatform::IndexedPack::Ptr pack) const
{
    auto allMods = static_cast<MinecraftInstance&>(m_base_instance).loaderModList()->allMods();
    return std::any_of(allMods.cbegin(), allMods.cend(), [pack](Mod* mod) {
        if (auto meta = mod->metadata(); meta)
            return metadataMatchesPack(*meta, *pack);
        return false;
    });
}

QVariant ModModel::getInstalledPackVersion(ModPlatform::IndexedPack::Ptr pack) const
{
    auto allMods = static_cast<MinecraftInstance&>(m_base_instance).loaderModList()->allMods();
    for (auto mod : allMods) {
        if (auto meta = mod->metadata(); meta && metadataMatchesPack(*meta, *pack)) {
            // Version ids only line up within one provider; for a cross
            // provider match there is no file id to mark as installed.
            return meta->provider == pack->provider ? meta->version() : QVariant();
        }
    }
    return {};
}

bool checkSide(ModPlatform::Side filter, ModPlatform::Side value)
{
    return (filter != ModPlatform::Side::ClientSide && filter != ModPlatform::Side::ServerSide) ||
           (value != ModPlatform::Side::ClientSide && value != ModPlatform::Side::ServerSide) || filter == value;
}

bool ModModel::checkFilters(ModPlatform::IndexedPack::Ptr pack)
{
    if (!m_filter)
        return true;
    return !(m_filter->hideInstalled && isPackInstalled(pack)) && checkSide(m_filter->side, pack->side);
}

bool ModModel::checkVersionFilters(const ModPlatform::IndexedVersion& v)
{
    if (!m_filter)
        return true;
    auto loaders = static_cast<MinecraftInstance&>(m_base_instance).getPackProfile()->getSupportedModLoaders();
    if (m_filter->loaders)
        loaders = m_filter->loaders;
    return (!optedOut(v) &&                                                         // is opted out(aka curseforge download link)
            (!loaders.has_value() || !v.loaders || loaders.value() & v.loaders) &&  // loaders
            checkSide(m_filter->side, v.side) &&                                    // side
            (m_filter->releases.empty() ||                                          // releases
             std::find(m_filter->releases.cbegin(), m_filter->releases.cend(), v.version_type) != m_filter->releases.cend()) &&
            m_filter->checkMcVersions(v.mcVersion));  // mcVersions
}

}  // namespace ResourceDownload
