// What the Miyoo builder writes and does without a Miyoo, a cross compiler or
// an SD card: the toolchain file and the CMake it generates from the shipped
// platform, the Onion app it packs, the cards it finds and how it installs on
// one (fake folders here), and that the framework id hands miyoo_mini to it
// rather than to the desktop builder.
//
// A mistake here shows up on the handheld as an app Onion does not list, a
// game built for the wrong CPU, or an install that deleted something else on
// the card, so it is worth pinning.

#include <gtest/gtest.h>

#include "editor/MiyooBuilder.h"
#include "editor/OnionApp.h"

#include <deki-editor/build/BuilderDispatch.h>
#include <deki-editor/build/PlatformConfig.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace DekiEditor;
namespace fs = std::filesystem;

namespace
{

const fs::path kPackage = DEKI_MIYOO_BOARDS_DIR;

std::string Read(const fs::path& p)
{
    std::ifstream f(p, std::ios::binary);
    std::ostringstream s;
    s << f.rdbuf();
    return s.str();
}

void Write(const fs::path& p, const std::string& text)
{
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << text;
}

bool Contains(const std::string& text, const std::string& part)
{
    return text.find(part) != std::string::npos;
}

size_t Count(const std::string& text, const std::string& part)
{
    size_t n = 0;
    for (size_t at = text.find(part); at != std::string::npos; at = text.find(part, at + 1))
    {
        ++n;
    }
    return n;
}

PlatformConfig MiyooMini()
{
    PlatformConfig config;
    EXPECT_TRUE(PlatformFromJson(Read(kPackage / "platforms" / "miyoo_mini" / "miyoo_mini.json"), config));
    return config;
}

// A folder of its own per test, removed afterwards.
class TempDir
{
public:
    TempDir()
    {
        m_Path = fs::temp_directory_path() /
                 ("deki-miyoo-" + std::string(::testing::UnitTest::GetInstance()->current_test_info()->name()));
        fs::remove_all(m_Path);
        fs::create_directories(m_Path);
    }
    ~TempDir()
    {
        std::error_code ec;
        fs::remove_all(m_Path, ec);
    }
    const fs::path& Path() const { return m_Path; }

private:
    fs::path m_Path;
};

// Only the framework matters to the dispatcher.
class StubBuilder : public ITargetBuilder
{
public:
    explicit StubBuilder(std::string framework)
        : m_Framework(std::move(framework))
    {
    }

    const char* GetName() const override { return "Stub"; }
    std::string GetFrameworkId() const override { return m_Framework; }
    void Build(const std::string&, BuildOutputCallback, BuildProgressCallback) override {}
    bool SupportsDeploy() const override { return false; }
    void Clean(const std::string&, BuildOutputCallback, BuildProgressCallback) override {}
    void Cancel() override {}
    bool IsBuilding() const override { return false; }
    BuildState GetState() const override { return BuildState::Idle; }
    BuildProgress GetProgress() const override { return {}; }
    void SetBuildOptions(const BuildOptions&) override {}
    const BuildOptions& GetBuildOptions() const override { return m_Options; }
    void SetPlatformConfig(const PlatformConfig&) override {}
    void ClearPlatformConfig() override {}
    void SetPackageDefines(const std::vector<std::string>&) override {}
    bool IsToolchainInstalled() const override { return true; }
    std::string GetToolchainStatus() const override { return "stub"; }
    std::string GetEnginePath(const std::string&) const override { return ""; }
    bool GenerateBuildFiles(const std::string&, const PlatformConfig&, const std::vector<std::string>&) override
    {
        return true;
    }
    std::string GetBuildDirectory(const std::string&) const override { return ""; }
    std::string GetBootPayloadDirectory(const std::string&) const override { return ""; }
    std::string GetPlatformKey() const override { return m_Framework; }

private:
    std::string m_Framework;
    BuildOptions m_Options;
};

}  // namespace

// --- The shipped platform and dispatch ---

