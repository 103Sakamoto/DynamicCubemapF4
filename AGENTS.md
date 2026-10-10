# Project continuity

Use release 1.4 as the user-selected baseline. Preserve its Realistic Reflections ownership protocol. Read BASELINE.md before editing. Preserve verified rendering behavior for unrelated changes.

Windows workspace root: E:\Coding\F4. Detours: E:\Coding\F4\vcpkg\installed\x64-windows-static-md. BUILD.cmd is self-locating and is run from a Visual Studio x64 Developer Command Prompt. Never give PowerShell backtick continuations for cmd.exe.

Plugin code lives in Source/src/Main.cpp; Tests contains standalone mocks with their own main functions and must not be compiled into the plugin. Keep external dependency sources and licenses intact. Tests do not establish Windows compilation or in-game correctness. Put release history in documentation, not rendering comments or log banners.
