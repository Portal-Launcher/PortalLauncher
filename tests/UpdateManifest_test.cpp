// SPDX-License-Identifier: GPL-3.0-only
#include <QTest>

#include "updater/UpdateManifest.h"

class UpdateManifestTest : public QObject {
    Q_OBJECT

   private slots:
    void test_keepsRelativeEntries()
    {
        const QString manifest = "prismlauncher.exe\n"
                                 "  qt.conf  \n"
                                 "\n"
                                 "platforms/qwindows.dll\r\n"
                                 "jars/NewLaunch.jar\n";
        QCOMPARE(parseUpdateManifest(manifest),
                 (QStringList{ "prismlauncher.exe", "qt.conf", "platforms/qwindows.dll", "jars/NewLaunch.jar" }));
    }

    void test_rejectsEscapes_data()
    {
        QTest::addColumn<QString>("entry");

        // the updater deletes by these names after an install; none of these
        // may ever resolve outside the install folder
        QTest::newRow("parent dir") << "../prismlauncher.cfg";
        QTest::newRow("nested parent") << "platforms/../../accounts.json";
        QTest::newRow("unix absolute") << "/etc/passwd";
        QTest::newRow("backslash root") << "\\Windows\\System32\\kernel32.dll";
        QTest::newRow("windows drive") << "C:/Windows/System32/kernel32.dll";
        QTest::newRow("windows drive backslash") << "C:\\Windows\\notepad.exe";
        QTest::newRow("unc share") << "\\\\server\\share\\file";
    }

    void test_rejectsEscapes()
    {
        QFETCH(QString, entry);
        QVERIFY2(parseUpdateManifest(entry).isEmpty(), qPrintable("accepted unsafe manifest entry: " + entry));
        // and it does not poison the entries around it
        QCOMPARE(parseUpdateManifest("safe.dll\n" + entry + "\nother.dll"), (QStringList{ "safe.dll", "other.dll" }));
    }

    void test_emptyManifest()
    {
        QVERIFY(parseUpdateManifest("").isEmpty());
        QVERIFY(parseUpdateManifest("\n\n   \n").isEmpty());
    }
};

QTEST_GUILESS_MAIN(UpdateManifestTest)

#include "UpdateManifest_test.moc"