TEST(MiyooBuilder, TheShippedPlatformIsTheMiyooBuildersAndValid)
{
    const PlatformConfig config = MiyooMini();
    EXPECT_EQ(config.id, "miyoo_mini");
    EXPECT_EQ(config.framework, "miyoo");
    MiyooBuilder builder;
    for (const auto& problem : builder.ValidatePlatform(config))
    {
        ADD_FAILURE() << problem;
    }
    // The platform's flags are the build's only optimisation (see the
    // generated CMake below), so it names exactly one -O.
    EXPECT_EQ(std::count_if(config.cxxFlags.begin(), config.cxxFlags.end(),
                            [](const std::string& f) { return f.rfind("-O", 0) == 0; }),
              1);
    // Logging is the build options' choice, not the platform's.
    EXPECT_EQ(std::find(config.defines.begin(), config.defines.end(), "DEKI_LOG_ENABLED"), config.defines.end());
}

TEST(MiyooBuilder, TheFrameworkIdPicksItOverTheDesktopBuilder)
{
    // miyoo_mini provides hosted, as the simulator does; only the
    // framework decides, so the desktop builder never builds it.
    MiyooBuilder miyoo;
    StubBuilder native("native");
    StubBuilder espidf("espidf");
    const std::vector<ITargetBuilder*> all{ &native, &espidf, &miyoo };
    EXPECT_EQ(SelectBuilder(all, MiyooMini().framework), &miyoo);
    EXPECT_EQ(SelectBuilder(all, "native"), &native);
}

TEST(MiyooBuilder, APrefixItDoesNotInstallOrAnOddCpuFlagIsRefused)
{
    MiyooBuilder builder;
    PlatformConfig other = MiyooMini();
    other.SetOption("toolchainPrefix", "aarch64-none-linux-gnu-");
    EXPECT_FALSE(builder.ValidatePlatform(other).empty());

    PlatformConfig none = MiyooMini();
    none.SetOption("toolchainPrefix", "");
    EXPECT_FALSE(builder.ValidatePlatform(none).empty());

    // cpuFlags land in the toolchain file between quotes.
    for (const char* bad : { "-march=armv7-a \"; message(x)", "-mfpu=${X}", "march=armv7-a", "-O2;-g" })
    {
        PlatformConfig c = MiyooMini();
        c.SetOption("cpuFlags", bad);
        EXPECT_FALSE(builder.ValidatePlatform(c).empty()) << bad;
    }
}

// --- The toolchain file and the configure line ---

TEST(MiyooBuilder, TheToolchainFileComesFromTheFrameworkOptions)
{
    const std::string text = MiyooBuilder::ToolchainFileText(MiyooMini(), "C:\\tc\\bin");
    EXPECT_TRUE(Contains(text, "set(CMAKE_SYSTEM_NAME Linux)\n"));
    EXPECT_TRUE(Contains(text, "set(CMAKE_SYSTEM_PROCESSOR arm)\n"));
    EXPECT_TRUE(Contains(text, "set(_DEKI_TOOLS \"C:/tc/bin/arm-none-linux-gnueabihf-\")\n"));
#ifdef _WIN32
    EXPECT_TRUE(Contains(text, "set(CMAKE_CXX_COMPILER \"${_DEKI_TOOLS}g++.exe\")\n"));
#else
    EXPECT_TRUE(Contains(text, "set(CMAKE_CXX_COMPILER \"${_DEKI_TOOLS}g++\")\n"));
#endif
    const std::string cpu = "-mcpu=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard";
    EXPECT_TRUE(Contains(text, "set(CMAKE_C_FLAGS_INIT \"" + cpu + "\")\n"));
    EXPECT_TRUE(Contains(text, "set(CMAKE_CXX_FLAGS_INIT \"" + cpu + "\")\n"));
    EXPECT_TRUE(Contains(text, "set(CMAKE_EXE_LINKER_FLAGS_INIT \"-static\")\n"));
    EXPECT_TRUE(Contains(text, "set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)\n"));
    // No optimisation here: that is the platform's cxxFlags, once.
    EXPECT_FALSE(Contains(text, "-O"));

    PlatformConfig dynamic = MiyooMini();
    dynamic.SetOptionBool("static", false);
    EXPECT_FALSE(Contains(MiyooBuilder::ToolchainFileText(dynamic, "/tc/bin"), "-static"));
}

