# Deki Miyoo Boards

Build target for the Miyoo Mini. Install the package, pick **Miyoo Mini** in the Build panel, and **Build** makes a MainUI app; **Install** puts it on the Miyoo's SD card, where it shows in the Apps menu.

MainUI is the Miyoo's menu. The stock firmware has it, and so does [Onion OS](https://onionui.github.io), which is built on it and keeps its card layout, so the same app installs on either. Tested on Onion; the stock firmware is expected to work the same way, but no one has run a game on it yet.

| Target | Handheld | Screen | Input |
| --- | --- | --- | --- |
| `miyoo_mini` | Miyoo Mini | 320x240 ARGB8888, scaled 2x onto the 640x480 panel through the Linux framebuffer | The buttons and d-pad, as keys, through `deki-input` |

The screen is ARGB8888, the panel's own format: gradients show without RGB565's banding, and a present is a plain scaled copy. Measured on a Miyoo Mini, a 6k-triangle 3D scene ran as fast as in RGB565 or faster (the copy no longer converts each pixel, which leaves the CPU to the 3D). The platform also gives deki-3d's software rasteriser 64-pixel tiles (`DEKI_3D_TILE_SIZE=64` in `miyoo_mini.json`), for a scene that leaves its tile size to the platform: on the Miyoo Mini they beat 32 (a 6k-triangle scene from 52.8 to 56.6 frames a second in RGB565, from 58.0 to 59.7 in ARGB8888).

> **Not in the Package Manager yet.** It needs engine and editor changes that come with the next Deki release after 0.22: the display's refresh pacing and frame count (`IDisplay::SetRefreshesPerFrame`, `IDisplay::GetFramesShown`), `deki/Neon.h`, and the editor's shared CMake generator for games built as one program, and the engine's own `main()` for them (`deki-engine-hosted`). It is listed in the registry with that release.

[Deki](https://dekiengine.com) is a modular C++ engine for embedded systems and desktop, with a visual editor. Every capability is a package, and a handheld is one too: this package has the handheld's display and input, and the builder that makes and installs the app.

## Install

Package Manager in the Deki Editor, or `DekiEditor --packages-add deki-miyoo-boards <project>`. It brings `deki-input` with it. The game is a Linux program: its `main()`, its memory and its file system are the engine's own, as for any game built as a program (`F:/` is the app's `flash/` folder).

Then pick the target in the Build panel, or:

```
DekiEditor --tool set_active_platform --args '{"platform_id":"miyoo_mini"}' <project>
```

## Toolchain

The Build panel lists what a Miyoo build needs, with what to do for anything missing (`toolchain_status` says the same):

- **Arm GNU Toolchain 15.2.rel1** (`arm-none-linux-gnueabihf`), Arm's cross compiler for hard-float ARM Linux. **Install** downloads it from developer.arm.com (about 270 MB), checks it against Arm's published SHA-256 and puts it in `~/.deki-editor/toolchains/arm-gnu-toolchain/`. Only the Windows x86_64 archive is pinned so far; on another host the toolchain list says so and a Miyoo build is refused.
- **CMake** and **Ninja**, found on PATH (or in `C:/msys64/mingw64/bin` and CMake's own install folder).
- **GCC 16 or newer for this machine**, which compiles the reflection step that runs before the game compiles (it needs `-freflection`). On Windows that is MSYS2's `mingw-w64-x86_64-gcc`.

The editor installs only the first; install the others yourself and restart the editor.

## What a build does

The game builds like the desktop simulator, as one executable, then cross-compiled:

- The toolchain file (`generated/build/miyoo_mini/toolchain.cmake`) comes from the platform's `frameworkOptions`: `toolchainPrefix` (the compiler), `cpuFlags` (ARMv7, Cortex-A7, NEON, hard float) and `static`. The game is linked statically, so it does not depend on the C library of the Miyoo's system.
- Only the packages the game uses and the platform can build are compiled, so nothing written for SDL3, ESP32 or Bluetooth reaches the ARM compiler.
- The platform's `cxxFlags` (`-O3`) are the optimisation, for the engine and the game alike; `linkFlags` has `-s`, which strips the symbols.
- Logging follows the Build panel's logging options.

The app is then in `generated/build/miyoo_mini/App/<Folder>/`, named after the project:

| File | What it is |
| --- | --- |
| `DekiGame` | The game, a static ARM Linux executable |
| `launch.sh` | What MainUI runs: goes to the app's folder and starts the game, its output to `dekigame.out` beside it |
| `config.json` | The label (the project's name), the icon and the launch script, for MainUI's Apps menu |
| `icon.png` | The Apps icon, the Deki cat |
| `flash/` | The game's data: the project settings, the boot scene and the assets (`F:/` on the handheld) |
| `.deki-app` | Says the editor made this folder, so a later Install may replace it |

The game also writes `dekigame.log` (its own log) and the files it saves into its folder on the card.

## Install

**Install** copies the app to `App/<Folder>` on the Miyoo's SD card: put the card in a card reader, and the target list shows each removable drive with `App/` and `Emu/` at its top, and whether it has Onion (`.tmp_update/`) or the stock firmware. The copy goes to a hidden folder first and each file is read back and checked against the build before it replaces the old app, so a card pulled out halfway still has the old one. Eject the card before taking it out.

Renaming the project (Project Settings > Project > Name) gives the app a new folder. The old one stays on the card, as it may hold the game's saves, and the next Install says which it is; delete it from the card when it is no longer wanted.

Only `App/<Folder>` is ever replaced, and only when the editor made it. An app of the same name installed another way is left alone, and Install says so; rename the project, or move that folder off the card.

Without Install: copy `App/<Folder>` into `App/` on the card yourself.

## The handheld

Boot order, as the boot scene lists it:

1. Display: `MiyooDisplaySetup` opens `/dev/fb0`, scales the game's 320x240 by whole pixels to fill the panel and turns it 180 degrees (the panel is mounted upside down). Frames are presented on their own thread, in step with the panel's refresh. The game draws into three buffers the display lends it, so a frame is handed to that thread without a copy, and the thread widens each row straight into the panel's next page: with NEON for ARGB8888 at 1x and 2x, in plain C++ for other formats and scales. The log says which at startup.
2. Input: `MiyooInputSetup` reads the buttons from `/dev/input/event0` and sends them as keys: the d-pad as the arrows, A and Start as Enter, B as Esc, X as Space, Y as Tab, Select as Backspace, L1/R1 as Q/E, L2/R2 as Z/C. **Menu** quits the game, back to MainUI.

Memory: the platform states 80 MB (`externalMemorySize`), what a game can use. On a Miyoo Mini with Onion, Linux has 85 MB available before the game starts (of 101 MB it reports); the rest is the game's code and screen buffers. deki-3d checks a model against it when the game is built, and says which model does not fit.

Not set up: sound, battery reading, brightness, rumble.
