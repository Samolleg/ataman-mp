# Separate map texture names for placed database items

The user chose to supply converted DXT1 textures alongside the original resources,
using terminal `map`: `unhumadg_03.br.1.x2map.mmp`. This supersedes the proposed
asset export in the [earlier investigation](database-item-texture-r5-investigation.md).
No installed assets/maps are edited or copied by this change.

The red test reproduced the first wrong value: item creation persisted
`unhumadg_03.br.1.x2` instead of the requested `unhumadg_03.br.1.x2map`.
The item-only naming function trims filenames, removes terminal `.mmp`, and adds
`map` once. Both persisted texture slots use the result. Creature/lever catalog
naming and the inventory constructor's equipment preview are unchanged.

Map-item preview requires the exact map resource in the active resource index
and successful texture loading. Missing resources produce a localized diagnostic
with the required filename and disabled Place. There is no return to the source
equipment texture. Ready map resources use authored model UVs, not the x2
equipment correction. Existing map records are not migrated automatically.

Validation:

- Red naming test fails before the production change; 13 focused tests pass after.
  Coverage includes extension/case/whitespace, no duplicate map suffix, empty
  input, ordinary inventory names and unchanged creature/lever defaults.
- User-supplied bronze map texture was found in the active profile and inspected
  read-only: DXT1, 512 x 512, 10 mip levels.
- JMV desktop and ANGLE preview, pointer placement, cancellation, click/history,
  save/reopen and lever conversion pass with that actual resource. Reopened and
  converted item previews retain the same appearance. A missing second material
  is rejected with its map filename instead of silently using the PNT3 original.
- JMV and Base binding checks also pass with explicitly generated synthetic DXT1
  colors; these scratch fixtures contain no copied game pixels and are not
  gameplay appearance evidence.
- Fresh creature/item/lever creation and real save/undo/save/redo/save pass.
- Release editor builds; runtime components use the existing local Build Tools
  fallback because the release wrapper assumes an unavailable Community path.
- r5 is finalized in staging and the permanent tester folder. Both executable
  SHA-256 values match: `b3e8272d31946666312df057bdf8470f7437e63d81af48a3aebffee3248d9f88`.
  The packaged editor was not launched and r4 was preserved.

## Final user acceptance and texture preparation

The initial DXT1 conversion retained the source equipment layout, so the correct
resource loaded but the dagger sampled the wrong region in both editor and game.
For the verified packed-x2 part, the user then prepared a 1024x1024 map texture
containing four unchanged 512x512 copies of the source image, in a 2x2 grid.
Before compression, output(x,y) = input(x mod 512,y mod 512). Generate mipmaps
from that final atlas and retain the `map` resource name. This reproduces the
equipment repeat transform without changing the model's UVs. Do not generalize
the operation to models already using full-image UVs.

The user supplied a corrected mapper screenshot and subsequently confirmed the
same correct appearance in the game. This is user-reported gameplay acceptance
of the bronze cheat dagger, not an agent-run game test or all-item validation.
The converted assets are user-maintained and are not included in this PR.

Doubling both atlas dimensions multiplies DXT1 storage by approximately four.
The user accepted this tradeoff for now; texture-size optimization and separate
models with rewritten UVs are deferred. The earlier r4 suffix fix alone was
insufficient. The known broad UI-widget diagnostic access violation and full
database/automatic-lever appearance coverage remain open in the parent roadmap.
