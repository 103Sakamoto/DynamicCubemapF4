# TintCapture02 standalone exterior binding fix

WaterCooler_Dirty.BGEM references Shared/Cubemaps/mipblur_DefaultOutside1.dds.
The named reference probe does not mark standalone material entries analyzed. Those entries could stay static indefinitely despite a valid live cube. Alternate views sharing that named resource could also be excluded from analysis without becoming bindable.

Before rejecting an unanalyzed standalone binding, recognize the named exterior resource and allow the calibrated live view directly. No extra tint or output scaling. Ordinary analyzed cubemaps, array styling, capture shaders and Glass-owned views retain their previous behavior.

Regression checks cover the unanalyzed named binding, waiting for live capture, restoring the original, and reapplying the live view. Windows build and in-game testing are separate validation steps.

Replace TintCapture01, restart Fallout 4, and revisit the water cooler. Move/rotate to populate capture and compare MCM enabled/disabled. Expect the log: TINT BIND: named standalone exterior uses calibrated live cube; no output scaling.

The remaining TintCapture01 settings and limitations below still apply.
# TintCapture01 test
Based on ExteriorBalance02; retains softness-view ownership repair and Glass interop.

- Capture calibration: fixed 0.25 (-2 EV), applied once to fresh geometry and sky RGB before history accumulation. This is empirical calibration, NOT automatic exposure measurement or inverse engine exposure.
- Authored fallback texels are not capture samples and are not multiplied by this factor. Transitions between fallback and captured data require in-game verification.
- Recognized exterior: output intensity 1; no tint.
- Other consistent cubemaps: luminance-preserving RGB tint, no grayscale blend. Color consistency threshold 0.72; saturation no longer grants a consistency exemption. Existing uncertain-identity safeguards remain.
- Authored reflectiveness: sqrt(luminance), clamped to 0.02..1 (previous floor 0.15). Eye probe 0.004806 now yields about 0.0693 instead of 0.15. The square-root mapping is retained for this scoped test.
- This authored reflectiveness still multiplies RGB; material alpha/transparency is unchanged. It is separate from capture exposure calibration.

Replace prior test package and restart the game to clear history. Compare eyes/puddles, exterior metal, and neutral reflections in the same daylight scene; then test night/interiors, rotation into uncaptured directions, and all three softness settings. Send the log and note whether capture/fallback transitions change brightness visibly.

No in-game verification has been performed for this build.
