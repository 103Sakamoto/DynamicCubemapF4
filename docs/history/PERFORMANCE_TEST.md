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
# CacheClock01 test
Based on PerfDiag02. Non-cube cache remains bounded to 512 views. Replaces arbitrary unordered_map eviction with a clock (second-chance) policy: a hit sets one recent-use bit, insertion sweeps recent bits and evicts the next unreferenced entry. At most 513 slot inspections per insertion; worst-case working sets larger than capacity may still thrash. No promise of FPS improvement.

Strong view references remain in the existing cache map; the clock stores borrowed pointers only. Reset clears the map and clock together. Tests cover the capacity bound, survival of a recently touched entry, eviction of an untouched entry, reset and reference release. All previous binding and performance mocks pass.

PERF CACHE counters and memory estimates remain enabled. Resource memory represented by 512 entries can change with the new policy; same entry count does not guarantee identical referenced bytes. The estimate remains logical referenced storage, not actual VRAM residency or memory retained solely by this mod.

Compare against PerfDiag02: restart, use the same save, warm up one minute, then repeat the same turning/walking route for 5-10 minutes without toggling or loading. Send the log and FPS. Compare evictions per second alongside bind volume and referenced memory. Rendering/capture/shaders/mips/cadence unchanged.

Prior diagnostic documentation follows.
# PerfDiag02 cache diagnostics
Based on PerfDiag01. Same 512-entry limit, eviction policy, shaders, capture and material settings.

PERF CACHE reports every 30 seconds:
- hits/misses: non-cube cache probes after excluding null and our own replacement views. Misses include known authored cubes and peer-owned textures; a miss alone is not a texture descriptor check or cache churn.
- inserts: newly classified ordinary views inserted in this window.
- evictions: insertions that removed an entry because the 512 limit was reached. Frequent evictions and insertions on the same warmed route are evidence worth investigating, but do not directly measure reinsertions of the same view.
- uniqueResources: deduplicated underlying resources referenced by the current cache.
- referencedLogicalMiB_est: mip/array/sample-aware logical bytes of recognized Texture2D formats and buffers. This is NOT actual resident VRAM, not a new allocation, and not memory retained solely by the mod. Unsupported resources are counted in unknownResources and omitted from bytes. Includes whole resources even if a view exposes only a subset. Does not include driver metadata/alignment, CPU table memory or the separate cubemap pool allocation.

Inspection happens once per report and adds diagnostic overhead. Counter overhead is also nonzero. Existing sparse timing limits still apply. Reading the descriptions does not read texture pixels or issue GPU readbacks. No cache-budget change was made.

Restart with this package instead of PerfDiag01. Warm up one minute, then turn and walk the same small area for 5-10 minutes without toggling or reloading. Send the log. We will compare eviction rate and resource coverage before deciding whether to increase the limit or change the eviction policy.

Validation: Windows DLL build, binding/dispatch regression tests, 513-view eviction test, Direct3D WARP timestamps, RGBA16F/BC1 byte estimates, unsupported-format handling, and shared-texture deduplication passed. In-game results pending.

Prior diagnostic details follow.
# PerfDiag01: session performance diagnostics
Based on BindPerf01. Rendering settings, shaders, cadence, capture resolution and ownership behavior are unchanged. Diagnostics add small unmeasured overhead; do not treat this as a faster release.

Every 30 seconds the normal log receives PERF COUNTS and PERF TIMING. Timings are mean/max milliseconds over sparse samples, not per-frame costs. Capture is sampled at most once per second. Binding timing samples every 4096 eligible PS binding calls and includes the original driver's call. Disabled-hook early returns are not timed.

GPU timestamps use a bounded four-slot disjoint-query ring. Poll once per second using DONOTFLUSH, never spin or wait; skip if full, reject incomplete/disjoint/failed samples. NA means no valid samples, not zero cost. WARP test validates query mechanics, not hardware performance or game correctness.

Stages:
- capture: accumulation setup, reprojection when active, capture dispatch.
- infer: binding restoration, capture-history mip generation and inference.
- publish: output copying/mip generation, material-pool replication/tint analysis and binding repair.
CPU times include submission/driver overhead and possible other hooks. GPU intervals include command-stream gaps and are approximate attribution, not isolated shader duration. Unrelated per-frame hooks outside these boundaries are not fully profiled. GPU samples can cross report boundaries; compare trends rather than summing CPU/GPU columns.

Counts: current pools/cubes, ready pools, authored/non-cube/replacement view caches, sampled session peaks, update/bind rates. poolTextureMiB_est estimates only allocated RGBA16F material-pool texture storage, not total VRAM/residency, original textures, or driver overhead. Growth while first encountering new assets is expected; indefinite growth on a warmed identical route merits investigation. Existing maximums remain enforced.

Test:
1. Restart Fallout 4 with this package instead of BindPerf01. Load the usual save and allow 60 seconds to warm up.
2. Repeat the same benchmark route at least three times in the SAME session for 10-15 minutes. Avoid save reloads and MCM toggles during this phase because they reset caches. Note benchmark start/end times and FPS.
3. Return to the same viewpoint. Toggle off for 30 seconds, then on; warm up 60 seconds and repeat the route once. This tests whether a plugin-state reset improves performance.
4. Save the log before launching another session. Send it with benchmark results and approximate run times. Optionally repeat after a full game restart.

The diagnostic build alone cannot prove BindPerf01 improved over TintCapture02; direct old/new benchmark comparison still requires equivalent runs. Its first purpose is locating cost and determining whether session growth correlates with slowdown.
