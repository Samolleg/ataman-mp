# Alpha 26 terrain paint modes R179: Test Current atlas restart

This focused build fixes Test Current after Modern paint changes the terrain
atlas. It covers both a published map identity and an editable draft with its
map-local atlas.

## Changes

- Test Current now stages changed working-atlas bytes even when new artwork
  reused an empty slot in an existing page and the page/slot counts did not
  grow.
- An unchanged terrain atlas still uses the fast in-process MPR/MOB hot reload.
- If the atlas bytes, page count, or slot count changed, Endurance stops only
  the game process it launched, completes guarded cleanup, and automatically
  starts a replacement test process with the new MPR and atlas together.
- The modal progress state remains active through that replacement. A failed
  cleanup or replacement launch is reported instead of pretending the reload
  succeeded.
- No installed or published RES archive is changed by Test Current.

Because a changed atlas requires a fresh game process, the replacement returns
through the selected entrance/spawn rather than preserving the exact live
player position.

## Focused test

1. Open published `zone1`, start **Test Current Map**, and reach the map.
2. Make a Modern paint change which adds or imports terrain-atlas artwork, then
   invoke Test Current again (`Ctrl+F5`).
3. Confirm Endurance closes and automatically restarts its owned game process.
   The staged map must load with the painted artwork: no cyan/transparent cells
   and no crash.
4. Repeat with an editable Zone1 draft identity and adjacent `-atlas.res`.
5. As a control, change only MPR/MOB data without changing atlas bytes and use
   Test Current again. It should retain the normal fast hot-reload path rather
   than restart the process.

Do not overwrite source maps or RES archives for this test. Stop the owned test
process through Endurance when finished.
