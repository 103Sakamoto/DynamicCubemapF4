# Performance evidence

Both comparisons below used the accepted RuntimeCompat01 implementation on Fallout 4 1.11.240, an RTX 4070, and the Vanilla mod-manager profile. Diagnostics and previews were off. Reprojection on / reprojection off / mod off repeated three times. All recorded frames were retained.

| Latest user runs, 2026-10-13 | Average FPS | 1% percentile FPS | 0.2% percentile FPS | Average FPS cost vs off |
|---|---:|---:|---:|---:|
| Reprojection on | 171.74 | 87.27 | 82.99 | 0.82% |
| Reprojection off | 172.08 | 87.15 | 82.90 | 0.62% |
| Mod off | 173.15 | 87.53 | 82.79 | - |

These nine captures lasted approximately 31-34 seconds. Comparing the first 30 seconds preserves the same ordering. Reprojection's small difference is below repeat-to-repeat variation. Enabled average FPS did not consistently decline; three short repetitions do not establish hours-long stability. The largest individual hitches, 56 ms and 41 ms, occurred with the mod off. Their cause is not established.

An earlier automated comparison used nine 60-second out-and-back walks with the same save reloaded for each run. It measured 169.87 FPS with reprojection, 169.88 without, and 173.97 with the mod off, about 2.4% overhead. The baseline varied, and one disabled run included a 256 ms hitch. Different movement and duration mean the two series do not prove a software improvement or regression.

VSync was active (SyncInterval=1) in both series. Results describe presented gameplay FPS in these configurations, not isolated shader execution or an uncapped performance guarantee. Percentile FPS is the reciprocal of the corresponding frame-time quantile, not the average FPS of the slowest fraction of frames.

The 1.5 cleanup preserves the same shader bytecodes and active runtime operations. Removing an uncalled wrapper and obsolete files is not claimed as an additional FPS improvement. Raw user captures and machine-specific telemetry are intentionally not redistributed.