TEST(MiyooBuilder, ItConfiguresWithNinjaForReleaseAndItsOwnToolchainFile)
{
    MiyooBuilder builder;
    const std::string line = builder.ConfigureCommand("C:\\p\\generated\\build\\miyoo_mini");
    EXPECT_TRUE(Contains(line, " -G Ninja "));
    EXPECT_TRUE(Contains(line, " -DCMAKE_BUILD_TYPE=Release"));
    EXPECT_TRUE(Contains(line, "\"-DCMAKE_TOOLCHAIN_FILE=C:/p/generated/build/miyoo_mini/toolchain.cmake\""));
    EXPECT_TRUE(Contains(line, "-DCMAKE_MAKE_PROGRAM="));
    EXPECT_TRUE(Contains(line, "-DDEKI_GXX16="));
    EXPECT_TRUE(Contains(line, "-DDEKI_ENGINE_PATH="));
}

TEST(MiyooBuilder, TheHostToolsGccVersionIsReadFromDumpVersion)
{
    EXPECT_EQ(MiyooHostTools::GccMajor("16.1.0"), 16);
    EXPECT_EQ(MiyooHostTools::GccMajor("15"), 15);
    EXPECT_EQ(MiyooHostTools::GccMajor("gcc"), -1);
    EXPECT_EQ(MiyooHostTools::GccMajor(""), -1);
}

// --- The generated CMake ---

namespace
{

class MiyooBuildFilesTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        const fs::path p = m_Dir.Path();
        Write(p / "deki.json", R"({"name":"My Game!","engine":"deki"})");
        Write(p / "deki-packages.json", R"({"packages":{}})");
        fs::create_directories(p / "src");
        fs::create_directories(p / "assets");
        AddPackage("deki-miyoo-boards", "DEKI_PACKAGE_MIYOO", R"(["miyoo"])", R"(["deki-input"])");
        AddPackage("deki-desktop-integration", "DEKI_PACKAGE_DESKTOP_HAL", R"(["hosted"])", "[]");
        AddPackage("deki-sdl3-integration", "DEKI_PACKAGE_SDL3", R"(["sdl3"])", "[]");
        AddPackage("deki-esp32-integration", "DEKI_PACKAGE_ESP32_HAL", R"(["esp32"])", "[]");
    }

    void AddPackage(const std::string& id, const std::string& define, const std::string& platformRequires,
                    const std::string& requires_)
    {
        const fs::path dir = m_Dir.Path() / "packages" / id;
        Write(dir / "package.json", "{\"id\":\"" + id + "\",\"buildDefine\":\"" + define + "\",\"platformRequires\":" +
                                        platformRequires + ",\"requires\":" + requires_ + "}");
        Write(dir / "package.cmake", "set(PACKAGE_PREFIX \"X\")\n");
    }

    std::string Generate(const PlatformConfig& config, const std::vector<std::string>& defines,
                         const BuildOptions& options = {})
    {
        builder.SetBuildOptions(options);
        EXPECT_TRUE(builder.GenerateBuildFiles(m_Dir.Path().string(), config, defines));
        return Read(MiyooBuilder::BuildDirectoryFor(m_Dir.Path().string(), config.id) / "CMakeLists.txt");
    }

    MiyooBuilder builder;
    TempDir m_Dir;
};

}  // namespace

