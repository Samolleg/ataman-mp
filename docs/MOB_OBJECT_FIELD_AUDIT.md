# MOB object-field manual audit

This audit compares `Obj_EN.html` and `Obj_RU.html` with Endurance's parser, serializer, and current
property UI. The manuals are useful tool-author documentation, but they are not an authoritative
engine schema.

The proposed English display labels, enum values, and concise evidence-only
tooltips are maintained in [`OBJECT_PROPERTY_TERMINOLOGY.md`](OBJECT_PROPERTY_TERMINOLOGY.md).
That specification changes presentation only; serialized field names and values
remain unchanged.

## Language comparison

The documents contain the same eight object families, 22 headings, 13 tables, and 80
signature-bearing rows. No field or section is present in only one language. The only notable wording
difference is `ACTION_PT_TURN_SPEED`: Russian describes movement/turning speed to a point, while
English says movement speed. This needs engine evidence before its label or behavior changes.

The reviewed English file has SHA-256
`8ED168287FD9A26C51447281C7C116A5AB3987C2077F08208B451DE21FE7093E`; the Russian file has
`5A53DBCFDE5D73C4F4ABCE92271C803EAC53A74FFCFB691A38B9F99D7603C002`.

## Useful additions to the editor UI

| Area | Current state | Recommended addition |
|---|---|---|
| Common object | Secondary texture and read-only ordinary object type are exposed | Keep resource validation profile-aware |
| Logic | All five slots are editable and appear as collapsed owner hierarchies in Behavior mode | Add route-level operations such as reverse and cyclic-route previews |
| Look/action point | Owned points are listed, navigable, copyable, and append-pastable with undo | Add an exact compound table for batch coordinate/timing edits |
| Magic trap | Areas and targets appear below their trap and support parent/sibling navigation | Add an exact list/table view for coordinates, radius, ordering, and count |
| Unit | Inventory strings and 51 Stats values exist | Add database-backed validation, enum/unit tooltips, and keep Stats collapsible/advanced |
| Lever | Science tuple is split into vague labels | Group and explain the tuple while preserving its raw values |
| Sound/light/particle | Values are editable but several labels lack units or symbolic meanings | Add tooltips, named particle/type choices, and raw fallback |
| References | Parent, ParentID, object ID, InScript, and QuestInfo are editable | Validate known references and warn when an ID appears in scripts before changing it |

Fields backed by resource/database names should support autocomplete and validation without forbidding
profile-specific JMV values.

Endurance exposes, draws, and edits all five serialized unit logic slots through
the selector shown in Logic mode. Undo/redo records the selected slot so a later
slot switch cannot redirect an existing history entry. Each slot owns a separate
ordered patrol array. Magic traps do not have logic slots; each trap owns one
ordered activation-area array and one ordered cast-target array.

## Better kept hidden or advanced

Record signatures and lengths, reserved/null tags, raw quaternion values, record ordering internals,
AI graph and VSS internals, and raw directory-membership IDs should not clutter the normal property
view. They belong in diagnostics or an advanced/raw inspector. The typed editor must still preserve
them exactly.

## Confirmed manual error: `0xBBAB0005`

The manuals correctly associate `0xBBAB0005` with magic-trap reloading, but incorrectly describe
its representation as a Byte boolean. A hashed Base/JMV/legacy corpus audit found 47 occurrences in
16 paths. Every occurrence declares 12 bytes and contains a four-byte little-endian payload; the
observed values are `15, 25, 30, 43, 44, 50, 74, 75, 90, 100, 104, 105, 150`, never `0` or `1`.
MobExplorer independently declares the tag as `ST_DWORD`, matching installed EILib and Endurance's
`MT_CAST_INTERVAL` implementation.

The adjacent `0xBBAC0005` field is not part of that discrepancy. All 47 paired occurrences declare
9 bytes and contain the one-byte value `0` or `1`, matching `LEVER_CAST_ONCE`/CastOnce. Thus
"reloading" and "cast interval" are compatible descriptions of `0xBBAB0005`; only the manuals'
Byte/boolean detail is wrong. Base-engine disassembly confirms that the DWord is copied unchanged
into a runtime countdown, decremented once per logic update, and reset after casting. It is therefore
measured in the engine's 15 Hz game ticks: `15` is one second and `150` is ten seconds. Preserve the
raw integer; a UI may show the exact `ticks / 15` conversion without rounding the stored value. See
`docs/validation/MT_CAST_INTERVAL_AUDIT.md`.

## MOB information absent from the manuals

