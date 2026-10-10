# FixedMipEnable01
Based on Control01 with only the EnableFix01 fallback-observation fix applied. Restores MCM sharpness selection: Sharper mip 0 (128px), Default mip 2 (32px), Softer mip 4 (8px). Replacement pools update only the selected mip; retained views and atomic mip switching remain intact. The complete live chain is still generated internally.
Named fallback loading is observed even when starting disabled, preserving the reference for later MCM enable. Capture and reflection substitution remain off while disabled. The 1024-entry cache, opt-in diagnostics, disable behavior, capture calibration and tint are inherited from Control01. No VanillaMip01 full-chain pool replication or ToggleDiag01 disabled audits.
Validation procedure: start disabled, load exterior save, enable; test all three sharpness options and repeated switching; compare the same performance route with diagnostics off.
# Control01: diagnostics and disable controls

Based on the accepted CacheClock02 (1024-entry clock cache).
Diagnostic mode is off by default. Enable it in MCM to collect detailed logs, sampled CPU/GPU timings and cache statistics. Turning it off releases timing queries and resets diagnostic counters; normal logging retains the startup header and failure messages.
Disabling Dynamic reflections restores original bindings, stops capture/reprojection and tint processing, clears replacement/cache state and stops diagnostics. Other MCM options are hidden. Settings take effect when gameplay resumes (settings poll interval 500 ms). Existing hooks remain pass-through and reusable capture resources remain allocated so re-enabling is supported. This is not DLL unloading or a claim of zero hook overhead.
Reflection quality, capture cadence, Glass ownership handling, tint and brightness behavior are unchanged.

Validation: Windows release build; binding/cache and dispatch regressions; D3D11 WARP diagnostic toggle/query-release tests. In-game validation remains required: toggle diagnostics on/off, disable/re-enable reflections, check menu visibility and original cubemaps, then test save/load with the mod disabled.
# CacheClock02: 1024-entry comparison
Based on CacheClock01. The only runtime changes are increasing both clock arrays from 512 to 1024 entries and the build identifier. Clock policy, diagnostics, shaders, capture, mip choices and update cadence remain unchanged.

Compare with CacheClock01 using the same save and turning/walking route after restarting. Warm up one minute, then run 5-10 minutes without toggling or reloading. Send the log and FPS. Look for lower eviction rates and check referenced resource storage: more entries may retain more resources. Logical referenced storage is not actual or incremental resident VRAM.

The capacity regression now inserts 1025 views, checks one eviction, protects a recently used entry, and verifies reset releases references. Earlier test notes below describe historical builds with smaller budgets.
# CacheClock01 working test

512-entry clock eviction policy, based on PerfDiag02. See PERFORMANCE_TEST.md. Prior entries are historical.

# PerfDiag02 working diagnostic build

Cache counters and referenced-resource estimates; budget stays 512. See PERFORMANCE_TEST.md. Earlier history follows.

# PerfDiag01 working diagnostic build

Based on BindPerf01. See PERFORMANCE_TEST.md for timing limitations and the repeated-route procedure. Previous entries are historical.

# BindPerf01 working test

CPU lookup and duplicate-check improvements only, based on TintCapture02. See BIND_PERF_TEST.md. Earlier entries below are historical.

# TintCapture02 working test

Fix standalone named exterior bindings; see TINT_CAPTURE_TEST.md. Prior history follows.

# TintCapture01 working test

Current behavior is documented in TINT_CAPTURE_TEST.md; earlier entries below are historical.

# ExteriorBalance02 working derivative

Based on ExteriorBalance01. Preserve its exterior brightness correction. See SOFTNESS_TEST.md for the retained-view change.

# ExteriorBalance01 working derivative

User requested exterior color preservation with restored authored brightness. Based directly on GlassInterop01_Perf01. See EXTERIOR_BALANCE_TEST.md; no Exposure01 global EV or Brightness01 curve.

# Development baseline: DynamicCubemapF4 1.3.1

## Perf01 working derivative ¡ª October 8, 2026

The user selected `E:\Coding\F4\DCF4_GlassInterop01` as the baseline for this optimization work, superseding the public-only development choice below. This working copy is `DCF4_GlassInterop01_Perf01`; the original GlassInterop01 and public source folders are unchanged. Preserve the shared ownership header and Glass binding behavior. See CHANGELOG.md for this derivative's changes. The historical validation below does not validate this derivative in game.

User-designated baseline for all subsequent features and debugging, accepted September 2026. Start from this release or its later explicit successor; do not resume an older experimental package by default.

Parent: DCF4_Universal_1.3_ReferenceRecovery_Test. The user reported successful gameplay; supplied DynamicCubemapF4(4).log identifies AE 1.11.240 and normal reference acquisition. No invalid-reference recovery occurred in that log, so its actual GPU failure path remains unproven. Earlier runtime work covered OG/NG/AE, but this log does not retest all runtimes or heavily modded configurations.

Parent Main.cpp SHA-256: 75f473756404005431d4d5623d38ce2450944f7223ac5b959c21b5e3bea02e3a

Release-only source edits: replace log/version identifiers with 1.3.1; move PluginVersion.cpp's F4SE metadata/query exports into Main.cpp; explicitly compile Main.cpp. Shader source and runtime implementation otherwise remain byte-for-byte identical. Version metadata is 1.3.1.0. Final Windows rebuild and smoke test of the repackaged release are still needed; no DLL was compiled in this environment.

Preserve fixed mip choices (Default=2, Sharper=0, Softer=4), 128x128 capture, separate sky history, per-candidate exterior matching, stable binding and validated reference recovery unless the user requests a change. The bright-eye issue was reproduced with the mod disabled and is not an active plugin-specific investigation.

Seven standalone extracted/mock C++ tests pass locally. They are not part of the DLL. Public source identifiers, unchanged shader/runtime body, MCM equivalence and archive integrity were checked. SHA256SUMS.txt records source/configuration files for later comparisons. Suggested Git tag: v1.3.1. No GitHub publication was performed during packaging.
