# Protection inventory

1. Exact runtime and hook preflight rejects unsupported or altered routes.
2. Gameplay activation recovery confirms active gameplay when a load notification was missed.
3. Named fallback recovery uses bounded retries and validated source identity/format.
4. Exterior identity/readiness retains original bindings while reference matching is pending or ambiguous.
5. Standalone material analysis retains originals when data is unavailable and retries.
6. Array analysis/publication preserves original texels for unresolved entries and validates streaming/write epochs before reuse.
7. Partial resource/shader failures clean up and retain authored bindings; views, cubes and caches remain bounded.
8. Capture/history checks validate camera, projection and movement, and reset on lifecycle changes, teleports and nonfinite data.
9. Reflective-draw buffer overrides validate identity/data, restore the original binding and use bounded reconciliation.
10. Compute passes restore previous shaders/class instances, resources, UAVs and constant buffers, retaining the existing peer-ownership protocol.

These safeguards remain unchanged from the accepted candidate. They are not a guarantee of recovery from every driver failure or incompatible mod. Diagnostics queries collect without flushing; unknown texture-streaming hooks prevent event-only caching rather than weakening validation.
