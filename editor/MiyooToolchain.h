#pragma once

// What a Miyoo build runs on the machine the editor is on.
//
// The cross compiler is Arm's own GNU toolchain for hard-float ARM Linux,
// arm-none-linux-gnueabihf, which the editor downloads (pinned archive and
// SHA-256) into ~/.deki-editor/toolchains like any backend's SDK. The rest
// it cannot install, so it finds them and says what is missing:
//   - CMake and Ninja, which configure and run the build
//   - a host GCC 16 or newer, which compiles the reflection codegen
//     (-freflection) that runs before the game compiles
//
// Arm publishes the toolchain for several hosts, but only the Windows x86_64
// archive is pinned here so far. On any other host the compiler shows as not
// installable, and the status says so.

#include <deki-editor/build/BuilderDefinition.h>
#include <deki-editor/build/TargetBuilder.h>

#include <mutex>
#include <string>
#include <vector>

namespace DekiEditor
{

/// The pinned cross compiler: the component id, its version, and the prefix
/// its tools carry.
inline constexpr const char* kMiyooCompilerComponent = "arm-gnu-toolchain";
inline constexpr const char* kMiyooCompilerVersion = "15.2.rel1";
inline constexpr const char* kMiyooCompilerPrefix = "arm-none-linux-gnueabihf-";
inline constexpr const char* kMiyooCompilerInstallPath = "arm-gnu-toolchain/15.2.rel1-arm-none-linux-gnueabihf";

/// The builder definition the editor's ToolchainComponentManager installs from:
/// the Arm GNU Toolchain for this host, or a component that cannot be
/// installed and explains why on a host without a pinned archive.
std::string MiyooToolchainDefinitionJson();

/// Whether this host has a pinned Arm GNU Toolchain archive.
bool MiyooHostHasPinnedCompiler();

/// One program the build needs from the machine.
struct MiyooHostTool
{
    std::string id;           // "cmake", "ninja", "host-gcc"
    std::string displayName;  // "CMake"
    std::string path;         // where it was found; empty when it was not
    std::string version;      // what it reports, where that matters
    bool ok = false;          // found, and new enough
    std::string problem;      // when not ok: what is wrong and what to do
};

/// Finds CMake, Ninja and a host GCC 16+, once, until Rescan(). Looking runs
/// `g++ -dumpversion`, and the Build panel asks for the components often.
class MiyooHostTools
{
public:
    std::vector<MiyooHostTool> Get() const;
    void Rescan();

    /// The tool with this id, or an empty one (ok false) when it is unknown.
    MiyooHostTool Find(const std::string& id) const;

    /// The GCC major version `g++ -dumpversion` printed ("16.1.0" -> 16), or
    /// -1 when it is not a version.
    static int GccMajor(const std::string& dumpVersion);

private:
    mutable std::mutex m_Mutex;
    mutable bool m_Scanned = false;
    mutable std::vector<MiyooHostTool> m_Tools;

    static std::vector<MiyooHostTool> Scan();
};

/// The first `name` on PATH (with .exe on Windows), or empty.
std::string MiyooFindOnPath(const std::string& name);

}  // namespace DekiEditor