TEST_F(MiyooBuildFilesTest, OnlyThePlatformsPackagesAreCompiled)
{
    // Asked for SDL3 and ESP32 too, as a project with those scenes would be:
    // the platform's capabilities leave them out, and its requiresPackages
    // bring the Miyoo package in even though no define named it.
    const std::string cmake = Generate(MiyooMini(), { "DEKI_PACKAGE_SDL3", "DEKI_PACKAGE_ESP32_HAL" });
    EXPECT_TRUE(Contains(cmake, "\"${DEKI_PROJECT_ROOT}/packages/deki-miyoo-boards/package.cmake\""));
    // The desktop package is installed but not built in: nothing the Miyoo
    // needs is in it, the game's entry being the engine's.
    EXPECT_FALSE(Contains(cmake, "deki-desktop-integration"));
    EXPECT_FALSE(Contains(cmake, "deki-sdl3-integration"));
    EXPECT_FALSE(Contains(cmake, "deki-esp32-integration"));
    EXPECT_FALSE(Contains(cmake, "packages/*/package.cmake"));
    EXPECT_FALSE(Contains(cmake, "SDL3::"));
    EXPECT_TRUE(Contains(cmake, "    DEKI_PACKAGE_MIYOO\n"));
    // The game's main() is the engine's, linked whatever packages it has.
    EXPECT_TRUE(Contains(cmake, "target_link_libraries(DekiGame PRIVATE deki-engine-hosted deki-engine-core"));
    EXPECT_TRUE(Contains(cmake, "    DEKI_HOSTED\n"));
    EXPECT_FALSE(Contains(cmake, "SIMULATOR"));
    EXPECT_TRUE(Contains(cmake, "\"DEKI_SCREEN_WIDTH=320\""));
    EXPECT_TRUE(Contains(cmake, "UNIT_PREFIX \"miyoo\""));
}

TEST_F(MiyooBuildFilesTest, ThePlatformsFlagsAreTheOnlyOptimisation)
{
    const std::string cmake = Generate(MiyooMini(), {});
    EXPECT_TRUE(Contains(cmake, "set(CMAKE_CXX_FLAGS_RELEASE \"-DNDEBUG\")\n"));
    EXPECT_TRUE(Contains(cmake, "set(CMAKE_C_FLAGS_RELEASE \"-DNDEBUG\")\n"));
    EXPECT_TRUE(Contains(cmake, "\"$<$<COMPILE_LANGUAGE:CXX>:-O3>\""));
    EXPECT_EQ(Count(cmake, "-O3"), 1u);
    EXPECT_FALSE(Contains(cmake, "-O2"));
    // Directory-wide, before the engine is added, so the engine gets them too.
    EXPECT_LT(cmake.find("add_compile_options("), cmake.find("add_subdirectory(\"${DEKI_ENGINE_PATH}\""));
    EXPECT_FALSE(Contains(cmake, "target_compile_options(DekiGame"));
    EXPECT_TRUE(Contains(cmake, "target_link_options(DekiGame PRIVATE -s)"));
}

// The platform's 3D tile size (deki-3d's DEKI_3D_TILE_SIZE: 64-pixel tiles
// beat 32 on a Miyoo Mini) reaches the code the game compiles.
TEST_F(MiyooBuildFilesTest, ThePlatformsTileSizeIsTheGamesDefine)
{
    const std::string cmake = Generate(MiyooMini(), {});
    const size_t defines = cmake.find("target_compile_definitions(DekiGame PRIVATE");
    ASSERT_NE(defines, std::string::npos);
    EXPECT_NE(cmake.find("    DEKI_3D_TILE_SIZE=64\n", defines), std::string::npos);
}

TEST_F(MiyooBuildFilesTest, LoggingFollowsTheBuildOptions)
{
    BuildOptions quiet;
    quiet.enableLogging = false;
    EXPECT_FALSE(Contains(Generate(MiyooMini(), {}, quiet), "DEKI_LOG_ENABLED"));

    BuildOptions verbose;
    verbose.enableLogging = true;
    verbose.enableInternalLogging = true;
    const std::string cmake = Generate(MiyooMini(), {}, verbose);
    EXPECT_TRUE(Contains(cmake, "target_compile_definitions(deki-engine-core PUBLIC DEKI_LOG_ENABLED)"));
    EXPECT_TRUE(Contains(cmake, "target_compile_definitions(deki-engine-core PUBLIC DEKI_LOG_INTERNAL_ENABLED)"));
}

TEST_F(MiyooBuildFilesTest, TheToolchainFileIsWrittenBesideIt)
{
    Generate(MiyooMini(), {});
    const fs::path dir = MiyooBuilder::BuildDirectoryFor(m_Dir.Path().string(), "miyoo_mini");
    EXPECT_EQ(dir.filename().string(), "miyoo_mini");
    EXPECT_TRUE(Contains(Read(dir / "toolchain.cmake"), "set(CMAKE_SYSTEM_NAME Linux)"));
}

