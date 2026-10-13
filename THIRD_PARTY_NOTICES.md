# Credits, licenses and original code



## Sky-color reduction

The exterior sky-color reduction adapts the 256-thread D3D11 shared-memory tree
reduction from Community Shaders' features/IBL/Shaders/IBL/DiffuseIBLCS.hlsl
(dev source consulted 2026-10-12). It reduces captured sky RGB and sample counts;
the spherical-harmonics/IBL framework is not ported. Missing-direction blending
uses Dynamic Cubemaps' REFLECTIONS inference weight. Original upstream terms and
exceptions remain in Licenses/CommunityShaders. Fallout 4 source selection,
viewport/depth guards, lifecycle and binding adapters are local integration.

https://github.com/community-shaders/skyrim-community-shaders/blob/dev/features/IBL/Shaders/IBL/DiffuseIBLCS.hlsl

https://github.com/community-shaders/skyrim-community-shaders/blob/dev/features/Dynamic%20Cubemaps/Shaders/DynamicCubemaps/InferCubemapCS.hlsl

## DynamicCubemapF4



Copyright (C) 2026 103Sakamoto. The existing project identifies its license as

GPL-3.0-only. The full license is included as LICENSE.txt. Modified Fallout 4

implementation and release changes are documented in CHANGELOG.md. This package retains the existing license choice and author credit.



## Skyrim Community Shaders - Doodlum and contributors



https://github.com/doodlum/skyrim-community-shaders



The cubemap capture/update and inference work is adapted from or inspired by

Community Shaders' Dynamic Cubemaps. It is not wholly original independent work.

Upstream identifies GPL-3.0-or-later with a Modding Exception and a GPL linking

exception. Original COPYING and EXCEPTIONS.md are in Licenses/CommunityShaders.

Preserve the original terms and exceptions for the upstream portions.



The historical upstream revision used for the initial port was not recorded.

No upstream logo or default DDS asset is redistributed.



## Dear-Modding-FO4/CommonLibF4 and commonlib-shared



https://github.com/Dear-Modding-FO4/commonlibf4

https://github.com/Dear-Modding-FO4/commonlib-shared



Bundled unmodified source snapshots are in Source/lib/CommonLibF4. Original GPL

licenses and modding/linking exceptions remain with those sources; convenience

copies are in Licenses/CommonLibF4 and Licenses/commonlib-shared. Authors and

contributors retain their existing copyright notices.



## DearModdingUI-API and Phosphor Icons



https://github.com/Dear-Modding-FO4/DearModdingUI-API



These are part of the bundled CommonLib source tree. Their original license

texts remain in place, with copies in Licenses/DearModdingUI-API and

Licenses/PhosphorIcons. Bundling this dependency tree does not mean the plugin

uses every UI asset at runtime.



## Microsoft Detours



https://github.com/microsoft/Detours



Copyright (c) Microsoft Corporation. MIT license; see Licenses/Detours/LICENSE.md.

The build uses the external Detours installation configured in BUILD.cmd.



## spdlog and fmt



https://github.com/gabime/spdlog

https://github.com/fmtlib/fmt



spdlog copyright (c) 2016 Gabi Melman; MIT license. License texts for spdlog

and fmt are included in Licenses. xmake resolves the dependency declared by

the bundled CommonLibF4 build recipe.



## Fallout 4 Mod Configuration Menu



https://github.com/reg2k/f4mcm

https://github.com/Neanka/MCM_0.1_AS3

https://www.nexusmods.com/fallout4/mods/21497



Credit to the MCM authors, including reg2k, Neanka and shadowslasher410. Its

settings/UI source was consulted for the independent configuration integration.

MCM binaries/scripts are not bundled or relicensed here; install MCM separately.



## Game and platform



Fallout 4, Bethesda game assets, F4SE runtime downloads, the Windows SDK and

Microsoft's compiler/runtime components are not included in this package.

Their owners retain their respective rights. This project is not an official

Bethesda product.





## Profiler and diagnostic integration



CS Profiler.h/Profiler.cpp and the CS-adapted LegitProfiler.h are reused from

community-shaders/skyrim-community-shaders commit

b81bf11cf126f41aaa53d7318b61293677d9c0cc (Doodlum and contributors).

Profiler adaptations: WRL COM pointers; 16 timer slots; checked initialization;

no reuse of unresolved queries; complete-batch nonblocking collection; timestamp

validation; explicit pass lifetime/reset; result sequence accessor. PoolTrace01

adds caller submission tags and recording/completed-tag accessors. DispatchTrace01

adds optional D3D11 pipeline counters to that existing ring, per-dispatch invocation

data and query result age. Timestamp collection, retirement and rolling statistics

remain unchanged apart from nonblocking collection of the optional counter. Its

existing performance-event callbacks are reused for native D3D11 annotations. The graph's

legend text uses a single string to avoid overlap at this overlay font size.

CS menu/localization/framework code is not linked. Original terms and exceptions

are included under Licenses/CSProfiler. CS's graph is based on Raikiri's

MIT-licensed LegitProfiler; its original license is included there as well.



Dear ImGui v1.91.9b by Omar Cornut and contributors is bundled under its MIT

license. Source and the DX11 backend are in Source/lib/imgui; license in

Licenses/ImGui. The Win32 backend is not used. The overlay uses hotkeys without

intercepting the game's window procedure or mouse input.



## Runtime compatibility inspection and adapter

The existing named-SRV getter follows Fallout 4 Community Shaders
TextureSrv in src/Effects/CloudLayerSource.cpp (GPL-3.0), with additional
exact-runtime, typed-interface, ownership, resource and device checks.
Native OG/NG renderer disassembly independently confirms the convention.
No game code, Steamless inspection tool or Addictol implementation is bundled.
Official Steamless was used only to inspect a workspace copy of NG code;
see https://github.com/atom0s/Steamless and docs/VALIDATION.md.
