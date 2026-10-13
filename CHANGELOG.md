# Changelog

## 1.5 - 2026-10-13

- Replace fixed sharpness presets with game-controlled sampling of the complete eight-level mip chain; restore sharp rain sampling.
- Optimize full-chain pool replication with one dispatch per pool and safe reuse of unchanged retained-original entries.
- Cache source/tint analysis until verified streaming or array writes invalidate it. Keep conservative checks when route verification is unavailable.
- Improve ordinary-view binding lookups while retaining every required D3D call, slot update, reference and ownership guard.
- Compile all thirteen shaders at build time; remove runtime compilation.
- Batch tint diagnostic reports and skip their construction when diagnostics are off.
- Add the Community Shaders-derived optional profiler, focused dispatch tracing and cubemap previews. Diagnostics, graph and previews default off.
- Default movement reprojection off. Allocate separate sky history only for exterior reprojection, never interiors or the direct capture path.
- Use sky-only scene color for exterior unobserved directions and scene-derived inference indoors. Remove the unused native-sky lookup, abandoned gradient experiment, separate brown interior fallback and obsolete inference variants.
- Verify exact OG163/NG984 streaming bodies and reuse the existing checked observers and named-SRV acquisition, including the exact reviewed Addictol 1.7.0 loader replacement.
- Remove the unused pool wrapper, redundant historical build wrappers and sibling-checkout comparison scripts from the public tree. Consolidate current tests and release tooling; preserve bundled dependency sources and licenses.

Release 1.5 preserves the accepted RuntimeCompat01 rendering kernels, tint, calibration, cache budgets, safety gates and capture scheduling. It does not redistribute game textures, executable code, inspection tools or benchmark telemetry.

Saved MCM choices continue to override defaults. Existing diagnostic settings are not forcibly reset. Old sharpness keys are ignored.

## 1.4

Public 1.4 introduced five fixed-mip sharpness presets, the disabled-startup enable fix, ordinary binding cache improvements, optional diagnostics and the existing Realistic Reflections ownership protocol. Historical development notes are under `docs/history`.