TEST_F(MiyooBuildFilesTest, AChangedToolchainFileStartsTheConfigureAfresh)
{
    // CMake reads a toolchain file only on the first configure.
    Generate(MiyooMini(), {});
    const fs::path cache = MiyooBuilder::BuildDirectoryFor(m_Dir.Path().string(), "miyoo_mini") / "build";
    Write(cache / "CMakeCache.txt", "old");
    Generate(MiyooMini(), {});
    EXPECT_TRUE(fs::exists(cache / "CMakeCache.txt")) << "unchanged settings keep the cache";

    PlatformConfig changed = MiyooMini();
    changed.SetOption("cpuFlags", "-march=armv7-a");
    Generate(changed, {});
    EXPECT_FALSE(fs::exists(cache / "CMakeCache.txt"));
}

TEST_F(MiyooBuildFilesTest, AMissingRequiredPackageFailsTheBuild)
{
    fs::remove_all(m_Dir.Path() / "packages" / "deki-miyoo-boards");
    EXPECT_FALSE(builder.GenerateBuildFiles(m_Dir.Path().string(), MiyooMini(), {}));
}

TEST_F(MiyooBuildFilesTest, TheProjectNameComesFromDekiJson)
{
    EXPECT_EQ(MiyooBuilder::ProjectName(m_Dir.Path().string()), "My Game!");
}

// --- The Onion app ---

TEST(OnionApp, TheFolderNameIsSafeOnTheCard)
{
    EXPECT_EQ(OnionAppFolderName("gpu3d"), "gpu3d");
    EXPECT_EQ(OnionAppFolderName("My Game!"), "My_Game");
    EXPECT_EQ(OnionAppFolderName("../../.tmp_update"), "tmp_update");
    EXPECT_EQ(OnionAppFolderName("  "), "DekiGame");
    EXPECT_EQ(OnionAppFolderName(""), "DekiGame");
    EXPECT_EQ(OnionAppFolderName(std::string(80, 'a')).size(), 48u);
}

TEST(OnionApp, TheLaunchScriptRunsTheGameFromItsFolderWithLFEndings)
{
    const std::string script = OnionLaunchScript("My Game\r\nrm -rf /");
    EXPECT_EQ(script.find('\r'), std::string::npos);
    EXPECT_EQ(script.rfind("#!/bin/sh\n", 0), 0u);
    EXPECT_TRUE(Contains(script, "\ncd \"$(dirname \"$0\")\"\n"));
    EXPECT_TRUE(Contains(script, "\nchmod +x ./DekiGame 2>/dev/null\n"));
    EXPECT_TRUE(Contains(script, "\n./DekiGame > ./dekigame.out 2>&1\n"));
    // The label is only ever in a comment.
    EXPECT_FALSE(Contains(script, "\nrm -rf /"));
}

TEST(OnionApp, TheConfigNamesAnAbsoluteIcon)
{
    const auto config = nlohmann::json::parse(OnionConfigJson("My \"Game\"", "My_Game"));
    EXPECT_EQ(config["label"], "My \"Game\"");
    EXPECT_EQ(config["icon"], "/mnt/SDCARD/App/My_Game/icon.png");
    EXPECT_EQ(config["launch"], "launch.sh");
    EXPECT_TRUE(config.contains("description"));
}

TEST(OnionApp, TheShippedIconIsThere)
{
    EXPECT_TRUE(fs::is_regular_file(kPackage / "resources" / "onion" / "icon.png"));
}

