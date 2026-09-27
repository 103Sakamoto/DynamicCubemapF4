# Protection inventory

Counted by purpose, not individual condition or polling loop: 10 groups in this release.

1. Runtime and hook preflight: reject unsupported runtimes or unexpected hook sites.
2. Gameplay activation recovery: confirm active gameplay if F4SE load notification was missed.
3. Named fallback recovery: reopen DDS when the native texture reference is missing, or validate a replacement candidate after sustained invalid samples; bounded retries and input/format validation.
4. Exterior identity and readiness: strict two-resolution sampled-content matching with per-candidate exclusion; block replacement while the reference is unready; preserve unresolved neutral array entries.
5. Standalone analysis readiness: retain original bindings for unavailable data and retry.
6. Array analysis and publication readiness: completed output stays bound during pending tint edits; initial output waits for analysis. preserve original texels for unavailable data; exempt positively identified exterior.
7. Resource and compute failure handling: clean partial initialization, retain originals when replacement unavailable, cap cached views/cubes.
8. Capture/history validity: fresh valid camera/projection checks, lifecycle reset, teleport/nonfinite movement history reset.
9. Rain override isolation and tracking: validate frame buffer identity/size/data, override only recognized plugin reflection draws, restore original binding; bounded binding audits repair tracking.
10. Graphics-state preservation: restore prior compute bindings and shader class instances after plugin passes.

Limitations: these protections are not a complete recovery guarantee. Existing-invalid-reference recovery is implemented; actual GPU failure-path validation is still needed. No general capture-stall watchdog is included. Recent AE sessions were user-tested; the 1.3.1 repackaged build still needs a Windows rebuild and smoke test.
