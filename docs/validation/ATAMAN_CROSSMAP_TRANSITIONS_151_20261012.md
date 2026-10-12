# EI-CROSSMAP-TRANSITIONS-151 — evidence

Date: 2026-10-12
Base: `main` `86c9928b13d4417a8934dee4394eb9e6d401b037`
VERSION: 151 (from 150)

## Behavior

Mixed-edge strokes plan material vertices across aligned map joins as one world surface, while each map writes tiles into its own atlas namespace. Shared border artwork is derived deterministically from the same world-cell key. “Fix transitions” stages seam cells from selected maps together, imports their material and edge sources, and preserves SEC/runtime classes. Misaligned map pairs remain excluded from a staged operation. A cohort stroke has one undo/redo record; save stages every changed MPR and publishes the shared RES once.

MPRs store per-map vertex material and height samples. The copied zone4/zone6 layout aligned 161 world XY boundary positions; 159 had slightly different height samples (maximum delta 0.000325189). This change reconciles boundary materials without rewriting heights.

## Copied real-map evidence

Fixture: `build-tests-151-crossmap-real/world-brush-seam` (zone4, zone6, copied `HDPack_Terrain.res`; no live files changed). The mixed-edge stroke crossed the join, saved and reopened both copies, and a single undo/redo restored/applied the two-map state. Seam repair then saved and reopened both maps. Final real integration took 350.6 s.

- Before image: [zone4/zone6 seam](ATAMAN_CROSSMAP_TRANSITIONS_151_20261012_before.png)
- After image: [zone4/zone6 seam](ATAMAN_CROSSMAP_TRANSITIONS_151_20261012_after.png)

Both large PNGs were opened and visually inspected. Pixel comparison after save/reopen: 0 mismatches across 125 non-SEC-excluded edge-cell pairs. Shared material vertices: 0 mismatches. SEC/runtime classes, authored slots, untouched interiors, and overlay presence passed. One cohort Undo/Redo restored physical payloads, semantic fields, and atlas state exactly. Repair was idempotent. Both copied MPRs reopened with the saved sectors intact; the shared copied RES was published once.

## Checks

- `terrain_virtual_atlas_tests`: 150 passed, 0 failed, 0 skipped (final source state before packaging).
- `world_terrain_sculpt_tests`: 46 passed, 0 failed, 5 optional installed-map skips.
- Mixed-edge OFF / one-map byte-identical 150 CLI and final Release UI widget check (`nativeAttempts=0`) are run after the source commit and recorded in the transfer update.

PNG SHA-256: before `52BAD10F7AE569544B0842D0F0E585B794D31156E954CCD8DA5D1ABF54063A0A`; after `A0602C4C7F99C4CBA90D55A03D6BF18A11525E5D89CED87B7659F6E797C1B86`.