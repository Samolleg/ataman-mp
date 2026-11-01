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
   preserves basecam object IDs, copies the full MPR, and prepares a separate
   map.txt candidate. Explicit `zoneLeaves` edits may change only the selected
   nonliving object ID fields; their byte exceptions are independently verified.
5. Configure a private renderer application directory, resource copies or
   strictly read-only aliases, inert game-presence markers and copied options.
   Redirect AppData, LocalAppData, USERPROFILE, TEMP, settings, automation and
   backup directories to the private run root. Remove Base map roots, recent
   files and startup/autosave behavior. Do not use the installed mapper executable.
6. Set `EI_BRIEF_ROOT` and `EI_BRIEF_MODE=old`, then run the renderer hidden.
   It additionally applies WA_DontShowOnScreen to every top-level widget before
   display. Stop on any load-error dialog. It captures the old terrain/MOB first.
   The new run captures the scene and regenerates only its embedded MOB AI graph with CView's production
   collision/terrain inputs, CAiGraphPreviewBuilder and standard graph finalization.
   `verify` freshly loads that final MOB and checks the production no-op save.
7. Run `python -B finish.py report <run-root> --cli <briefing_zone.exe>` to produce
   transfer/review CSVs and an independent byte-boundary verification receipt.
   Recheck input hashes, inspect the PNGs, and stop for owner review. This tool
   never bakes Nav64 and never authorizes live installation.

## Reviewed revision

`audit <request.json> <result.json>` uses CMobDocument and
CScriptReferenceAnalyzer on a list of copied files. Each request entry supplies
`path`, provenance `source`, and `kind` (`mob` or `text`); archive text members may
also supply `member`. Keep this inventory and the streamed archive-index evidence
in `reference-audit/`, outside Git. A matching small ID in another MOB is a separate
scope, not permission to rewrite a briefing unit reference.

`revise.py prepare` consumes that audit, reserves all audited object IDs and
literal references, and assigns free IDs within the original declared range to
the 22 conflicting static records. It refuses unhandled basecam lookups to removed
statics or computed ID expressions instead of silently leaving them unresolved.
The original basecam unit IDs and all other unit fields remain protected.

Run renderer modes `compare-old` and `compare-new` against the same resource
snapshot. They wait for the production first-frame deferred terrain work, settle
the scene, and use human layer 3 of the native classifier. `revise.py adjust` moves
only newly blocked walking destinations to the closest passable half-metre cell
centre within 2 m. Previously blocked placements and aim targets stay as authored;
uncertain landmarks remain review findings even after a passability correction.

Run `new`, `verify`, `finish.py report`, then `revise.py review`. The review checks
the final full-graph classifier against the comparison, requires zero parser
warnings, and exports static ID exceptions, all original blocker classifications,
adjustments, and marker requests. `mark-old` / `mark-new` capture production scenes
with only doubtful points labelled by physical review.csv line (header = 1).
Coincident points share a label; leader lines and separated boxes retain every
row number. Original-coordinate and new-coordinate captures use the same view.

The revision preserves the original edge-zone destination verbatim. Only bz1g's
deploy/remove rectangles and restrict centre change. The default catalog mode
validates map.txt against the original archive without writing any texts.res;
review captures clear travel overlays rather than patching a resource archive.

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

- Nonliving source subtrees are compared byte-for-byte in order, with exceptions
  restricted to the explicitly declared four-byte ID leaves. Lights, sounds,
  particles, model fields, unknown data and all other bytes must match.
- Each unit subtree is independently reconstructed from its original bytes and
  the declared position/orientation leaf edits; all other bytes must match.
- The embedded script cipher retains its original key. Plaintext is treated as
  bytes; only numeric tokens at recognized coordinate argument positions change.
  Comments, line endings, IDs, timers, strings and scenario code stay exact.
- Other root scenario blocks and diplomacy are unchanged. The AI graph is a
  separate, explicitly replaced section. No AddMob closure is imported.
- Catalog editing prepares only bz1g local coordinates. The original destination,
  all other bz1g data and the entire gz1g registration are preserved. Eventual
  publication must merge the named member against the then-current live archive.
- Human passability is the mapper's static classifier, not a gameplay test or
  an animal-specific routing guarantee. A look target inside an object can be
  intentional. Review counts include repeated and disabled logic coordinates.
- Automated loading, raw equality and screenshots do not execute the scenario
  or replace owner review of uncertain landmark matches and duplicate IDs.

