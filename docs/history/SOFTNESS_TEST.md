# FixedMipEnable01
Based on Control01 with only the EnableFix01 fallback-observation fix applied. Restores MCM sharpness selection: Sharper mip 0 (128px), Default mip 2 (32px), Softer mip 4 (8px). Replacement pools update only the selected mip; retained views and atomic mip switching remain intact. The complete live chain is still generated internally.
Named fallback loading is observed even when starting disabled, preserving the reference for later MCM enable. Capture and reflection substitution remain off while disabled. The 1024-entry cache, opt-in diagnostics, disable behavior, capture calibration and tint are inherited from Control01. No VanillaMip01 full-chain pool replication or ToggleDiag01 disabled audits.
Validation procedure: start disabled, load exterior save, enable; test all three sharpness options and repeated switching; compare the same performance route with diagnostics off.
# ExteriorBalance02 ¡ª retained material views ¡ª October 9, 2026

Based on ExteriorBalance01, retaining its successful exterior brightness/color policy. Investigates continuous apparent sharpness alternation after changing MCM softness.

A regression test reproduced a concrete binding failure in ExteriorBalance01: after replacing the live material SRV, rebinding the previously retained SRV did not select the new view and could register our own replacement as an authored input. The prior pointer-only ownership test recognized only the latest SRVs. This is a plausible cause of the user's symptom, not a confirmed in-game reproduction.

The fix caches material views by mip (bounded to eight per live/pool texture), recognizes retained views as ours, redirects intercepted old bindings to the currently selected view, and preserves the authored slot for restoration. Returning to an earlier softness reuses its view after populating that mip. Texture recreation clears the live cache; pool destruction releases its cache. Existing assumptions about peers restoring overrides promptly and not carrying them across lifecycle resets still apply.

Install the complete archive over previous DCF4 test packages, disable the old packages and restart Fallout 4. Keep the same scene and reprojection setting. Cycle Default -> Sharper -> Softer -> Default several times, first standing still and then moving. Check that the exterior brightness remains correct, and test MCM disable/enable and Glass if used. Send DynamicCubemapF4.log. If an old replacement is observed, the log reports once: MIP: retained replacement view redirected to current softness.

Validation: retained-view test failed before the fix, then passed after it. Tests also cover repeated switches, retained array views, restoration outside the PS hook, authored slot retention, Glass overrides and reference balance. Existing dispatch/rain tests pass. The shaders, brightness policy and MCM controls are unchanged. This does not establish the in-game artifact is resolved.
