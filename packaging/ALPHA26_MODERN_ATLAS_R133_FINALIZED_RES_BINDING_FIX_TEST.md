# Alpha 26 modern-atlas R133 finalized RES binding fix test

This revision fixes finalized modern MPRs that render correctly in Evil Islands
but reopen in the editor with scrambled or unrelated terrain textures.

## Changes

- The editor now binds a finalized MPR to one complete RES archive whose
  decoded terrain-atlas hash exactly matches the saved semantic profile.
- Generic resource lookup can no longer assemble one finalized atlas from an
  older backup RES plus the newly published RES merely because their member
  names overlap.
- Existing finalized MPRs recover automatically from their stored atlas hash;
  they do not need to be re-finalized. New publications also store a portable
  relative archive hint so subsequent opens normally avoid the archive scan.
- If no matching complete archive is configured, the editor refuses the open
  with a clear resource-binding error instead of displaying mixed textures.
- Save now remembers any coherent active `.res` publication target, not only a
  file literally named `textures.res`.

## Focused manual test

1. Keep `HDPack_Terrain - Copy.res` and the current `HDPack_Terrain.res` in the
   configured JMV resource folder, as in the reported setup.
2. Open the supplied finalized `zone3obr.mpr`. Its terrain must match the game
   and the intact `zone3obr test.mpr` draft rather than showing mixed pages.
3. Repeat with finalized `zone4.mpr` and `zone4 test.mpr`.
4. Make one small modern paint edit in either finalized map and choose
   **Save MPR**. The existing `HDPack_Terrain.res` target should be reused.
5. Close and reopen the saved MPR. Confirm the terrain remains correct and the
   map remains editable.

## Known limitations

- This build does not mutate, rename, or remove older/backup RES archives.
- Rotation-equivalent physical deduplication remains disabled by decision.
- This build does not change the semantic catalog, brush icons, or masks.
