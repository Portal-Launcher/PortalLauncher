// SPDX-License-Identifier: GPL-3.0-only
#include "CrashAnalyzer.h"

#include <QRegularExpression>
#include <QSet>

namespace CrashAnalyzer {

namespace {

/** Map a JVM class-file major version to the Java release that produces it. */
int javaVersionForClassFile(int classFileMajor)
{
    // Class file 45 = Java 1.1; the offset has been stable ever since.
    return classFileMajor - 44;
}

void addCulprit(Finding& finding, const QString& raw)
{
    const QString id = raw.trimmed();
    // Ignore obvious non-mod ids the regexes can catch.
    static const QSet<QString> ignored = { "minecraft", "java", "fabricloader", "fabric", "forge", "neoforge", "quilt_loader" };
    if (id.isEmpty() || ignored.contains(id.toLower()))
        return;
    if (!finding.culprits.contains(id))
        finding.culprits.append(id);
}

}  // namespace

QList<Finding> analyze(const QString& logText)
{
    QList<Finding> findings;

    // --- Out of memory -----------------------------------------------------
    if (logText.contains(QLatin1String("java.lang.OutOfMemoryError"))) {
        Finding f;
        f.title = QObject::tr("The game ran out of memory");
        f.explanation = QObject::tr(
            "Java hit its memory limit and the game could not continue. Big modpacks and high render distances need "
            "more memory than the default.");
        f.suggestion = QObject::tr(
            "Raise the maximum memory in Settings > Java > Memory (or this instance's Settings > Java). 6-8 GB is a "
            "good range for large packs; avoid giving Java more than half your RAM.");
        findings.append(f);
    }

    // --- Wrong Java version ------------------------------------------------
    {
        static const QRegularExpression classFileRe(
            QStringLiteral("class file version (\\d+)\\.0"));
        const auto match = classFileRe.match(logText);
        if (match.hasMatch() || logText.contains(QLatin1String("UnsupportedClassVersionError"))) {
            Finding f;
            f.title = QObject::tr("This pack needs a newer Java than it was launched with");
            if (match.hasMatch()) {
                const int needed = javaVersionForClassFile(match.captured(1).toInt());
                f.explanation =
                    QObject::tr("Something in this instance was built for Java %1, but the instance launched with an "
                                "older Java, so the game refused to start.")
                        .arg(needed);
                f.suggestion = QObject::tr(
                    "Install Java %1 (Settings > Java > Management > Download) and select it for this instance, or "
                    "turn auto-detection back on so the launcher picks the right one.")
                                   .arg(needed);
            } else {
                f.explanation = QObject::tr(
                    "Something in this instance was built for a newer Java than the one it launched with, so the game "
                    "refused to start.");
                f.suggestion = QObject::tr(
                    "Install a newer Java (Settings > Java > Management > Download) and select it for this instance.");
            }
            findings.append(f);
        }
    }

    // --- Mixin failures ----------------------------------------------------
    {
        static const QRegularExpression mixinFromModRe(
            QStringLiteral("[Mm]ixin (?:apply|transformation)[^\\n]{0,200}?(?:for|from) mod ([A-Za-z0-9_\\-]+)"));
        static const QRegularExpression mixinConfigRe(
            QStringLiteral("(?:MixinApplyError|MixinTransformerError|InvalidMixinException)[^\\n]{0,300}?([A-Za-z0-9_\\-]+)\\.mixins?\\.json"));
        Finding f;
        auto it = mixinFromModRe.globalMatch(logText);
        while (it.hasNext())
            addCulprit(f, it.next().captured(1));
        auto configIt = mixinConfigRe.globalMatch(logText);
        while (configIt.hasNext())
            addCulprit(f, configIt.next().captured(1));
        if (!f.culprits.isEmpty() || logText.contains(QLatin1String("MixinApplyError")) ||
            logText.contains(QLatin1String("MixinTransformerError"))) {
            f.title = f.culprits.isEmpty()
                          ? QObject::tr("A mod's mixin failed to apply")
                          : QObject::tr("A mixin from %1 failed to apply").arg(f.culprits.first());
            f.explanation = QObject::tr(
                "A mod tried to patch the game's code and the patch no longer fits, which usually means the mod is "
                "incompatible with your Minecraft version or with another installed mod.");
            f.suggestion = f.culprits.isEmpty()
                               ? QObject::tr("Update your mods, then disable recently added ones until the crash stops.")
                               : QObject::tr("Update %1 first; if there is no update, disable it and try again.")
                                     .arg(f.culprits.first());
            findings.append(f);
        }
    }

    // --- Missing dependencies (Fabric/Quilt) --------------------------------
    {
        static const QRegularExpression fabricDepRe(QStringLiteral(
            "[Mm]od '([^']{1,64})' \\(([a-z0-9_\\-]+)\\)[^\\n]{0,120}requires[^\\n]{0,120}of (?:mod )?'([^']{1,64})' \\(([a-z0-9_\\-]+)\\)"));
        Finding f;
        QStringList missing;
        auto it = fabricDepRe.globalMatch(logText);
        while (it.hasNext()) {
            const auto match = it.next();
            addCulprit(f, match.captured(2));
            if (!missing.contains(match.captured(3)))
                missing.append(match.captured(3));
        }
        if (!missing.isEmpty()) {
            f.title = QObject::tr("Missing or outdated required mods: %1").arg(missing.join(", "));
            f.explanation = QObject::tr(
                "A mod in this pack depends on other mods (or newer versions of them) that are not installed, so the "
                "loader stopped before the game could start.");
            f.suggestion = QObject::tr("Install or update the listed mods; the log's full text names the versions needed.");
            findings.append(f);
        } else if (logText.contains(QLatin1String("Unmet dependency listing")) ||
                   logText.contains(QLatin1String("DependencyResolutionException"))) {
            f.title = QObject::tr("Some mods are missing required dependencies");
            f.explanation = QObject::tr(
                "The mod loader refused to start because one or more mods need other mods that are not installed.");
            f.suggestion = QObject::tr("Scroll the log for the words \"requires\" or \"depends\" to see which ones.");
            findings.append(f);
        }
    }

    // --- Missing dependencies (Forge/NeoForge) ------------------------------
    if (logText.contains(QLatin1String("Missing or unsupported mandatory dependencies")) ||
        logText.contains(QLatin1String("Missing mandatory dependencies"))) {
        Finding f;
        static const QRegularExpression forgeDepRe(
            QStringLiteral("Mod ID: '([a-z0-9_\\-]+)'[^\\n]{0,120}Requested by: '([a-z0-9_\\-]+)'"));
        QStringList missing;
        auto it = forgeDepRe.globalMatch(logText);
        while (it.hasNext()) {
            const auto match = it.next();
            if (!missing.contains(match.captured(1)))
                missing.append(match.captured(1));
            addCulprit(f, match.captured(2));
        }
        f.title = missing.isEmpty() ? QObject::tr("Some mods are missing required dependencies")
                                    : QObject::tr("Missing required mods: %1").arg(missing.join(", "));
        f.explanation =
            QObject::tr("Forge stopped loading because one or more mods need other mods that are not installed.");
        f.suggestion = QObject::tr("Install the missing mods (or remove the mods that require them).");
        findings.append(f);
    }

    // --- Duplicate mods -----------------------------------------------------
    {
        static const QRegularExpression duplicateRe(
            QStringLiteral("[Dd]uplicate mods? (?:found[:\\s]*)?['\"]?([a-z0-9_\\-]+)"));
        if (logText.contains(QLatin1String("DuplicateModsFoundException")) ||
            logText.contains(QLatin1String("duplicate mod")) || logText.contains(QLatin1String("Duplicate mod"))) {
            Finding f;
            auto it = duplicateRe.globalMatch(logText);
            while (it.hasNext())
                addCulprit(f, it.next().captured(1));
            f.title = QObject::tr("The same mod is installed twice");
            f.explanation = QObject::tr(
                "Two copies of one mod (usually an old version next to a new one) are in the mods folder, and the "
                "loader refuses to pick one.");
            f.suggestion =
                QObject::tr("Use Find Duplicates on the Mods page to clean this up in one click.");
            findings.append(f);
        }
    }

    // --- Graphics driver / display init -------------------------------------
    if (logText.contains(QLatin1String("Pixel format not accelerated")) ||
        logText.contains(QLatin1String("WGL: The driver does not appear to support OpenGL")) ||
        logText.contains(QLatin1String("Failed to create display")) ||
        logText.contains(QLatin1String("GLFW error 65542"))) {
        Finding f;
        f.title = QObject::tr("Your graphics driver could not start the game");
        f.explanation = QObject::tr(
            "The game could not create an OpenGL window. This is a graphics driver problem, not a mod problem - it "
            "often appears after a Windows update replaces the GPU driver with a generic one.");
        f.suggestion = QObject::tr(
            "Update your graphics driver from the GPU maker's site (NVIDIA/AMD/Intel), then reboot. On laptops, also "
            "make sure Java runs on the dedicated GPU in Windows graphics settings.");
        findings.append(f);
    }

    // --- NoSuchMethod / NoClassDef (generic version mismatch) ---------------
    {
        static const QRegularExpression missingRefRe(
            QStringLiteral("java\\.lang\\.(?:NoSuchMethodError|NoClassDefFoundError|NoSuchFieldError): ([\\w.$/]+)"));
        const auto match = missingRefRe.match(logText);
        if (match.hasMatch()) {
            Finding f;
            f.title = QObject::tr("Two mods (or a mod and the game) do not fit together");
            f.explanation = QObject::tr(
                "A mod called code that does not exist in this setup (%1). That almost always means one mod was built "
                "against a different version of another mod, of the loader, or of Minecraft.")
                                .arg(match.captured(1));
            f.suggestion = QObject::tr(
                "Update all mods to versions for this exact Minecraft and loader version. If it started after adding "
                "or updating one mod, put that one back.");
            findings.append(f);
        }
    }

    return findings;
}

}  // namespace CrashAnalyzer
