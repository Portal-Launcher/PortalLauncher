// SPDX-License-Identifier: GPL-3.0-only
#include <QTest>
#include <QTemporaryDir>

#include "modplatform/modrinth/shared/ModrinthSharedApi.h"
#include "modplatform/modrinth/shared/ModrinthSharedAttachment.h"

using namespace ModrinthShared;

class ModrinthSharedTest : public QObject {
    Q_OBJECT

   private slots:
    void test_parseInviteRef_data()
    {
        QTest::addColumn<QString>("ref");
        QTest::addColumn<QString>("expected");

        QTest::newRow("bare id") << "abc123" << "abc123";
        QTest::newRow("bare id trimmed") << "  abc123  " << "abc123";
        QTest::newRow("full link") << "https://modrinth.com/share/xYz09" << "xYz09";
        QTest::newRow("schemeless link") << "modrinth.com/share/abc" << "abc";
        QTest::newRow("www subdomain") << "https://www.modrinth.com/share/abc" << "abc";
        QTest::newRow("trailing slash") << "https://modrinth.com/share/abc/" << "abc";
        QTest::newRow("other host") << "https://example.com/share/abc" << "";
        QTest::newRow("host suffix trick") << "https://notmodrinth.com/share/abc" << "";
        QTest::newRow("host prefix trick") << "https://modrinth.com.evil.example/share/abc" << "";
        QTest::newRow("no id after share") << "https://modrinth.com/share/" << "";
        QTest::newRow("no share segment") << "https://modrinth.com/mod/sodium" << "";
        QTest::newRow("bad characters") << "abc$123" << "";
        QTest::newRow("path traversal") << "../abc" << "";
        QTest::newRow("too long") << QString(65, 'a') << "";
        QTest::newRow("empty") << "" << "";
    }

    void test_parseInviteRef()
    {
        QFETCH(QString, ref);
        QFETCH(QString, expected);
        QCOMPARE(parseInviteRef(ref), expected);
    }

    void test_isTrustedDownloadUrl_data()
    {
        QTest::addColumn<QString>("url");
        QTest::addColumn<bool>("trusted");

        QTest::newRow("modrinth cdn") << "https://cdn.modrinth.com/data/AANobbMI/versions/x/sodium.jar" << true;
        QTest::newRow("modrinth apex") << "https://modrinth.com/x.jar" << true;
        QTest::newRow("service bucket")
            << "https://shared-instances.9ddae624c98677d68d93df6e524a6061.r2.cloudflarestorage.com/pKjQg68h/0/AE2-Things.jar?"
               "x-id=GetObject&X-Amz-Algorithm=AWS4-HMAC-SHA256&X-Amz-Signature=abc"
            << true;
        QTest::newRow("plain http") << "http://cdn.modrinth.com/x.jar" << false;
        QTest::newRow("host suffix trick") << "https://notmodrinth.com/x.jar" << false;
        QTest::newRow("host prefix trick") << "https://cdn.modrinth.com.evil.example/x.jar" << false;
        QTest::newRow("other bucket") << "https://evil.9ddae624c98677d68d93df6e524a6061.r2.cloudflarestorage.com/x.jar" << false;
        QTest::newRow("bucket without account") << "https://shared-instances.r2.cloudflarestorage.com/x.jar" << false;
        QTest::newRow("bucket prefix trick") << "https://shared-instances.abcdef0123456789.r2.cloudflarestorage.com.evil.example/x.jar"
                                             << false;
        QTest::newRow("empty") << "" << false;
    }

    void test_isTrustedDownloadUrl()
    {
        QFETCH(QString, url);
        QFETCH(bool, trusted);
        QCOMPARE(isTrustedDownloadUrl(QUrl(url)), trusted);
    }

    void test_attachmentRoundTrip()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());

        Attachment att;
        att.id = "a1B2c3";
        att.role = "owner";
        att.appliedVersion = 7;
        att.configSpec = "config/,options.txt";
        att.autoPush = true;
        att.iconSha1 = "0123456789abcdef0123456789abcdef01234567";
        att.iconCheckedAt = 1755000000;
        att.lastChangeLog = QStringList{ "Added Sodium", "Removed OptiFine" };
        att.lastChangeVersion = 6;
        att.lastInviteLink = "https://modrinth.com/share/xYz09";
        att.optionalProjects = QStringList{ "AANobbMI" };
        att.optionalFiles = QStringList{ "extra-mod.jar" };
        att.disabledOptional = QStringList{ "AANobbMI" };
        ManagedFile mf;
        mf.rel = "mods/sodium.jar";
        mf.sha1 = "da39a3ee5e6b4b0d3255bfef95601890afd80709";
        mf.size = 12345;
        mf.source = "modrinth:abcd1234";
        mf.optionalKey = "AANobbMI";
        att.managedFiles.append(mf);
        att.managedConfigs.append("config/sodium.json");

        QVERIFY(att.save(root.path()));
        auto loaded = Attachment::load(root.path());
        QVERIFY(loaded.has_value());

        QCOMPARE(loaded->id, att.id);
        QCOMPARE(loaded->role, att.role);
        QCOMPARE(loaded->appliedVersion, att.appliedVersion);
        QCOMPARE(loaded->configSpec, att.configSpec);
        QCOMPARE(loaded->autoPush, att.autoPush);
        QCOMPARE(loaded->iconSha1, att.iconSha1);
        QCOMPARE(loaded->iconCheckedAt, att.iconCheckedAt);
        QCOMPARE(loaded->lastChangeLog, att.lastChangeLog);
        QCOMPARE(loaded->lastChangeVersion, att.lastChangeVersion);
        QCOMPARE(loaded->lastInviteLink, att.lastInviteLink);
        QCOMPARE(loaded->optionalProjects, att.optionalProjects);
        QCOMPARE(loaded->optionalFiles, att.optionalFiles);
        QCOMPARE(loaded->disabledOptional, att.disabledOptional);
        QCOMPARE(loaded->managedConfigs, att.managedConfigs);
        QCOMPARE(loaded->managedFiles.size(), 1);
        QCOMPARE(loaded->managedFiles[0].rel, mf.rel);
        QCOMPARE(loaded->managedFiles[0].sha1, mf.sha1);
        QCOMPARE(loaded->managedFiles[0].size, mf.size);
        QCOMPARE(loaded->managedFiles[0].source, mf.source);
        QCOMPARE(loaded->managedFiles[0].optionalKey, mf.optionalKey);
    }

    void test_attachmentRejectsBadId()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());

        // A hand-edited or malicious id must never load: it ends up in
        // service URL paths and an icon filename.
        Attachment att;
        att.id = "../../etc";
        att.role = "member";
        QVERIFY(att.save(root.path()));
        QVERIFY(!Attachment::load(root.path()).has_value());
    }

    void test_attachmentMissing()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        QVERIFY(!Attachment::load(root.path()).has_value());
    }
};

QTEST_GUILESS_MAIN(ModrinthSharedTest)

#include "ModrinthShared_test.moc"
