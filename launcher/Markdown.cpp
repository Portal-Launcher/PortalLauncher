// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Prism Launcher - Minecraft Launcher
 *  Copyright (C) 2023 Joshua Goins <josh@redstrate.com>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "Markdown.h"

#include "MediaUtils.h"

#include <QObject>
#include <QRegularExpression>

namespace {

/** Extracts the src attribute out of a raw HTML tag. */
QString tagSource(const QString& tag)
{
    static const QRegularExpression srcRegex(R"(\bsrc\s*=\s*["']?([^"'\s>]+))", QRegularExpression::CaseInsensitiveOption);
    return srcRegex.match(tag).captured(1).replace(QStringLiteral("&amp;"), QStringLiteral("&"));
}

/** QTextBrowser cannot render iframes or videos; they come out as glitchy
 *  empty space. Replace them with a link that opens the video externally.
 */
QString replaceMediaEmbeds(QString html)
{
    static const QRegularExpression embedRegex(R"((<iframe\b[^>]*>(?:(?!</iframe>).)*(?:</iframe>)?|<video\b[^>]*>(?:(?!</video>).)*(?:</video>)?|<img\b[^>]*>))",
                                               QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);

    QString result;
    qsizetype last = 0;
    auto it = embedRegex.globalMatch(html);
    while (it.hasNext()) {
        auto match = it.next();
        result += html.mid(last, match.capturedStart() - last);

        const QString tag = match.captured();
        QString src = tagSource(tag);
        const bool imageTag = tag.startsWith(QStringLiteral("<img"), Qt::CaseInsensitive);
        if (src.isEmpty()) {
            // <video><source src="..."></video> keeps the URL on the inner tag
            static const QRegularExpression sourceTag(R"(<source\b[^>]*>)", QRegularExpression::CaseInsensitiveOption);
            src = tagSource(sourceTag.match(tag).captured());
        }

        // Normal image tags, including animated GIFs and WebP files, stay in
        // the document and are handled by VariableSizedImageObject.
        if (imageTag && MediaUtils::kindFromUrl(QUrl(src)) != MediaUtils::Kind::Video) {
            result += tag;
        } else if (!src.isEmpty()) {
            result += QString("<p>▶ <a href=\"%1\">%2</a></p>")
                          .arg(MediaUtils::watchableUrl(QUrl(src)).toString().toHtmlEscaped(), QObject::tr("Watch video"));
        }
        last = match.capturedEnd();
    }
    result += html.mid(last);
    return result;
}

}  // namespace

QString markdownToHTML(const QString& markdown)
{
    const QByteArray markdownData = markdown.toUtf8();
    char* buffer = cmark_markdown_to_html(markdownData.constData(), markdownData.length(), CMARK_OPT_NOBREAKS | CMARK_OPT_UNSAFE);

    QString htmlStr(buffer);

    free(buffer);

    return replaceMediaEmbeds(htmlStr);
}