namespace
{
// A built app as MiyooBuilder packs it, under <dir>/App/<folder>.
fs::path MakeApp(const fs::path& dir, const std::string& folder,
                 const std::string& game = "\x7f"
                                           "ELF game")
{
    Write(dir / "src" / "DekiGame", game);
    Write(dir / "src" / "flash" / "project_data.bin", "settings");
    Write(dir / "src" / "flash" / "assets" / "a.bin", "asset");
    Write(dir / "src" / "icon.png", "png");
    OnionAppSources sources;
    sources.executable = dir / "src" / "DekiGame";
    sources.payload = dir / "src" / "flash";
    sources.icon = dir / "src" / "icon.png";
    sources.label = folder;
    sources.folder = folder;
    std::string error;
    EXPECT_TRUE(AssembleOnionApp(sources, dir / "App", error)) << error;
    return dir / "App" / folder;
}

fs::path MakeCard(const fs::path& root)
{
    fs::create_directories(root / ".tmp_update");
    fs::create_directories(root / "App" / "Terminal");
    Write(root / "App" / "Terminal" / "config.json", "{}");
    return root;
}
}  // namespace

TEST(OnionApp, TheAppFolderHasEverythingOnionNeeds)
{
    TempDir dir;
    const fs::path app = MakeApp(dir.Path(), "My_Game");
    for (const char* f : { "DekiGame", "launch.sh", "config.json", "icon.png", "flash/project_data.bin",
                           "flash/assets/a.bin", ".deki-app" })
    {
        EXPECT_TRUE(fs::is_regular_file(app / f)) << f;
    }
    EXPECT_EQ(Read(app / "launch.sh").find('\r'), std::string::npos);
    EXPECT_EQ(nlohmann::json::parse(Read(app / "config.json"))["icon"], "/mnt/SDCARD/App/My_Game/icon.png");

    // Packing again leaves only the new app: App/ is the build's own.
    MakeApp(dir.Path(), "Renamed");
    EXPECT_FALSE(fs::exists(dir.Path() / "App" / "My_Game"));
}

TEST(OnionApp, PackingWithoutAPayloadSaysSo)
{
    TempDir dir;
    Write(dir.Path() / "DekiGame", "x");
    Write(dir.Path() / "icon.png", "x");
    OnionAppSources sources;
    sources.executable = dir.Path() / "DekiGame";
    sources.payload = dir.Path() / "flash";
    sources.icon = dir.Path() / "icon.png";
    sources.label = sources.folder = "G";
    std::string error;
    EXPECT_FALSE(AssembleOnionApp(sources, dir.Path() / "App", error));
    EXPECT_TRUE(Contains(error, "boot payload")) << error;
}

// --- Finding cards, installing ---

TEST(OnionCards, OnlyACardWithOnionsLayoutCounts)
{
    TempDir dir;
    const fs::path onion = MakeCard(dir.Path() / "onion");
    fs::create_directories(dir.Path() / "apps-only" / "App");
    fs::create_directories(dir.Path() / "update-only" / ".tmp_update");
    fs::create_directories(dir.Path() / "empty");
    Write(dir.Path() / "file" / ".tmp_update", "not a folder");
    fs::create_directories(dir.Path() / "file" / "App");

    std::vector<std::string> roots;
    for (const char* name : { "apps-only", "onion", "update-only", "empty", "file", "missing" })
    {
        roots.push_back((dir.Path() / name).string());
    }
    const std::vector<std::string> cards = OnionCardsAmong(roots);
    ASSERT_EQ(cards.size(), 1u);
    EXPECT_EQ(fs::path(cards[0]), onion);
}

TEST(OnionCards, AnInstallPutsTheAppInAppAndChecksIt)
{
    TempDir dir;
    const fs::path app = MakeApp(dir.Path(), "My_Game");
    const fs::path card = MakeCard(dir.Path() / "card");

    std::string error;
    std::vector<float> progress;
    ASSERT_TRUE(InstallOnionApp(
        app, card, "My_Game", nullptr, [&](float v, const std::string&) { progress.push_back(v); }, nullptr, error))
        << error;
    for (const char* f : { "DekiGame", "launch.sh", "config.json", "icon.png", "flash/assets/a.bin", ".deki-app" })
    {
        EXPECT_EQ(Read(card / "App" / "My_Game" / f), Read(app / f)) << f;
    }
    EXPECT_FALSE(progress.empty());
    EXPECT_FLOAT_EQ(progress.back(), 1.0f);
    // Nothing else on the card changed, and no staging folder is left.
    EXPECT_EQ(Read(card / "App" / "Terminal" / "config.json"), "{}");
    EXPECT_TRUE(fs::is_directory(card / ".tmp_update"));
    size_t entries = 0;
    for ([[maybe_unused]] const auto& e : fs::directory_iterator(card / "App"))
    {
        ++entries;
    }
    EXPECT_EQ(entries, 2u);
}

