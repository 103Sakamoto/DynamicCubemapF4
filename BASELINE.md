# Development baseline: DynamicCubemapF4 1.3.1

User-designated baseline for all subsequent features and debugging, accepted September 2026. Start from this release or its later explicit successor; do not resume an older experimental package by default.

Parent: DCF4_Universal_1.3_ReferenceRecovery_Test. The user reported successful gameplay; supplied DynamicCubemapF4(4).log identifies AE 1.11.240 and normal reference acquisition. No invalid-reference recovery occurred in that log, so its actual GPU failure path remains unproven. Earlier runtime work covered OG/NG/AE, but this log does not retest all runtimes or heavily modded configurations.

Parent Main.cpp SHA-256: 75f473756404005431d4d5623d38ce2450944f7223ac5b959c21b5e3bea02e3a

Release-only source edits: replace log/version identifiers with 1.3.1; move PluginVersion.cpp's F4SE metadata/query exports into Main.cpp; explicitly compile Main.cpp. Shader source and runtime implementation otherwise remain byte-for-byte identical. Version metadata is 1.3.1.0. Final Windows rebuild and smoke test of the repackaged release are still needed; no DLL was compiled in this environment.

Preserve fixed mip choices (Default=2, Sharper=0, Softer=4), 128x128 capture, separate sky history, per-candidate exterior matching, stable binding and validated reference recovery unless the user requests a change. The bright-eye issue was reproduced with the mod disabled and is not an active plugin-specific investigation.

Seven standalone extracted/mock C++ tests pass locally. They are not part of the DLL. Public source identifiers, unchanged shader/runtime body, MCM equivalence and archive integrity were checked. SHA256SUMS.txt records source/configuration files for later comparisons. Suggested Git tag: v1.3.1. No GitHub publication was performed during packaging.
