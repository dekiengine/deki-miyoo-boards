# Changelog

Notable changes to `deki-miyoo-boards`. Engine and editor changes are in the
[engine changelog](https://github.com/dekiengine/deki-engine/blob/master/CHANGELOG.md).

## Unreleased

### Renamed projects, and the Miyoo Mini's memory
- An app's `.deki-app` names its project (a hash of the project's folder, not
  the folder itself). After a project is renamed, an install says which app on
  the card is the same project under its earlier name and leaves it there,
  since it may hold the game's saves (`EarlierAppsOfProject`).
- `miyoo_mini` states 80 MB of memory (`externalMemorySize`), so deki-3d
  checks a model against it as on a board: measured on a Miyoo Mini with
  Onion, Linux has 85 MB available before the game starts, and the rest is the
  game's code and screen buffers. It also grants packages `memory:external`,
  which the hosted memory serves from the same heap.

### Stock firmware cards too
- An install takes any Miyoo SD card, with Onion or the stock firmware: a
  removable drive with `App/` and `Emu/` at its top, as MainUI lays them out
  on both. It used to need Onion's `.tmp_update/`. The card list says which
  system each card has ("Miyoo SD card (E:, Onion)"). Tested on Onion
  only.
- The target is called **Miyoo Mini**, not "Miyoo Mini (Onion OS)", and the
  code names follow what the app is: a MainUI app (`MainUiApp.h`, was
  `OnionApp.h`; `resources/mainui/`), on a Miyoo card (`FindMiyooCards`,
  `IsMiyooCard`, `NamesMiyooCard`).

### Fixed
- An install names its card as written: `E:`, `E:\` and `e:/` are the same
  card (`NamesMiyooCard`); only the exact `E:` was accepted. A drive that is
  not a Miyoo card is still refused, and the message lists the cards that
  are connected.

### Changed
- `miyoo_mini` provides `hosted` (an operating system starts the game as a
  program), not `desktop-os`. Needs the editor release that names it so.

### Added
- **The Miyoo Mini as a build target** (`miyoo_mini`, framework `miyoo`): the
  editor's Miyoo builder cross-compiles the game with the Arm GNU toolchain
  it installs (a toolchain component), packs a MainUI app (`App/<Folder>/`
  with `config.json`, `launch.sh`, the icon and the game's data), and
  installs it on the Miyoo's SD card, each file read back and checked.
- **The display** (`MiyooDisplaySetup`): `/dev/fb0` at the panel's ARGB8888,
  the game's 320x240 scaled by whole pixels and turned 180 degrees, flipped
  between pages in step with the panel's refresh, which it measures at start.
  Frames are presented on their own thread: the game draws into three buffers
  the display lends it, so a frame is handed over without a copy, and each
  row is widened straight into the panel's next page, with NEON for ARGB8888
  at 1x and 2x. It holds each frame as many refreshes as the game's frame rate
  asks, says when a frame stays up longer than the rest (a visible hitch), and
  counts the frames shown for a frame-rate counter
  (`IDisplay::GetFramesShown`).
- **The buttons** (`MiyooInputSetup`), MENU quitting the game.
- It requires `deki-input` alone. The game's `main()`, memory and file system
  are the engine's (`deki-engine-hosted`), as for any game run as a program;
  `deki-desktop-integration` is not needed.
- The platform's ARGB8888 screen and 64-pixel tiles for deki-3d's software
  rasteriser (`DEKI_3D_TILE_SIZE=64`).
- Tests: the builder's toolchain file, CMake and MainUI app, installing on fake
  SD cards, and the row widening, whose NEON path they hold to the plain one
  pixel for pixel when built for the Miyoo.
