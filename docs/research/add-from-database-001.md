# EI-ADD-FROM-DB-001: creation research and acceptance cases

Status: partial implementation; see the
[validation checkpoint](../validation/add-from-database-001.md). Baseline is
`ffdc28ea9f749896ddffdd583e2a6a5e44d5201a` on
`contrib/Samolleg/add-from-db`. The parent scope remains all three categories.

## Original issue and repeatable acceptance cases

Use a disposable editor workspace with an active Base or JMV profile and a new
empty MOB. Do not modify installed maps or the user's saved workspace layout.

1. With no selected object, open Create Object. Expected: a searchable database
   picker with Creatures, Collectible items, and Usable objects/levers; names,
   model/preview, supported defaults, and secondary advanced fields.
2. Select a monster prototype and place it. Expected: a real UNIT using that
   prototype's game values, correct appearance, and a fresh positive map ID.
3. Select a DB item (including a material where required) and place it.
   Expected: an actual collectible that awards the exact item once. A scenery
   model alone does not pass. User decision: native pickup without a new scenario.
4. Select a lever prototype and place it. Expected: a real LEVER with valid
   interaction/state fields. State switching and a scripted consequence must be
   distinguished; a new lever must not silently inherit another map's quest.
5. For each category, cancel before placement, place/move, undo, redo, save,
   reopen, then undo/redo/save again. Expected: exact ownership, stable identity,
   unchanged script bytes, and preserved unrelated raw data.
6. Repeat in a populated MOB and after switching active MOB/profile. Expected:
   no stale profile/template or wrong destination. Missing resources and
   unsupported creation data must block that entry with an actionable reason.
7. Check gameplay in both profiles on disposable maps: creature behavior,
   actual inventory pickup, lever use, save/load, and no duplicate pickup.

Baseline source trace (GUI execution remains pending):

- `CCreateObjectForm::onObjectChoose` constructs a default node and optionally
  copies selected-node parameters; it does not obtain a DB creation descriptor.
  This is the first missing state in the requested pipeline.
- `on_buttonCreate_clicked` requires exactly one selected same-type node with
  raw provenance and otherwise returns before `CCreateNodeCommand` is pushed.
  Thus the no-selection case above cannot reach placement on this baseline.
- `CMob::createNode(QJsonObject)` assigns a new ID and binds supplied provenance.
  It does not independently synthesize a complete verified raw object record.
- `CMob::buildRawObjectSequenceCandidate` rejects nodes without matching hashed
  raw source/clean subtrees. This protection is required and must remain.
- `CCreateNodeCommand` retains the exact node for undo/redo and supports provenance
  rebasing after save. Creation should use that ownership contract.
- Script generation is excluded by the user's native-pickup requirement.

No packaged GUI was launched and no automated runtime reproduction is claimed.
Before implementation, turn the acceptance cases into focused behavioral tests
against the chosen creation mechanism and record the original failing result.

## Read-only evidence collected on 2026-09-11

Installed maps and database archives were parsed in memory; no installed data
was edited or copied into the repository. This report contains no private source
contents or provenance. Research uses the user's explicit read-only authorization.
The named EI safety/reference skills were not present in the available skill
catalog or the searched local skill/plugin directories.

- The local corpus contains 99 Base and 153 JMV MOBs. All 252 passed bounded
  section traversal. Observed ordinary OBJECT subtypes were 53/54/55 and one
  exceptional 51 in each profile; observed UNIT subtypes were 50/51/52; LEVER
  subtype was 60. This scan found no separate ground-item object in those
  envelopes. It does not prove that no other engine mechanism exists.
- There were 408 Base and 1,109 JMV LEVER records. These counts describe this
  local corpus, not the older 1,110-record JMV corpus in the lever audit.
- Both active database archives contain `levers.ldb` with 37 prototype rows.
  Field 0 contains model-resource identifiers in the inspected rows. The public
  database schema exposes use coordinates/count, scale, switch time/material/
  sound/text, but the relational `SProfileDatabaseSnapshot` has no lever catalog.
- `items.idb` supplies item definitions; the current relational item record
  provides category/type/material/texture metadata, not a MOB pickup record.
- Encrypted MOB scripts were decoded in memory using the existing documented
  parser algorithm. Aggregate calls found: Base `GiveItem` 20,
  `GiveQuestItem` 34, `AddLoot` 1, `WasLooted` 1; JMV `GiveItem` 610,
  `GiveQuestItem` 55, `AddLoot` 4, `WasLooted` 6. Their presence supports further
  research into script-based pickup, not a claim of a tested generated script.
- Existing visual resolution deliberately refuses blind database appearance
  substitution for non-humanoid creatures. Some JMV humanoid instances also
  use an embedded assembly different from the DB mask model. Creation needs
  independently validated appearance defaults, not a renderer-policy bypass.

Relevant existing evidence: [lever structure audit](world-object-to-lever-audit.md),
[property semantics](../OBJECT_PROPERTY_TERMINOLOGY.md),
`source/documents/database_schema/dbheaders.txt`, and
`source/runtime/providers/profile_visual_resolver_support.cpp`.

## Constraints and remaining work

- R2, resolved by the user on 2026-09-11: native pickup without a new scenario.
  The previously proposed generated-script route is rejected. Investigate the
  native item/bag data and loader; do not replace pickup with scenery or scripts.
- R4, follow-up research: the stock MOB object loader handles ordinary objects,
  units, levers, torches, traps, lights, sounds, and particles. A separate native
  inventory item is not an additional supported MOB envelope in the inspected
  loader. Binary dispatch also supports ordinary object subtype 56, but its
  class uses ordinary world-object behavior; the type alone does not add an
  inventory reference or pickup operation. Do not treat it as a loot bag.
- Native loot collection is tied to a unit and its inventory/drop definitions.
  A stock item-database loot record describes inventory contents, not a map
  container. No complete independent ground-item-to-MOB path was established in
  the inspected stock/JMV facilities. This is a bounded finding, not proof that
  no other runtime extension can provide such a path.
- User correction on 2026-09-11: create the selected object on the map from
  `database.res`. The follow-up creature-loot choice is withdrawn; there is no
  pending permission to obtain before implementing the database picker and
  direct creation. Creature loot is not an authorized substitute. MOB is only
  the internal persistence format, not the user's source or selection workflow.
- A dead unit disguised as a bag is not an approved or validated workaround.
  Importing zero HP does not itself establish a complete dead/lootable state.
- R3, necessary research: determine complete creation records and appearance
  defaults for creatures, and model/texture/interaction defaults for levers.
  Catalog availability alone does not settle either question.
- Implemented since investigation: synthetic catalog failure reproduced/fixed;
  database picker, fresh creature/lever records, click placement, and guarded
  creation. A separate save/undo/save/redo failure was reproduced and fixed by
  retaining the creation command's current raw/clean source before removal.
- Next: complete native pickup without changing the requested workflow, finish
  automatic lever appearance, and verify UI/gameplay. Passing catalog and
  persistence checks do not close these acceptance criteria.
- The prior offscreen `--ui-widget-check` access violation is a separate known
  gap. It was reproduced under the minimal Qt platform at this checkpoint.

## Confidence audit

Material gaps: independent native ground-item mechanism;
new creature appearance/subtype defaults; complete fresh raw-record schema;
lever effects beyond switching state; GUI/runtime and save-after-undo behavior.
The implemented creation path uses existing type defaults and explicit database
identity. Lever Scale is not treated as instance complexion: installed records
show these are different properties. Missing or ambiguous texture defaults remain
a visible manual choice. The parent task is not complete.
