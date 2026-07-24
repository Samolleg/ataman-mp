# Landscape UI stability validation R50

## Changelist

- add a generated two-green-mountain Landscape Tools icon with stone caps;
- open Landscape Tools above Loaded Map/Maps Browser in its dock column;
- defer hidden-state processing across Qt's transient undock visibility event,
  preventing dock-layout re-entry and brush cancellation during undocking;
- move the AI navigation widget to the viewport's bottom-left corner;
- make the graph widget opaque so old label/button pixels cannot remain cached
  over the OpenGL viewport;
- widen and relayout graph source data onto stable separate lines;
- place Loaded graph override before Regenerate preview;
- add minimize/expand beside Show/Hide graph;
- use only lossless-document NID fields for duplicate detection, eliminating
  false duplicate logs from projected objects with no authored NID.

## Automated evidence

- `zone1testhut.mob --validate --json`: strict coverage of all 1,588,711 bytes,
  1,701 objects, byte-identical no-op, and no duplicate-NID finding;
- supplied MOB SHA-256:
  `E95A4AC4E9B0E22B6B38FBD8B750476F3B804EF731B40CD7CEBA7467C3067609`;
- the generated icon is stored as project-local PNG and multi-resolution ICO;
- focused tests, release build, and headless widget checks must pass before the
  candidate is packaged.

## Manual test cases

1. Open Landscape Tools. Confirm the mountain icon appears on the viewport
   button and View menu. Confirm Landscape Tools is above Loaded Map.
2. Begin a paint stroke, release it, then undock Landscape Tools. Confirm there
   is no crash, the tools remain active, and they can be docked again.
3. Close and reopen docked and floating Landscape Tools using the cross, `L`,
   View menu, and viewport button. Confirm the ordinary layout restores.
4. Regenerate the AI graph. Confirm its widget is bottom-left; the opaque
   background contains no stale text or buttons; source and dimensions do not
   overlap; Loaded graph override precedes Regenerate preview.
5. Use the new minimize/expand control. Show/Hide graph remains available while
   minimized and the widget stays anchored to the bottom-left on resize.
6. Open `zone1testhut.mob` and MOB Family Validation. Confirm there is no
   duplicate-NID warning and the outliner remains consistent with validation.

No test step saves or replaces the supplied game-side map files.
