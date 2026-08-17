// SPDX-License-Identifier: GPL-3.0-only

#include "MediaUtils.h"

#include <QFileInfo>
#include <QImageReader>
#include <QMimeDatabase>
#include <QMimeType>
#include <QRegularExpression>
#include <QSet>
#include <QUrlQuery>

namespace {

MediaUtils::Kind kindFromSuffix(QString suffix)
{
    suffix = suffix.trimmed().toLower();
    if (suffix.startsWith('.'))
        suffix.remove(0, 1);

    static const QSet<QString> animatedExtensions{ QStringLiteral("gif"), QStringLiteral("apng") };
    static const QSet<QString> imageExtensions{ QStringLiteral("png"),  QStringLiteral("jpg"),  QStringLiteral("jpeg"),
                                                QStringLiteral("bmp"),  QStringLiteral("webp"), QStringLiteral("svg"),
                                                QStringLiteral("svgz"), QStringLiteral("rgb"),  QStringLiteral("ico"),
                                                QStringLiteral("tif"),  QStringLiteral("tiff"), QStringLiteral("avif") };
    static const QSet<QString> videoExtensions{ QStringLiteral("mp4"),  QStringLiteral("m4v"), QStringLiteral("webm"),
                                                QStringLiteral("mov"),  QStringLiteral("ogv"), QStringLiteral("ogg"),
                                                QStringLiteral("avi"),  QStringLiteral("mkv"), QStringLiteral("wmv"),
                                                QStringLiteral("mpeg"), QStringLiteral("mpg"), QStringLiteral("m3u8") };

    if (animatedExtensions.contains(suffix))
        return MediaUtils::Kind::AnimatedImage;
    if (imageExtensions.contains(suffix))
        return MediaUtils::Kind::Image;
    if (videoExtensions.contains(suffix))
        return MediaUtils::Kind::Video;
    return MediaUtils::Kind::Unknown;
}

MediaUtils::Kind kindFromCandidate(const QString& candidate)
{
    if (candidate.isEmpty())
        return MediaUtils::Kind::Unknown;

    const QUrl nestedUrl(candidate);
    const QString path = nestedUrl.isValid() && !nestedUrl.path().isEmpty() ? nestedUrl.path() : candidate;
    return kindFromSuffix(QFileInfo(path).suffix());
}

}  // namespace

namespace MediaUtils {

Kind kindFromUrl(const QUrl& url)
{
    if (!url.isValid())
        return Kind::Unknown;

    const QString host = url.host().toLower();
    static const QSet<QString> videoHosts{ QStringLiteral("youtube.com"),
                                           QStringLiteral("www.youtube.com"),
                                           QStringLiteral("youtube-nocookie.com"),
                                           QStringLiteral("www.youtube-nocookie.com"),
                                           QStringLiteral("youtu.be"),
                                           QStringLiteral("vimeo.com"),
                                           QStringLiteral("www.vimeo.com"),
                                           QStringLiteral("player.vimeo.com") };
    if (videoHosts.contains(host))
        return Kind::Video;

    if (auto kind = kindFromCandidate(url.path()); kind != Kind::Unknown)
        return kind;

    const QUrlQuery query(url);
    static const QStringList mediaQueryKeys{ QStringLiteral("filename"), QStringLiteral("file"),   QStringLiteral("name"),
                                             QStringLiteral("url"),      QStringLiteral("format"), QStringLiteral("ext") };
    for (const auto& key : mediaQueryKeys) {
        const QString value = query.queryItemValue(key, QUrl::FullyDecoded);
        if (auto kind = kindFromCandidate(value); kind != Kind::Unknown)
            return kind;
        if (key == QStringLiteral("format") || key == QStringLiteral("ext")) {
            if (auto kind = kindFromSuffix(value); kind != Kind::Unknown)
                return kind;
        }
    }

    return Kind::Unknown;
}

Kind kindFromFile(const QString& path, const QUrl& sourceUrl)
{
    const auto urlKind = kindFromUrl(sourceUrl);
    QImageReader reader(path);
    if (reader.canRead()) {
        if (reader.supportsAnimation() && (reader.imageCount() > 1 || urlKind == Kind::AnimatedImage))
            return Kind::AnimatedImage;
        return Kind::Image;
    }

    const QMimeType mime = QMimeDatabase().mimeTypeForFile(path, QMimeDatabase::MatchContent);
    if (mime.name().startsWith(QStringLiteral("video/")) || mime.inherits(QStringLiteral("application/vnd.apple.mpegurl")) ||
        mime.inherits(QStringLiteral("application/x-mpegurl"))) {
        return Kind::Video;
    }

    return urlKind;
}

QUrl watchableUrl(const QUrl& sourceUrl)
{
    QUrl url(sourceUrl);
    if (url.scheme().isEmpty() && url.toString().startsWith(QStringLiteral("//")))
        url = QUrl(QStringLiteral("https:") + url.toString());

    const QString host = url.host().toLower();
    const QString path = url.path();
    static const QRegularExpression youtubePath(QStringLiteral(R"(^/embed/([A-Za-z0-9_-]+))"));
    static const QRegularExpression vimeoPath(QStringLiteral(R"(^/video/(\d+))"));

    if (host == QStringLiteral("youtube.com") || host == QStringLiteral("www.youtube.com") ||
        host == QStringLiteral("youtube-nocookie.com") || host == QStringLiteral("www.youtube-nocookie.com")) {
        if (const auto match = youtubePath.match(path); match.hasMatch())
            return QUrl(QStringLiteral("https://www.youtube.com/watch?v=%1").arg(match.captured(1)));
    }
    if (host == QStringLiteral("player.vimeo.com")) {
        if (const auto match = vimeoPath.match(path); match.hasMatch())
            return QUrl(QStringLiteral("https://vimeo.com/%1").arg(match.captured(1)));
    }

    return url;
}

}  // namespace MediaUtils
