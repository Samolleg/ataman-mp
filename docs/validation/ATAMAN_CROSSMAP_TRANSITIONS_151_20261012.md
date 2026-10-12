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

- `terrain_virtual_atlas_tests`: 150 passed, 0 failed, 0 skipped; `world_terrain_sculpt_tests`: 46 passed, 0 failed, 5 optional skips.
- Final Release `--terrain-mixed-edge-check`: 70 passed, 0 failed, 0 skipped in 67.4 s. The offscreen QPA attempt stopped before checks because the clipboard isolation adapter requires Windows QPA; retry passed with `nativeAttempts=0`, unchanged sequence and formats.
- Mixed-edge OFF, one-map output: VERSION 150 and 151 each serialized 319,029,944 bytes; both SHA-256 `046FBFA4A828E8586DEF6FE511A91D59CD7DCBE8FD25B576F8E33C012D70B51B` (byte-identical).
- Final Release `--ui-widget-check`: pass in 11.1 s; clipboard `nativeAttempts=0`, `unchanged=1`. The required widget command includes its built-in `darkThemeWorks=1` assertion; no separate dark-theme suite ran.

## Release and installation

Source commit: `0ec9e6c27590f6210b97bb23eb87d7425033fce1` (`main`). Release 151 was built from its clean `git archive`; staged `--version` reported `EI Mapper Endurance Edition 151`. Tested, staged and installed EXE SHA-256: `CF6362BADD49BBC2CB863F574BE8FFCFA904DDE49B289740C844C2F6ECCA6E0C`.

Fresh staging: `build-staging-151-crossmap-verified`; finalizer/validator passed, 132 files, 130 prior paths, one explicitly allowed removal of obsolete `ATAMAN_MOB_LOAD_148_20261012.md`, 0 unexplained removals. SHA256SUMS manifest SHA-256: `8EDF943A775B5B03F05335F7D1790CB9057571070A0559EFBB1B977976410C02`.

Installed 150→151 at `artifacts/EI-Mapper-Ataman-2-first-test` in 19.2 s. The process check found no running `EIMapperEndurance.exe`. Nine managed files changed, one obsolete managed changelist was removed, and all 23,034 unmanifested/user files matched their before-install inventory (inventory SHA-256 `8E327CC9D982D911CF58D74D275C0D7718F9B715B293F373B10C5C3012332257`). Rollback150 is retained at `artifacts/rollback150-EI-CROSSMAP-TRANSITIONS-151-20261012`. The first guarded attempt rolled back after a PowerShell JSON-array shape mismatch; the corrected readback and install passed. Final evidence is committed with the transfer notes.

Durations: real copied-map seam integration 350.6 s; final mixed-edge CLI 67.4 s; UI widget 11.1 s. No editor/game acceptance is claimed.

PNG SHA-256: before `52BAD10F7AE569544B0842D0F0E585B794D31156E954CCD8DA5D1ABF54063A0A`; after `A0602C4C7F99C4CBA90D55A03D6BF18A11525E5D89CED87B7659F6E797C1B86`.