`python -B test_migrate.py` checks nested calls, symbolic coordinates, comments,
quoted prose, source token offsets and cipher preservation of all 256 byte values.

## Shared-terrain rigid-group revision

`rigid.py prepare <new-root> --previous <reviewed-root> --config <request.json>
--cli <briefing_zone.exe>` consumes original inspect/input copies plus an external
request: `unitIds`, two `landmarks` (`old`/`new` object IDs), reviewed `inventory`
matches, `removeRows` and `cameraFocus`. Two retained landmarks determine a single
rotation and translation; no scale or independent passability nudges are applied
to members. Every stored guard/patrol/look coordinate of each selected unit follows
that frame, including disabled logic. Entity Z offsets and zero ground placeholders
stay authored; explicit nonzero logic Z retains its difference from the sampled
old surface on the new surface. Quaternion directions use the same rotation.

The shared-MPR compose option writes only the MOB. The catalog candidate changes
the selected briefing's MPR reference to the existing terrain name, preserves its
MOB, type and original edge destination, and retains the reviewed exit rectangles.
Native RES member replacement writes a separate work candidate; all other members
and metadata are validated. Existing matched inventory subtrees stay exact and
are never duplicated. Missing inventory requires explicit source paths/free IDs.
Only complete, individually selected effect-call lines can be removed; a line
containing any other call is rejected.

The optional `heights` upgrade uses the native `patch` transaction to update an
already composed candidate's declared logic leaves while retaining its rebuilt
graph. `report` independently verifies static bytes/ID exceptions, unit noncoordinate
bytes, script-coordinate spans plus authorized deletions, scenario blocks, IDs,
lookups and every pairwise XY distance. Input identities remain external private
run data. `test_rigid.py` exercises rigidity/no scaling and exact line-deletion
boundaries; run it together with `test_migrate.py`.

`scene.json` supplies the existing working MPR path and candidate map.txt. New-scene
rendering opens that terrain and the separate MOB using production `openMapPair`
with an explicit briefing Zone ID; it requires the resulting identity to be brief.
`squad-old`/`squad-new` use explicit, ground-relative matching cameras. `mark-new`
shows only remaining findings. `compare-rigid` investigates common translations
within 2 m at 0.25 m intervals without applying them. This sampled search does not
prove that every continuous translation is impossible.

The numbered `review.csv` retains the previous physical row slots: resolved rows
are OK, removed effects are marked removed, and only open findings are annotated.
`review-open.csv` is the compact list of open findings. This keeps the owner's
existing screenshot numbers stable while the review set shrinks. For briefing
zones the mapper intentionally hides the minimap; the production terrain view
still uses the catalog-selected MPR. Nav64 identities include both MPR and MOB;
an existing mesh for a different MOB must never be substituted.

## Explicit static deletion and live-catalog merge

Native `remove <request.json> <candidate.mob>` requires each static object's
section path and original subtree SHA. It rejects unit deletion, duplicate or
missing requests, preserves all retained raw records in order, and strict-loads
the result. Graph regeneration remains a separate production renderer operation.
`prune.py verify` independently reconstructs the entire final file from the
original, the explicit deleted subtree set, the final embedded graph and changed
ancestor lengths. Unit, script and all retained object bytes must be identical;
parser warnings, duplicate IDs or incoming script/ParentID references fail.

`prune.py catalog` starts from a newly copied current archive. It requires the
target zone still to match the reviewed baseline, merges only the approved MPR
and local deploy/remove/restrict changes, and preserves the rest of the current
map.txt byte-for-byte. Native member replacement validates every other archive
member. `test_prune.py` checks preservation of concurrent owner changes elsewhere,
refusal of a changed target, semantic exit changes and duplicate zone definitions.
These commands only prepare candidates; live deployment needs explicit owner
authorization and a separately reviewed process/lock/SHA/backup/atomic-replace
procedure. Never substitute a previous whole working archive for the current one.

Renderer scene requests may explicitly select `zoneId`, `zoneType`, `mob`,
`unitCount`, `mobCount` and `validateFamily`. This supports a game zone's unchanged
AddMob closure alongside its primary MOB. The exact primary owns its regenerated
embedded graph and no-op proof. Native family validation and the production log
must contain zero AddMob/dependency/duplicate-ID load warnings; unrelated legacy
family range findings are recorded separately. Post-install validation and captures
must use a fresh copy of the installed profile, with only private settings/locks.
Delete owned test profile copies after all required checks pass, retaining the
reports, screenshots, source/candidate identities and deployment receipts.
