# Alpha 26 modern-atlas R135 MPR profile collision fix test

This revision fixes finalized or draft MPRs with the same published resource
identity overwriting each other's editor-side terrain profile. It also makes
generated blends use the same reviewed uniform artwork shown by the brush icon.

## Changes

- Portable modern-terrain profiles are now scoped to the exact MPR filename as
  well as its runtime resource identity. Saving `zone3obr test.mpr` and later
  `zone3obr.mpr` can no longer replace one shared `zone3obr` profile JSON.
- Existing legacy MPRs whose namespace-only profile was already overwritten can
  open visually from a complete sibling `<map>-atlas.res`. The mismatched JSON
  is ignored instead of blocking the map or mixing texture pages.
- A legacy-collision map without any remaining exact semantic profile opens in
  visual/manual tile mode with Material painting disabled. The editor does not
  guess a lost semantic mapping from physical slots.
- RES binding errors now list the decoded hash of every complete candidate,
  making a stale profile distinguishable from a missing or partial archive.
- Generated missing blends now take their uniform compositor base from the
  reviewed local uniform used by the palette icon. They no longer silently use
  the first same-family slot after an icon correction.
- No installed MPR, RES, profile, or game file is modified by this build.

## Focused manual test

1. Open `zone3obr test.mpr` beside `zone3obr test-atlas.res`, then open
   `zone4 test.mpr` beside `zone4 test-atlas.res`. Both should render their
   sibling atlas instead of refusing the stale legacy profile. Confirm the
   textures match the maps in game.
2. For either legacy-collision map, confirm Landscape Tools clearly reports
   that semantic Material painting is disabled. Manual tile and non-terrain
   editing remain available; do not use this old pair as a semantic paint test.
3. From a valid source MPR, save two different modern drafts/finalized maps that
   deliberately share one resource identity. Reopen both and confirm each uses
   its own sibling RES/profile without changing the other.
4. On a fresh valid Zone3Obr/Zone4 map, paint Ground / Gray and Ground / Gravel
   greenish strokes. The uniform brush icon, uniform interior, and generated
   edge blends must use coherent reviewed artwork.

## Known limitations

- An exact semantic profile that was already overwritten cannot be reconstructed
  safely from finalized physical slot numbers alone. R135 opens those old maps
  with their correct sibling pixels but deliberately does not invent paint
  identities. Fresh R135 saves retain independent profiles normally.
- Rotation-equivalent physical deduplication remains disabled by decision.
