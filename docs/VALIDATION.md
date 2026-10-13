# Release validation

## Accepted gameplay implementation

Before the 1.5 cleanup, RuntimeCompat01 was tested using the Vanilla mod-manager profile:

- AE 1.11.240 / F4SE 0.7.9: MCM disable/re-enable, exterior reload, interior save load and actual interior-to-exterior transition passed.
- NG 1.10.984 / F4SE 0.7.2: capture, disable/re-enable through the polled settings file, interior reload and interior-to-exterior transition passed. The installed AE MCM DLL was incompatible with NG, so NG menu integration was not established.
- OG 1.10.163: functionality was confirmed by the user. This is not a separate record of every toggle/reload/transition case. Automated OG launches previously failed before F4SE initialization, with no new plugin log.

Both successful automated runtime sessions verified all ten streaming routes with reviewed Addictol 1.7.0 and recovered live publication after resets. The original reporter's exact missing-reference mod setup remains unverified.

Two nine-run CapFrameX series are summarized in PERFORMANCE.md. Performance is route/configuration dependent. No claim of universal compatibility, zero stutter or hours-long stability is made.

## 1.5 cleanup boundary

The only runtime source changes from that accepted build are removal of the uncalled UniversalReplicatePool wrapper, version metadata/banner, and overlay title. The MCM diagnostic help documents the existing preview hotkey. All thirteen shaders and active rendering/safety/cache/lighting/scheduling operations are protected by the accepted identity audit. Dependency sources and licenses are unchanged.

The public tree excludes old experiment wrappers, stale comparison scripts, temporary binaries, raw captures/logs, generated native game-code fixtures and obsolete patches. Current behavior fixtures and build/package tools are retained and made independent of sibling experiment checkouts.

The completed release build and regression results are recorded in RELEASE-CHECKS.md. Mock/WARP/static checks are not new in-game tests of the release label.