The manuals omit document-level and management data: `OBJECT_DB_FILE`, SC/PR type, embedded scripts,
main/secondary ID ranges, diplomacy and player names, world wind/time/ambient/sunlight, VSS, AI
graphs, and `DIRICTORY`/`FOLDER`/`DIRICTORY_ELEMENTS`. They also omit optional/legacy records such as
`OBJ_INDEX`, `OBJ_TEXTURE`, `OBJ_DEF_LOGIC`, `OBJ_R`, `SOUND_VOLUME`, `UNIT_R`, `UNIT_ITEMS`, old logic
markers, and unknown/profile-specific Base/JMV payloads.

## `zone13 (6).mob`: confirmed MapED defect

SHA-256: `3FCBE733F01C2510CB62741E5622CC66DBCE76C37B4D2FAD3B766C7FDD4331AD`.

- At offset `0x2B6EF`, `MT_AREAS` declares `0x20` bytes for two 12-byte tuples. The exact total is
  `8-byte header + 4-byte count + 24-byte payload = 0x24`; it is four bytes short.
- At offset `0x2B713`, `MT_TARGETS` declares `0x10` bytes for one 8-byte tuple. The exact total is
  `8-byte header + 4-byte count + 8-byte payload = 0x14`; it is four bytes short.

The bytes exactly match the modder's report. MapEd omitted the count DWord from each declared total.
MapEd was a popular legacy editor used to create maps for multiple existing mods, so this defect is
expected to occur in historically important content. Supporting it is a targeted compatibility and
recovery measure for known MapEd output, not a general relaxation of MOB structural validation.
Endurance's generated serializer already computes both totals correctly.

The strict document parser now accepts only this exact count-derived legacy shape, retains the
malformed header for a byte-identical no-op save, and reports warning code
`mob.compat.maped_array_length` in validation and the workflow log. Endurance's array writer
calculates correct lengths, but hardened saving currently blocks direct edits to one of these
malformed arrays until a dedicated repair/edit transaction is implemented. Broader malformed
lengths are still errors.

The current byte-preserving save behavior is not the final validation policy. The roadmap now tracks
whether saving any recognized game-tolerated defect should require an explicit consolidated choice:
repair it, knowingly preserve it, or cancel. Recovery on open should remain non-destructive, while
repair remains opt-in and Endurance must never silently produce known-invalid output.

## Initial semantic warnings

The validator also reports, without blocking load or save:

- `mob.semantic.empty_trap_targets` when a magic trap contains a targets array with count zero;
- `mob.semantic.overlapping_unit_parts` when a unit explicitly enables two dotted model parts in the
  same anatomical/equipment slot (`hd`, `hp`, `bd`, `rh`, `lh`, or `lg`, including numbered hand
  roots such as `rh3`). Base limb/body entries without a dotted attachment are not treated as
  competing equipment;
- `mob.semantic.wearable_bodyparts_missing` and
  `mob.semantic.wearable_all_parts_enabled` as Critical, report-only findings
  for standard wearable assemblies, while creature/integral empty masks remain
  accepted;
- `mob.semantic.helmet_hair_fallback` when any database-equipped character has
  authored hair, the native hair and selected helmet members exist, its
  explicit mask does not load the database-selected hair, and no helmet-specific
  hair replacement exists; the suggestion is `hr` for the two legacy aliases
  and exact `hr.NN` for every other assembly;
- `mob.semantic.missing_core_bodyparts` for omitted standard non-hair anatomy;
  and
- `mob.semantic.duplicate_bodypart_token` as Info while retaining exact order
  and duplicates.

These warnings preserve the source data. They identify game-tolerated but suspicious authoring
states and are emitted to both validation output and the workflow log. Missing
`hr` by itself is intentionally ignored. Hairless database templates skip the
runtime hair path, while missing assembly evidence and Import-local appearance
produce no hair finding. The helmet rule is not limited to standard humanoid
anatomy: every character on the database equipment path uses the same
assembly-backed decision. Import units use embedded model/equipment.
Unknown/model-specific tokens remain visible and lossless and do not receive a
vocabulary-only warning. See
[`validation/CHARACTER_BODYPART_VALIDATION.md`](validation/CHARACTER_BODYPART_VALIDATION.md).

## Research queue

- Establish exact runtime semantics for ParentID, inventory and spell strings,
  optional sound records, and all 51 Stats fields.
- Keep the look-point turn-speed value as its verified unsigned 32-bit field.
- Complete object and particle type maps, directory membership semantics, optional record coverage,
  and profile-specific JMV extensions.
- Inventory other game-tolerated malformed legacy structures. Any future exception must be narrow,
  preserve raw bytes, and emit a workflow plus validation warning.
