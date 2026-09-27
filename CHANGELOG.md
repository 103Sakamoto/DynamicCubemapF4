# Changelog

## 1.3.1
- Improve exterior fallback recognition when several copies appear in texture arrays, reducing session-dependent tint mistakes.
- Keep completed replacements bound while tint updates are pending, reducing unnecessary switches to original reflections.
- Preserve original textures while analysis data is unavailable.
- Remove custom full-face softening and use standard mip generation.
- Rework MCM softness: Sharper uses mip 0, Default mip 2, Softer mip 4. Changes preserve capture history.
- Recover from persistent invalid exterior-reference samples by validating a reopened candidate before replacing the reference.
- Reduce analysis readback copies to the results needed and improve shader/query failure diagnostics.
- Remove unused shader code and consolidate plugin implementation and version exports into Main.cpp.

This release retains the working capture, reprojection, separate sky history, tint/intensity policy and rain bypass. Public packaging introduces no rendering changes relative to the accepted ReferenceRecovery build.
