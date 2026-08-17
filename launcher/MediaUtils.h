// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QString>
#include <QUrl>

namespace MediaUtils {

enum class Kind { Unknown, Image, AnimatedImage, Video };

/** Classifies media from its URL, including common filename and format query parameters. */
Kind kindFromUrl(const QUrl& url);

/** Classifies a downloaded file by content, falling back to its source URL. */
Kind kindFromFile(const QString& path, const QUrl& sourceUrl = {});

/** Converts known embed-player URLs to their normal browser-friendly page. */
QUrl watchableUrl(const QUrl& url);

}  // namespace MediaUtils
