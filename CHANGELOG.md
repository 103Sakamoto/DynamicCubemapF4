# 1.4

- Five fixed-mip presets: Sharpest 0, Sharper 1, Neutral 2 (default), Softer 3, Softest 4.
- Updates only the selected replacement-pool mip; retains views for stable switching.
- 1024-entry clock cache and faster replacement lookup.
- Optional diagnostic mode, off by default; other MCM controls hide when disabled.
- Fixed enabling after starting disabled by observing the original fallback texture's initial load.
- Preserves capture calibration, color tint without desaturation, authored reflectiveness, BGEM exterior recognition and shared Glass ownership protocol.
- Cleaned release guidance; experimental history moved to docs/history. Separate runtime-only and source/license packages.

Upgrade: replace DLL and MCM files. New sharpness key starts at Neutral; existing diagnostic preferences remain user-controlled.

Validation: optimized Windows build and local binding, five-level dispatch, fallback startup, and WARP diagnostics tests passed. No new in-game test of the five-option menu has been performed.
