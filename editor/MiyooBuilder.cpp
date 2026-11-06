#include "MiyooBuilder.h"

#include "OnionApp.h"

#include <deki-editor/EditorPaths.h>
#include <deki-editor/EditorSettings.h>
#include <deki-editor/Paths.h>
#include <deki-editor/ShellCommand.h>
#include <deki-editor/build/BuilderDefinition.h>
#include <deki-editor/build/CMakeGenUtils.h>
#include <deki-editor/build/ExecutableCMakeGen.h>
#include <deki-editor/build/PlatformConfig.h>
#include <deki-editor/build/ProjectPaths.h>
#include <deki/LogSystem.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace DekiEditor
{

namespace
{
// What the Miyoo's toolchain tools are called on this host.
#ifdef _WIN32
constexpr const char* kMiyooHostExe = ".exe";
#else
constexpr const char* kMiyooHostExe = "";
#endif

constexpr const char* kMiyooToolchainFile = "toolchain.cmake";

std::string MiyooCMakePath(std::string path)
{
    std::replace(path.begin(), path.end(), '\\', '/');
    return path;
}

// A frameworkOptions value that is written into the toolchain file: letters,
// digits and the punctuation flags and tool names use, nothing CMake or a
// shell reads as syntax.
bool MiyooIsPlainToken(const std::string& s)
{
    if (s.empty())
    {
        return false;
    }
    for (char c : s)
    {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' ||
                        c == '_' || c == '.' || c == '=' || c == '+' || c == ',';
        if (!ok)
        {
            return false;
        }
    }
    return true;
}

std::vector<std::string> MiyooSplitFlags(const std::string& flags)
{
    std::vector<std::string> out;
    std::istringstream in(flags);
    std::string flag;
    while (in >> flag)
    {
        out.push_back(flag);
    }
    return out;
}

// True when `path` is a 32-bit little-endian ELF for ARM (e_machine 40): what
// the Miyoo runs. Anything else means the cross compiler was not used.
bool MiyooIsArmElf(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    unsigned char h[20] = {};
    if (!in.read(reinterpret_cast<char*>(h), sizeof(h)))
    {
        return false;
    }
    return h[0] == 0x7f && h[1] == 'E' && h[2] == 'L' && h[3] == 'F' && h[4] == 1 /* 32-bit */ &&
           h[5] == 1 /* little-endian */ && h[18] == 40 && h[19] == 0;
}
}  // namespace

MiyooBuilder::MiyooBuilder()
{
    BuilderDefinition def;
    std::string error;
    if (ParseBuilderDefinition(MiyooToolchainDefinitionJson(), def, error))
    {
        m_ToolchainMgr.Initialize(def);
    }
    else
    {
        DEKI_LOG_ERROR("MiyooBuilder: its toolchain definition does not parse: %s", error.c_str());
    }
}

MiyooBuilder::~MiyooBuilder()
{
    Cancel();
    if (m_BuildThread.joinable())
    {
        m_BuildThread.join();
    }
}

// ============================================================================
// Paths
// ============================================================================

fs::path MiyooBuilder::BuildDirectoryFor(const std::string& projectPath, const std::string& platformId)
{
    // Per platform, as every target's build/<id>: two Miyoo platforms in one
    // project must not share a build tree.
    return ProjectPaths::Build(projectPath) / (platformId.empty() ? std::string("miyoo") : platformId);
}

std::string MiyooBuilder::GetBuildDirectory(const std::string& projectPath) const
{
    return BuildDirectoryFor(projectPath, m_PlatformConfig.id).string();
}

std::string MiyooBuilder::CompilerBinDir()
{
    return MiyooCMakePath(EditorPaths::GetToolchainsDir()) + "/" + kMiyooCompilerInstallPath + "/bin";
}

std::string MiyooBuilder::ProjectName(const std::string& projectPath)
{
    std::ifstream in(fs::path(projectPath) / "deki.json");
    const nlohmann::json project = nlohmann::json::parse(in, nullptr, /*allow_exceptions*/ false);
    if (project.is_object() && project.contains("name") && project["name"].is_string() &&
        !project["name"].get<std::string>().empty())
    {
        return project["name"].get<std::string>();
    }
    return fs::path(projectPath).lexically_normal().filename().string();
}

// ============================================================================
// Toolchain
// ============================================================================

bool MiyooBuilder::CompilerInstalled() const
{
    for (const auto& c : m_ToolchainMgr.GetComponents())
    {
        if (c.id == kMiyooCompilerComponent)
        {
            return c.status == ToolchainComponentStatus::Installed ||
                   c.status == ToolchainComponentStatus::UpdateAvailable;
        }
    }
    return false;
}

bool MiyooBuilder::IsToolchainInstalled() const
{
    if (!CompilerInstalled())
    {
        return false;
    }
    for (const auto& tool : m_HostTools.Get())
    {
        if (!tool.ok)
        {
            return false;
        }
    }
    return true;
}

std::string MiyooBuilder::GetToolchainStatus() const
{
    std::vector<std::string> problems;
    if (!MiyooHostHasPinnedCompiler())
    {
        problems.push_back("No Arm GNU Toolchain archive is pinned for this host yet, so the editor cannot "
                           "install one: Miyoo builds need a Windows x86_64 machine for now.");
    }
    else if (!CompilerInstalled())
    {
        problems.push_back("The Arm GNU Toolchain (arm-none-linux-gnueabihf, about 270 MB) is not installed: "
                           "install it from the Build panel's toolchain list or with install_toolchain_component.");
    }
    for (const auto& tool : m_HostTools.Get())
    {
        if (!tool.ok)
        {
            problems.push_back(tool.problem);
        }
    }
    if (problems.empty())
    {
        std::string status = std::string("Arm GNU Toolchain ") + kMiyooCompilerVersion;
        for (const auto& tool : m_HostTools.Get())
        {
            status += ", " + tool.displayName + (tool.version.empty() ? "" : " " + tool.version);
        }
        return status;
    }
    std::string status;
    for (const auto& p : problems)
    {
        status += (status.empty() ? "" : " ") + p;
    }
    return status;
}

std::vector<ToolchainComponent> MiyooBuilder::GetToolchainComponents() const
{
    std::vector<ToolchainComponent> components = m_ToolchainMgr.GetComponents();
    // The host tools the editor cannot install: shown so a missing one is
    // seen, with what to do as the tooltip.
    for (const auto& tool : m_HostTools.Get())
    {
        ToolchainComponent c;
        c.id = tool.id;
        c.displayName = tool.displayName;
        c.status = tool.ok ? ToolchainComponentStatus::Installed : ToolchainComponentStatus::NotInstalled;
        c.installedVersion = tool.version;
        c.canInstall = false;
        c.tooltip = tool.ok ? tool.path : tool.problem;
        components.push_back(c);
    }
    return components;
}

void MiyooBuilder::CheckForToolchainUpdates(std::function<void()> onComplete)
{
    // The host tools may have been installed since the editor looked.
    m_HostTools.Rescan();
    FirmwareBuilderBase::CheckForToolchainUpdates(std::move(onComplete));
}

// ============================================================================
// Platform
// ============================================================================

std::vector<std::string> MiyooBuilder::ValidatePlatform(const PlatformConfig& config) const
{
    std::vector<std::string> problems;

    const std::string prefix = config.Option("toolchainPrefix");
    if (prefix.empty())
    {
        problems.push_back(std::string("frameworkOptions.toolchainPrefix is not set; the toolchain this builder "
                                       "installs is ") +
                           kMiyooCompilerPrefix);
    }
    else if (prefix != kMiyooCompilerPrefix)
    {
        problems.push_back("frameworkOptions.toolchainPrefix '" + prefix +
                           "' names a toolchain this builder does not install; the one it installs is " +
                           kMiyooCompilerPrefix);
    }

    const std::string cpuFlags = config.Option("cpuFlags");
    for (const auto& flag : MiyooSplitFlags(cpuFlags))
    {
        if (flag[0] != '-' || !MiyooIsPlainToken(flag))
        {
            problems.push_back("frameworkOptions.cpuFlags has '" + flag +
                               "', which is not a compiler flag (a '-' and letters, digits and = . , + _ -)");
        }
    }

    if (config.screenWidth <= 0 || config.screenHeight <= 0)
    {
        problems.push_back("screenWidth and screenHeight must be set: the game renders at that size");
    }
    return problems;
}

std::vector<std::pair<std::string, std::string>> MiyooBuilder::DescribePlatform(const PlatformConfig& config) const
{
    std::vector<std::pair<std::string, std::string>> rows;
    rows.push_back({ "Toolchain", config.Option("toolchainPrefix") + " (Arm GNU " + kMiyooCompilerVersion + ")" });
    rows.push_back({ "CPU", config.Option("cpuFlags") });
    rows.push_back({ "Linking", config.OptionBool("static") ? "static" : "dynamic" });
    rows.push_back({ "Output", "Onion OS app" });
    return rows;
}

// ============================================================================
// Build files
// ============================================================================

std::string MiyooBuilder::ToolchainFileText(const PlatformConfig& config, const std::string& compilerBin)
{
    const std::string prefix = config.Option("toolchainPrefix");
    // arm-none-linux-gnueabihf- runs on "arm"; the first part of a GNU triple
    // is the processor.
    const std::string processor = prefix.substr(0, prefix.find('-'));
    const std::string tools = MiyooCMakePath(compilerBin) + "/" + prefix;
    const std::string cpu = config.Option("cpuFlags");

    std::ostringstream f;
    f << "# Generated by Deki Editor — do not edit\n";
    f << "# Cross-compiles for " << config.displayName << " with the Arm GNU Toolchain " << kMiyooCompilerVersion
      << ",\n";
    f << "# from the platform's frameworkOptions (toolchainPrefix, cpuFlags, static).\n";
    f << "set(CMAKE_SYSTEM_NAME Linux)\n";
    f << "set(CMAKE_SYSTEM_PROCESSOR " << processor << ")\n";
    f << "set(_DEKI_TOOLS \"" << tools << "\")\n";
    f << "set(CMAKE_C_COMPILER \"${_DEKI_TOOLS}gcc" << kMiyooHostExe << "\")\n";
    f << "set(CMAKE_CXX_COMPILER \"${_DEKI_TOOLS}g++" << kMiyooHostExe << "\")\n";
    f << "set(CMAKE_AR \"${_DEKI_TOOLS}ar" << kMiyooHostExe << "\" CACHE FILEPATH \"\")\n";
    f << "set(CMAKE_RANLIB \"${_DEKI_TOOLS}ranlib" << kMiyooHostExe << "\" CACHE FILEPATH \"\")\n";
    f << "set(CMAKE_STRIP \"${_DEKI_TOOLS}strip" << kMiyooHostExe << "\" CACHE FILEPATH \"\")\n";
    f << "set(CMAKE_C_FLAGS_INIT \"" << cpu << "\")\n";
    f << "set(CMAKE_CXX_FLAGS_INIT \"" << cpu << "\")\n";
    if (config.OptionBool("static"))
    {
        // The game carries its own C and C++ libraries, so it does not depend
        // on the ones the handheld's firmware has.
        f << "set(CMAKE_EXE_LINKER_FLAGS_INIT \"-static\")\n";
    }
    // Programs (the reflection codegen's compiler) come from this machine;
    // libraries and headers only from the cross toolchain.
    f << "set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)\n";
    f << "set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)\n";
    f << "set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)\n";
    return f.str();
}

std::string MiyooBuilder::ConfigureCommand(const std::string& buildDir) const
{
    const MiyooHostTool cmake = m_HostTools.Find("cmake");
    const MiyooHostTool ninja = m_HostTools.Find("ninja");
    const MiyooHostTool gcc = m_HostTools.Find("host-gcc");
    // Release, with the platform's cxxFlags as the optimisation (the generated
    // CMakeLists says so). The reflection codegen's compiler is named rather
    // than searched for, so it is the one the toolchain status checked.
    return "\"" + cmake.path + "\" -G Ninja -S . -B build -DCMAKE_BUILD_TYPE=Release" +
           " \"-DCMAKE_TOOLCHAIN_FILE=" + MiyooCMakePath(buildDir) + "/" + kMiyooToolchainFile + "\"" +
           " \"-DCMAKE_MAKE_PROGRAM=" + ninja.path + "\"" + " \"-DDEKI_GXX16=" + gcc.path + "\"" +
           EditorSettings::GetCMakeLocationArgs();
}

bool MiyooBuilder::GenerateBuildFiles(const std::string& projectPath, const PlatformConfig& config,
                                      const std::vector<std::string>& packageDefines)
{
    const fs::path buildDir = BuildDirectoryFor(projectPath, config.id);
    std::error_code ec;
    fs::create_directories(buildDir, ec);
    if (ec)
    {
        DEKI_LOG_ERROR("MiyooBuilder: cannot create %s: %s", buildDir.string().c_str(), ec.message().c_str());
        return false;
    }

    const std::vector<std::string> problems = ValidatePlatform(config);
    for (const auto& problem : problems)
    {
        DEKI_LOG_ERROR("MiyooBuilder: platform '%s': %s", config.id.c_str(), problem.c_str());
    }
    if (!problems.empty())
    {
        return false;
    }

    // The toolchain file. CMake reads it on the first configure only, so a
    // changed one (other CPU flags, a moved toolchain) needs a fresh cache:
    // otherwise the build silently keeps the old compiler settings.
    const fs::path toolchainFile = buildDir / kMiyooToolchainFile;
    const std::string toolchain = ToolchainFileText(config, CompilerBinDir());
    {
        std::ifstream in(toolchainFile, std::ios::binary);
        const std::string before((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (in && before != toolchain)
        {
            DEKI_LOG_EDITOR("MiyooBuilder: the toolchain settings changed, so %s configures from scratch",
                            config.id.c_str());
            fs::remove(buildDir / "build" / "CMakeCache.txt", ec);
            fs::remove_all(buildDir / "build" / "CMakeFiles", ec);
        }
    }
    if (!CMakeGen::WriteIfChanged(toolchainFile, toolchain))
    {
        DEKI_LOG_ERROR("MiyooBuilder: cannot write %s", toolchainFile.string().c_str());
        return false;
    }

    CMakeGen::ExecutableCMakeOptions options;
    options.builderName = "MiyooBuilder";
    options.kindLabel = "Miyoo, cross-compiled for ARM Linux";
    options.codegenUnitPrefix = "miyoo";
    options.allInstalledPackages = false;
    options.engineLogsAlways = false;
    options.enableLogging = m_BuildOptions.enableLogging;
    options.enableInternalLogging = m_BuildOptions.enableInternalLogging;
    options.platformFlagsOwnOptimisation = true;

    std::string error;
    if (!CMakeGen::GenerateExecutableCMakeLists(projectPath, buildDir.string(), config, packageDefines, options, error))
    {
        DEKI_LOG_ERROR("MiyooBuilder: cannot generate the build for platform '%s': %s", config.id.c_str(),
                       error.c_str());
        return false;
    }
    return true;
}

// ============================================================================
// Build, clean
// ============================================================================

void MiyooBuilder::Build(const std::string& projectPath, BuildOutputCallback outputCallback,
                         BuildProgressCallback progressCallback)
{
    RunOnBuildThread([this, projectPath, outputCallback, progressCallback]()
                     { DoBuild(projectPath, outputCallback, progressCallback); });
}

void MiyooBuilder::Clean(const std::string& projectPath, BuildOutputCallback outputCallback,
                         BuildProgressCallback progressCallback)
{
    RunOnBuildThread([this, projectPath, outputCallback, progressCallback]()
                     { DoClean(projectPath, outputCallback, progressCallback); });
}

void MiyooBuilder::Report(BuildState state, const std::string& text, float value, const BuildProgressCallback& progress)
{
    SetProgress(state, text, value);
    if (progress)
    {
        progress(GetProgress());
    }
}

void MiyooBuilder::Fail(const std::string& error, const BuildOutputCallback& out, const BuildProgressCallback& progress)
{
    if (out)
    {
        out(error, true);
    }
    SetError(error);
    if (progress)
    {
        progress(GetProgress());
    }
}

int MiyooBuilder::Run(const std::string& command, const std::string& workDir, const BuildOutputCallback& out)
{
    return RunShellCommand(
        command, workDir,
        [&out](const std::string& line)
        {
            if (out)
            {
                const bool isError = line.find("error") != std::string::npos ||
                                     line.find("Error") != std::string::npos ||
                                     line.find("FAILED") != std::string::npos;
                out(line, isError);
            }
        },
        &m_CancelRequested);
}

void MiyooBuilder::DoBuild(const std::string& projectPath, BuildOutputCallback out, BuildProgressCallback progress)
{
    Report(BuildState::Building, "Checking the toolchain...", 0.0f, progress);
    m_HostTools.Rescan();
    if (!IsToolchainInstalled())
    {
        Fail("The Miyoo toolchain is not ready: " + GetToolchainStatus(), out, progress);
        return;
    }

    const std::string buildDir = GetBuildDirectory(projectPath);
    if (out)
    {
        out("Building " + m_PlatformConfig.displayName + " in " + buildDir, false);
    }

    Report(BuildState::Building, "Generating build files...", 0.05f, progress);
    if (!GenerateBuildFiles(projectPath, m_PlatformConfig, m_PackageDefines))
    {
        Fail("Could not generate the build files; the Console says why.", out, progress);
        return;
    }

    Report(BuildState::Building, "Configuring...", 0.1f, progress);
    int code = Run(ConfigureCommand(buildDir), buildDir, out);
    if (m_CancelRequested)
    {
        Fail("Build cancelled", out, progress);
        return;
    }
    if (code != 0)
    {
        Fail("CMake configure failed (exit code " + std::to_string(code) + "); the lines above say why.", out,
             progress);
        return;
    }

    Report(BuildState::Building, "Compiling...", 0.3f, progress);
    code = Run("\"" + m_HostTools.Find("cmake").path + "\" --build build", buildDir, out);
    if (m_CancelRequested)
    {
        Fail("Build cancelled", out, progress);
        return;
    }
    if (code != 0)
    {
        Fail("The build failed (exit code " + std::to_string(code) + "); the lines above say why.", out, progress);
        return;
    }

    const fs::path executable = fs::path(buildDir) / "build" / "bin" / "Release" / "DekiGame";
    if (!MiyooIsArmElf(executable))
    {
        Fail(executable.string() + " is not an ARM Linux executable, so the cross compiler was not used. Clean the "
                                   "build and build again.",
             out, progress);
        return;
    }

    // The Onion app, from the build and the boot payload the editor exported.
    Report(BuildState::Building, "Packing the Onion app...", 0.95f, progress);
    std::string iconDir;
    for (const auto& pkg : CMakeGen::ScanPackageManifests(projectPath))
    {
        if (pkg.id == "deki-miyoo-boards")
        {
            iconDir = (fs::path(GetPackagesDirectory(projectPath)) / pkg.dirName).string();
        }
    }
    OnionAppSources sources;
    sources.executable = executable;
    sources.payload = GetBootPayloadDirectory(projectPath);
    sources.icon = fs::path(iconDir) / "resources" / "onion" / "icon.png";
    sources.label = ProjectName(projectPath);
    sources.folder = OnionAppFolderName(sources.label);
    std::string error;
    if (iconDir.empty() || !AssembleOnionApp(sources, fs::path(buildDir) / "App", error))
    {
        Fail("Could not pack the Onion app: " + (iconDir.empty() ? "deki-miyoo-boards is not installed" : error), out,
             progress);
        return;
    }
    if (out)
    {
        out("Onion app: " + (fs::path(buildDir) / "App" / sources.folder).generic_string(), false);
        out("Install puts it on the SD card; or copy App/" + sources.folder + " into App/ on the card yourself.",
            false);
    }
    Report(BuildState::Completed, "Build succeeded", 1.0f, progress);
}

void MiyooBuilder::DoClean(const std::string& projectPath, BuildOutputCallback out, BuildProgressCallback progress)
{
    Report(BuildState::Building, "Cleaning...", 0.0f, progress);
    const fs::path buildDir = GetBuildDirectory(projectPath);
    std::error_code ec;
    for (const char* sub : { "build", "App" })
    {
        const fs::path dir = buildDir / sub;
        if (!fs::exists(dir, ec))
        {
            continue;
        }
        fs::remove_all(dir, ec);
        if (ec)
        {
            Fail("Clean failed: cannot remove " + dir.string() + ": " + ec.message(), out, progress);
            return;
        }
        if (out)
        {
            out("Removed: " + dir.string(), false);
        }
    }
    Report(BuildState::Completed, "Clean succeeded", 1.0f, progress);
}

// ============================================================================
// Deploy: install on the SD card
// ============================================================================

std::vector<DeployTarget> MiyooBuilder::EnumerateDeployTargets() const
{
    return FindOnionCards();
}

void MiyooBuilder::Deploy(const std::string& projectPath, const std::string& deployTargetId,
                          BuildOutputCallback outputCallback, BuildProgressCallback progressCallback)
{
    // Only a card that is listed: an id from somewhere else could name any
    // folder, and an install deletes the app it replaces.
    const std::vector<DeployTarget> cards = FindOnionCards();
    std::string cardId = deployTargetId;
    std::string problem;
    if (cardId.empty())
    {
        if (cards.size() == 1)
        {
            cardId = cards.front().id;
        }
        else if (cards.empty())
        {
            problem = "No Onion SD card found. Put the Miyoo's SD card in a card reader (or connect the Miyoo over "
                      "USB as a drive) and try again; a card with Onion has .tmp_update/ and App/ at its top.";
        }
        else
        {
            problem = "More than one Onion SD card is connected; choose one:";
            for (const auto& c : cards)
            {
                problem += " " + c.id;
            }
        }
    }
    else if (std::none_of(cards.begin(), cards.end(), [&](const DeployTarget& c) { return c.id == cardId; }))
    {
        problem = "'" + cardId + "' is not a connected Onion SD card (a removable drive with .tmp_update/ and App/).";
    }

    if (!problem.empty())
    {
        // Reported through the same path as a deploy that started, so the
        // Build panel and build_status show it.
        RunOnBuildThread([this, problem, outputCallback, progressCallback]()
                         { Fail(problem, outputCallback, progressCallback); });
        return;
    }
    RunOnBuildThread([this, projectPath, cardId, outputCallback, progressCallback]()
                     { DoDeploy(projectPath, cardId, outputCallback, progressCallback); });
}

void MiyooBuilder::DoDeploy(const std::string& projectPath, const std::string& cardId, BuildOutputCallback out,
                            BuildProgressCallback progress)
{
    const std::string folder = OnionAppFolderName(ProjectName(projectPath));
    const fs::path app = fs::path(GetBuildDirectory(projectPath)) / "App" / folder;
    const fs::path card = OnionCardRoot(cardId);
    Report(BuildState::Deploying, "Installing on " + cardId + "...", 0.0f, progress);
    if (out)
    {
        out("Installing " + app.generic_string() + " as App/" + folder + " on " + cardId, false);
    }

    std::string error;
    const bool ok = InstallOnionApp(
        app, card, folder, out, [&](float value, const std::string& text)
        { Report(BuildState::Deploying, text, value, progress); }, &m_CancelRequested, error);
    if (!ok)
    {
        Fail("Install failed: " + error, out, progress);
        return;
    }
    if (out)
    {
        out("Installed App/" + folder + " on " + cardId +
                ". Eject the card safely before taking it out; the game "
                "is in Onion's Apps.",
            false);
    }
    Report(BuildState::Completed, "Installed on " + cardId, 1.0f, progress);
}

}  // namespace DekiEditor

// ---------------------------------------------------------------------------
// Builder plugin entry points
//
// In editor/, so the editor-side package DLL exports them and a game build,
// which leaves editor/ out, does not.
// ---------------------------------------------------------------------------

#include <deki-editor/build/BuilderPlugin.h>

extern "C"
{
    DEKI_BUILDER_API const DekiBuilderAbi* DekiBuilderGetAbi(void)
    {
        static const DekiBuilderAbi kAbi = DekiBuilderThisAbi((uint32_t)sizeof(DekiEditor::PlatformConfig),
                                                              (uint32_t)sizeof(DekiEditor::CMakeGen::PackageEntry));
        return &kAbi;
    }

    DEKI_BUILDER_API const char* DekiBuilderGetName(void)
    {
        return "Miyoo Builder";
    }

    DEKI_BUILDER_API const char* DekiBuilderGetVersion(void)
    {
        return "1.0.0";
    }

    DEKI_BUILDER_API int DekiBuilderGetBuilderCount(void)
    {
        return 1;
    }

    DEKI_BUILDER_API DekiEditor::ITargetBuilder* DekiBuilderCreateBuilder(int index)
    {
        return index == 0 ? new DekiEditor::MiyooBuilder() : nullptr;
    }

    DEKI_BUILDER_API void DekiBuilderDestroyBuilder(DekiEditor::ITargetBuilder* builder)
    {
        delete builder;  // in THIS module: its vtable and operator delete live here
    }

}  // extern "C"
