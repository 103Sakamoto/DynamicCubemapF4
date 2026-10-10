# 1.4 validation
Run run-local-tests.cmd for binding/cache/mip restoration, all five dispatch levels and disabled-startup observation. test-diagnostics.cmd uses a WARP D3D11 device. Scripts currently specify the development machine tool paths; adjust these for another installation. Older notes below are historical.

# Developer checks

`run_performance_test.py` extracts the current pool replication and rain draw routines. Its CPU mocks verify selected mip dispatch dimensions and bindings, state/reference restoration, tint upload reuse, dry/invalid/nonfinite/rain snapshots, and draw forwarding. Run with Python and a C++20 compiler selected through CXX. It does not test GPU output or the complete MCM view-creation transaction.

These seven .cpp files are standalone extracted/mock tests, each with its own main() and mock platform objects. They are deliberately separate from Main.cpp and excluded from the xmake plugin target. They do not run in game and do not belong in a player download. Merging them into the plugin would introduce conflicting declarations/entry points.

Compile each separately with a C++20 compiler and run it. They cover DDS loading, encoding, sampled matching, readiness/publication, recovery timing and candidate commit decisions. They are snapshots: changes to the production routines must also update the extracted test code. These checks do not exercise the real D3D11 driver or F4SE runtime.

The additional `interop_test.cpp` uses `run_interop_test.py` to extract current
production routines and compile them against the doubles in `interop_stubs`.
Run `python3 Tests/run_interop_test.py` from the repository root with g++ available
(or set CXX). Do not compile this test by itself or add its stub include path to
the plugin build. See INTEROP_TEST.md for coverage and runtime limitations.

Run test-exterior-balance.cmd to compile extracted production classification/intensity expressions against CPU doubles. Updated interop tests cover exterior pool-capacity and publication fallback.
