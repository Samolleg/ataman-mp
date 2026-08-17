# Alpha 26 terrain paint modes R171 decoded-atlas reuse test

R170 materially improved Zone2 loading, but its workflow log confirmed that the
same exact terrain atlas was still decoded and hashed more than once during one
MPR activation. This revision removes that remaining duplicate work while
keeping the exact archive-binding checks fail-closed.

## Confirmed R170 timing evidence

- `MPR editor load phases ... totalMs:2710`
- `rendererAtlasAndSectorsMs:1203`
- `tileTableMs:338`
- `materialCatalogMs:838`
- `Terrain material catalog phases ... atlasHashMs:421`
- `prevalidatedBindingReused:true`

The verified archive lookup had already decoded the atlas and computed its
pixel hash. The renderer, tile table, and catalog then rebuilt or re-hashed the
same artwork. The 338 ms tile-table pass plus the 421 ms catalog hash account
for roughly 0.76 seconds of avoidable synchronous work in that trace.

## Root cause and change

- **The exact decoded atlas is retained for the current MPR activation.** Once
  the RES archive has passed the existing full decoded-pixel verification, its
  decoded tile images can be adopted by the tile table instead of being decoded
  from the same pages again.
- **The already verified decoded hash is reused by the catalog.** A second hash
  of those same images is skipped only when the verified images were actually
  adopted by the current tile table.
- **Reuse is guarded by the archive snapshot and semantic identity.** Archive
  path, size, last-modified time, namespace, slot count, decoded pixel hash, and
  semantic profile must still agree. Any mismatch takes the original complete
  decode-and-hash path.
- **The cache is one-shot.** It is cleared after the material catalog consumes
  it, so later resource/profile changes cannot silently retain stale pixels.
- **The log exposes adoption.** `Terrain material catalog phases` now reports
  `prevalidatedTilesAdopted` alongside `prevalidatedBindingReused`.

No resolver, paint output, mask, semantic assignment, runtime terrain type,
atlas capacity, or save/publication behavior changed.

## Focused manual check

1. Open Zone2 through the same resource profile used for R170.
2. Send the newest `Terrain material catalog phases`, `MPR editor load phases`,
   and `Map workspace load phases` lines from `logs/workflow.log`.
3. The load should report `prevalidatedTilesAdopted:true`, `atlasHashMs:0`, and
   a near-zero `tileTableMs`. If any guard fails, adoption is deliberately false
   and the complete fallback path remains visible in the timings.
4. Open Manual mode and Material Atlas. Confirm that physical tile thumbnails,
   slot selection, painting, and undo still use the correct artwork.

## Automated validation

- Document suite: 124 passed, 0 failed, 6 expected fixture skips.
- Virtual-atlas suite: 113 passed, 0 failed.
- Landscape-tools widget suite: 7 passed, 0 failed.
- Exact decoded-image reuse and archive-mutation fallback regressions: PASS.
- Retained Qt 5.12.12 MinGW 7.3 x64 release wrapper: PASS.
- Headless `--ui-widget-check`: explicit PASS.

## Manual validation boundary

The adoption and fallback contracts are covered headlessly. Actual Zone2 wall
time still depends on the configured RES profile, storage cache, and renderer,
so the user's new timing lines are the authoritative performance measurement.
The editor and game were not launched by the build process.
