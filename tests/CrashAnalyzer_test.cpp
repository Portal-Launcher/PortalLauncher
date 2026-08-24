// SPDX-License-Identifier: GPL-3.0-only
#include <QTest>

#include "logs/CrashAnalyzer.h"

using CrashAnalyzer::analyze;

class CrashAnalyzerTest : public QObject {
    Q_OBJECT
   private slots:
    void test_cleanLogHasNoFindings()
    {
        const auto findings = analyze(QStringLiteral(
            "[12:00:00] [main/INFO]: Loading Minecraft 1.21.1\n"
            "[12:00:05] [Render thread/INFO]: Sound engine started\n"
            "[12:00:09] [Render thread/INFO]: Created: 1024x512 textures-atlas\n"));
        QVERIFY(findings.isEmpty());
    }

    void test_outOfMemory()
    {
        const auto findings = analyze(QStringLiteral(
            "[12:00:09] [Server thread/ERROR]: Encountered an unexpected exception\n"
            "java.lang.OutOfMemoryError: Java heap space\n"
            "\tat net.minecraft.world.chunk.ChunkSection.<init>(ChunkSection.java:21)\n"));
        QCOMPARE(findings.size(), 1);
        QVERIFY(findings[0].title.contains("memory"));
    }

    void test_wrongJavaVersion()
    {
        const auto findings = analyze(QStringLiteral(
            "Error: LinkageError occurred while loading main class net.fabricmc.loader.impl.launch.knot.KnotClient\n"
            "\tjava.lang.UnsupportedClassVersionError: net/fabricmc/loader/impl/launch/knot/KnotClient has been "
            "compiled by a more recent version of the Java Runtime (class file version 65.0), this version of the "
            "Java Runtime only recognizes class file versions up to 61.0\n"));
        QCOMPARE(findings.size(), 1);
        // class file 65 = Java 21
        QVERIFY(findings[0].explanation.contains("Java 21"));
    }

    void test_mixinFailureNamesTheMod()
    {
        const auto findings = analyze(QStringLiteral(
            "org.spongepowered.asm.mixin.throwables.MixinApplyError: Mixin apply for mod sodium_extra failed "
            "sodium-extra.mixins.json:MixinWorldRenderer from mod sodium_extra -> net.minecraft.class_761: Unable to "
            "locate obfuscation mapping\n"));
        QCOMPARE(findings.size(), 1);
        QVERIFY(findings[0].culprits.contains(QStringLiteral("sodium_extra")));
    }

    void test_fabricMissingDependency()
    {
        const auto findings = analyze(QStringLiteral(
            "net.fabricmc.loader.impl.FormattedException: Some of your mods are incompatible with the game or each "
            "other!\nA potential solution has been determined:\n"
            "Mod 'Sodium Extra' (sodium-extra) 0.5.4+mc1.20.4 requires version 0.5.8 or later of 'Sodium' (sodium), "
            "but only the wrong version is present: 0.5.3+mc1.20.4!\n"));
        QCOMPARE(findings.size(), 1);
        QVERIFY(findings[0].title.contains(QStringLiteral("Sodium")));
        QVERIFY(findings[0].culprits.contains(QStringLiteral("sodium-extra")));
    }

    void test_forgeMissingDependency()
    {
        const auto findings = analyze(QStringLiteral(
            "Missing or unsupported mandatory dependencies:\n"
            "\tMod ID: 'geckolib', Requested by: 'ironsspellbooks', Expected range: '[4.2.1,)', Actual version: "
            "'[MISSING]'\n"));
        QCOMPARE(findings.size(), 1);
        QVERIFY(findings[0].title.contains(QStringLiteral("geckolib")));
        QVERIFY(findings[0].culprits.contains(QStringLiteral("ironsspellbooks")));
    }

    void test_duplicateMod()
    {
        const auto findings = analyze(QStringLiteral(
            "net.fabricmc.loader.impl.FormattedException: Duplicate mod 'sodium' (from sodium-fabric-0.5.8.jar and "
            "sodium-fabric-0.5.3.jar)\n"));
        QCOMPARE(findings.size(), 1);
        QVERIFY(findings[0].suggestion.contains(QStringLiteral("Find Duplicates")));
        QVERIFY(findings[0].culprits.contains(QStringLiteral("sodium")));
    }

    void test_graphicsDriver()
    {
        const auto findings = analyze(QStringLiteral(
            "Caused by: org.lwjgl.LWJGLException: Pixel format not accelerated\n"
            "\tat org.lwjgl.opengl.WindowsPeerInfo.nChoosePixelFormat(Native Method)\n"));
        QCOMPARE(findings.size(), 1);
        QVERIFY(findings[0].title.contains(QStringLiteral("graphics")));
    }

    void test_noSuchMethod()
    {
        const auto findings = analyze(QStringLiteral(
            "java.lang.NoSuchMethodError: net.minecraft.class_310.method_1551()Lnet/minecraft/class_310;\n"
            "\tat someone.mod.Thing.tick(Thing.java:50)\n"));
        QCOMPARE(findings.size(), 1);
        QVERIFY(findings[0].explanation.contains(QStringLiteral("class_310")));
    }

    void test_multipleFindingsCoexist()
    {
        const auto findings = analyze(QStringLiteral(
            "java.lang.OutOfMemoryError: Java heap space\n"
            "Caused by: org.lwjgl.LWJGLException: Pixel format not accelerated\n"));
        QCOMPARE(findings.size(), 2);
    }
};

QTEST_GUILESS_MAIN(CrashAnalyzerTest)

#include "CrashAnalyzer_test.moc"
