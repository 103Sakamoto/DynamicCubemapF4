# Current release tests

Run `RUN_TESTS.cmd` from a Visual Studio x64 Developer Command Prompt, with Python 3.9+, Windows SDK, Visual Studio LLVM/clang++ and `DCF4_DETOURS` configured as in the main README. Set `DCF4_CLANG` if clang++ is not discoverable.

The sequential runner reuses existing production extractions and CPU/WARP fixtures. It covers:

- Accepted source/dependency/license identity and all thirteen shader manifests; default diagnostics/reprojection/preview behavior.
- Authored/peer binding ownership, refcounts, cache reuse, forwarding and disabled-startup reference observation.
- Full-chain replication pixel equality, retained-original reuse and source invalidation.
- Array streaming/upload/copy/discard epochs, incomplete/dark slots, stale probes and typed notification forwarding.
- Embedded shader creation, rollback/retry, guarded ImGui bytecode and compiler-error build rejection.
- Sky-only color masks, observed-radiance preservation, lifecycle/resource failures and graphics-state restoration.
- Optional exterior sky history, interior exclusion and reprojection toggles.
- Batched tint logging and zero sampled-clock/report activity when diagnostics is off.
- The Community Shaders profiler query ring, focused dispatch counters, reset/failure handling and hidden preview gating.

Output and logs go to `Tests/temp` or `Tests/bin`, excluded from packages and Git. Tests are separate programs, never plugin entry points. They do not launch Fallout 4, alter game files, or establish in-game correctness or hardware FPS.

Historical sibling-checkout diff scripts and generated native-code fixtures have been removed from the public tree. `release_scope.py` replaces those comparisons with an accepted-runtime hash manifest. Frozen reference HLSL remains test-only; runtime compilation is absent from the plugin.

## Optional runtime binary audit

`RUN_TESTS.cmd --runtime-binaries` also verifies exact OG/NG renderer and streaming bodies and exercises typed adapters/Detours against temporary local fixtures. Supply these environment variables:

- `DCF4_OG_EXE`: exact inspected OG executable matching `runtime-profiles/OG.json`.
- `DCF4_OG_DATABASE`: matching OG Address Library file.
- `DCF4_NG984_EXE`: exact unpacked inspection copy matching `runtime-profiles/NG984.json`.
- `DCF4_NG984_DATABASE`: matching NG Address Library file.
- `DCF4_ADDICTOL_DLL`: reviewed Addictol 1.7.0 DLL for the optional adapter test.

The test checks file hashes before using the fixtures. It does not load or execute game/Addictol code. No such binaries are included. A packaged Steam executable can differ from the unpacked inspection file; do not bypass a failed hash check. See `docs/VALIDATION.md` for the distinction between binary auditing and gameplay results.
