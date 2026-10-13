# Accepted public 1.5 baseline

Release 1.5 continues accepted RuntimeCompat01 / SkyColor01 / BindCPU01 / GameMip02 behavior. Its runtime cleanup removes only the uncalled UniversalReplicatePool wrapper; active publication uses UniversalReplicationScope and UniversalDispatchPool directly. Release version, startup banner and overlay title are updated. MCM help now describes the existing Ctrl+F10 preview control.

All thirteen embedded shader bytecodes and compiler settings, capture resolution, eight mips, authored tint/reflectiveness, exposure calibration, sky-only exterior fallback, interior inference, source validation, streaming epochs, cache budgets, full-chain pool kernels, lighting and scheduling remain identical to the tested candidate. Dependencies and licenses remain intact. Tests/accepted_runtime_identity.json and Tests/release_scope.py guard this boundary without requiring historical sibling checkouts.

Diagnostics and reprojection default OFF. Graph and previews begin hidden when diagnostics initializes; previews perform no work while hidden. Saved MCM preferences still apply. Disabling reflections restores authored bindings and stops active reflection work, retaining guarded passive reference observation for re-enable.

Reject unsupported runtimes and unknown hooks. Preserve exact OG163/NG984/AE240 route checks, the reviewed Addictol loader checks, typed forwarding, original binding restoration, all COM references and the Realistic Reflections ownership protocol. No synchronous GPU waits or altered scheduling were introduced by this release cleanup.

Game mip selection and full-chain views are intentional. Do not restore the obsolete fixed-mip controls, native sky lookup, sky-gradient experiment, brown interior cube or old shader variants. Keep relevant CPU/WARP behavior tests; obsolete one-experiment comparison scripts are not part of current validation.
