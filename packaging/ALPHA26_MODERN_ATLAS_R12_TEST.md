# Modern atlas r12 tester notes

This revision fixes the three failures reported from r11:

- Base terrain atlases now use a bounded multi-row GPU layout. The 4,096-slot
  collection is 8x8 pages instead of an invalid 65,536x1,024 strip, and the
  same layout is restored after live atlas previews.
- Expanded probes repeat byte-identical authored Zone18 MMP pages. They no
  longer alter pixels to manufacture synthetic identities, so semantic
  inference can recover every repeated slot and material strokes remain
  paintable.
- Test Current Map activates the runtime bridge while the JMV main thread stays
  runnable after patch quiescence, avoiding the loader-lock timeout that made
  the game fail to start.

The package includes five editor-openable collections at 1,024, 2,048, 4,096,
8,192, and 16,384 slots. Their MPR/RES fixtures intentionally include repeated
stock JMV Zone18 terrain pages, which are authorized editor test data. The
default `zone18.mob` continues to resolve from the configured JMV installation.

## Focused manual test

1. Select the `jamais-vu` profile and refresh Maps and Collections.
2. Open `Zone18 expanded atlas - 2048 slots`, then the 4,096-slot collection.
   Confirm both load textured terrain; the Loose Map warning is expected for
   these isolated generated namespaces.
3. Paint several strokes with different ground materials. Every stroke must
   appear and remain paintable; no complete-field refusal is expected.
4. Use **Test Current Map** on the 2,048-slot collection and confirm the game
   reaches the staged Zone18 workload with terrain textures.
5. Repeat editor loading for 8,192 and 16,384 slots. These are capacity probes,
   so their repeated/checkered world layout is deliberate rather than an
   artistic blend showcase.

## Validation note

The owned runtime harness proves the revised bridge activation completes. Its
latest local 2,048-slot rendering smoke was stopped by the currently selected
dgVoodoo backend returning `DirectDrawCreate` `E_FAIL` before a capturable game
window existed; this is recorded as an unrelated graphics-backend failure, not
as an atlas pass. The prior controlled Zone18 campaign remains the runtime proof
through 16,384 packed slots. Normal authoring finalization remains limited to
1,024 slots pending a separate policy change.
