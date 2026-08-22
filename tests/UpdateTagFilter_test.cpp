// SPDX-License-Identifier: GPL-3.0-only
#include <QTest>

#include "updater/UpdateTagFilter.h"

class UpdateTagFilterTest : public QObject {
    Q_OBJECT

   private slots:
    void test_candidates_data()
    {
        QTest::addColumn<QString>("tag");
        QTest::addColumn<bool>("candidate");

        QTest::newRow("portal release") << "1.0.6" << true;
        QTest::newRow("v prefixed") << "v1.2.0" << true;
        QTest::newRow("two part") << "1.1" << true;
        QTest::newRow("four part") << "1.0.6.1" << true;
        QTest::newRow("upstream plain") << "11.0.3" << true;
        // suffixed tags must stay invisible: the legacy 11.0.3-shared release
        // outranked every 1.x build numerically and got offered as an update
        QTest::newRow("legacy shared") << "11.0.3-shared" << false;
        QTest::newRow("prerelease") << "11.1.0-pre1" << false;
        QTest::newRow("beta") << "1.0.6-beta1" << false;
        QTest::newRow("rc") << "1.0.6rc1" << false;
        QTest::newRow("trailing dot") << "1.0." << false;
        QTest::newRow("whitespace") << "1.0.6 " << false;
        QTest::newRow("words") << "latest" << false;
        QTest::newRow("empty") << "" << false;
    }

    void test_candidates()
    {
        QFETCH(QString, tag);
        QFETCH(bool, candidate);
        QCOMPARE(isUpdateCandidateTag(tag), candidate);
    }
};

QTEST_GUILESS_MAIN(UpdateTagFilterTest)

#include "UpdateTagFilter_test.moc"
