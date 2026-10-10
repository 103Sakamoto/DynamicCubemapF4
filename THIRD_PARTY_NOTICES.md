# Credits, licenses and original code

## DynamicCubemapF4

Copyright (C) 2026 103Sakamoto. The existing project identifies its license as
GPL-3.0-only. The full license is included as LICENSE.txt. Modified Fallout 4
implementation and release changes are documented in CHANGELOG.md. This package retains the existing license choice and author credit.

## Skyrim Community Shaders ¡ª Doodlum and contributors

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
