# Ordinary database item placement, r3

Task: EI-ADD-FROM-DB-001-R4. On 2026-09-11 the user explicitly replaced native
pickup with ordinary object placement. The user will convert the object to a
lever and attach an existing interaction script. No new game runtime is needed.

## Reproduction and implementation

Before the production change, the installed-resource picker diagnostic reproduced
`FAIL: selected database item must enable Place on map` while weapon previews
passed. The catalog unconditionally rejected items; the picker, placement clone
and fresh-record builder had no ordinary-object creation path.

The picker now requests a persistent CWorldObj from the shared item visual
builder. JMV weapon assembly parts and their selected texture are stored in normal
map fields. Standalone inventory figures use their ordinary resource identity;
the world-object loader restores their UV treatment after reopen and conversion.
Unsupported/missing visuals remain unavailable. No private source or game assets
are copied, no scenario is generated, and no installed data is edited.

Fresh ordinary-object records use the same bounded serialize/parse/reopen and
hashed provenance checks as creatures and levers. The existing mouse placement
transaction supplies cancel, click, and one-action undo/redo.

## Validation

- Red test reproduced on pre-fix code; the same enabled-button assertion now passes.
- JMV screenshot selection `2h metal battle sword`, material changes, immediate
  hidden picker/pointer attachment, movement, cancellation, commit and history pass.
- Base weapon and JMV desktop/ANGLE runs pass. NPC textured previews and placement
  are exercised in the same diagnostic.
- The placed weapon reopens with the same model/material; conversion of its raw
  record to a lever preserves the appearance fields. Preview, reopened object and
  converted lever images match exactly for the checked JMV sword; visually inspected.
- Synthetic creature, ordinary item and lever diagnostics pass guarded creation,
  untouched script bytes, hash rejection, save/undo/save/redo/save and distinct IDs.
- Thirteen focused QtTest results pass, zero failures/skips, including installed
  catalogs, constructor policies/materials and unchanged archive members. Restricted
  execution produced no test report; the authorized unrestricted run produced the
  complete result, and only that run is counted.
- Release editor and both runtime components build. The release wrapper encounters
  the known absent Visual Studio Community path; the unchanged runtime sources were
  built with installed Build Tools using the original flags, as in r2.
- Broad `--ui-widget-check` still exits with 0xC0000005 on minimal. It is a known
  separate gap; no broad UI pass is claimed.

## Handoff and confidence

Revision r3 preserves the already-used r2 folder. The staged and permanent
unpacked packages both pass the distribution finalizer, with no removed files.
Their executable hashes match the tested release:
`cd41d7c60ce817d52b84d6b56cc70a0fbf4b71cabe09b9bb6d6da6442a8847aa`.
Neither packaged editor was launched by the agent.

No uncertainty remains about the reproduced button rejection or the tested
ordinary-item persistence path. Full database coverage, visual variants needing
multiple composited textures, actual game interaction and the user's script are
not established by these tests. No game was launched. The parent task remains open
for its other appearance/acceptance gaps; native automatic pickup is no longer
part of the requested item implementation.
