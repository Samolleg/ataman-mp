# EI-TERRAIN-ATLAS-HDPACK-52 — validation

Date: 2026-09-23
Base: Ataman 51, `8b03699774cedab87c8ae59aeefa4f7c8d453f07`
Branch: `codex/terrain-atlas-hdpack-v52`

## Implemented behavior

- Modern draft saves merge the exact atlas pages, embedded material profile and
  semantic field into the configured `HDPack_Terrain.res`.
- Finalized modern MPR publication writes its pages/profile to that same archive;
  map-local `*-atlas.res` publication and synchronization were removed.
- Opening maps prioritizes a configured HDPack over stale per-map copies.
- Exact recovery snapshots merge into the configured HDPack and preserve other
  archive members. Replacing differing shared members requires user confirmation.
- Existing map-local atlas archives remain available as a legacy read fallback;
  they are not written by these save paths.

## Checks

- `git diff --check`: PASS before commit.
- Rebuilt from source commit `74adbecabcfae5911057c0497d71e69c71468557`
  using the retained Qt 5.12.12 MinGW 7.3 kit and fresh shadow directory
  `build-release-52-terrain-atlas-hdpack-rebuild`: PASS.
- Staged `--version`: PASS, output `EI Mapper Endurance Edition 52`.
- Fresh package `build-staging-52-terrain-atlas-hdpack`: finalizer PASS;
  all 101 files are covered by valid SHA-256 entries. Staged executable hash
  matches the fresh build: `2e43c2e1aa99915555069d802882b29cf67ca3d04263a46a8d18727d34e477ab`.
- Permanent installation `artifacts/EI-Mapper-Ataman-2-first-test`: updated to
  52; all 101 installed managed files match the package manifest. The previous
  51 managed files were verified against their manifest and backed up under
  `artifacts/rollback-EI-Mapper-Ataman-v51-to-52-20260923` before replacement.
  User files outside the package manifest were not copied over or removed.
- Automated behavior tests and the game/editor scenario were not run for this
  rebuild. No live HDPack, MPR, or game installation was modified.

## Acceptance still needed

Using disposable copies of a map and its configured HDPack:

1. Save a modern draft and verify its exact page names, embedded profile, and
   semantic field are present in `HDPack_Terrain.res`.
2. Reopen the draft and confirm the terrain texture/material palette is correct.
3. Publish a finalized MPR, reopen it, and confirm the MPR and HDPack resolve the
   same atlas identity and pixels.
4. Compare unrelated RES members and confirm they remain byte-identical; confirm
   no new map-local `*-atlas.res` was created or changed.
5. Exercise a failed MPR commit and verify the RES rollback leaves both inputs
   unchanged.

These runtime cases have not been proven by compilation.
