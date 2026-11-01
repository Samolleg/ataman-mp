# Briefing-zone preparation utility

This developer utility links the mapper's accepted production objects. It does
not add editor behavior, change VERSION, install a mapper, publish a mod, or
contain private map data. All map inputs, landmark correspondences, intermediate
files and outputs belong in an external, private run directory.

Build `briefing_zone.pro` and `render.pro` with the matching Qt/MinGW kit and
`BUILD_ROOT=<accepted shadow-build directory>`. The source tree adjacent to
BUILD_ROOT must match that build. Do not link an editor build containing other
contributors' unfinished changes. Both targets exclude the application's main
and assembly-check entry points; the renderer creates its own isolated process
with the production MainWindow and CView.

## Workflow

1. Copy the authorized MPR/MOB inputs with shared read access. Record their
   SHA-256 values and guard them again at completion. Keep all output paths
   outside live resources and installed editor directories.
2. Run `briefing_zone inspect <copy> <json>` for each source MPR/MOB and copied
   texts archive. CMobDocument and CMprDocument perform strict parsing and
   byte-identical no-op serialization checks. The inspect JSON includes raw leaf
   payloads so private scenario text never needs to be embedded in this tool.
3. Supply a reviewed landmark JSON array with `old`/`new` object IDs, `label`,
   correspondence evidence, and `review: true` where uncertain. Run
   `python -B migrate.py <run-root> <landmarks.json> --cli <briefing_zone.exe>`.
   The run directory's `in/` and `inspect/` files follow the filenames used by
   this specific basecamp task. No automatic correspondence search is performed.
4. Run `python -B finish.py compose <run-root> --cli <briefing_zone.exe>`.
   It applies provenance-checked leaf edits and native raw-subtree composition,
   preserves the original object IDs, copies the full MPR, and prepares a separate
   catalog candidate. Conflicting IDs are reported rather than silently repaired.
5. Configure a private renderer application directory, resource copies or
   strictly read-only aliases, inert game-presence markers and copied options.
   Redirect AppData, LocalAppData, USERPROFILE, TEMP, settings, automation and
   backup directories to the private run root. Remove Base map roots, recent
   files and startup/autosave behavior. Do not use the installed mapper executable.
6. Set `EI_BRIEF_ROOT` and `EI_BRIEF_MODE=old`, then run the renderer hidden.
   It additionally applies WA_DontShowOnScreen to every top-level widget before
   display. Stop on any load-error dialog. It captures the old terrain/MOB first.
   For `new`, bind the *private* resource copy to the catalog candidate using
   atomic file replacement; never write through a hardlink. The new run captures
   the scene and regenerates only its embedded MOB AI graph with CView's production
   collision/terrain inputs, CAiGraphPreviewBuilder and standard graph finalization.
   `verify` freshly loads that final MOB and checks the production no-op save.
7. Run `python -B finish.py report <run-root> --cli <briefing_zone.exe>` to produce
   transfer/review CSVs and an independent byte-boundary verification receipt.
   Recheck input hashes, inspect the PNGs, and stop for owner review. This tool
   never bakes Nav64 and never authorizes live installation.

`sample` reads a copied MPR through CTerrainEditModel and samples its displaced
terrain surface. Unit and MOB logic Z values retain their terrain-relative
offsets; SetCP's explicit destination Z follows the new terrain. Effects retain
their altitude above the original surface. XY positions use the nearest reviewed
landmark's local frame, including its yaw; unit orientation follows the same
rotation. Distant and uncertain matches, blocked targets, water and ID conflicts
remain review findings. Nothing is moved automatically merely to hide a finding.

MPR MP/SEC member names form a terrain/atlas namespace, not a briefing Zone ID.
The exact new MPR namespace and material payloads are retained. The external
basegipat/basecam registration and zone type remain in map.txt; changing the MPR
namespace without corresponding textures would invalidate the atlas binding.

## Verification boundaries

- Nonliving source object subtrees are compared byte-for-byte, including order,
  duplicate records, lights, sounds, particles and unknown fields.
- Each unit subtree is independently reconstructed from its original bytes and
  the declared position/orientation leaf edits; all other bytes must match.
- The embedded script cipher retains its original key. Plaintext is treated as
  bytes; only numeric tokens at recognized coordinate argument positions change.
  Comments, line endings, IDs, timers, strings and scenario code stay exact.
- Other root scenario blocks and diplomacy are unchanged. The AI graph is a
  separate, explicitly replaced section. No AddMob closure is imported.
- Catalog editing prepares only the bz1g local coordinates and the paired link;
  the gz1g source registration is preserved. The entire candidate texts archive
  is a review artifact; eventual installation must merge its named member against
  the then-current live archive rather than overwrite unrelated resource edits.
- Human passability is the mapper's static classifier, not a gameplay test or
  an animal-specific routing guarantee. A look target inside an object can be
  intentional. Review counts include repeated and disabled logic coordinates.
- Automated loading, raw equality and screenshots do not execute the scenario
  or replace owner review of uncertain landmark matches and duplicate IDs.

`python -B test_migrate.py` checks nested calls, symbolic coordinates, comments,
quoted prose, source token offsets and cipher preservation of all 256 byte values.
