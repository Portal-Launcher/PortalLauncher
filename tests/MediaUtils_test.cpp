#include <QTest>

#include <Markdown.h>
#include <MediaUtils.h>

class MediaUtilsTest : public QObject {
    Q_OBJECT

   private slots:
    void classifiesMetadataUrls()
    {
        QCOMPARE(MediaUtils::kindFromUrl(QUrl("https://cdn.modrinth.com/data/id/images/demo.GIF?cache=1")),
                 MediaUtils::Kind::AnimatedImage);
        QCOMPARE(MediaUtils::kindFromUrl(QUrl("https://media.example.test/proxy?filename=demo.webm&token=abc")), MediaUtils::Kind::Video);
        QCOMPARE(MediaUtils::kindFromUrl(QUrl("https://media.example.test/file?ext=gif")), MediaUtils::Kind::AnimatedImage);
        QCOMPARE(MediaUtils::kindFromUrl(QUrl("https://edge.example.test/media?format=mp4")), MediaUtils::Kind::Video);
        QCOMPARE(MediaUtils::kindFromUrl(QUrl("https://cdn.modrinth.com/data/id/images/demo.webp")), MediaUtils::Kind::Image);
        QCOMPARE(MediaUtils::kindFromUrl(QUrl("https://www.youtube.com/watch?v=abc_123")), MediaUtils::Kind::Video);
        QCOMPARE(MediaUtils::kindFromUrl(QUrl("https://vimeo.com/123456")), MediaUtils::Kind::Video);
    }

    void convertsEmbedUrls()
    {
        QCOMPARE(MediaUtils::watchableUrl(QUrl("//www.youtube-nocookie.com/embed/abc_123")),
                 QUrl("https://www.youtube.com/watch?v=abc_123"));
        QCOMPARE(MediaUtils::watchableUrl(QUrl("https://player.vimeo.com/video/123456?autoplay=1")), QUrl("https://vimeo.com/123456"));
    }

    void normalizesDescriptionMedia()
    {
        const QString html =
            markdownToHTML(QStringLiteral("<video controls><source src=\"https://cdn.example.test/demo.mp4?x=1&amp;y=2\"></video>\n"
                                          "![clip](https://cdn.example.test/proxy?filename=clip.webm)\n"
                                          "![animation](https://cdn.modrinth.com/data/id/images/demo.gif)"));

        QVERIFY(!html.contains(QStringLiteral("<video"), Qt::CaseInsensitive));
        QCOMPARE(html.count(QStringLiteral("Watch video")), 2);
        QCOMPARE(html.count(QStringLiteral("<img"), Qt::CaseInsensitive), 1);
        QVERIFY(html.contains(QStringLiteral("demo.gif")));
        QVERIFY(html.contains(QStringLiteral("<img"), Qt::CaseInsensitive));
    }
};

QTEST_GUILESS_MAIN(MediaUtilsTest)

#include "MediaUtils_test.moc"
