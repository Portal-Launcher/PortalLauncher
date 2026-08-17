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

#include <QObject>
#include <QRegularExpression>

namespace {

/** Extracts the src attribute out of a raw HTML tag. */
QString tagSource(const QString& tag)
{
    static const QRegularExpression srcRegex(R"(\bsrc\s*=\s*["']?([^"'\s>]+))", QRegularExpression::CaseInsensitiveOption);
    return srcRegex.match(tag).captured(1);
}

/** Turns embed player URLs into their normal watchable counterparts. */
QString watchableUrl(QString url)
{
    static const QRegularExpression youtubeEmbed(R"(^(?:https?:)?//(?:www\.)?(?:youtube(?:-nocookie)?\.com)/embed/([A-Za-z0-9_-]+))",
                                                 QRegularExpression::CaseInsensitiveOption);
    if (auto match = youtubeEmbed.match(url); match.hasMatch())
        return QString("https://www.youtube.com/watch?v=%1").arg(match.captured(1));
    if (url.startsWith("//"))
        url.prepend("https:");
    return url;
}

/** QTextBrowser cannot render iframes or videos; they come out as glitchy
 *  empty space. Replace them with a link that opens the video externally.
 */
QString replaceMediaEmbeds(QString html)
{
    static const QRegularExpression embedRegex(R"((<iframe\b[^>]*>(?:(?!</iframe>).)*(?:</iframe>)?|<video\b[^>]*>(?:(?!</video>).)*(?:</video>)?))",
                                               QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);

    QString result;
    qsizetype last = 0;
    auto it = embedRegex.globalMatch(html);
    while (it.hasNext()) {
        auto match = it.next();
        result += html.mid(last, match.capturedStart() - last);

        const QString tag = match.captured();
        QString src = tagSource(tag);
        if (src.isEmpty()) {
            // <video><source src="..."></video> keeps the URL on the inner tag
            static const QRegularExpression sourceTag(R"(<source\b[^>]*>)", QRegularExpression::CaseInsensitiveOption);
            src = tagSource(sourceTag.match(tag).captured());
        }

        if (!src.isEmpty()) {
            result += QString("<p>▶ <a href=\"%1\">%2</a></p>")
                          .arg(watchableUrl(src).toHtmlEscaped(), QObject::tr("Watch video"));
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
