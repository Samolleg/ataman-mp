# EI-MAP-INVENTORY-188 — 2026-10-16

Base: main d57a9b7032ed3fa457b0980ef05dc70926bf8343, Ataman187.
Scope: extend Add object / Items with the active database's read-only mapitems-catalog.json, preserving equipment, existing maps and owner data.

## Implementation

- Schema2 catalog beside the active database. Bind quick/quest rows by stable identity and live type/material/model/field21 selectors; record indices and whole-archive hashes do not define identity. Material names/types/codes are also checked against the live DB.
- Use the declared LNK/FIG/BON sources without copying game assets. Place the standalone FIG as an ordinary world object, with exact primary/secondary texture and item/material descriptor. Resource overrides and absent assets are refused.
- Russian names/search, available materials, authored HD then SD texture candidates. Missing model/texture and spell-dependent artwork show a diagnostic with disabled placement.
- Inventory FIG UVs address one logical 4x4 cell; the MMP stores that cell image. Reuse the existing repeat sampler/UV scale4 while retaining source vertex UVs. No enlarged intermediate texture or asset rewrite. The old normalized map-texture path retains its previous parameters.

## Verification

Fresh shadow build: build-release-188-inventory. Qt5.12.12 MinGW73_64; matching GCC; application and both x86 runtime components rebuilt. Built --version: EI Mapper Endurance Edition188 (space before188 in CLI).

Final EXE SHA256: 5D5228725D8218399D3C4914A88D958E574BC9CAA7315C6408CB8FEFBFF240D4.
Receipts/logs: build-inventory-188-20261016/{final-probe-receipt.json,inventory.out,equipment.out,creation.out,version.out}.

- Inventory native PASS: all105 quick +96 quest identities from the actual database tables. All198 available identities selected, Escape/retry/placed with positive ID, validated MOB candidate, exact name/model/primary+secondary texture reopen, one-step Undo/Redo.
- Material switching PASS (wand1, multiple distinct material textures); localized search and restored equipment controls PASS. Invalid/absent catalogs and eight stale geometry/type/material/field21 bindings refused with no stale cache reuse.
- Rendering probe: wand HD512x512, quest HD512x512, quick SD64x64. Source FIG cell bounds retained; existing shader UV scale selects the full image. The initially blank wand became a1619-color frame. Rendered preview PNGs inspected; this is editor evidence, not art/game acceptance.
- Equipment regression PASS:329 identities including230 armor/all7 types,26 material/variant placements, preview/Escape/ID/save/reopen/Undo/Redo and invalid-material/model/catalog refusals. Existing no-texture equipment prototypes remain resource gaps; not newly repaired by this task.
- Generic --database-creation-check PASS: fresh MOB creation, hierarchy/BON checks, reopen, ID safety, actual creation-command history, save/undo/save/redo/save, light creation.
- All four final check receipts refer to the same EXE hash. No interactive owner editor or game was launched. Global UI/clipboard tests were not needed for this picker change.

The first coverage assertion incorrectly used categories from the flattened name-keyed snapshot, where two identities are overwritten by same-named records in another category. The final test enumerates the original two tables and verifies the complete identity set. An actual blank-HD preview found during material testing was fixed by the UV mapping above before final PASS.

## Source limitations and handoff

Three catalog records intentionally cannot be placed: ancientsword and donesenie have no geometry; spell container has spell-dependent artwork without a fixed texture. They remain visible with a reason. Ordinary objects do not acquire pickup/quest scripts automatically; interaction is assigned after conversion to a lever, as with equipment.

Catalog SHA256 before/after read-only testing: A6212EE15DBD84956CE31A2F827A602241EA1E2E0261BB7D449BD145396A5563. No JSON, game RES, map or save was written.

Fresh staging/finalizer and guarded permanent update are recorded separately in build-inventory-188-20261016/{staging-receipt.json,install-receipt.json}. Owner interactive/game acceptance remains open.