TEST(OnionCards, ReinstallingReplacesTheEditorsOwnApp)
{
    TempDir dir;
    const fs::path card = MakeCard(dir.Path() / "card");
    std::string error;
    ASSERT_TRUE(InstallOnionApp(MakeApp(dir.Path() / "one", "G"), card, "G", nullptr, nullptr, nullptr, error));
    Write(card / "App" / "G" / "flash" / "stale.bin", "left from before");

    ASSERT_TRUE(
        InstallOnionApp(MakeApp(dir.Path() / "two", "G", "new game"), card, "G", nullptr, nullptr, nullptr, error))
        << error;
    EXPECT_EQ(Read(card / "App" / "G" / "DekiGame"), "new game");
    EXPECT_FALSE(fs::exists(card / "App" / "G" / "flash" / "stale.bin"));
}

TEST(OnionCards, AnAppTheEditorDidNotInstallIsLeftAlone)
{
    TempDir dir;
    const fs::path app = MakeApp(dir.Path(), "Terminal");
    const fs::path card = MakeCard(dir.Path() / "card");
    std::string error;
    EXPECT_FALSE(InstallOnionApp(app, card, "Terminal", nullptr, nullptr, nullptr, error));
    EXPECT_TRUE(Contains(error, "not installed by the Deki Editor")) << error;
    EXPECT_EQ(Read(card / "App" / "Terminal" / "config.json"), "{}");
    EXPECT_FALSE(fs::exists(card / "App" / "Terminal" / "DekiGame"));
}

TEST(OnionCards, NothingIsWrittenToADriveWithoutOnion)
{
    TempDir dir;
    const fs::path app = MakeApp(dir.Path(), "G");
    const fs::path notACard = dir.Path() / "usb";
    fs::create_directories(notACard / "App");
    std::string error;
    EXPECT_FALSE(InstallOnionApp(app, notACard, "G", nullptr, nullptr, nullptr, error));
    EXPECT_FALSE(fs::exists(notACard / "App" / "G"));
}

TEST(OnionCards, ACancelledInstallLeavesTheOldAppAndNoStaging)
{
    TempDir dir;
    const fs::path card = MakeCard(dir.Path() / "card");
    std::string error;
    ASSERT_TRUE(InstallOnionApp(MakeApp(dir.Path() / "one", "G", "old"), card, "G", nullptr, nullptr, nullptr, error));
    std::atomic<bool> cancel{ true };
    EXPECT_FALSE(InstallOnionApp(MakeApp(dir.Path() / "two", "G", "new"), card, "G", nullptr, nullptr, &cancel, error));
    EXPECT_EQ(Read(card / "App" / "G" / "DekiGame"), "old");
    EXPECT_FALSE(fs::exists(card / "App" / ".G.deki-install"));
}

// An install names its card as someone wrote it: the editor's tool, a script,
// the Build panel. The same drive with or without its separator is the card.
TEST(OnionCards, ACardIsTheSameWithOrWithoutATrailingSeparator)
{
#ifdef _WIN32
    EXPECT_TRUE(NamesOnionCard("E:", "E:"));
    EXPECT_TRUE(NamesOnionCard(R"(E:\)", "E:"));
    EXPECT_TRUE(NamesOnionCard("e:/", "E:"));
    EXPECT_FALSE(NamesOnionCard("F:", "E:"));
    EXPECT_FALSE(NamesOnionCard(R"(E:\App)", "E:"));
#else
    EXPECT_TRUE(NamesOnionCard("/media/user/ONION/", "/media/user/ONION"));
    EXPECT_FALSE(NamesOnionCard("/media/user/onion", "/media/user/ONION"));
    EXPECT_FALSE(NamesOnionCard("/media/user/ONION/App", "/media/user/ONION"));
#endif
    EXPECT_FALSE(NamesOnionCard("", "E:"));
}
