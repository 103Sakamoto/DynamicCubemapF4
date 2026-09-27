# DynamicCubemapF4 1.3.1

Scene-based dynamic cubemap reflections for Fallout 4, with movement reprojection, separate sky history, authored material tint/intensity preservation, rain desaturation bypass and persistent MCM controls.

## Requirements
- Supported game executables: OG 1.10.163, NG 1.10.984, AE 1.11.240.
- F4SE and Address Library files matching the installed game runtime.
- Fallout 4 Mod Configuration Menu for the in-game controls. Defaults remain available without its menu.

## Player installation
Use the player ZIP from the release, not this source tree. Install it through your mod manager, or merge its F4SE and MCM folders into the game's Data folder. Launch through the matching F4SE loader. No additional ESP or Papyrus script is supplied.

In MCM, Dynamic Cubemap F4 provides:
- Dynamic reflections: enabled by default; disable to restore original cubemap bindings.
- Reprojection: enabled by default; adds GPU work while moving.
- Reflection softness: Default=mip 2 (32x32), Sharper=mip 0 (128x128), Softer=mip 4 (8x8).

Softness fixes the sampled mip globally for replacement views. Other material lighting properties remain active. The full 128x128 capture and mip chain are still generated for every mode. Previous saved choices are retained; missing settings use Default. No keyboard toggles are included.

## Build
Extract this folder under E:\Coding\F4. Use Visual Studio **x64 Developer Command Prompt**:

```bat
call "E:\Coding\F4\DCF4_Universal_1.3.1_Public\Source\BUILD.cmd"
```

Requires MSVC with C++23 support, Windows SDK, xmake 3.0 or newer, and Detours installed at E:\Coding\F4\vcpkg\installed\x64-windows-static-md. BUILD.cmd selects its own Source directory and checks the Detours headers/library. xmake resolves additional declared dependencies; network access may be needed on first configuration.

DLL output: Source\build\windows\x64\releasedbg\DynamicCubemapF4.dll.
Bundled CommonLib source and license notices are retained. Plugin implementation and F4SE version/query exports are consolidated in Source/src/Main.cpp; PCH.hpp remains the build's precompiled header.

## Create the player download
After building and checking the DLL in game, run:

```bat
call "E:\Coding\F4\DCF4_Universal_1.3.1_Public\PACKAGE.cmd"
```

This creates dist\DynamicCubemapF4-1.3.1.zip from the built DLL, MCM configuration and license/credit files. It excludes developer tests, build caches, logs and saved MCM settings. No precompiled DLL is included in this source package.

## Support and limitations
Send Data/F4SE/Plugins/DynamicCubemapF4.log and DynamicCubemapF4.previous.log. Mod managers may redirect generated files into their overwrite/output folder. The log header is DynamicCubemapF4 1.3.1.

Capture depends on visible scene information and retained history; unseen scenery uses fallback. It is not a six-camera full-world render. Dedicated world-water support, roof awareness and night compensation are not implemented. Custom rendering plugins can require compatibility testing.

Invalid-reference recovery validates a reopened candidate after persistent unusable samples, with bounded retries. It does not repair a lost graphics device or guarantee recovery from every rendering failure. A recovery DDS load may briefly hitch.

See CHANGELOG.md for changes, BASELINE.md for provenance/validation, GITHUB.md for publishing, and THIRD_PARTY_NOTICES.md plus LICENSE.txt for attribution and license texts.
