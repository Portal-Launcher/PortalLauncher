// SPDX-License-Identifier: GPL-3.0-only
#include <QCryptographicHash>
#include <QTemporaryDir>
#include <QTest>

#include "FileSystem.h"
#include "net/ContentCache.h"

/** The content pool stores files relative to the working directory (the
 *  launcher's data folder), so each test pins the cwd to a fresh temp dir. */
class ContentCacheTest : public QObject {
    Q_OBJECT

    QTemporaryDir m_dataDir;
    QString m_oldCwd;

    const QString m_payload = "portal content pool test payload";

    QString writePayload(const QString& path, const QByteArray& bytes)
    {
        if (!FS::ensureFilePathExists(path))
            return {};
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly))
            return {};
        f.write(bytes);
        f.close();
        return path;
    }

   private slots:
    void initTestCase()
    {
        QVERIFY(m_dataDir.isValid());
        m_oldCwd = QDir::currentPath();
        QVERIFY(QDir::setCurrent(m_dataDir.path()));
    }

    void cleanupTestCase() { QDir::setCurrent(m_oldCwd); }

    void test_missDoesNotInvent()
    {
        QCOMPARE(ContentCache::find("sha1", "0123456789abcdef0123456789abcdef01234567"), QString());
    }

    void test_rejectsUnsafeKeys()
    {
        QCOMPARE(ContentCache::find("sha1", "../escape"), QString());
        QCOMPARE(ContentCache::find("sha1", "short"), QString());
        QCOMPARE(ContentCache::find("", "0123456789abcdef"), QString());
    }

    void test_storeFindDeploy()
    {
        const QByteArray bytes = m_payload.toUtf8();
        const QString source = writePayload(FS::PathCombine(m_dataDir.path(), "source.jar"), bytes);
        QVERIFY(!source.isEmpty());

        const QString sha1 =
            QString(QCryptographicHash::hash(bytes, QCryptographicHash::Sha1).toHex());

        ContentCache::store("sha1", sha1, source);
        const QString cached = ContentCache::find("sha1", sha1);
        QVERIFY(!cached.isEmpty());

        // Deploy to a nested destination that does not exist yet.
        const QString dest = FS::PathCombine(m_dataDir.path(), "instA/minecraft/mods", "copy.jar");
        QVERIFY(ContentCache::deploy(cached, dest, "sha1", sha1));
        QVERIFY(QFile::exists(dest));

        QFile out(dest);
        QVERIFY(out.open(QIODevice::ReadOnly));
        QCOMPARE(out.readAll(), bytes);
    }

    void test_corruptEntryIsDroppedNotServed()
    {
        const QByteArray bytes = QByteArrayLiteral("legit bytes");
        const QString sha1 =
            QString(QCryptographicHash::hash(bytes, QCryptographicHash::Sha1).toHex());

        // Plant a cache entry whose content does not match its key, the way a
        // torn write or disk fault would.
        QVERIFY(FS::ensureFolderPathExists(FS::PathCombine(m_dataDir.path(), "cache", "content")));
        const QString entry = FS::PathCombine(m_dataDir.path(), "cache", "content", "sha1-" + sha1);
        QVERIFY(!writePayload(entry, QByteArrayLiteral("corrupted bytes")).isEmpty());

        const QString cached = ContentCache::find("sha1", sha1);
        QVERIFY(!cached.isEmpty());

        const QString dest = FS::PathCombine(m_dataDir.path(), "instB/minecraft/mods", "mod.jar");
        QVERIFY(!ContentCache::deploy(cached, dest, "sha1", sha1));
        // The bad entry is gone, so the caller's fallback redownloads fresh.
        QVERIFY(!QFile::exists(entry));
        QVERIFY(!QFile::exists(dest));
    }

    void test_deployOverwritesStaleFile()
    {
        const QByteArray bytes = QByteArrayLiteral("new version bytes");
        const QString source = writePayload(FS::PathCombine(m_dataDir.path(), "new.jar"), bytes);
        const QString sha1 =
            QString(QCryptographicHash::hash(bytes, QCryptographicHash::Sha1).toHex());
        ContentCache::store("sha1", sha1, source);

        const QString dest = FS::PathCombine(m_dataDir.path(), "instC/minecraft/mods", "mod.jar");
        QVERIFY(!writePayload(dest, QByteArrayLiteral("old version bytes")).isEmpty());

        const QString cached = ContentCache::find("sha1", sha1);
        QVERIFY(ContentCache::deploy(cached, dest, "sha1", sha1));
        QFile out(dest);
        QVERIFY(out.open(QIODevice::ReadOnly));
        QCOMPARE(out.readAll(), bytes);
    }
};

QTEST_GUILESS_MAIN(ContentCacheTest)

#include "ContentCache_test.moc"
