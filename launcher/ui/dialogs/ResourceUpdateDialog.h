#pragma once

#include "BaseInstance.h"
#include "ResourceDownloadTask.h"
#include "ReviewMessageBox.h"

#include "minecraft/mod/ModFolderModel.h"

#include "modplatform/CheckUpdateTask.h"

class Mod;
class ModrinthCheckUpdate;
class FlameCheckUpdate;
class ConcurrentTask;

class ResourceUpdateDialog final : public ReviewMessageBox {
    Q_OBJECT
   public:
    explicit ResourceUpdateDialog(QWidget* parent,
                                  BaseInstance* instance,
                                  ResourceFolderModel* resourceModel,
                                  QList<Resource*>& searchFor,
                                  bool includeDeps,
                                  QList<ModPlatform::ModLoaderType> loadersList = {});

    void checkCandidates();

    void appendResource(const CheckUpdateTask::Update& info, QStringList requiredBy = {});

    const QList<ResourceDownloadTask::Ptr> getTasks();
    auto indexDir() const -> QDir { return m_resourceModel->indexDir(); }

    auto noUpdates() const -> bool { return m_noUpdates; };
    /** Resources that were left as they are, one human readable line each. */
    auto skipped() const -> const QStringList& { return m_skipped; }
    /** Text to append to an "everything is up to date" message so skipped
     *  resources do not silently read as up to date. Empty when nothing was skipped. */
    QString skippedNote() const;
    auto aborted() const -> bool { return m_aborted; };

   private:
    auto ensureMetadata() -> bool;
    /** Runs the provider update checks for the given resources, appends found
     *  updates to the dialog and reports resources the provider had no usable
     *  version for. Returns false when the user aborted. */
    bool runUpdateCheck(QList<Resource*>& modrinth,
                        QList<Resource*>& flame,
                        QList<std::tuple<Resource*, QString, QUrl>>& failed,
                        QList<std::shared_ptr<GetModDependenciesTask::PackDependency>>& selectedVers);
    /** Mods that turned out to be on no provider are remembered for a week so
     *  the next check neither hashes and queries them again nor asks which
     *  provider to try. */
    bool recentlyUnresolved(Resource* resource) const;
    void rememberUnresolved(Resource* resource);
    QStringList findMissingDependencies(const QList<std::shared_ptr<GetModDependenciesTask::PackDependency>>& selectedVers);

   private slots:
    void onMetadataEnsured(Resource* resource);
    void onMetadataFailed(Resource* resource,
                          bool tryOthers = false,
                          ModPlatform::ResourceProvider firstChoice = ModPlatform::ResourceProvider::MODRINTH);

   private:
    QWidget* m_parent;

    ResourceFolderModel* m_resourceModel;

    QList<Resource*>& m_candidates;
    QList<Resource*> m_modrinthToUpdate;
    QList<Resource*> m_flameToUpdate;

    ConcurrentTask::Ptr m_secondTryMetadata;
    QList<std::tuple<Resource*, QString>> m_failedMetadata;
    QList<std::tuple<Resource*, QString, QUrl>> m_failedCheckUpdate;

    QHash<QString, ResourceDownloadTask::Ptr> m_tasks;
    BaseInstance* m_instance;

    QStringList m_skipped;
    QStringList m_switched;
    bool m_noUpdates = false;
    bool m_aborted = false;
    bool m_includeDeps = false;
    QList<ModPlatform::ModLoaderType> m_loadersList;
};
