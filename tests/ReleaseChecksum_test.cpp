// SPDX-License-Identifier: GPL-3.0-only
#include <QTest>

#include "updater/ReleaseChecksum.h"

class ReleaseChecksumTest : public QObject {
    Q_OBJECT

   private slots:
    void test_findsAssetInComment()
    {
        const QString body =
            "Notes above.\n\n## Download and install\n...\n\n<!--\n"
            "sha256 PortalLauncher-Setup-1.0.8.exe 37FBD23F4B8B80DB676FFD01A765E21B5CB0A0924BDDA8B8CD978C1659E0E1BF\n"
            "sha256 PortalLauncher-Windows-MSVC-1.0.8.zip ad6d48d9099e7c6e9eb8ca07db93fb94c0ec1f37193404772535f53e5724eae4\n"
            "-->\n";
        QCOMPARE(releaseBodyChecksum(body, "PortalLauncher-Windows-MSVC-1.0.8.zip"),
                 QString("ad6d48d9099e7c6e9eb8ca07db93fb94c0ec1f37193404772535f53e5724eae4"));
        // case of the hex digits does not matter, the result is normalised
        QCOMPARE(releaseBodyChecksum(body, "portallauncher-setup-1.0.8.exe"),
                 QString("37fbd23f4b8b80db676ffd01a765e21b5cb0a0924bdda8b8cd978c1659e0e1bf"));
    }

    void test_missingOrMalformed()
    {
        QCOMPARE(releaseBodyChecksum("", "x.zip"), QString());
        QCOMPARE(releaseBodyChecksum("sha256 x.zip deadbeef\n", "x.zip"), QString());  // too short
        QCOMPARE(releaseBodyChecksum("sha256 y.zip " + QString(64, 'a') + "\n", "x.zip"), QString());
        // a hash mentioned in running text, not on its own line, is not trusted
        QCOMPARE(releaseBodyChecksum("the sha256 x.zip " + QString(64, 'a') + " is fine", "x.zip"), QString());
    }
};

QTEST_GUILESS_MAIN(ReleaseChecksumTest)

#include "ReleaseChecksum_test.moc"
