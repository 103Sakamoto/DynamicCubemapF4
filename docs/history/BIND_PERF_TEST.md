# BindPerf01 CPU binding optimization

Based on TintCapture02, not the unbuilt MipPerf02 edits.

Replacement view recognition now uses a borrowed-pointer hash table mapping current and retained mip views to the current material view. Rebuilds occur at pool allocation, mip commit, live texture recreation, and reset. ComPtr ownership remains with the existing resources and mip caches. Ordinary texture bindings do not rebuild the table.

BindingResult carries both the resolved view and its replacement status. The PS resource hook and binding repair reuse that status instead of checking the result again. Glass-owned views retain their existing bypass and are not cached in this table.

No shader, capture resolution, update cadence, MCM, mip-generation, tint, or intensity changes. Existing Default=2, Sharper=0, Softer=4 remains. Full binding audits are unchanged.

Validation: extracted production binding tests include a one-lookup assertion, old mip aliases, removed-alias cleanup, standalone named exterior, Glass overrides, toggles, arrays and reference balance. Performance dispatch mocks also run. These do not establish an FPS gain or in-game compatibility.

Install instead of TintCapture02 and restart. Compare the same performance route, check rainy reflections and Glass, cycle sharpness, toggle MCM, and load another save. Send the log and benchmark results.
