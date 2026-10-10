# DynamicCubemapF4 1.4

Scene-based dynamic cubemap reflections for Fallout 4. Includes movement reprojection, authored tint and reflectiveness, and compatibility with Realistic Reflections renderers using the shared ownership protocol.

## Downloads

Use **DynamicCubemapF4-1.4.zip** for installation (DLL and MCM files only). **DynamicCubemapF4-1.4-source.zip** contains source, bundled dependencies, tests, build/package tools, documentation and licenses. Both are available on the [Releases page](https://github.com/103Sakamoto/DynamicCubemapF4/releases).

## Requirements and installation

Supported runtime profiles: Fallout 4 1.10.163, 1.10.984 and 1.11.240; VR is not supported. Install matching F4SE and Address Library, and MCM for in-game controls. Install the runtime ZIP through your mod manager, replacing both DLL and MCM configuration, then restart the game. No ESP or Papyrus scripts are supplied. Local 1.4 testing is not a claim of new in-game validation on every supported runtime.

## Settings

| Reflection sharpness | Mip | Resolution per face |
|---|---:|---:|
| Sharpest | 0 | 128 x 128 |
| Sharper | 1 | 64 x 64 |
| Neutral (default) | 2 | 32 x 32 |
| Softer | 3 | 16 x 16 |
| Softest | 4 | 8 x 8 |

Each preset fixes the sampled mip of dynamic replacements. Capture stays 128 x 128; replacement pools update only the selected level. The internal capture mip chain remains available. Changes apply when gameplay resumes.

Dynamic reflections and movement reprojection default on. Disable reflections to restore original bindings and stop capture/replacement processing; other options hide. Passive fallback-load observation remains active so enabling after a disabled startup works. Hooks stay installed for re-enabling.

**Diagnostic mode defaults off.** Enable it only to collect detailed logs and sampled timings. Normal logging keeps startup information and errors. Existing user settings override defaults. The new `iSharpnessMode` key defaults to Neutral; obsolete `iRoughnessMode` values are ignored. An existing `bDiagnostics=1` remains enabled until switched off.

The 1024-entry binding cache and retained views preserve performance and stable sharpness switching. Capture calibration and authored tint remain unchanged from the accepted fixed-mip test build. Lower fallback mip blur can differ from the original DDS because the blended output's mip chain is regenerated.

## Building

Requires Visual Studio C++23/x64 tools, Windows SDK, xmake 3.0+ and Microsoft Detours. Bundled CommonLib sources are under `Source/lib`; xmake resolves its additional declared dependencies, which may require network access.

From an x64 Developer Command Prompt:

```bat
set "DCF4_DETOURS=E:\Coding\F4\vcpkg\installed\x64-windows-static-md"
call Source\BUILD.cmd
```

Override `DCF4_DETOURS` with your installation path. Output: `Source/build/windows/x64/releasedbg/DynamicCubemapF4.dll` (optimized build). Run `PACKAGE.cmd` to produce both archives and checksums. Python 3 is required for packaging.

## Validation and support

Local regression tests cover binding ownership, restoration, retained mip views, selected-mip dispatch for all five presets, and fallback observation when starting disabled. WARP tests cover diagnostic queries. These do not replace in-game testing. Test all five presets and startup-disabled -> enabled in your installation.

For support, enable Diagnostic mode and provide `Data/F4SE/Plugins/DynamicCubemapF4.log` and `.previous.log` after reproducing. Disable diagnostics for performance comparisons.

## Credits and licenses

Copyright (C) 2026 103Sakamoto. See [LICENSE.txt](LICENSE.txt) and [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md), including Community Shaders, CommonLibF4, Detours and other dependency notices. Full license texts and bundled dependency sources are in the source download. Historical development notes are in `docs/history`.
