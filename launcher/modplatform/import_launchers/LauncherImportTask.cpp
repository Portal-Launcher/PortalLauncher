// SPDX-License-Identifier: GPL-3.0-only
#include "LauncherImportTask.h"

#include <QDir>
#include <QThreadPool>
#include <QtConcurrentRun>

#include "FileSystem.h"
#include "NullInstance.h"
#include "minecraft/MinecraftInstance.h"
#include "minecraft/PackProfile.h"
#include "modplatform/modrinth/shared/ModrinthSharedAttachment.h"
#include "modplatform/technic/TechnicPackProcessor.h"
#include "settings/INISettingsObject.h"

namespace {

/** Files and folders no launcher's game directory needs carried across: they
 *  are either rebuilt by the launcher (versions, libraries, assets, natives)
 *  or private to the old one. Applied on top of the scanner's own excludes. */
const QStringList COMMON_EXCLUDES = {
    "versions", "libraries", "assets", "natives", "runtime", "logs", "crash-reports", "webcache", "webcache2",
    "launcher_profiles.json", "launcher_accounts.json", "launcher_settings.json", "launcher_ui_state.json",
    "launcher_msa_credentials.bin", "launcher_log.txt", "launcher_cef_log.txt", "launcher.log", "clientId.txt",
    "usercache.json", "usernamecache.json", "realms_persistence.json", ".mixin.out", ".fabric",
};

Filter topLevelExcludes(QStringList names)
{
    return [names = std::move(names)](const QString& relativePath) {
        // FS::copy hands us the path relative to the copy root, so the first
        // segment is the top-level entry
        const QString normalized = QDir::fromNativeSeparators(relativePath);
        const QString first = normalized.section('/', 0, 0, QString::SectionSkipEmpty);
        return names.contains(first, Qt::CaseInsensitive);
    };
}

}  // namespace

void LauncherImportTask::executeTask()
{
    setStatus(tr("Copying files from %1…").arg(m_found.launcherName()));
    setAbortable(false);
    setProgress(1, 2);

    m_copyFuture = QtConcurrent::run(QThreadPool::globalInstance(), [this] {
        if (m_found.isMultiMCFormat()) {
            FS::copy whole(m_found.instanceDir, m_stagingPath);
            return whole();
        }
        QStringList excludes = COMMON_EXCLUDES;
        excludes += m_found.excludes;
        FS::copy gameFiles(m_found.gameDir, FS::PathCombine(m_stagingPath, "minecraft"));
        gameFiles.matcher(topLevelExcludes(excludes));
        return gameFiles();
    });
    connect(&m_copyFutureWatcher, &QFutureWatcher<bool>::finished, this, &LauncherImportTask::copyFinished);
    connect(&m_copyFutureWatcher, &QFutureWatcher<bool>::canceled, this, &LauncherImportTask::emitAborted);
    m_copyFutureWatcher.setFuture(m_copyFuture);
}

void LauncherImportTask::copyFinished()
{
    if (!m_copyFuture.result()) {
        emitFailed(tr("Some files could not be copied from %1.").arg(m_found.location()));
        return;
    }
    setStatus(tr("Setting up the instance…"));
    setProgress(2, 2);
    if (m_found.isMultiMCFormat())
        finishMultiMC();
    else if (m_found.source == LauncherImport::Source::Technic)
        finishTechnic();
    else
        finishGeneric();
}

void LauncherImportTask::finishTechnic()
{
    // Technic packs carry their own version.json (and often a modpack.jar);
    // the same processor the Technic zip import uses turns that into
    // components, so an imported pack is set up exactly like a fresh one.
    m_technicProcessor.reset(new Technic::TechnicPackProcessor());
    connect(m_technicProcessor.get(), &Technic::TechnicPackProcessor::succeeded, this, &LauncherImportTask::emitSucceeded);
    connect(m_technicProcessor.get(), &Technic::TechnicPackProcessor::failed, this, &LauncherImportTask::emitFailed);
    m_technicProcessor->run(m_globalSettings, name(), m_instIcon, m_stagingPath);
}

void LauncherImportTask::finishMultiMC()
{
    // A copied share attachment would make this instance claim membership of
    // the original's shared pack; it has to start unshared.
    ModrinthShared::Attachment::remove(m_stagingPath);

    auto instanceSettings = std::make_unique<INISettingsObject>(FS::PathCombine(m_stagingPath, "instance.cfg"));
    NullInstance instance(m_globalSettings, std::move(instanceSettings), m_stagingPath);
    instance.setName(name());
    if (m_instIcon != "default")
        instance.setIconKey(m_instIcon);
    emitSucceeded();
}

void LauncherImportTask::finishGeneric()
{
    auto instanceSettings = std::make_unique<INISettingsObject>(FS::PathCombine(m_stagingPath, "instance.cfg"));
    MinecraftInstance instance(m_globalSettings, std::move(instanceSettings), m_stagingPath);
    {
        SettingsObject::Lock lock(instance.settings());
        instance.settings()->set("InstanceType", "OneSix");

        auto components = instance.getPackProfile();
        components->buildingFromScratch();
        components->setComponentVersion("net.minecraft", m_found.mcVersion, true);
        if (!m_found.loaderUid.isEmpty() && !m_found.loaderVersion.isEmpty())
            components->setComponentVersion(m_found.loaderUid, m_found.loaderVersion, true);
        components->saveNow();

        instance.setName(name());
        if (m_instIcon != "default")
            instance.setIconKey(m_instIcon);
    }
    emitSucceeded();
}
