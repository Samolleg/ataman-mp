# EI Mapper Endurance 0.1.0-alpha.17 validation

## Exact compound movement undo

Alpha.16 could leave a MOB marked changed after undoing a unit move when the
unit had patrol or look points. Unit movement translated child coordinates by
floating-point deltas, and undo applied the inverse delta. Repeated arithmetic
did not always reproduce the original float bytes.

Alpha.17 snapshots and restores the complete compound movement state:

- Unit position.
- Guard placement.
- Every patrol-point position.
- Every look-point position.

Undo and redo restore those snapshots directly instead of deriving child
positions through inverse arithmetic. Dirty-state comparison remains exact.

## Undo history

The previous `QUndoStack` was unlimited. General settings now include **Undo
history limit**, defaulting to 512 commands. The configured limit is applied on
the next editor start because Qt only permits setting the limit on an empty
undo stack.

## Focused manual test

1. Open a copied MOB containing a unit with patrol and look points.
2. Move the unit in object mode and confirm all attached points move with it.
3. Undo and confirm the unit and every attached point return exactly and the
   changed marker clears.
4. Save As and require a byte-identical SHA-256 match with the source.
5. Redo, confirm the changed marker returns, then undo again and confirm it
   clears consistently over several cycles.
6. Change Undo history limit, restart, and confirm the workflow log records the
   configured value.

## Local validation

- Windows x64 Qt 5.12.12/MinGW release build: passed.
- Existing document, backup, resource-profile, resource-name, duplicate-ID,
  and runtime-provider suites: 44 passed, 0 failed.
- Exact compound movement reproduction: pending tester confirmation on the
  reported MOB/unit.
