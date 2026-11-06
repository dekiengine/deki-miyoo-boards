#include "OnionApp.h"

#include <deki-editor/EditorHttpUtils.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <fstream>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace DekiEditor
{

namespace
{
bool OnionWriteText(const fs::path& path, const std::string& text)
{
    // Binary, so a "\n" stays one byte on Windows.
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << text;
    return static_cast<bool>(out);
}

// Every regular file under `root`, relative to it.
std::vector<fs::path> OnionFilesUnder(const fs::path& root, std::error_code& ec)
{
    std::vector<fs::path> files;
    for (auto it = fs::recursive_directory_iterator(root, ec); !ec && it != fs::recursive_directory_iterator();
         it.increment(ec))
    {
        std::error_code fileError;
        if (it->is_regular_file(fileError))
        {
            files.push_back(fs::relative(it->path(), root, fileError));
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}
}  // namespace

std::string OnionAppFolderName(const std::string& projectName)
{
    std::string out;
    for (unsigned char c : projectName)
    {
        const bool keep =
            (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
        out += keep ? static_cast<char>(c) : '_';
    }
    // Trimmed of the '_' that stood for spaces and the like at either end.
    const size_t first = out.find_first_not_of('_');
    if (first == std::string::npos)
    {
        return "DekiGame";
    }
    out = out.substr(first, out.find_last_not_of('_') - first + 1);
    if (out.size() > 48)
    {
        out.resize(48);
    }
    return out;
}

std::string OnionLaunchScript(const std::string& label)
{
    // The label goes in a comment only; a newline in it would end the comment.
    std::string safeLabel = label;
    std::replace(safeLabel.begin(), safeLabel.end(), '\n', ' ');
    std::replace(safeLabel.begin(), safeLabel.end(), '\r', ' ');
    return "#!/bin/sh\n"
           "# Starts " +
           safeLabel +
           ", made with Deki. It runs from its own folder, where it finds\n"
           "# flash/ (its data) and writes its files; what it prints goes to dekigame.out.\n"
           "cd \"$(dirname \"$0\")\"\n"
           "chmod +x ./DekiGame 2>/dev/null\n"
           "./DekiGame > ./dekigame.out 2>&1\n";
}

std::string OnionConfigJson(const std::string& label, const std::string& folder)
{
    nlohmann::ordered_json config;
    config["label"] = label;
    // Onion resolves a relative icon path against its own folder, not the
    // app's, so it has to be the absolute path on the device.
    config["icon"] = "/mnt/SDCARD/App/" + folder + "/icon.png";
    config["launch"] = "launch.sh";
    config["description"] = "Made with Deki";
    return config.dump(2) + "\n";
}

bool AssembleOnionApp(const OnionAppSources& sources, const fs::path& appsRoot, std::string& error)
{
    std::error_code ec;
    if (!fs::is_regular_file(sources.executable, ec))
    {
        error = "the game was not built: " + sources.executable.string() + " does not exist";
        return false;
    }
    if (!fs::is_regular_file(sources.payload / "project_data.bin", ec))
    {
        error = "the boot payload is missing (" + sources.payload.string() +
                " has no project_data.bin); build from the editor, which exports it";
        return false;
    }
    if (!fs::is_regular_file(sources.icon, ec))
    {
        error = "the app icon " + sources.icon.string() + " is missing; reinstall the deki-miyoo-boards package";
        return false;
    }

    // The build's own App/ folder: it holds the one app this build makes, so a
    // folder left from before a project rename does not linger beside it.
    fs::remove_all(appsRoot, ec);
    const fs::path app = appsRoot / sources.folder;
    fs::create_directories(app, ec);
    if (ec)
    {
        error = "cannot create " + app.string() + ": " + ec.message();
        return false;
    }

    fs::copy_file(sources.executable, app / "DekiGame", fs::copy_options::overwrite_existing, ec);
    if (!ec)
    {
        fs::copy(sources.payload, app / "flash", fs::copy_options::recursive, ec);
    }
    if (!ec)
    {
        fs::copy_file(sources.icon, app / "icon.png", fs::copy_options::overwrite_existing, ec);
    }
    if (ec)
    {
        error = "cannot copy the app's files into " + app.string() + ": " + ec.message();
        return false;
    }

    if (!OnionWriteText(app / "launch.sh", OnionLaunchScript(sources.label)) ||
        !OnionWriteText(app / "config.json", OnionConfigJson(sources.label, sources.folder)) ||
        !OnionWriteText(app / kOnionAppMarker,
                        "Installed by the Deki Editor, which replaces this folder when it installs " + sources.label +
                            " again.\n"))
    {
        error = "cannot write the app's files into " + app.string();
        return false;
    }
    return true;
}

bool IsOnionCard(const fs::path& root)
{
    std::error_code ec;
    return fs::is_directory(root / ".tmp_update", ec) && fs::is_directory(root / "App", ec);
}

std::vector<std::string> OnionCardsAmong(const std::vector<std::string>& roots)
{
    std::vector<std::string> cards;
    for (const auto& root : roots)
    {
        if (IsOnionCard(root))
        {
            cards.push_back(root);
        }
    }
    return cards;
}

fs::path OnionCardRoot(const std::string& deployTargetId)
{
#ifdef _WIN32
    // "E:" on its own means the current directory on E:, not its root.
    if (deployTargetId.size() == 2 && deployTargetId[1] == ':')
    {
        return fs::path(deployTargetId + "\\");
    }
#endif
    return fs::path(deployTargetId);
}

namespace
{
// A card's directory written one way: forward slashes, no trailing one, and
// on Windows, where drive letters and paths ignore case, lower case.
std::string CardKey(const std::string& id)
{
    std::string key = OnionCardRoot(id).lexically_normal().generic_string();
    while (key.size() > 1 && key.back() == '/')
    {
        key.pop_back();
    }
#ifdef _WIN32
    std::transform(key.begin(), key.end(), key.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
#endif
    return key;
}
}  // namespace

bool NamesOnionCard(const std::string& given, const std::string& cardId)
{
    return !given.empty() && CardKey(given) == CardKey(cardId);
}

std::vector<DeployTarget> FindOnionCards()
{
    std::vector<DeployTarget> targets;
#ifdef _WIN32
    // Removable drives only: an SD card in a reader, or the Miyoo itself over
    // USB. A fixed disk that happens to have the same folders is not a card.
    const DWORD mask = GetLogicalDrives();
    for (int i = 0; i < 26; ++i)
    {
        if ((mask & (1u << i)) == 0)
        {
            continue;
        }
        const std::string id = std::string(1, static_cast<char>('A' + i)) + ":";
        const std::string root = id + "\\";
        if (GetDriveTypeA(root.c_str()) != DRIVE_REMOVABLE || !IsOnionCard(root))
        {
            continue;
        }
        char volume[MAX_PATH + 1] = {};
        std::string label = "Onion SD card (" + id;
        if (GetVolumeInformationA(root.c_str(), volume, sizeof(volume), nullptr, nullptr, nullptr, nullptr, 0) &&
            volume[0] != '\0')
        {
            label += " " + std::string(volume);
        }
        targets.push_back({ id, label + ")" });
    }
#else
    // Where desktops mount removable media: /media/<user>/<volume>,
    // /run/media/<user>/<volume>, /Volumes/<volume>.
    std::vector<std::string> roots;
    std::error_code ec;
    for (const char* parent : { "/Volumes", "/media", "/run/media" })
    {
        for (const auto& entry : fs::directory_iterator(parent, ec))
        {
            roots.push_back(entry.path().string());
            std::error_code inner;
            for (const auto& nested : fs::directory_iterator(entry.path(), inner))
            {
                roots.push_back(nested.path().string());
            }
        }
    }
    for (const auto& root : OnionCardsAmong(roots))
    {
        targets.push_back({ root, "Onion SD card (" + root + ")" });
    }
#endif
    return targets;
}

bool InstallOnionApp(const fs::path& app, const fs::path& cardRoot, const std::string& folder,
                     const BuildOutputCallback& log, const std::function<void(float, const std::string&)>& onProgress,
                     const std::atomic<bool>* cancel, std::string& error)
{
    auto say = [&](const std::string& line)
    {
        if (log)
        {
            log(line, false);
        }
    };
    auto progress = [&](float value, const std::string& text)
    {
        if (onProgress)
        {
            onProgress(value, text);
        }
    };
    auto cancelled = [&]() { return cancel != nullptr && cancel->load(); };

    std::error_code ec;
    if (!fs::is_regular_file(app / "config.json", ec) || !fs::is_regular_file(app / "DekiGame", ec))
    {
        error = "there is no built app at " + app.string() + "; build first";
        return false;
    }
    if (!IsOnionCard(cardRoot))
    {
        error = cardRoot.string() + " is not an SD card with Onion OS on it (no .tmp_update/ and App/ at its top)";
        return false;
    }
    if (folder.empty() || folder != OnionAppFolderName(folder))
    {
        error = "'" + folder + "' is not an app folder name";
        return false;
    }

    const fs::path dest = cardRoot / "App" / folder;
    // A hidden name of this app's own, beside it: only this install writes it.
    const fs::path staging = cardRoot / "App" / ("." + folder + ".deki-install");
    if (fs::exists(dest, ec) && !fs::exists(dest / kOnionAppMarker, ec))
    {
        error = "App/" + folder +
                " on the card was not installed by the Deki Editor, so it is left alone. "
                "Rename the project, or move that folder off the card yourself";
        return false;
    }

    const std::vector<fs::path> files = OnionFilesUnder(app, ec);
    if (ec || files.empty())
    {
        error = "cannot read the built app at " + app.string() + (ec ? ": " + ec.message() : std::string());
        return false;
    }

    // A staging folder left by an install that was cut short.
    fs::remove_all(staging, ec);
    fs::create_directories(staging, ec);
    if (ec)
    {
        error = "cannot write to the card (" + staging.string() + "): " + ec.message();
        return false;
    }

    auto fail = [&](const std::string& why)
    {
        error = why;
        std::error_code cleanup;
        fs::remove_all(staging, cleanup);
        return false;
    };

    // Copy, then check, file by file: the check reads back what landed on the
    // card, so a full or failing card is caught here rather than on the device.
    const float steps = static_cast<float>(files.size()) * 2.0f;
    float done = 0.0f;
    for (const auto& rel : files)
    {
        if (cancelled())
        {
            return fail("cancelled");
        }
        progress(done / steps, "Copying " + rel.generic_string());
        fs::create_directories((staging / rel).parent_path(), ec);
        fs::copy_file(app / rel, staging / rel, fs::copy_options::overwrite_existing, ec);
        if (ec)
        {
            return fail("cannot copy " + rel.generic_string() + " to the card: " + ec.message());
        }
        done += 1.0f;
    }
    say("Copied " + std::to_string(files.size()) + " files; checking them");
    for (const auto& rel : files)
    {
        if (cancelled())
        {
            return fail("cancelled");
        }
        progress(done / steps, "Checking " + rel.generic_string());
        std::error_code sizeError;
        const auto want = fs::file_size(app / rel, sizeError);
        const auto have = fs::file_size(staging / rel, sizeError);
        if (sizeError || want != have)
        {
            return fail(rel.generic_string() + " did not copy whole (" + std::to_string(have) + " of " +
                        std::to_string(want) + " bytes); is the card full?");
        }
        if (EditorHttpUtils::Sha256File((app / rel).string()) != EditorHttpUtils::Sha256File((staging / rel).string()))
        {
            return fail(rel.generic_string() + " reads back different from what was written; the card may be failing");
        }
        done += 1.0f;
    }

    // Swap the checked copy in for the old app.
    if (fs::exists(dest, ec))
    {
        fs::remove_all(dest, ec);
        if (ec)
        {
            return fail("cannot remove the old App/" + folder + " from the card: " + ec.message());
        }
    }
    fs::rename(staging, dest, ec);
    if (ec)
    {
        // The old app is gone by now, so the checked copy stays where it is.
        error = "cannot move the new app into App/" + folder + ": " + ec.message() + ". The checked copy is App/" +
                staging.filename().string() + " on the card; rename it to " + folder + " by hand";
        return false;
    }
    progress(1.0f, "Installed");
    return true;
}

}  // namespace DekiEditor
