# Realistic Reflections ¨C Glass compatibility test

Based on DynamicCubemapF4 1.3.1, commit `dd37eef92f4f1e918eb6806a84e275f0730653cb`.
This is an experimental source package, not a new public baseline. No DLL is included.
The plugin metadata and ordinary log header remain 1.3.1. Do not identify this test
solely by the version number; use this package's source/checksums and your rebuilt DLL.

## Build and install

Extract the complete `DCF4_GlassInterop01` folder into `E:\Coding\F4`.
From Visual Studio's **x64 Developer Command Prompt**, run this single line:

```bat
call "E:\Coding\F4\DCF4_GlassInterop01\Source\BUILD.cmd"
```

The existing self-locating script selects the project directory and supplies
`E:\Coding\F4\vcpkg\installed\x64-windows-static-md` for Detours. No separate
configuration command or PowerShell continuation characters are needed.
Requires the same MSVC/Windows SDK/xmake/dependencies as the 1.3.1 baseline.

Output:
`E:\Coding\F4\DCF4_GlassInterop01\Source\build\windows\x64\releasedbg\DynamicCubemapF4.dll`.
Replace the installed `Data\F4SE\Plugins\DynamicCubemapF4.dll` with that DLL,
preferably using a separate test mod in the mod manager. Keep a copy of the working
1.3.1 DLL for rollback. No MCM configuration changes are needed.

Use a Glass build implementing the supplied version-1 ownership protocol.
This patch cannot recognize Glass captures from a build that does not tag them.

After successful build, `PACKAGE.cmd` can create
`dist\DynamicCubemapF4-1.3.1-GlassInterop01-test.zip` for joint testing.
Do not upload this as the stable 1.3.1 player release.

## Changes and contract

- The supplied `ReflectionInterop.h` is copied unchanged. Its GUIDs and meanings
  match the Realistic Reflections developer's proposal in `01-1-.txt`.
- Uncached `RR_OWNED` views (or views whose underlying resource carries that tag)
  bypass substitution and both registration caches. We retain no peer-view reference.
- The slot's previously remembered **authored** view remains available during a
  temporary peer override. Restoring our stand-in therefore does not erase the
  original texture needed by binding repair, MCM disable and mip changes.
- Live and pooled replacement textures carry `RR_PEER_REPLACEMENT` before their
  views are created. Resource tags remain readable through recreated material views.
- Failed outgoing tag writes produce one warning per process and do not disable
  rendering. Glass may yield affected surfaces when a tag could not be written.
- Capture, shaders, tint, mip choices, rain override, runtime offsets and MCM files
  are unchanged. No additional GPU passes, readbacks or capture textures are added.

All peer ownership tags must be set before the resource/view's first intercepted
binding, stay immutable, and remain attached throughout its lifetime. Cached
authored views skip the ownership query. Tagged peer views are deliberately
uncached and queried on every intercepted bind; this is CPU overhead, not a
once-per-created-view check. Resource-only tags require additional interface and
resource queries. No measured frame-time claim is made.

Glass must restore the saved stand-in promptly after its draw, only if its own
image is still bound. This integration assumes the same immediate-context render
thread used by the existing DCF4 hook. It does not add synchronization for a peer
mutating bindings on another thread or holding a temporary override across a save
load, MCM settings application, resource reset, or deferred composite callback.

Forward glass with completed captures is the target. The supplied proposal leaves
opaque/deferred glass using DCF4's array to DCF4; it does not implement a composite
array. Only resources tagged with this protocol are exempted: no texture-name or
content-fingerprint exceptions are introduced.

## Check in game

1. Without Glass, compare metal/puddles and normal MCM off/on and softness changes
   with stable 1.3.1. The patch should not change their rendering.
2. With the matching Glass build, inspect forward glass before and after Glass's
   capture completes. DCF4 can supply a fallback first; Glass should then own that draw.
3. Test MCM off/on and all three softness settings while looking at glass and nearby
   non-glass metal. Confirm neither becomes black, loses reflection, or stays stale.
4. Repeat in rain and clear weather. The rain override is still scoped by DCF4's
   tracked replacement bindings; mixed peer/DCF4 slots and draw-hook nesting need
   actual testing. Private-data tags alone do not establish rain compatibility.
5. Move between cells, load another save, and compare both plugin loading orders if
   your test setup can control them. Hook chaining and driver behavior are unverified.

Once the enabled DCF4 binding path sees a tagged peer view, it logs once per process:

```text
INTEROP: RR-owned view observed; binding preserved.
```

This proves recognition, not successful Glass rendering. No such line is expected
without a tagged peer binding. An absent line can mean the wrong DLL, an untagged
Glass build, no eligible binding, or the DCF4 hook not being active.
Send `Data\F4SE\Plugins\DynamicCubemapF4.log`, its `.previous.log` if relevant,
and Glass's diagnostics. Mod managers may place generated logs in their output folder.

## Local verification and limitations

`python3 Tests/run_interop_test.py` extracts the current production binding and
repair routines and compiles them against portable D3D/COM mocks with g++ (or CXX).
It checks view/resource tags, cache exclusion, balanced peer references, repeated
temporary overrides, restored authored bindings, MCM gating, recreated mip views,
standalone pools, arrays and a failed tag write. The mock files are excluded from
the plugin target. The original seven standalone checks remain separate.

Mock checks passed. Shader strings and the MCM configuration were compared with
the base commit. These checks do not establish Windows compilation, GPU correctness,
performance, hook-order compatibility or OG/NG/AE runtime behavior. All three runtime
profiles are retained, but this integration needs joint in-game testing.

`DynamicCubemapF4-GlassInterop01.patch` contains only Main.cpp and the new shared
header changes relative to the base commit, for review/application to a clean 1.3.1
checkout. Do not apply it a second time to this already-patched package.
