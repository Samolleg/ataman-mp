# Ataman193 — EI-BASE-TILES-HD-COVERAGE-193 — 2026-10-16

Scope: all 41 DejaVu maps only. Original maps/resources are read-only; saves target private copies. Other game profiles are excluded from this delivery.

## Cause and correction

The original matcher required RGB MSE <=64 and a uniqueness margin even after the bound atlas profile had established the material. Native HD detail and visually similar originals failed that test. A second filter required the donor physical class to equal the destination class. Both unnecessarily rejected ready HD assets.

Material identity now selects the bank; pixel distance ranks original boundaries/orientations without vetoing an established material. Destination physical classes are retained. All ready variants with an identical protected boundary are pooled across originals and donor classes. Different boundaries retain nearest-anchor selection to avoid introducing seams. Water class6, water/underwater families, animated slots, overlay-used slots and nonuniform transition signatures remain excluded. Existing MMP pages remain byte-identical. No new transition baker is introduced.

## Corpus checks

41/41 maps:335128 recognized base cells matched,0 unmatched. Old190 rejected55709 of these. zone19 now38014/38014, including former18343 omissions. All41 passed apply/Undo exact/Redo exact, private serialization and reopening of MPR+RES, original-page byte equality, unchanged overlay/materials/animations and destination physical classes. These counts describe recognized uniform base cells, not every terrain cell.

Private receipts: build-base-hd-192-20261016/dejavu-fixed-results/audit.json and dejavu-invariants-results/audit.json. Reproducible harness: tests/terrain_base_hd_corpus_check.cpp/.pro; EI_HD_GAME_ROOT, EI_HD_LIBRARY and EI_HD_AUDIT_OUTPUT configure private fixtures.

| DejaVu map | Base cells converted | Rejected by190 | Save/reopen invariants |
|---|---:|---:|---|
| basegipat.mpr | 2227 | 2 | PASS |
| bz10k.mpr | 2484 | 0 | PASS |
| bz11k.mpr | 2192 | 0 | PASS |
| bz13h.mpr | 2083 | 0 | PASS |
| bz14h.mpr | 2041 | 0 | PASS |
| bz15h.mpr | 1958 | 0 | PASS |
| bz16h.mpr | 2065 | 0 | PASS |
| bz18h.mpr | 2028 | 0 | PASS |
| bz19g.mpr | 2283 | 0 | PASS |
| bz2g.mpr | 1002 | 0 | PASS |
| bz3g.mpr | 2470 | 0 | PASS |
| bz4g.mpr | 1599 | 0 | PASS |
| bz5g.mpr | 1355 | 0 | PASS |
| bz6g.mpr | 1267 | 0 | PASS |
| bz7g.mpr | 1898 | 0 | PASS |
| bz8k.mpr | 1453 | 0 | PASS |
| bz9k.mpr | 1817 | 0 | PASS |
| zone1.mpr | 14391 | 3000 | PASS |
| zone10.mpr | 3378 | 0 | PASS |
| zone11.mpr | 23453 | 15596 | PASS |
| zone12.mpr | 24233 | 0 | PASS |
| zone12dun2.mpr | 2958 | 85 | PASS |
| zone13.mpr | 19148 | 13570 | PASS |
| zone14.mpr | 6434 | 836 | PASS |
| zone15.mpr | 16133 | 0 | PASS |
| zone16.mpr | 6637 | 0 | PASS |
| zone17.mpr | 18537 | 3220 | PASS |
| zone18.mpr | 37964 | 900 | PASS |
| zone18dun1.mpr | 3824 | 0 | PASS |
| zone19.mpr | 38014 | 18343 | PASS |
| zone2.mpr | 1837 | 3 | PASS |
| zone20.mpr | 4186 | 0 | PASS |
| zone3obr.mpr | 12713 | 0 | PASS |
| zone4.mpr | 15800 | 8 | PASS |
| zone5_1.mpr | 5705 | 0 | PASS |
| zone6.mpr | 7191 | 0 | PASS |
| zone6_2.mpr | 7325 | 0 | PASS |
| zone7.mpr | 9496 | 2 | PASS |
| zone8.mpr | 8752 | 0 | PASS |
| zone9.mpr | 11928 | 4 | PASS |
| zonemainmenunew.mpr | 2869 | 140 | PASS |

## Delivery status

Native production-command verification on a private zone19 copy passed147 checks:38014 changed cells,59 distinct HD outputs,72 per-source variant entries; exact GPU readback, Undo/Redo before/after Save, two Saves and reopen, original MMP pages unchanged. Receipt: build-base-hd-192-20261016/results/receipt.json (146 assertions plus receipt-write assertion). Fresh Release193 built from accepted192 plus scoped HD changes, including both runtime components. Final native test linked to these Release193 objects also passed147 checks/exit0. The reusable corpus harness compiles against Release193. Installation is pending. Owner visual/game acceptance remains separate.
