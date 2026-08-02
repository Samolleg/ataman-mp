# 0.3.0-alpha.26 character body-part validation r6 test

## Changes

- Helmet hair preview and validation now use one database/assembly decision.
  The editor loads, removes, replaces, or restores the database-selected hair
  member the same way for every database-equipped character type.
- Plain `hr` selects database hair only for the legacy `unhuma` and `unhufe`
  assemblies. Orcs, equipped undead/skeletons, and other assemblies require
  their exact database member such as `hr.00` or `hr.01`.
- A warning is emitted only when the database-selected helmet leaves hair
  enabled under that algorithm. It is concise and gives the effective token:
  `NID ...: helmet TT leaves hair enabled; enable 'PART' so the game hides it.`
- Empty/all-parts masks, authored `hr.NN.armorTT` replacements, hairless
  database rows, Import-local equipment, and unavailable assembly members use
  their separate source states instead of a guessed family/zone rule.
- Validation remains report-only and lossless. It does not repair data, block
  Save/Test Current Map, or normalize unknown body-part tokens.

## Focused test

Use copied test MOBs only; do not edit an installed map.

1. Open JMV `zone3obr` and compare NIDs 4500 and 4634. Their hair should remain
   enabled under helmet 04 in the editor, matching the current game result.
2. On NID 4500, verify plain `hr` does not hide hair, then enable `hr.01` and
   confirm it does. On NID 4634, use `hr.00`.
3. Open `Autozone3obradd` NID 800004. Confirm its warning suggests plain `hr`;
   enabling `hr` should hide its database hair under helmet 06.
4. Confirm the validation messages contain only NID, helmet, problem, and
   suggested part--not the full serialized array or prototype details.
5. Recheck Parts multi-selection: clicking a checkbox in selected rows applies
   that state to all selected rows and keeps the selection.

## Known limitations

- The warning proves that hair remains enabled under the runtime part-selection
  algorithm; it deliberately does not claim that every enabled hair/helmet mesh
  pair visibly intersects.
- The exact `hr.01`/`hr.00` repairs for NIDs 4500/4634 are source- and
  assembly-derived but still need the focused in-game confirmation above.
- No automatic repair action is provided.
