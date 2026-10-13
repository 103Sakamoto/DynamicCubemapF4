# Completed release checks - 2026-10-13

PASS: optimized MSVC x64 releasedbg DLL build.

PASS: built DLL exports Load, Query and Version; packed version is 1.5.0, and exact compatible runtime versions remain OG163/NG984/AE240. The only D3DCompiler import is the unchanged D3DCreateBlob allocator; there is no D3DCompile import.

PASS: accepted identity audit; all thirteen embedded shader bytecodes unchanged from RuntimeCompat01. Only an uncalled wrapper, version/banner/title and diagnostic help differ.

| Regression step | Result |
|---|---|
| release_scope.py | PASS |
| run_interop_test.py | PASS |
| run_performance_test.py | PASS |
| run_enable_test.py | PASS |
| run_bind_cpu_tests.py | PASS |
| run_array_acceptance_test.py | PASS |
| run_stream_cache_test.py | PASS |
| run_stream_audit_test.py | PASS |
| run_tint_cache_test.py | PASS |
| run_precompiled_tests.py | PASS |
| run_tint_batch_tests.py | PASS |
| run_optional_sky_tests.py | PASS |
| run_sky_color_tests.py | PASS |
| run_dispatch_trace_tests.py | PASS |
| array_guard_test-build | PASS |
| array_guard_test | PASS |
| array_write_test-build | PASS |
| array_write_test | PASS |

The suite reuses existing production CPU/WARP fixtures. Two historical mocks were brought up to date for the already-accepted legacy-view adapter and lazy logger; no production operations changed. Generated outputs and private logs are excluded.

The original OG executable used for the previous full binary audit is no longer available at its recorded path on this machine, so that optional full-file audit was not repeated for this packaging pass. Exact profiles and checked adapters remain covered by the unchanged-source audit and prior runtime validation. The optional audit runner is retained for user-supplied binaries.

Mock/WARP/identity checks do not establish new gameplay testing or eliminate all driver/runtime/mod-stack risks. See VALIDATION.md for accepted gameplay results.
