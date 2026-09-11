# EI-ADD-FROM-DB-001 validation checkpoint

Status: partial implementation; the parent task remains open. Native collectible
placement, complete automatic lever appearance, and gameplay acceptance are not
delivered by this checkpoint.

## Reproduced failures

- On the original baseline, the synthetic database picker test expected one
  lever prototype without any map/selection; the relational catalog returned
  zero. The first missing state was database-backed creation metadata.
- The real creation command failed `save -> undo -> save -> redo -> save`:
  saving rebound provenance and discarded the creation template; saving while
  undone removed the only remaining source record. The last save correctly
  refused `mob.object_sequence.ui_provenance`. Creation undo now retains the
  exact current raw/clean record and its hash before removing the object.

## Automated results (2026-09-11)

- `profile_database_tests`: seven focused cases, plus setup/cleanup; nine passes,
  zero failures/skips. Covers synthetic catalog links, absent maps, invalid or
  unsupported entries, zero creature complexion, separation of lever Scale
  from instance complexion, provider isolation, authoring undo, and installed
  archive byte identity on no-op.
- Read-only installed catalog checks: Base 843 entries, 389 creatures with
  creation metadata, 37 levers; JMV 2,059 entries, 1,105 creatures with creation
  metadata, 37 levers. Metadata coverage does not prove available graphics or
  gameplay behavior. No installed archive was changed or retained in artifacts.
- `EIMapperEndurance --database-creation-check`: passes both creature and lever
  creation without a selected node, bounded raw-record parsing, editable reopen,
  exact script/metadata preservation on removal, repeated insertion, corrupt hash
  rejection, actual `CCreateNodeCommand` undo/redo, save/undo/save/redo/save, and
  multiple fresh IDs in a populated destination. Fixtures are synthetic and
  temporary; the diagnostic does not open the editor window or installed game.
- Qt release executable compiled. The release wrapper's runtime-component step
  points to an unavailable Visual Studio edition on this host; both unchanged
  runtime sources were separately rebuilt with the installed Build Tools using
  the same compiler/linker options. No build-script changes were included.
- `--ui-widget-check` still exits with `0xC0000005` under the minimal Qt platform,
  matching the previously recorded broad-widget-check gap. No pass is claimed.
- The intermediate unpacked tester folder was finalized in a permanent location
  outside the worktree. Distribution validation passes; no package was launched.
  Tested and packaged executable SHA-256:
  `c85955740916248d8fe47468e4dd665c5bba8e10be1f753b917263334087361e`.

## Manual gaps and choices

The r1 results above are retained as a checkpoint. Preview and placement defects
reported afterwards are fixed and validated in
[the r2 video regression report](add-from-database-video-regressions.md).

The picker uses active database identities and the existing type defaults,
without copying another map object's quests, logic, or appearance. A monster's
race TypeId, model, skin and complexion seed its instance; database stat import
remains enabled through the existing non-embedded mode. Lever FLD3 is not mapped
to the three figure-interpolation values: installed instances establish that
these are different properties. A texture is auto-selected only for a unique
resource-name match; otherwise the user must choose one. This is incomplete
automatic lever appearance, not a claimed database texture link.

Unverified beyond r2's focused off-screen widget/render checks: user acceptance on
loaded terrain, profile switching while
placing, gameplay appearance/equipment/AI of all creature families, lever use,
and native ground-item pickup. The last item has no implemented independent
creation path. The item list explicitly disables placement. No generated
scenario or creature-loot substitute is included.
