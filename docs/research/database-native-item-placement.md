# EI-ADD-FROM-DB-001-R4: native item placement checkpoint

Historical checkpoint, superseded by the user's later 2026-09-11 instruction:
place an ordinary object; the user will convert it to a lever and attach an
existing script. No runtime compatibility decision is pending. See
[r3 implementation validation](../validation/database-item-placement-r3.md).

Status: unresolved, 2026-09-11. The user authorized implementation after
confirming NPC placement in r2. This checkpoint does not implement pickup or
close the parent task. No installed game files were changed.

## Original failing acceptance case

1. Open Add from database with an active map and choose Items.
2. Select `2h metal battle sword` and material `bronze`, as in the user's r2
   screenshot. The textured preview is present.
3. Expected: Place on map is enabled; activating it closes the picker and shows
   the selected item at the pointer. Click commits; Escape cancels.
4. Actual: Place on map is disabled, with the explicit unimplemented message.

The first rejecting decision is the unconditional item `unavailableReason` in
`source/resources/database_map_catalog.cpp`. `applyDatabaseMapEntry` also rejects
the item category; the picker returns after building its preview. The viewport
and fresh-record builder accept only creatures and levers. Removing the UI
restriction cannot implement the missing game behavior.

Full acceptance also requires the exact database item/material to enter inventory
once, normal interaction, no script generation, and persistence before and after
pickup. Editor save/reopen, undo/redo, cancellation, resource-profile changes,
and unchanged unrelated script/raw bytes must remain correct. A model displayed
in the editor or game is insufficient evidence.

## Bounded findings

- The earlier installed-map scan covered 252 Base/JMV maps and did not establish
  an independent collectible object format. See
  [initial research](add-from-database-001.md).
- Further read-only inspection of the available stock and addon facilities did
  not establish an independent ground-item creation and pickup API. Inventory
  item construction and world-object construction are distinct operations.
  The inspected native collection route targets a unit and transfers its loot.
  The addon collection changes examined retain that target requirement.
- Ordinary world-object subtype 56 does not by itself bind an inventory item or
  provide collection behavior. A database item definition alone does not supply
  that missing connection.
- The Endurance runtime components currently contain no independent item pickup
  implementation. Existing map-launch integration is not evidence that such a
  feature already exists or can be safely added using the same hooks.

These findings apply to the inspected facilities. They are not proof that every
possible game version or third-party extension lacks this capability. No private
source, source excerpts, addresses, or private provenance are included here.

## Compatibility decision before a runtime prototype

The concrete next option is to investigate and prototype an Endurance runtime
extension for independent ground items. This changes the compatibility contract:
maps using the feature would require the matching runtime component. Pickup in
an ordinary game launch could not be promised. This dependency must be resolved
with the user before selecting it as the implementation route.

If that dependency is accepted, prove a disposable, single-item runtime prototype
before enabling the production picker. The prototype must establish:

1. A stable item/material identity and world position loaded without new scenario
   code or a disguised unit.
2. Visible geometry, hit selection, reachable interaction and exact inventory
   transfer once; cancellation or failure must leave the item collectible.
3. State restoration across game save/load, including after collection, with no
   duplication. Multiplayer compatibility needs separate evidence before claims.
4. Version detection and clear rejection of an unsupported runtime, with no
   mutation of installed files.

Only after that proof should editor serialization, cursor placement, history and
packaging be implemented for the chosen representation. No persistence extension
or binary hook is selected or asserted safe at this checkpoint.

If ordinary unmodified game compatibility is mandatory, the next step is a known
working independent pickup example or additional capability evidence. The
already rejected script and disguised-corpse routes remain excluded.

## Confidence audit

Confirmed: the editor deliberately blocks items and lacks their creation path;
the selected weapon preview is not the current defect. Unverified: feasibility
of a new independent pickup runtime, its compatibility with both profiles and
game persistence/network behavior. No new executable or gameplay verification
is claimed. Implementation remains open pending the compatibility decision.
