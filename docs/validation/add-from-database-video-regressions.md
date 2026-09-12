# EI-ADD-FROM-DB-001: video regressions R6–R8

## Original repeatable acceptance (captured before code changes)

With the JMV profile and an open map, open Add from database:

1. Select AncientCurseM25, then ArmoredTurtle110. Their DB textures are Curse00
   and turtleblue. Expected: textured geometry. Recorded: cyan silhouettes.
2. Select Items, then 2h long spear. Expected: weapon geometry with a compatible
   material texture. Recorded: empty preview, because unsupported placement
   returns before attempting any visual lookup.
3. Select AncientCurseM25 and click Place on map. Expected: the window hides
   immediately, object appears at the pointer, follows motion, click commits one
   creation, Escape removes the pending object without history. Recorded: window
   remains open and demands a texture that is already selected.

Source trace before fixes: CPreview::draw shares CView's object shader without
resetting customColor; fshader uses solid customColor whenever its alpha is nonzero.
The placement preflight calls textureImage, whose CPU body-atlas path rejects
non-PNT3 textures although the OpenGL loader supports DXT. The form's placement
event filter consumes pointer moves without creating or drawing any ghost.

The supplied video was decoded locally and sampled across its 30 seconds. It and
its extracted frames are excluded from source control and tester distributions.

## Fixes and verification

- CPreview owns its object shader, while geometry/textures remain shared. Map
  overlay color, lighting and shadow uniforms cannot change a preview's shader.
- Placement checks the actual renderer resource loader, supporting ordinary DXT
  as well as PNT3. Missing texture resources still fail explicitly.
- Item preview construction is shared with the existing item/spell constructor.
  The picker exposes compatible materials and renders items even while their
  native pickup placement remains unavailable.
- Fresh database creation enters the viewport's existing pending paste/move
  ownership transaction: immediate visible node, pointer motion, commit on click,
  cancellation without history, and a single undo action. Opening the picker again
  cancels the previous pending creation. CPU terrain/grid picking supplies positions.

`--database-preview-check` exercises the production form, resources, rendering,
button slot, movement transaction, pointer update and click handler. It keeps the
widgets off screen and uses a synthetic empty destination; installed assets are
read only. Scratch settings, screenshots and data are excluded from distributions.
Inputs: `EI_TEST_PREVIEW_DATABASE`, `EI_TEST_RESOURCE_ROOTS` (semicolon separated),
`EI_TEST_PREVIEW_OUTPUT` (scratch folder).

The previous committed preview implementation was rebuilt temporarily against
the new regression: AncientCurseM25 rendered only two colors and the test failed.
The fixed source was restored and rebuilt. Results on the actual Windows drivers:

| Check | Result |
| --- | --- |
| JMV, desktop OpenGL | Pass: both recorded creatures, weapon and second material |
| JMV, ANGLE/OpenGL ES | Pass: same visual and placement cases |
| Installed base-resource profile, desktop OpenGL | Pass: creature, weapon and materials |
| Immediate pending creation, picker hidden, pointer movement on neutral grid | Pass |
| Cancel, repeat, click, one-action undo/redo, validated save candidate | Pass |
| Original creation/reopen/hash/save/undo/save/redo/save diagnostic | Pass for creature and lever |
| Catalog/constructor/profile/archive regression tests | 13 passes including setup/cleanup, no failures/skips |
| Broad `--ui-widget-check`, minimal platform | Existing 0xC0000005 remains; no pass claimed |

Captured creature and weapon images were visually inspected. Test results prove
the recorded preview and pointer transaction, not full gameplay or every database
entry. No game was launched. Terrain picking uses the existing terrain ray query;
the automated placement transaction uses a neutral grid.

## Handoff scope and confidence audit

This is revision r2 of 0.5.0-alpha's database-creation tester, staged separately to
preserve the already-used r1 folder and its user settings. Qt release builds and
both runtime components compile. The wrapper's Visual Studio Community path is
unavailable on this host; unchanged runtime sources were built with the installed
Build Tools and the original compiler/linker options, as for r1.

No material uncertainty remains about the reproduced shader-state, DXT-check,
weapon-preview suppression or absent pending-object causes. Compatible material
selection is a preview choice, not a claim of a unique DB material default.
Parent native collectible placement, complete lever texture defaults, and broad
gameplay acceptance remain open. Weapons can be previewed but their Place button
remains disabled; no scenario or decorative pickup substitute is created.

The unpacked r2 folder passed the distribution finalizer in both staging and its
permanent tester location. The package was not launched. Tested/staged/delivered
executable SHA-256:
`8692842ac9622c5eb42965d71c4d0ce2e0ff1e772f27945411fdb400ae945d9d`.
