# Ataman196 — EI-BASE-HD-SEAMS-196 — 2026-10-16

## Contract and implementation

Owner requested checks against neighboring base tiles/transitions, unchanged water/transitions, DejaVu copies, build and installation. When strict rejection left no useful HD coverage, the owner explicitly chose original rims with HD interiors. This decision replaces rejection-only as the primary strategy; rejection remains a final guard.

HD variants are rotated inside the texture before attaching the original rim. The MPR cell rotation is preserved. The outer24 pixels retain the original image;24–48px feather inward to HD. These are base-tile interiors only; no transition-generation kernel is restored. Existing water, overlay-used slots, animation frames, nonuniform signatures, original MMP pages and physical properties remain unchanged.

Original DXT palettes and pixel indices are copied, or indices nearest-expanded for128→256, without quantizing the original colors again. The complete prepared tile encoding is retained in the working atlas staging/commit and passed to normal Save and isolated playtest staging. The writer uses it only while its decoded pixels exactly match the current working tile; a later intentional pixel edit invalidates that override. Ordinary atlas saving with no override retains its behavior. Original pages are never recompressed by this path.

Checks use the renderer's1/16 UV inset, packed cell orientation and bilinear samples at every half texel of the common display resolution. Every changed edge must equal its original edge. Neighbor edge energy, peak difference and channel bias must not grow. Both preview and actual saved DXT base-level pixels are checked. Unknown external neighbors require an unchanged map boundary. Unsafe replacements are removed and neighboring checks revisited until stable; skipped cells have a separate visible counter. This is not a claim about every mip/distance/shading condition; owner visual/game acceptance remains separate.

## Verification

- Native Release196:348 PASS/exit0 on private zone19; actual conversion and Cancel button mouse events, unchanged state/Undo on Cancel,38014 converted cells,0 unmatched/0 seam-rejected,239 distinct outputs,288 per-source variant entries. Every converted visible rim exact before and after Save/reopen, GPU readback, Undo/Redo before/after Save, two Saves and original pages byte-identical. Receipt: build-base-hd-196-20261016/results/receipt.json (347 assertions plus receipt-write assertion).
- QtTest:7 PASS/0 FAIL/0 SKIP; mixed native pages, original-page preservation, authored tail edits, physical padding and multi-page output without overrides.
- Synthetic checker distinguishes unused gutters from the visible edge and rejects a rotated stripe that introduces a seam.
- Full41-map DejaVu corpus:41/41 PASS,335128 converted cells,0 unmatched and0 seam-rejected. Checks include every changed rim and all neighboring edges in preview and decoded saved MMP pages, unchanged overlays/water materials/animations/types, Undo/Redo and MPR/RES reopen. Private receipts: build-base-hd-196-20261016/hybrid-corpus-results/audit.json.

Fresh Release196 and both runtime components are built on accepted195. Moving the shared DXT helper into resources left the tested executable hash identical. Installed196: source main87fa08254f2b3fc02c518eb0632f90e3994fa607;651 compiler/resource inputs verified. Raw/staged --version196, finalizer and130 installed managed files PASS. All17384 owner files preserved byte-for-byte. EXE084800E9A8CFF6AC31995A11ADCF7EDFC141A382EDB0B44DC74435C33FE5F232; manifest23FBE8D8BFB49868948FE576F85EB8CD43412F55773FBEEB52D1E2CA1BC6FC86. Retained rollback195-to196-base-hd-20261016 is reconstructable; only the previous identified194 backup retired. Receipts: build-base-hd-196-20261016/{build-receipt,staging-receipt,install-receipt,rollback-retirement}.json. No GitHub push. Live maps/resources are read-only and all writes target owned private copies.
| DejaVu map | Converted | Rim/neighbor/Save checks |
|---|---:|---|
| basegipat.mpr | 2227 | PASS |
| bz10k.mpr | 2484 | PASS |
| bz11k.mpr | 2192 | PASS |
| bz13h.mpr | 2083 | PASS |
| bz14h.mpr | 2041 | PASS |
| bz15h.mpr | 1958 | PASS |
| bz16h.mpr | 2065 | PASS |
| bz18h.mpr | 2028 | PASS |
| bz19g.mpr | 2283 | PASS |
| bz2g.mpr | 1002 | PASS |
| bz3g.mpr | 2470 | PASS |
| bz4g.mpr | 1599 | PASS |
| bz5g.mpr | 1355 | PASS |
| bz6g.mpr | 1267 | PASS |
| bz7g.mpr | 1898 | PASS |
| bz8k.mpr | 1453 | PASS |
| bz9k.mpr | 1817 | PASS |
| zone1.mpr | 14391 | PASS |
| zone10.mpr | 3378 | PASS |
| zone11.mpr | 23453 | PASS |
| zone12.mpr | 24233 | PASS |
| zone12dun2.mpr | 2958 | PASS |
| zone13.mpr | 19148 | PASS |
| zone14.mpr | 6434 | PASS |
| zone15.mpr | 16133 | PASS |
| zone16.mpr | 6637 | PASS |
| zone17.mpr | 18537 | PASS |
| zone18.mpr | 37964 | PASS |
| zone18dun1.mpr | 3824 | PASS |
| zone19.mpr | 38014 | PASS |
| zone2.mpr | 1837 | PASS |
| zone20.mpr | 4186 | PASS |
| zone3obr.mpr | 12713 | PASS |
| zone4.mpr | 15800 | PASS |
| zone5_1.mpr | 5705 | PASS |
| zone6.mpr | 7191 | PASS |
| zone6_2.mpr | 7325 | PASS |
| zone7.mpr | 9496 | PASS |
| zone8.mpr | 8752 | PASS |
| zone9.mpr | 11928 | PASS |
| zonemainmenunew.mpr | 2869 | PASS |
