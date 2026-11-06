#include "MiyooToolchain.h"

#include <deki-editor/Localization.h>
#include <deki-editor/ShellCommand.h>

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <filesystem>
#include <sstream>

namespace fs = std::filesystem;

namespace DekiEditor
{

bool MiyooHostHasPinnedCompiler()
{
#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
    return true;
#else
    return false;
#endif
}

std::string MiyooToolchainDefinitionJson()
{
    nlohmann::json compiler = {
        { "id", kMiyooCompilerComponent },
        { "displayName", "Arm GNU Toolchain (arm-none-linux-gnueabihf)" },
        { "required", true },
        { "installPath", kMiyooCompilerInstallPath },
        { "versionCheck", { { "type", "static" }, { "version", kMiyooCompilerVersion } } },
    };

    if (MiyooHostHasPinnedCompiler())
    {
        // Arm's archive for a Windows x86_64 host. It has no folder of its own
        // inside (bin/, lib/ and the rest are at the top), so it unpacks
        // straight into installPath. The digest is Arm's published .sha256asc.
        compiler["canInstall"] = true;
        compiler["fallback"] = {
            { "version", kMiyooCompilerVersion },
            { "url", "https://developer.arm.com/-/media/Files/downloads/gnu/15.2.rel1/binrel/"
                     "arm-gnu-toolchain-15.2.rel1-mingw-w64-x86_64-arm-none-linux-gnueabihf.zip" },
            { "sha256", "acba98ff50971e77e86b941386e72c9a642b0d5a81f6a6df1c69f2bf32917fbd" },
        };
        compiler["detection"] = { { "windows", "{installPath}/bin/arm-none-linux-gnueabihf-g++.exe" } };
        compiler["tooltip"] = "The cross compiler for the Miyoo Mini (ARMv7, hard float, Linux), from Arm";
    }
    else
    {
        // Nothing to download and nothing to detect: no archive is pinned
        // for this host, and taking whatever compiler is on PATH would build
        // with a toolchain nobody checked.
        compiler["canInstall"] = false;
        compiler["detection"] = nlohmann::json::object();
        compiler["tooltip"] = "No Arm GNU Toolchain archive is pinned for this host yet; Miyoo builds need a "
                              "Windows x86_64 machine for now";
    }

    nlohmann::json def = {
        { "id", "miyoo" },
        { "name", "Miyoo (Onion OS)" },
        { "family", "linux-arm" },
        { "components", nlohmann::json::array({ compiler }) },
    };
    return def.dump();
}

// The tooltips above, marked so the translation table has them. The Platforms
// panel shows them through Tr().
[[maybe_unused]] static constexpr const char* kMiyooToolchainTooltips[] = {
    TrMark("The cross compiler for the Miyoo Mini (ARMv7, hard float, Linux), from Arm"),
    TrMark("No Arm GNU Toolchain archive is pinned for this host yet; Miyoo builds need a Windows x86_64 machine "
           "for now"),
};

// ============================================================================
// Host tools
// ============================================================================

namespace
{
#ifdef _WIN32
constexpr const char* kMiyooExe = ".exe";
#else
constexpr const char* kMiyooExe = "";
#endif
}  // namespace

std::string MiyooFindOnPath(const std::string& name)
{
    const char* path = std::getenv("PATH");
    if (path == nullptr)
    {
        return "";
    }
#ifdef _WIN32
    const char separator = ';';
#else
    const char separator = ':';
#endif
    const std::string file = name + kMiyooExe;
    std::stringstream dirs(path);
    std::string dir;
    while (std::getline(dirs, dir, separator))
    {
        if (dir.empty())
        {
            continue;
        }
        std::error_code ec;
        const fs::path candidate = fs::path(dir) / file;
        if (fs::is_regular_file(candidate, ec))
        {
            return candidate.generic_string();
        }
    }
    return "";
}

namespace
{
// The first line a program prints for `args`, or empty when it cannot run.
std::string MiyooFirstLine(const std::string& program, const std::string& args)
{
    std::string first;
    const int code = RunShellCommand("\"" + program + "\" " + args, "",
                                     [&first](const std::string& line)
                                     {
                                         if (first.empty())
                                         {
                                             first = line;
                                         }
                                     });
    return code == 0 ? first : std::string();
}

// Where a tool may be when it is not on PATH: an editor started from the
// Start menu does not see a shell's PATH.
std::vector<std::string> MiyooKnownLocations(const std::string& name)
{
    std::vector<std::string> out;
#ifdef _WIN32
    out.push_back("C:/msys64/mingw64/bin/" + name + kMiyooExe);
    if (name == "cmake")
    {
        out.push_back("C:/Program Files/CMake/bin/cmake.exe");
    }
#else
    out.push_back("/usr/local/bin/" + name);
    out.push_back("/usr/bin/" + name);
#endif
    return out;
}

std::string MiyooLocate(const std::string& name)
{
    std::string found = MiyooFindOnPath(name);
    if (!found.empty())
    {
        return found;
    }
    for (const auto& candidate : MiyooKnownLocations(name))
    {
        std::error_code ec;
        if (fs::is_regular_file(candidate, ec))
        {
            return candidate;
        }
    }
    return "";
}
}  // namespace

int MiyooHostTools::GccMajor(const std::string& dumpVersion)
{
    size_t i = 0;
    int major = 0;
    while (i < dumpVersion.size() && dumpVersion[i] >= '0' && dumpVersion[i] <= '9')
    {
        major = major * 10 + (dumpVersion[i] - '0');
        ++i;
    }
    return i == 0 ? -1 : major;
}

std::vector<MiyooHostTool> MiyooHostTools::Scan()
{
    std::vector<MiyooHostTool> tools;

    MiyooHostTool cmake;
    cmake.id = "cmake";
    cmake.displayName = "CMake";
    cmake.path = MiyooLocate("cmake");
    cmake.ok = !cmake.path.empty();
    if (cmake.ok)
    {
        // "cmake version 3.31.2"
        const std::string line = MiyooFirstLine(cmake.path, "--version");
        cmake.version = line.rfind("cmake version ", 0) == 0 ? line.substr(14) : line;
    }
    else
    {
        cmake.problem = "CMake was not found. Install CMake 3.16 or newer (cmake.org, or in MSYS2: pacman -S "
                        "mingw-w64-x86_64-cmake), put it on PATH and restart the editor.";
    }
    tools.push_back(cmake);

    MiyooHostTool ninja;
    ninja.id = "ninja";
    ninja.displayName = "Ninja";
    ninja.path = MiyooLocate("ninja");
    if (ninja.path.empty() && !cmake.path.empty())
    {
        // Often installed beside CMake.
        std::error_code ec;
        const fs::path beside = fs::path(cmake.path).parent_path() / (std::string("ninja") + kMiyooExe);
        if (fs::is_regular_file(beside, ec))
        {
            ninja.path = beside.generic_string();
        }
    }
    ninja.ok = !ninja.path.empty();
    if (ninja.ok)
    {
        ninja.version = MiyooFirstLine(ninja.path, "--version");
    }
    else
    {
        ninja.problem = "Ninja was not found. Install it (ninja-build.org, or in MSYS2: pacman -S "
                        "mingw-w64-x86_64-ninja), put it on PATH and restart the editor.";
    }
    tools.push_back(ninja);

    // The reflection codegen is compiled with -freflection, which GCC has
    // from 16. The generated CMake looks in the same places, in this order,
    // when it is not told which compiler to use; the builder always tells it.
    MiyooHostTool gcc;
    gcc.id = "host-gcc";
    gcc.displayName = "Host GCC 16+ (reflection codegen)";
    std::vector<std::string> candidates;
#ifdef _WIN32
    candidates.push_back("C:/msys64/mingw64/bin/g++.exe");
#endif
    for (const char* name : { "g++-16", "g++" })
    {
        const std::string found = MiyooFindOnPath(name);
        if (!found.empty())
        {
            candidates.push_back(found);
        }
    }
    std::string tooOld;
    for (const auto& candidate : candidates)
    {
        std::error_code ec;
        if (!fs::is_regular_file(candidate, ec))
        {
            continue;
        }
        const std::string version = MiyooFirstLine(candidate, "-dumpfullversion");
        const int major = GccMajor(version);
        if (major >= 16)
        {
            gcc.path = candidate;
            gcc.version = version;
            gcc.ok = true;
            break;
        }
        if (tooOld.empty() && major > 0)
        {
            tooOld = candidate + " is GCC " + version;
        }
    }
    if (!gcc.ok)
    {
        gcc.problem = std::string("The reflection step needs GCC 16 or newer on this machine") +
                      (tooOld.empty() ? " and none was found" : " (" + tooOld + ")") +
#ifdef _WIN32
                      ". Install MSYS2's GCC (pacman -S mingw-w64-x86_64-gcc) into C:/msys64, or put a GCC 16 "
                      "g++ on PATH, and restart the editor.";
#else
                      ". Install g++-16 and restart the editor.";
#endif
    }
    tools.push_back(gcc);

    return tools;
}

std::vector<MiyooHostTool> MiyooHostTools::Get() const
{
    std::lock_guard<std::mutex> lock(m_Mutex);
    if (!m_Scanned)
    {
        m_Tools = Scan();
        m_Scanned = true;
    }
    return m_Tools;
}

void MiyooHostTools::Rescan()
{
    std::vector<MiyooHostTool> tools = Scan();
    std::lock_guard<std::mutex> lock(m_Mutex);
    m_Tools = std::move(tools);
    m_Scanned = true;
}

MiyooHostTool MiyooHostTools::Find(const std::string& id) const
{
    for (const auto& tool : Get())
    {
        if (tool.id == id)
        {
            return tool;
        }
    }
    return {};
}

}  // namespace DekiEditor
