# Project continuity

Use public 1.5 as the accepted baseline. Read BASELINE.md before editing. Preserve the verified shaders, ownership protocol, source/cache safety, full mip chains, lighting and scheduling unless the user explicitly requests a change.

Plugin code lives in Source/src. Tests are standalone mocks/WARP/inspection programs and must never be compiled into the plugin. Keep bundled dependency sources, copyright notices and licenses intact.

BUILD.cmd is self-locating and runs from a Visual Studio x64 Developer Command Prompt. Use DCF4_DETOURS and DCF4_PYTHON for external installations. Never give PowerShell backtick continuations for cmd.exe.

Use RUN_TESTS.cmd for current portable regression checks. Optional binary audits require user-provided paths; never redistribute game or third-party executable bytes. Tests do not establish in-game correctness or GPU benchmark results. Put development history in documentation, not rendering comments or startup banners. Do not overwrite existing release tags.
