#pragma once

// Builds a Deki game for a Miyoo handheld running Onion OS, and installs it on
// the handheld's SD card.
//
// In this package's editor/ folder, so it is compiled into the package's
// editor-side DLL and registered when a project has the package installed,
// and never into a game, which leaves editor/ out. It claims the framework id
// "miyoo"; platforms/miyoo_mini/miyoo_mini.json names it.
//
// The game is the desktop simulator's kind of build (one executable, from
// CMakeGen::GenerateExecutableCMakeLists) cross-compiled for ARM Linux:
//   - the toolchain file comes from the platform's frameworkOptions
//     (toolchainPrefix, cpuFlags, static) and names the Arm GNU Toolchain the
//     editor installs (MiyooToolchain.h)
//   - only the packages active for the platform are compiled, so nothing
//     written for SDL3, ESP-IDF or Bluetooth reaches the ARM compiler
//   - the platform's cxxFlags are the optimisation, for the engine and the game
// then packed as an Onion app (OnionApp.h) in <build>/App/<Folder>/.
//
// Deploy ("Install") copies that folder to App/ on an Onion SD card in a
// removable drive: the card in a reader, or the Miyoo over USB.

#include <deki-editor/IconsTabler.h>
#include <deki-editor/Localization.h>
#include <deki-editor/build/FirmwareBuilderBase.h>

#include "MiyooToolchain.h"

#include <filesystem>
#include <string>
#include <vector>

namespace DekiEditor
{

class MiyooBuilder : public FirmwareBuilderBase
{
public:
    MiyooBuilder();
    ~MiyooBuilder() override;

    // Core operations
    void Build(const std::string& projectPath, BuildOutputCallback outputCallback = nullptr,
               BuildProgressCallback progressCallback = nullptr) override;
    void Clean(const std::string& projectPath, BuildOutputCallback outputCallback = nullptr,
               BuildProgressCallback progressCallback = nullptr) override;

    // Deploy: install the app on an Onion SD card
    bool SupportsDeploy() const override { return true; }
    const char* GetDeployLabel() const override { return "Install"; }
    std::vector<DeployTarget> EnumerateDeployTargets() const override;
    void Deploy(const std::string& projectPath, const std::string& deployTargetId,
                BuildOutputCallback outputCallback = nullptr,
                BuildProgressCallback progressCallback = nullptr) override;

    // Toolchain: the Arm GNU Toolchain (installable) and the host tools (found)
    bool IsToolchainInstalled() const override;
    std::string GetToolchainStatus() const override;
    std::vector<ToolchainComponent> GetToolchainComponents() const override;
    void CheckForToolchainUpdates(std::function<void()> onComplete = nullptr) override;

    // Build file generation
    bool GenerateBuildFiles(const std::string& projectPath, const PlatformConfig& config,
                            const std::vector<std::string>& packageDefines) override;

    // Identity
    const char* GetName() const override { return "Miyoo (Onion OS)"; }
    const char* GetIcon() const override { return ICON_TI_DEVICE_GAMEPAD; }
    const char* GetDescription() const override
    {
        return TrMark("Build an Onion OS app for a Miyoo handheld and install it on its SD card");
    }
    std::string GetFrameworkId() const override { return "miyoo"; }
    std::vector<std::pair<std::string, std::string>> DescribePlatform(const PlatformConfig& config) const override;
    std::vector<std::string> ValidatePlatform(const PlatformConfig& config) const override;

    std::string GetBuildDirectory(const std::string& projectPath) const override;
    // The boot payload is the app's flash/ (F:/ on the device).
    std::string GetBootPayloadDirectory(const std::string& projectPath) const override
    {
        return GetBuildDirectory(projectPath) + "/flash";
    }

    // ---- the parts a test reaches -------------------------------------------

    /// The CMake toolchain file for `config`, with the cross compiler at
    /// `compilerBin` (its bin/ folder).
    static std::string ToolchainFileText(const PlatformConfig& config, const std::string& compilerBin);

    /// The CMake configure line run in the build directory.
    std::string ConfigureCommand(const std::string& buildDir) const;

    /// The build directory of platform `platformId`: generated/build/<id>.
    static std::filesystem::path BuildDirectoryFor(const std::string& projectPath, const std::string& platformId);

    /// The project's name, from deki.json; its folder's name when it has none.
    static std::string ProjectName(const std::string& projectPath);

    /// Where the installed cross compiler's bin/ folder is.
    static std::string CompilerBinDir();

private:
    MiyooHostTools m_HostTools;

    void DoBuild(const std::string& projectPath, BuildOutputCallback out, BuildProgressCallback progress);
    void DoClean(const std::string& projectPath, BuildOutputCallback out, BuildProgressCallback progress);
    void DoDeploy(const std::string& projectPath, const std::string& cardId, BuildOutputCallback out,
                  BuildProgressCallback progress);

    bool CompilerInstalled() const;
    // Runs `command` in `workDir`, its output to `out`. The exit code.
    int Run(const std::string& command, const std::string& workDir, const BuildOutputCallback& out);
    void Fail(const std::string& error, const BuildOutputCallback& out, const BuildProgressCallback& progress);
    void Report(BuildState state, const std::string& text, float value, const BuildProgressCallback& progress);
};

}  // namespace DekiEditor
