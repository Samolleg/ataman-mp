# Landscape preview manual test

This build exercises live canonical terrain editing only. Changed MPR saving is intentionally disabled.

## Safe fixture setup

1. Copy an MPR (and optional matching MOB) to a disposable test folder. Do not open a map from the live game installation.
2. Record the copied MPR's SHA-256 before opening it.
3. Open the copied MPR in Endurance and, if desired, its copied MOB.

## Sculpting

1. Open **View > Landscape Tools (Preview)** and enable the sculpt brush.
2. Test Raise, Lower, Flatten, and Smooth with several radii and strengths.
3. Drag across a sector boundary. The surface must remain connected with no crack or duplicated edge.
4. Use Undo and Redo. One continuous drag must appear as one undo entry and restore/reapply the complete stroke.
5. With a MOB open, confirm objects visually reproject while their stored MOB data remains unchanged. Treat the navigation warning as expected.

## Heightmap preview

1. Use a small grayscale PNG/BMP/TIFF with an obvious orientation marker.
2. Import once with and once without **Flip image vertically**.
3. Enter explicit black/white raw-height bounds and confirm the preview summary before applying.
4. Undo must restore the entire original surface in one step.

## Texture tiles and overlays

1. Open **MPR Atlas Tiles**, select atlas tiles, and use the existing tile-brush mode.
2. Paint and rotate land tiles, then Undo/Redo.
3. Confirm **Atlas category** is read-only and the Materials tab is disabled.
4. On a map with an existing overlay, paint an active overlay cell and confirm it updates.
5. Attempt an inactive overlay cell. Endurance must not create a liquid surface silently.

## Save guard and source integrity

1. After any terrain edit, invoke **Save MPR** and **Save MPR As**. Both must refuse changed-MPR output.
2. Close/unload and choose **No** when prompted to save the preview.
3. Re-hash the copied source MPR. Its SHA-256 must match the pre-test hash exactly.
4. If anything crashes or renders incorrectly, attach `logs/workflow.log`, the generated dump, map/profile name, and the exact tool settings. Do not attach proprietary map files publicly.
