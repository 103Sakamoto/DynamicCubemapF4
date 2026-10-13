# DynamicCubemapF4 1.5

Scene-based dynamic cubemap reflections for Fallout 4, with authored material tint and reflectiveness, environment-matched fallback color, and optional movement reprojection.

## Downloads and installation

Use **DynamicCubemapF4-1.5.zip** for installation. It contains only the plugin DLL and MCM configuration. **DynamicCubemapF4-1.5-source.zip** contains the complete project source, bundled dependency sources, licenses, tests, build tools and documentation. Downloads and checksums are on the [1.5 release page](https://github.com/103Sakamoto/DynamicCubemapF4/releases/tag/v1.5).

Supported exact Fallout 4 versions: **1.10.163, 1.10.984 and 1.11.240**. VR and other executable versions are unsupported. Install the matching F4SE and Address Library, plus a matching Mod Configuration Menu installation for in-game controls.

Install the runtime ZIP through your mod manager, replacing both the previous DLL and MCM configuration, then restart the game. No ESP, Papyrus scripts or replacement game textures are required or supplied.

## Changes from 1.4

- Game-controlled mip selection exposes all eight mip levels, including sharp rainy reflections. The old sharpness presets have been removed.
- Full-chain material pools update all required dynamic entries in one dispatch per pool. Unchanged entries that retain their original texture reuse verified published data.
- Source analysis is reused until verified streaming/write notifications invalidate it. Unknown or altered streaming routes retain conservative checks.
- Exterior missing directions use sky-only color sampled from existing scene/depth inputs on the GPU. Interiors use scene-derived inference; the separate brown interior fallback is removed.
- Shader compilation happens at build time. Thirteen embedded shaders are supplied in the DLL.
- Ordinary texture binding lookups avoid repeated classification work. Tint diagnostic reports are batched and are not constructed when diagnostics are off.
- The optional profiler reuses Skyrim Community Shaders code and includes CPU/GPU views and cubemap previews.
- Exact OG/NG streaming verification and named-texture acquisition are expanded while preserving reference validation and peer ownership checks.

Capture resolution remains 128 x 128 per face. Authored tint, reflectiveness, exposure calibration, full mip chains and the accepted capture scheduling are preserved. Material shaders still affect the final appearance; regenerated fallback mips can look different from a DDS texture's authored blur.

## Settings

| Setting | Default | Behavior |
|---|---|---|
| Dynamic reflections | On | Replaces eligible material cubemaps with the live result. |
| Reproject reflections while moving | Off | Adjusts geometry history for movement. Separate sky history is used only for confirmed exterior reprojection. |
| Diagnostic mode | **Off** | Enables diagnostic logs, sampled timings and the optional profiler. |

Disabling reflections restores original cubemap bindings and stops reflection processing and diagnostics. Other MCM controls hide while disabled. Passive reference-load observation remains available so reflections can be enabled after starting disabled.

**Diagnostics, the graph and previews are off by default.** When Diagnostic mode is enabled, Ctrl+F6 shows/hides the graph, Ctrl+F7 switches GPU/CPU, and Ctrl+F10 shows/hides cubemap previews. With previews visible, Ctrl+F8 selects the preview mip and Ctrl+F9 switches RGB/confidence. The preview mip does not change reflection sampling.

Existing `Data/MCM/Settings/DynamicCubemapF4.ini` preferences override packaged defaults. An existing `bDiagnostics=1` remains enabled until changed in MCM. Old sharpness/roughness keys are ignored; no user settings file is included in the download. Turn diagnostics off for normal gameplay and FPS comparisons.

## Performance and validation

The latest nine user captures on an RTX 4070, with diagnostics/previews off, measured about **0.6% average FPS overhead without reprojection** and **0.8% with reprojection**. They were three approximately 31-34-second runs per mode, in a fixed sequence with VSync active. This is a configuration-specific result, not a universal performance guarantee. An earlier automated route measured about 2.4%; capture protocols differed. See [docs/PERFORMANCE.md](docs/PERFORMANCE.md).

The accepted implementation was tested in game on 1.11.240 and 1.10.984; 1.10.163 functionality was confirmed by the user. NG tests used the polled settings file because the installed AE MCM DLL was incompatible with NG. The original reporter's exact missing-reference installation is still unverified.

Release checks cover shader identity, state/reference restoration, full-chain GPU output, streaming notifications, pool invalidation, disabled-startup enable, sky-only sampling, optional sky lifecycle and diagnostics. Mock/WARP/static checks are not substitutes for in-game testing. See [docs/VALIDATION.md](docs/VALIDATION.md) and [SAFEGUARDS.md](SAFEGUARDS.md).

## Building

Requires Windows x64, Visual Studio C++23 tools/Windows SDK, xmake 3.0+, Python 3.9+, and Microsoft Detours. Additional dependencies declared by bundled CommonLibF4 are resolved by xmake. Run from an x64 Developer Command Prompt:

```bat
set "DCF4_DETOURS=C:\path\to\vcpkg\installed\x64-windows-static-md"
set "DCF4_PYTHON=python"
call BUILD.cmd
call RUN_TESTS.cmd
call PACKAGE.cmd
```

`DCF4_DETOURS` must contain `include/detours/detours.h` and `lib/detours.lib`. Tests also need Visual Studio LLVM tools or `DCF4_CLANG` pointing to `clang++.exe`. DLL output: `Source/build/windows/x64/releasedbg/DynamicCubemapF4.dll`. Packaging produces the two ZIPs and `SHA256SUMS-1.5.txt` in `dist`.

For support, enable Diagnostic mode, reproduce the issue, then provide `Data/F4SE/Plugins/DynamicCubemapF4.log` and `DynamicCubemapF4.previous.log`. GPU timestamp intervals include possible waits and are not pure GPU busy time.

Copyright (C) 2026 103Sakamoto. See [LICENSE.txt](LICENSE.txt) and [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for original upstream terms and credits, including Community Shaders, CommonLibF4, Detours and Dear ImGui.
