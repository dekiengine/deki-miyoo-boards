#pragma once

// A Deki game as an Onion OS app, and getting it onto the Miyoo's SD card.
//
// Onion lists every folder under /mnt/SDCARD/App/ that holds a config.json,
// shows its label and icon in the Apps menu, and runs its launch script. A
// built game becomes such a folder:
//
//   App/<Folder>/
//     DekiGame      the game, a static ARM Linux executable
//     launch.sh     cd to the folder, run the game, its output to dekigame.out
//     config.json   label, icon (an absolute path: Onion needs one), launch
//     icon.png      the Apps menu icon
//     flash/        the boot payload and assets (F:/ on the device)
//     .deki-app     says the editor made this folder, so it may replace it
//
// The folder is named after the project. Installing replaces that one folder
// on the card, and only when the editor made it: an app of the same name
// installed some other way is left alone, and the install says so.

#include <deki-editor/build/TargetBuilder.h>

#include <atomic>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace DekiEditor
{

/// The file that marks an app folder as the editor's.
inline constexpr const char* kOnionAppMarker = ".deki-app";

/// A project name as an app folder name: letters, digits, '-' and '_' kept,
/// anything else made '_', at most 48 characters. "DekiGame" when nothing is
/// left.
std::string OnionAppFolderName(const std::string& projectName);

/// launch.sh, with LF line endings (the Miyoo's shell reads a CR as part of
/// the command).
std::string OnionLaunchScript(const std::string& label);

/// config.json. The icon is the absolute path the card has on the device.
std::string OnionConfigJson(const std::string& label, const std::string& folder);

/// What an app folder is made from.
struct OnionAppSources
{
    std::filesystem::path executable;  // the built DekiGame
    std::filesystem::path payload;     // the boot payload directory, which becomes flash/
    std::filesystem::path icon;        // icon.png
    std::string label;                 // the name the Apps menu shows
    std::string folder;                // OnionAppFolderName of the project
};

/// Makes <appsRoot>/<folder>/ from `sources`. appsRoot is the build's own
/// App/ directory, emptied first so it holds this one app. False, with
/// `error` set, when something is missing or cannot be written.
bool AssembleOnionApp(const OnionAppSources& sources, const std::filesystem::path& appsRoot, std::string& error);

/// Whether `root` is an SD card set up with Onion: it has .tmp_update/ (where
/// Onion keeps itself) and App/ at the top.
bool IsOnionCard(const std::filesystem::path& root);

/// The roots among `roots` that are Onion cards, in the order given.
std::vector<std::string> OnionCardsAmong(const std::vector<std::string>& roots);

/// Removable drives holding an Onion card, as deploy targets: the id is the
/// drive ("E:" on Windows, a mount point elsewhere), the label says what it is.
std::vector<DeployTarget> FindOnionCards();

/// The directory an Onion card's deploy target id names ("E:" is "E:\").
std::filesystem::path OnionCardRoot(const std::string& deployTargetId);

/// Whether `given`, a deploy target as someone wrote it, names the card whose
/// id is `cardId`: the same drive or mount point, with or without a trailing
/// separator ("E:", "E:\" and "e:/" are one drive on Windows).
bool NamesOnionCard(const std::string& given, const std::string& cardId);

/// Copies the app folder `app` to <cardRoot>/App/<folder>, replacing what is
/// there when the editor made it. Copies into a temporary folder beside it
/// first and checks every file against the original (size and SHA-256), so a
/// card pulled out halfway keeps the old app instead of half of the new one.
/// `onProgress` gets 0..1. False, with `error` set, on any problem.
bool InstallOnionApp(const std::filesystem::path& app, const std::filesystem::path& cardRoot, const std::string& folder,
                     const BuildOutputCallback& log, const std::function<void(float, const std::string&)>& onProgress,
                     const std::atomic<bool>* cancel, std::string& error);

}  // namespace DekiEditor
