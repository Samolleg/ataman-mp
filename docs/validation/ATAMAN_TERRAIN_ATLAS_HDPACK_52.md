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
- Release build: PASS using the retained Qt 5.12.12 MinGW 7.3 kit from the
  Endurance checkout, with a fresh Ataman shadow directory
  `build-release-52-terrain-atlas-hdpack`.
- Build output shows `ENDURANCE_VERSION="52"`; the executable was produced.
- `--version` runtime probe did not return in the available wait window, so the
  version output check is NOT PASS. Only processes launched from this task's
  shadow executable were stopped; the existing installed mapper was not touched.
- Automated tests and game/editor scenario were not run. No live HDPack, MPR,
  game installation, or permanent installation was modified.

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
