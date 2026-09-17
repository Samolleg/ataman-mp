# Ataman 15 — JSON-owned editor worlds

2026-09-17. EI-WORLD-ADD-015. Branch `codex/world-json-v15`, base
`edbc82ecf0b0ec4d9fc07e418e4740d8995c2816` (installed Ataman 14).

## Result and ownership

- MainWindow builds its separate editor-world context from
  `world-map-layouts.json`, physical MPR dimensions and resource paths. It no
  longer calls map.txt inference to populate or reset the viewport world.
  Existing game catalogue/test/entrance features retain their own identity.
- Schema 2 adds resource paths, names and dimensions to stored world records.
  Legacy field names `allodId`/`zoneId` remain for compatibility; new records
  have independent IDs, and worlds no longer require connected travel routes.
  Schema 1 reads without coordinate loss. Before first v2 write, its exact bytes
  are backed up to `.before-v2.bak`; lock and byte comparison reject concurrent
  changes. Missing/bad JSON does not trigger map.txt fallback.
- Legacy resource identities resolve by unambiguous filename in configured
  map roots. A same-name MOB is used only when unambiguous. Different MOB names
  must be chosen in World maps; their identity cannot be recovered from old
  coordinate-only JSON without the removed game-table dependency.
- Add map: choose profile/file MPR and optional MOB, validate documents, preview
  bounds and coordinates, snap edges within two world units, LMB commits, Esc
  cancels. The first map is placed at 0,0; an open map not yet in the layout is
  registered with it as the world base. No automatic copying of source files.
- World maps lists every record, opens optional-MOB terrain, prioritizes a
  selected neighbor, edits resource links/name/coordinates and removes membership.
  Files are never deleted by these operations. Removing an anchor reassigns its
  references while preserving absolute coordinates. Missing files retain records.
- Five neighbors remain the upload/display budget. Newly added/explicitly selected
  maps have priority. Duplicate legacy physical MPRs are retained in JSON but
  rendered once; manager navigation keeps the selected record's world coordinates.
- In-profile paths are relative; external paths remain absolute. Added maps use
  the active profile's existing renderer/resource resolution. This is not a resource
  importer or a command to register game locations or manufacture transitions.

## Checks

- Fresh `build-release-15-world`, log `build-world15.log`: Release build PASS.
- `build-tests-15-world_map_layout_document/results.txt`: 17 PASS, zero failures.
  JSON-only empty world, append/reopen, duplicates, replace, missing resources,
  external-write conflict, anchor removal, profile isolation, migration/backup,
  preservation of coordinates and source files; inherited document regression.
- `build-tests-15-neighbor_map_layout/results.txt`: 7 PASS, zero failures.
  World placement independent of travel links, profile/world boundaries,
  coordinate transforms and suppression of duplicate physical terrain.
- Hidden desktop-OpenGL real-resource integration on zone9 through
  `EI_WORLD_JSON_CHECK=1` plus existing neighbor-check environment: 23 PASS,
  exit 0 (`neighbor-perf.stdout.txt`, `neighbor-perf.json` in the shadow folder).
  Covers CPU preparation, GPU upload, generation cancellation, geometry/cache,
  placement preview, rendered overlay, Esc, one-click confirmation, camera
  preservation and priority over the five-neighbor budget. Actual MPR/MOB hashes
  are unchanged. JSON modifications in tests use temporary fixture directories.
- The initial placement-frame comparison failed because direct `paintGL()` did
  not present the new buffer. The harness now uses `updateGL()` and checks the
  presented image. Capture inspected: terrain and colored preview present; the
  hidden surface uses partial viewport coverage, so this is submission evidence,
  not full user-window visual acceptance.
- Version, database creation, resource navigation, normal scenarios (47/47): PASS.
  General UI-widget check: timeout 120 seconds, same prior limitation, not PASS.
  Its owned process was stopped. A build attempted while that test held the EXE
  failed at linking; rebuilding after test exit passed. Early compile failures
  were corrected before validation; none are counted as successful builds.

## Manual acceptance still required

In the ordinary user window, add a new MPR/MOB, move its preview and cancel;
then place it, reopen the program, open it from World maps, change coordinates
and remove it from the layout. Also open a terrain-only MPR, select a missing
file's replacement and confirm the scene after migration from the user's JSON.
Interactive file dialogs and the full MainWindow workflow have not been driven
automatically. Original neighbor-scene visual/FPS acceptance remains open.
Installed game resources and the user's actual layout were not modified by the
agent. The new runtime writes layout changes only when the user performs them.
