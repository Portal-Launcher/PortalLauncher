// SPDX-License-Identifier: GPL-3.0-only
#include "PackSquash.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>
#include <QTimer>

#include "Application.h"
#include "FileSystem.h"
#include "MMCZip.h"
#include "net/ContentCache.h"
#include "settings/SettingsObject.h"

namespace {

// Cache namespace for optimized packs. Bumped when the options below change,
// so old results are not reused for new settings.
const char* CACHE_KIND = "pksq1";

constexpr int OPTIMIZE_TIMEOUT_MS = 5 * 60 * 1000;

/** Lossless only. PackSquash quantizes textures and re-encodes audio by
 *  default, which is a bigger win but silently changes what players see and
 *  hear - not something to do to someone else's pack behind their back. */
QString optionsFile(const QString& packDir, const QString& outputPath)
{
    return QString(
               "pack_directory = '%1'\n"
               "output_file_path = '%2'\n"
               // fully spec-conformant zips: the other zip modes trade
               // compatibility with other tools for size, which is a bad deal
               // for files we hand to other people
               "zip_spec_conformance_level = 'pedantic'\n"
               "never_store_squash_times = true\n"
               "\n"
               "['**/*.png']\n"
               "color_quantization_target = 'none'\n"
               "\n"
               "['**/*.ogg']\n"
               "transcode_ogg = false\n")
        .arg(QString(packDir).replace('\\', '/'), QString(outputPath).replace('\\', '/'));
}

bool enabledInSettings()
{
    return APPLICATION->settings()->get("OptimizeSharedPacks").toBool();
}

}  // namespace

QString PackSquash::binaryPath()
{
#ifdef Q_OS_WIN
    const QString name = QStringLiteral("packsquash.exe");
#else
    const QString name = QStringLiteral("packsquash");
#endif
    const QString path = FS::PathCombine(QCoreApplication::applicationDirPath(), name);
    return QFileInfo::exists(path) ? path : QString();
}

bool PackSquash::isAvailable()
{
    return !binaryPath().isEmpty();
}

bool PackSquash::canOptimize(const QString& fileType)
{
    return fileType == QLatin1String("resourcepack") || fileType == QLatin1String("datapack");
}

void PackSquash::optimize(QObject* ctx, const QString& zipPath, const QString& sourceSha1, std::function<void(QString)> callback)
{
    const QString binary = binaryPath();
    if (binary.isEmpty() || sourceSha1.isEmpty() || !enabledInSettings()) {
        callback(zipPath);
        return;
    }

    // Already optimized this exact pack before, for this instance or another.
    const QString cached = ContentCache::find(CACHE_KIND, sourceSha1);
    if (!cached.isEmpty()) {
        callback(cached);
        return;
    }

    auto tempDir = std::make_shared<QTemporaryDir>();
    if (!tempDir->isValid()) {
        callback(zipPath);
        return;
    }

    const QString extractDir = FS::PathCombine(tempDir->path(), "pack");
    if (!MMCZip::extractDir(zipPath, extractDir)) {
        qDebug() << "PackSquash: could not unpack" << zipPath << "- uploading it as is";
        callback(zipPath);
        return;
    }

    const QString outputPath = FS::PathCombine(tempDir->path(), "optimized.zip");
    const QString optionsPath = FS::PathCombine(tempDir->path(), "packsquash.toml");
    try {
        FS::write(optionsPath, optionsFile(extractDir, outputPath).toUtf8());
    } catch (...) {
        callback(zipPath);
        return;
    }

    auto* process = new QProcess(ctx);
    process->setProgram(binary);
    process->setArguments({ optionsPath });
    process->setProcessChannelMode(QProcess::MergedChannels);

    // tempDir is captured so it outlives the run; the callback fires exactly
    // once, from whichever of these arrives first.
    auto done = std::make_shared<bool>(false);
    auto finish = [callback, zipPath, sourceSha1, outputPath, tempDir, done, process](bool ok) {
        if (*done)
            return;
        *done = true;
        QString result = zipPath;
        if (ok && QFileInfo::exists(outputPath)) {
            const qint64 before = QFileInfo(zipPath).size();
            const qint64 after = QFileInfo(outputPath).size();
            // Keep whichever is smaller; an already-optimized pack can come
            // out slightly bigger, and shipping the bigger file would be silly.
            if (after > 0 && after < before) {
                ContentCache::store(CACHE_KIND, sourceSha1, outputPath);
                const QString stored = ContentCache::find(CACHE_KIND, sourceSha1);
                if (!stored.isEmpty()) {
                    result = stored;
                    qDebug() << "PackSquash:" << QFileInfo(zipPath).fileName() << before / 1024 << "KiB ->" << after / 1024 << "KiB";
                }
            }
        }
        process->deleteLater();
        callback(result);
    };

    QObject::connect(process, &QProcess::finished, ctx, [finish, process](int exitCode, QProcess::ExitStatus status) {
        const bool ok = status == QProcess::NormalExit && exitCode == 0;
        if (!ok)
            qDebug() << "PackSquash failed (exit" << exitCode << "):" << QString::fromUtf8(process->readAll()).left(500);
        finish(ok);
    });
    QObject::connect(process, &QProcess::errorOccurred, ctx, [finish](QProcess::ProcessError) { finish(false); });

    process->start();
    if (!process->waitForStarted(5000)) {
        finish(false);
        return;
    }
    // Do not let a wedged optimizer hold a push forever.
    QTimer::singleShot(OPTIMIZE_TIMEOUT_MS, process, [process]() {
        if (process->state() != QProcess::NotRunning)
            process->kill();
    });
}
