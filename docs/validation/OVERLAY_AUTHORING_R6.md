# Overlay authoring R6 validation

R6 repairs the land-only SEC upgrade used by the first Add overlay surface
stroke, keeps the selected Landscape Tools mode for the current editor session,
restores normal layer visibility when the tool closes, and permits valid
semantic land paint to recover unauthored packed values left by earlier edits.

Use only a copied test map. Do not modify the original game corpus.

## Automated evidence

- `endurance_document_tests`: 73 passed, 0 failed, 3 private-fixture skips.
- First radius-1 activation on a synthetic type-0 SEC activates all five cells
  and an identical second click is a no-op.
- Semantic recovery overwrites an unauthored core tile with the requested valid
  family while leaving an unauthored transition-ring neighbour unchanged.
- `EIMapperEndurance.exe --ui-widget-check`: exit 0, including programmatic
  land/overlay mode material-list synchronization.

## Manual test

1. Open a clean copied Zone 1 MPR and choose **Landscape Tools > Add/remove
   overlay surfaces > Add surface**. Use a circle larger than size 1 over an
   area with no overlay. One click must fill every cell inside the outline; a
   second click in the same place must make no further change.
2. Close Landscape Tools with `L`, inspect animated rendering, then press `L`
   again. The same tool mode must still be selected. It must not silently
   switch to Paint land tiles.
3. Hide either terrain layer from Landscape Tools, then close the tool. Main
   terrain and overlays must both return to visible.
4. Switch between Paint land tiles, Add/remove overlay surfaces, and Paint
   existing overlays. Each mode must show only its valid material families;
   an overlay-only family such as Swamp must not remain selected in land mode.
5. On a copy of an MPR affected by R5, choose a normal valid Zone 1 land family
   and paint over the corrupted mosaic. The full brush core must be replaced;
   `terrain.semantic_catalog` must not refuse the stroke merely because the
   existing packed value is absent from the approved namespace template.
6. Undo and redo each operation, save as a new MPR, reopen it, and confirm the
   repaired land and newly activated overlay cells persist.

Expected warnings about a recovered unknown tile are diagnostic. Newly written
tiles must still belong to the approved atlas namespace; recovery never invents
an atlas slot or mutates unknown cells outside the brush.
