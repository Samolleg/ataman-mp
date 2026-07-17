# Alpha.41 redo and multi-edit validation

Alpha.41 fixes the alpha.40 failure where a same-MOB paste could be saved,
undone, saved again, redone, and then refused by the raw-provenance safety gate.
The refusal was fail-closed: the destination and backups were not changed.

## Supplied evidence

- `zone1test.mob` and `zone1test3a.mob` are byte-identical:
  `B3630692B1E1E5AF1CA5B54E2007AB7566A16502D9672CDD3A980EB993112411`.
- `zone1test3.mob` is the five-object version:
  `A4FC8002F7AE0496E8695392F748CE5D51822EE57CEABC352F45ACD65F662BBA`.
- Strict parsing covered every byte in all three supplied MOBs. Their no-op
  serializations are byte-identical. Object counts are 1693, 1698, and 1693.
- The workflow log shows successful provenance rebinding after both saves and
  the later refusal at `mob.object_sequence.ui_provenance [1425]` after redo.
- The same log records the open-time backup for `zone1test.mob` under the
  Endurance LocalAppData backup store.

## Automated checks

- Production Windows x64 Qt5/MinGW build: pass.
- Backup manager, duplicate-ID report, lossless document, resource name,
  resource profile, runtime provider, and frustum suites: pass.
- Headless UI check: pass, including mixed model-parts visibility, compatible
  multi-field paste, atomic paste undo/redo, and atomic logic-point undo/redo.
- Supplied MOB validate/round-trip checks: pass.
- Version probe: `EI Mapper Endurance Edition 0.1.0-alpha.41`.

## Focused tester sequence

1. On a disposable MOB, paste several objects and finish placement.
2. Save As version A, undo to the original state, and Save As version B.
3. Redo the pasted objects and save version C. The save must succeed.
4. Repeat with patrol and look points. Creation plus placement must be one
   history item in both directions.
5. Reopen every output, compare it against the intended baseline, and test the
   changed map in JMV with `drawall`, unload/reload, and graceful exit.
