// SPDX-License-Identifier: GPL-3.0-only
#include <QTemporaryDir>
#include <QTest>

#include "FileSystem.h"
#include "modplatform/import_launchers/LauncherScanner.h"

using namespace LauncherImport;

class LauncherScannerTest : public QObject {
    Q_OBJECT

    static bool writeFile(const QString& path, const QByteArray& bytes)
    {
        if (!FS::ensureFilePathExists(path))
            return false;
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly))
            return false;
        f.write(bytes);
        return true;
    }

   private slots:
    void test_vanillaVersionIds_data()
    {
        QTest::addColumn<QString>("id");
        QTest::addColumn<QString>("uid");
        QTest::addColumn<QString>("version");

        QTest::newRow("modern forge") << "1.20.1-forge-47.2.0" << "net.minecraftforge" << "47.2.0";
        QTest::newRow("old forge") << "1.12.2-forge-14.23.5.2860" << "net.minecraftforge" << "14.23.5.2860";
        QTest::newRow("neoforge") << "neoforge-21.1.65" << "net.neoforged" << "21.1.65";
        QTest::newRow("fabric") << "fabric-loader-0.15.3-1.20.1" << "net.fabricmc.fabric-loader" << "0.15.3";
        QTest::newRow("quilt") << "quilt-loader-0.21.0-1.20.1" << "org.quiltmc.quilt-loader" << "0.21.0";
        QTest::newRow("vanilla") << "1.20.1" << "" << "";
        QTest::newRow("snapshot") << "24w33a" << "" << "";
    }

    void test_vanillaVersionIds()
    {
        QFETCH(QString, id);
        QFETCH(QString, uid);
        QFETCH(QString, version);
        QString gotUid, gotVersion;
        const bool hasLoader = parseVanillaVersionId(id, gotUid, gotVersion);
        QCOMPARE(hasLoader, !uid.isEmpty());
        QCOMPARE(gotUid, uid);
        QCOMPARE(gotVersion, version);
    }

    void test_loaderUids()
    {
        QCOMPARE(loaderUidFor("Forge"), QString("net.minecraftforge"));
        QCOMPARE(loaderUidFor("neoforge"), QString("net.neoforged"));
        QCOMPARE(loaderUidFor("fabric"), QString("net.fabricmc.fabric-loader"));
        QCOMPARE(loaderUidFor("quilt"), QString("org.quiltmc.quilt-loader"));
        QCOMPARE(loaderUidFor("vanilla"), QString());
        QCOMPARE(loaderUidFor(""), QString());
    }

    void test_curseforge()
    {
        QTemporaryDir dir;
        QVERIFY(writeFile(FS::PathCombine(dir.path(), "minecraftinstance.json"),
                          R"({"name":"All the Mods 9","gameVersion":"1.20.1","baseModLoader":{"name":"forge-47.2.20","minecraftVersion":"1.20.1"}})"));
        const auto inst = parseCurseForgeInstance(dir.path());
        QVERIFY(inst.isValid());
        QCOMPARE(inst.source, Source::CurseForge);
        QCOMPARE(inst.name, QString("All the Mods 9"));
        QCOMPARE(inst.mcVersion, QString("1.20.1"));
        QCOMPARE(inst.loaderUid, QString("net.minecraftforge"));
        QCOMPARE(inst.loaderVersion, QString("47.2.20"));
        QCOMPARE(inst.gameDir, dir.path());
        QVERIFY(inst.excludes.contains("minecraftinstance.json"));
    }

    void test_curseforgeVanilla()
    {
        QTemporaryDir dir;
        QVERIFY(writeFile(FS::PathCombine(dir.path(), "minecraftinstance.json"), R"({"name":"Plain","gameVersion":"1.21"})"));
        const auto inst = parseCurseForgeInstance(dir.path());
        QVERIFY(inst.isValid());
        QVERIFY(inst.loaderUid.isEmpty());
        QVERIFY(inst.loaderDescription().isEmpty());
    }

    void test_atlauncher()
    {
        QTemporaryDir dir;
        QVERIFY(writeFile(FS::PathCombine(dir.path(), "instance.json"),
                          R"({"id":"1.20.1","launcher":{"name":"Vault Hunters","loaderVersion":{"type":"Forge","version":"47.1.3","minecraft":"1.20.1"}}})"));
        const auto inst = parseATLauncherInstance(dir.path());
        QVERIFY(inst.isValid());
        QCOMPARE(inst.source, Source::ATLauncher);
        QCOMPARE(inst.name, QString("Vault Hunters"));
        QCOMPARE(inst.loaderUid, QString("net.minecraftforge"));
        QCOMPARE(inst.loaderVersion, QString("47.1.3"));
        // an ATLauncher file must not be mistaken for an XMCL or GDLauncher one
        QVERIFY(!parseXMCLInstance(dir.path()).isValid());
        QVERIFY(!parseGDLauncherInstance(dir.path()).isValid());
    }

    void test_xmcl()
    {
        QTemporaryDir dir;
        QVERIFY(writeFile(FS::PathCombine(dir.path(), "instance.json"),
                          R"({"name":"Create Pack","runtime":{"minecraft":"1.20.1","forge":"","fabricLoader":"0.15.11","quiltLoader":""}})"));
        const auto inst = parseXMCLInstance(dir.path());
        QVERIFY(inst.isValid());
        QCOMPARE(inst.source, Source::XMCL);
        QCOMPARE(inst.loaderUid, QString("net.fabricmc.fabric-loader"));
        QCOMPARE(inst.loaderVersion, QString("0.15.11"));
        QVERIFY(!parseATLauncherInstance(dir.path()).isValid());
    }

    void test_gdlauncher()
    {
        QTemporaryDir dir;
        QVERIFY(FS::ensureFolderPathExists(FS::PathCombine(dir.path(), "instance")));
        QVERIFY(writeFile(FS::PathCombine(dir.path(), "instance.json"),
                          R"({"name":"Carbon Pack","game_configuration":{"version":{"Standard":{"release":"1.20.4","modloaders":[{"type_":"neoforge","version":"20.4.80"}]}}}})"));
        const auto inst = parseGDLauncherInstance(dir.path());
        QVERIFY(inst.isValid());
        QCOMPARE(inst.source, Source::GDLauncher);
        QCOMPARE(inst.mcVersion, QString("1.20.4"));
        QCOMPARE(inst.loaderUid, QString("net.neoforged"));
        QCOMPARE(inst.loaderVersion, QString("20.4.80"));
        QCOMPARE(inst.gameDir, FS::PathCombine(dir.path(), "instance"));
    }

    void test_gdlauncherLegacy()
    {
        QTemporaryDir dir;
        QVERIFY(writeFile(FS::PathCombine(dir.path(), "config.json"),
                          R"({"loader":{"loaderType":"forge","mcVersion":"1.16.5","loaderVersion":"1.16.5-36.2.39"}})"));
        const auto inst = parseGDLauncherLegacyInstance(dir.path());
        QVERIFY(inst.isValid());
        QCOMPARE(inst.loaderUid, QString("net.minecraftforge"));
        // the game version prefix old GDLauncher kept on forge versions is dropped
        QCOMPARE(inst.loaderVersion, QString("36.2.39"));
    }

    void test_technic()
    {
        QTemporaryDir dir;
        QVERIFY(writeFile(FS::PathCombine(dir.path(), "bin/version.json"), R"({"id":"1.12.2-forge","inheritsFrom":"1.12.2"})"));
        QVERIFY(writeFile(FS::PathCombine(dir.path(), "mods/something.jar"), "jar"));
        const auto inst = parseTechnicInstance(dir.path());
        QVERIFY(inst.isValid());
        QCOMPARE(inst.source, Source::Technic);
        QCOMPARE(inst.gameDir, dir.path());
        // a plain mods folder without bin/ is not a Technic pack
        QTemporaryDir plain;
        QVERIFY(writeFile(FS::PathCombine(plain.path(), "mods/something.jar"), "jar"));
        QVERIFY(!parseTechnicInstance(plain.path()).isValid());
    }

    void test_multimc()
    {
        QTemporaryDir root;
        const QString inst = FS::PathCombine(root.path(), "instances", "MyPack");
        QVERIFY(writeFile(FS::PathCombine(inst, "instance.cfg"), "[General]\nInstanceType=OneSix\nname=My Pack\niconKey=creeper\n"));
        QVERIFY(writeFile(FS::PathCombine(inst, "mmc-pack.json"),
                          R"({"components":[{"uid":"net.minecraft","version":"1.20.1"},{"uid":"org.quiltmc.quilt-loader","version":"0.21.0"}],"formatVersion":1})"));
        const auto found = parseMultiMCInstance(inst, Source::PolyMC);
        QVERIFY(found.isValid());
        QVERIFY(found.isMultiMCFormat());
        QCOMPARE(found.source, Source::PolyMC);
        QCOMPARE(found.name, QString("My Pack"));
        QCOMPARE(found.mcVersion, QString("1.20.1"));
        QCOMPARE(found.loaderUid, QString("org.quiltmc.quilt-loader"));
        QCOMPARE(found.instanceDir, inst);
    }

    void test_vanillaRoot()
    {
        QTemporaryDir root;
        const QString dot = root.path();
        QVERIFY(writeFile(FS::PathCombine(dot, "launcher_profiles.json"),
                          R"({"profiles":{
                                "a":{"name":"Fabric 1.20","lastVersionId":"fabric-loader-0.15.3-1.20.1","type":"custom"},
                                "b":{"name":"","lastVersionId":"latest-release","type":"latest-release"},
                                "c":{"name":"Plain","lastVersionId":"1.19.4","type":"custom"}
                              }})"));
        QVERIFY(writeFile(FS::PathCombine(dot, "versions/fabric-loader-0.15.3-1.20.1/fabric-loader-0.15.3-1.20.1.json"),
                          R"({"id":"fabric-loader-0.15.3-1.20.1","inheritsFrom":"1.20.1"})"));
        // without the launcher's manifest there is no way to say what "latest" is
        QCOMPARE(parseVanillaRoot(dot).size(), 2);

        QVERIFY(writeFile(FS::PathCombine(dot, "versions/version_manifest_v2.json"), R"({"latest":{"release":"1.21.8","snapshot":"25w31a"}})"));
        const auto found = parseVanillaRoot(dot);
        QCOMPARE(found.size(), 3);

        const FoundInstance* fabric = nullptr;
        const FoundInstance* plain = nullptr;
        const FoundInstance* latest = nullptr;
        for (const auto& f : found) {
            if (f.name == "Fabric 1.20")
                fabric = &f;
            if (f.name == "Plain")
                plain = &f;
            if (f.name == "Minecraft (latest release)")
                latest = &f;
        }
        QVERIFY(fabric && plain && latest);
        QCOMPARE(latest->mcVersion, QString("1.21.8"));
        QVERIFY(latest->loaderUid.isEmpty());
        QCOMPARE(fabric->mcVersion, QString("1.20.1"));
        QCOMPARE(fabric->loaderUid, QString("net.fabricmc.fabric-loader"));
        QCOMPARE(fabric->loaderVersion, QString("0.15.3"));
        QCOMPARE(plain->mcVersion, QString("1.19.4"));
        QVERIFY(plain->loaderUid.isEmpty());
        // the vanilla launcher's own folders never get copied into an instance
        QVERIFY(plain->excludes.contains("versions"));
        QVERIFY(plain->excludes.contains("libraries"));
        QVERIFY(plain->excludes.contains("assets"));
    }

    void test_scanPathDetectsInstancesFolder()
    {
        QTemporaryDir root;
        QVERIFY(writeFile(FS::PathCombine(root.path(), "Instances/One/minecraftinstance.json"), R"({"name":"One","gameVersion":"1.20.1"})"));
        QVERIFY(writeFile(FS::PathCombine(root.path(), "Instances/Two/minecraftinstance.json"), R"({"name":"Two","gameVersion":"1.18.2"})"));
        QVERIFY(writeFile(FS::PathCombine(root.path(), "Instances/Junk/readme.txt"), "not an instance"));

        // pointing at the launcher root, the instances folder, or one instance all work
        QCOMPARE(scanPath(root.path()).size(), 2);
        QCOMPARE(scanPath(FS::PathCombine(root.path(), "Instances")).size(), 2);
        QCOMPARE(scanPath(FS::PathCombine(root.path(), "Instances", "One")).size(), 1);
        QVERIFY(scanPath(FS::PathCombine(root.path(), "Instances", "Junk")).isEmpty());
        QVERIFY(scanPath(FS::PathCombine(root.path(), "does-not-exist")).isEmpty());
    }
};

QTEST_GUILESS_MAIN(LauncherScannerTest)

#include "LauncherScanner_test.moc"
