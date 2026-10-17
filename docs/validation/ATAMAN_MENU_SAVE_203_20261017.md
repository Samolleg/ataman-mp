# Ataman203 — EI-MENU-SAVE-203 — 2026-10-17

## Problem and behavior

Owner's File > Save MPR command202 refused an active zone3obr edit with 'Save the world terrain session together before saving a separate draft or publishing this map.' saveLand excluded the single active dirty map from the shared-save route, then saveChangedLandscapeDocument refused any dirty shared session. Multi-map saves also fell through into a second active-map publication. Menu availability only considered the active map/publication.

Save MPR now finishes the stroke, synchronizes active data and saves any nonempty shared dirty set as one batch, then returns. Active ordinary edits join that batch. Material synchronization no longer depends on an existing working atlas. The real menu action includes shared dirty state; changes in that boolean notify MainWindow once, avoiding a new per-stamp UI update. Existing checked atlas publication,202 history rebasing and companion transaction mechanism remain in use.

## Validation

Private work: `build-menu-save-203-20261017`.
- `baseline.out`: native202 MainWindow, real mouse click opening File and selecting Save MPR. Exact owner refusal reproduced; action emitted once, then failed the expected shared-save dialog gate. This negative run is not counted as PASS.
- `final.out`:79 PASS /exit0 on fresh Release203 objects. Hidden production MainWindow and QMenu (not a replacement QAction and not direct calls to saveWorldTerrainMaps). File menu opened by QTest mouseClick, actual action rectangle clicked, production triggered signal counted.
- Actual vertex strokes on copies of DejaVu zone3obr and zone9. Cancel retains data/history; one-map save commits MPR and a resolvable atlas; Undo/Redo after save exact. Two dirty maps save together, both disk MPR states and embedded atlas bindings read back.
- A synthetic private companion participates in the same multi-map batch; successful cohort/checkpoint status remains set and callback runs once. This is not a claim of editing a live MOB/travel record.
- Clean active zone1 with dirty zone9: menu stays enabled, saves only dirty neighbor, preserves Redo through a saved Undo; resaving Redo works. Final fresh reopen of zone3obr and zone9 matches expected sector states.
- Publication-preflight callback permits only the private fixture RES. Original game data read for copies/resources only; no test writes to live maps/RES. Owner on-screen/game acceptance remains separate.

## Build and handoff

Fresh Release203 and both runtime components built, including final menu availability changes. Pending scoped commit, source-byte verification, clean staging and guarded installation over202. Preserve all owner files, retain verified202 program rollback. No GitHub publication.
