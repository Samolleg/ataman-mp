# Terrain flipbook finalization R9

This test build fixes the remaining atlas-order and publication failures found
with the `zone1testoverlays6finallykinda` draft.

## Changes

- Finalization now treats each live flipbook as an atomic contiguous block
  instead of preserving its complete source MMP page. Unreferenced pre-repack
  frame copies and unrelated blend tiles on those pages are no longer carried
  into the clean atlas. Live cell references and animation records are remapped
  together, and every resulting range remains inside one 64-slot MMP page.
- Selecting a Modern material prepares its complete approved-HD physical family
  before later blend resolution. Shift+R can no longer insert newly generated
  blends between uniform variants that are still being realized.
- If an Endurance-owned Test Current process is using the selected terrain RES,
  Finalize now offers to stop it, waits for resource release, and only then
  begins the atomic MPR+RES publication. Success is logged only after both
  commits complete.
- Resolved animation ranges use white text in the dark MPR Materials table.

The reported Lava/Water blend masks were not changed in this build. Their
generated masks are reviewed semantic-atlas choices, so the marked visual cases
must go through a separate re-review rather than being silently reclassified.

## Focused checks

1. Open the attached R8/R9 draft and choose **Finalize and Save As**. If Test
   Current is running, choose **Stop Test Current and continue**. Confirm the
   game closes, publication completes, and no `Access is denied` replacement
   error appears.
2. Reopen the finalized pair. Confirm both Water/Emerald and Water/Still dark
   animation ranges are contiguous and page-local. The obsolete Emerald source
   frame copies should not survive merely because they shared an old source
   page; ordinary live slots remain intact.
3. Introduce Water/Still dark with Modern painting, then use Shift+R. Confirm
   generated blend tiles lie outside the prepared uniform family rather than
   splitting it. Create the flipbook and verify it in Test Current.
4. Confirm the **Resolved range** column remains readable (white text) in MPR
   Materials.

This package does not modify installed maps or archives automatically. As usual,
review the publication destination and keep the editor-created backup until the
result has been tested in game.
