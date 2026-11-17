[![Nightly build](https://github.com/MinicraftPlus/minicraft-plus-revived/actions/workflows/autobuild.yml/badge.svg?branch=main)](https://github.com/MinicraftPlus/minicraft-plus-revived/actions/workflows/autobuild.yml)
[![CodeQL](https://github.com/MinicraftPlus/minicraft-plus-revived/actions/workflows/codeql-analysis.yml/badge.svg?branch=main)](https://github.com/MinicraftPlus/minicraft-plus-revived/actions/workflows/codeql-analysis.yml)

# Minicraft+

![Minicraft+](https://user-images.githubusercontent.com/37084190/138313821-75ac3112-7044-45c1-bdbb-d89f2333c2c0.png)

Minicraft+ is an overhaul mod of Minicraft, a game made by Markus "Notch" Persson in the Ludum Dare 22 contest. To learn
more about Minicraft take a look at [our website](https://minicraftplus.github.io/), talk to the community at
our [Discord](https://discord.me/minicraft), or check out
our [wiki.gg Wiki](https://minicraft.wiki.gg/wiki/).

Check the [releases](https://github.com/the-dharex/minicraft-cpp/releases) page to download the latest
version, or older versions.

## Major features

* Four new gamemodes
    * Creative
    * Hardcore
    * Score
    * Survival
* Saving and loading
* More mobs
* Personal crafting menu
* Beds
* Mob spawners with loot
* Ruined structures with loot
* An expanded storyline
* Key binding customization
* Tutorials
* Achievements
* Resource packs
* Better rendering
* Skins
* Quests
* Support for several languages
* and many, many more!

## System Prerequisites

The C++ version supports Windows, macOS and Linux on PC, and Nintendo Switch.

Building the PC version requires:

* A C++20-compatible compiler
* CMake 3.20 or newer
* GLFW 3
* `glslangValidator`
* OpenGL

## Current goals and ideas

Take a look at the [ideas](ideas/) folder or
the [issues](https://github.com/minicraftplus/minicraft-plus-revived/issues) page.

## Getting the game and run the game

Head over [releases](https://github.com/minicraftplus/minicraft-plus-revived/releases) and find the latest version of
Minicraft+.
On PC, run `minicraft_pc.exe` on Windows or `minicraft_pc` on macOS or Linux.
On Nintendo Switch, use `minicraft_switch.nro` and make sure `assets.pak` is included in the application RomFS.

## Localization

This project is running with an external localization platform called Lokalise. You can now [head over Lokalise to contribute localization](https://app.lokalise.com/public/42925017655336d50b3007.03067253/)!

## How to build/run in development

This project uses CMake to build the C++ source code.

1. Download the source code by clicking the green code button, and download it as a ZIP.
2. Extract the contents of the folder.
3. Open a terminal and enter `cd <folder_location>`.
4. Configure and build the PC version:
   ```sh
   cmake -S . -B build
   cmake --build build
   ```
5. Run `build/minicraft_pc` on macOS or Linux, or `build/minicraft_pc.exe` on Windows.

The build produces precompiled shaders, the `pak_packer` and `embed_shaders` tools, and `minicraft_tests`.
