Local validation: fixed-mip binding/restoration tests, selected-mip dispatch tests, disabled/enabled fallback-load regression, and WARP diagnostic tests passed. Runtime source comparison confirms only the enable fix and identifier differ from Control01. In-game verification remains required.

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

# ExteriorBalance02 ！ retained material views ！ October 9, 2026

Based on ExteriorBalance01, retaining its successful exterior brightness/color policy. Investigates continuous apparent sharpness alternation after changing MCM softness.

A regression test reproduced a concrete binding failure in ExteriorBalance01: after replacing the live material SRV, rebinding the previously retained SRV did not select the new view and could register our own replacement as an authored input. The prior pointer-only ownership test recognized only the latest SRVs. This is a plausible cause of the user's symptom, not a confirmed in-game reproduction.

The fix caches material views by mip (bounded to eight per live/pool texture), recognizes retained views as ours, redirects intercepted old bindings to the currently selected view, and preserves the authored slot for restoration. Returning to an earlier softness reuses its view after populating that mip. Texture recreation clears the live cache; pool destruction releases its cache. Existing assumptions about peers restoring overrides promptly and not carrying them across lifecycle resets still apply.

Install the complete archive over previous DCF4 test packages, disable the old packages and restart Fallout 4. Keep the same scene and reprojection setting. Cycle Default -> Sharper -> Softer -> Default several times, first standing still and then moving. Check that the exterior brightness remains correct, and test MCM disable/enable and Glass if used. Send DynamicCubemapF4.log. If an old replacement is observed, the log reports once: MIP: retained replacement view redirected to current softness.

Validation: retained-view test failed before the fix, then passed after it. Tests also cover repeated switches, retained array views, restoration outside the PS hook, authored slot retention, Glass overrides and reference balance. Existing dispatch/rain tests pass. The shaders, brightness policy and MCM controls are unchanged. This does not establish the in-game artifact is resolved.

# ExteriorBalance01 ！ October 9, 2026

Based on GlassInterop01 Perf01. Recognized exterior cubes bypass authored hue/desaturation as before, but no longer bypass the authored luminance scale. Brightness is clamp(sqrt(clamp(Y,0,1)),0.15,1) for usable exterior luminance, independently of hue consistency. Ordinary material policies are retained. The supplied 1.3 log's Y=0.072524 yields approximately 0.269302, rather than 1.0.

Array entries and analyzed standalone exterior copies use the same scale. Standalone exterior copies receive a styled pool; if pool capacity prevents allocation, the authored binding remains instead of using an unscaled shared live cube. Existing reference identity, ambiguous-candidate guards, recovery, Glass ownership and Perf01 optimizations remain. Capture, inference and replication shader strings are unchanged. The current fixed-mip softness behavior remains; this does not restore 1.3's roughness filtering.

Install this entire test archive as a separate mod, with earlier DCF4 test packages disabled, then restart the game. Enable Dynamic reflections and use the same softness/reprojection choices as your comparison. There is no global exposure reduction, compression curve, or diagnostic readback loop. Saved iExposureReduction and bBrightnessDiagnostics settings from Exposure01 are ignored. Glass compatibility is included.

Check the affected weapon at noon and at night, sky color, nearby non-exterior materials and Glass, MCM off/on, all softness choices, and save/cell transitions. Send DynamicCubemapF4.log. The header identifies ExteriorBalance01. EXTERIOR_EXCLUDED now means excluded from authored color treatment only; its scale should reflect the original cube's luminance, not necessarily a universal 0.269302.

Validation: Windows MSVC x64 releasedbg build passed. Extracted-production policy tests passed for duplicate exterior candidates, ordinary tint/neutral materials, conservative original retention, and invalid luminance. Updated Glass binding tests passed including exterior pool exhaustion and pending publication; Perf01 dispatch/rain tests passed. In-game appearance is not yet verified. This is a targeted test fix, not a confirmed final release.

# Changelog

## 1.3.1
- Improve exterior fallback recognition when several copies appear in texture arrays, reducing session-dependent tint mistakes.
- Keep completed replacements bound while tint updates are pending, reducing unnecessary switches to original reflections.
- Preserve original textures while analysis data is unavailable.
- Remove custom full-face softening and use standard mip generation.
- Rework MCM softness: Sharper uses mip 0, Default mip 2, Softer mip 4. Changes preserve capture history.
- Recover from persistent invalid exterior-reference samples by validating a reopened candidate before replacing the reference.
- Reduce analysis readback copies to the results needed and improve shader/query failure diagnostics.
- Remove unused shader code and consolidate plugin implementation and version exports into Main.cpp.

This release retains the working capture, reprojection, separate sky history, tint/intensity policy and rain bypass. Public packaging introduces no rendering changes relative to the accepted ReferenceRecovery build.
# GlassInterop01 Perf01 ！ October 8, 2026

- Based on the user-selected GlassInterop01 compatibility source. Ownership tags and peer binding behavior are preserved.
- Replacement pools dispatch only the selected material mip (one dispatch instead of eight). Full capture and live mip generation remain unchanged. Softness changes populate the new pool mip before committing replacement views, including callbacks without capture; failed view preparation leaves the current views intact.
- Draw hooks bypass the constant-buffer query when the latest valid, finite desaturation snapshot is already neutral. Binding audits remain active. Rain, invalid snapshots, and peer ownership continue through their existing paths.
- MSVC x64 releasedbg DLL build passed. Extracted production interop and performance mock checks passed. These checks do not establish in-game compatibility, visual equivalence, or FPS improvement.
- In-game checks: compare all three softness modes, change softness repeatedly, test rain and clear weather with Glass and nearby metal, toggle DCF4, and load/change cells. Compare frame times against the original GlassInterop01 at the same location and settings. The public version metadata remains 1.3.1; identify this derivative using the Perf01 archive and checksums.
