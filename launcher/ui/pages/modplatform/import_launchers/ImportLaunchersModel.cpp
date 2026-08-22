// SPDX-License-Identifier: GPL-3.0-only
#include "ImportLaunchersModel.h"

#include <QThreadPool>
#include <QtConcurrentRun>

#include "Application.h"
#include "StringUtils.h"
#include "Version.h"
#include "settings/SettingsObject.h"
#include "ui/widgets/ProjectItem.h"

namespace LauncherImport {

namespace {
const char* EXTRA_PATHS_SETTING = "ImportLauncherPaths";

QString fallbackIconName(Source source)
{
    switch (source) {
        case Source::CurseForge:
            return QStringLiteral("flame");
        case Source::ModrinthApp:
            return QStringLiteral("modrinth");
        case Source::ATLauncher:
            return QStringLiteral("atlauncher");
        case Source::Vanilla:
            return QStringLiteral("grass");
        default:
            return QStringLiteral("launcher");
    }
}
}  // namespace

ListModel::ListModel(QObject* parent) : QAbstractListModel(parent)
{
    connect(&m_watcher, &QFutureWatcher<QList<FoundInstance>>::finished, this, [this] {
        beginResetModel();
        m_found = m_future.result();
        m_scanning = false;
        endResetModel();
        emit scanFinished(m_found.size());
    });
}

QStringList ListModel::extraPaths() const
{
    return APPLICATION->settings()->get(EXTRA_PATHS_SETTING).toString().split(';', Qt::SkipEmptyParts);
}

void ListModel::addExtraPath(const QString& path)
{
    auto paths = extraPaths();
    if (path.isEmpty() || paths.contains(path))
        return;
    paths.append(path);
    APPLICATION->settings()->set(EXTRA_PATHS_SETTING, paths.join(';'));
}

void ListModel::refresh()
{
    if (m_scanning)
        return;
    m_scanning = true;
    const QStringList extras = extraPaths();
    m_future = QtConcurrent::run(QThreadPool::globalInstance(), [extras] {
        auto found = scanDefaultLocations();
        for (const QString& path : extras)
            for (const auto& inst : scanPath(path))
                addUnique(found, inst);
        return found;
    });
    m_watcher.setFuture(m_future);
}

QIcon ListModel::iconFor(const FoundInstance& inst) const
{
    const QString key = inst.iconPath.isEmpty() ? QStringLiteral("theme:") + fallbackIconName(inst.source) : inst.iconPath;
    auto it = m_iconCache.constFind(key);
    if (it != m_iconCache.constEnd())
        return *it;
    QIcon icon;
    if (!inst.iconPath.isEmpty())
        icon = QIcon(inst.iconPath);
    if (icon.isNull() || icon.availableSizes().isEmpty())
        icon = QIcon::fromTheme(fallbackIconName(inst.source));
    m_iconCache.insert(key, icon);
    return icon;
}

QVariant ListModel::data(const QModelIndex& index, int role) const
{
    const int row = index.row();
    if (!index.isValid() || row < 0 || row >= m_found.size())
        return {};
    const auto& inst = m_found.at(row);

    switch (role) {
        case Qt::DisplayRole:
        case UserDataTypes::TITLE:
            return inst.name;
        case Qt::DecorationRole:
            return iconFor(inst);
        case Qt::ToolTipRole:
            return tr("%1\n%2").arg(inst.launcherName(), QDir::toNativeSeparators(inst.location()));
        case Qt::SizeHintRole:
            return QSize(0, 58);
        case UserDataTypes::DESCRIPTION: {
            QString line = inst.mcVersion == QLatin1String("?") ? inst.launcherName()
                                                                  : tr("%1 - Minecraft %2").arg(inst.launcherName(), inst.mcVersion);
            const QString loader = inst.loaderDescription();
            if (!loader.isEmpty())
                line += QStringLiteral(" - ") + loader;
            return line;
        }
        case UserDataTypes::INSTALLED:
            return false;
        case Qt::UserRole:
            return QVariant::fromValue(inst);
        default:
            return {};
    }
}

// ---------------------------------------------------------------- filter

FilterModel::FilterModel(QObject* parent) : QSortFilterProxyModel(parent)
{
    m_sortings.insert(tr("Sort by Launcher"), ByLauncher);
    m_sortings.insert(tr("Sort by Name"), ByName);
    m_sortings.insert(tr("Sort by Game Version"), ByGameVersion);
}

QString FilterModel::translateCurrentSorting() const
{
    return m_sortings.key(m_currentSorting);
}

void FilterModel::setSorting(Sorting sorting)
{
    m_currentSorting = sorting;
    invalidate();
}

void FilterModel::setSearchTerm(const QString& term)
{
    m_searchTerm = term.trimmed();
    invalidate();
}

bool FilterModel::filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const
{
    if (m_searchTerm.isEmpty())
        return true;
    const auto inst = sourceModel()->data(sourceModel()->index(sourceRow, 0, sourceParent), Qt::UserRole).value<FoundInstance>();
    return inst.name.contains(m_searchTerm, Qt::CaseInsensitive) || inst.launcherName().contains(m_searchTerm, Qt::CaseInsensitive) ||
           inst.mcVersion.contains(m_searchTerm, Qt::CaseInsensitive) || inst.loaderDescription().contains(m_searchTerm, Qt::CaseInsensitive);
}

bool FilterModel::lessThan(const QModelIndex& left, const QModelIndex& right) const
{
    const auto l = sourceModel()->data(left, Qt::UserRole).value<FoundInstance>();
    const auto r = sourceModel()->data(right, Qt::UserRole).value<FoundInstance>();
    switch (m_currentSorting) {
        case ByGameVersion:
            // newest first; the view sorts ascending so invert here
            return Version(l.mcVersion) > Version(r.mcVersion);
        case ByLauncher:
            if (l.source != r.source)
                return static_cast<int>(l.source) < static_cast<int>(r.source);
            [[fallthrough]];
        case ByName:
            return StringUtils::naturalCompare(l.name, r.name, Qt::CaseInsensitive) < 0;
    }
    return false;
}

}  // namespace LauncherImport
