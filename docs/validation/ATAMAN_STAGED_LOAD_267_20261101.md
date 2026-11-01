# Ataman267 — staged workspace loading, 2026-11-01

Task EI-STAGED-LOAD-267-20261101. Baseline main
`4f6648d1d820954495de2c76e4a084987e6d4ea8`, installed266.
The owner's requested order is implemented:

1. Active MPR, then primary MOB, under the existing foreground load fence.
2. Recursive AddMob dependencies after returning control to the event loop.
3. Touching-neighbor terrain, if neighbor display is enabled.
4. Static scenery from those touching maps' MOBs.
5. Remaining neighbor MPRs.

MPR-only open remains terrain-only. Collections open their explicit MOBs first.
The neighbor objects retain their existing scenery scope; this change does not
introduce NPCs, sounds or gameplay helpers into background maps.

## Implementation and preservation

- Detached workers read/strictly parse a MOB, create its recovery snapshot and
  align raw object provenance. Node/GL construction remains in the owning GUI
  context, at most16 objects with a4ms budget per pulse. Partial documents are
  never registered. A fully validated document is adopted without activating it
  or clearing Undo. Projection/backup/source validation failures remain warnings
  and do not prevent traversal of other dependencies.
- Canonical physical file identities deduplicate aliases, shared dependencies
  and cycles. Surviving explicit roots resume when another manual open interrupts
  loading. Map/source/script/resource changes invalidate the pending generation.
  Dirty-transition Cancel leaves the current documents intact.
- Neighbor stages have worker/upload barriers, also on cache hits. Source
  fingerprints are checked between terrain and object stages and before adopting
  worker results. Turning neighbors off cancels pending results. CPU workers own
  values, never GUI nodes or GL handles.
- MCP exposes `loadingAddMobs`; edit/export waits for a complete editable family.
  `neighborLoadStage` reports the neighbor phase. Visual-sequence diagnostics
  cannot finish between primary visuals and later AddMob adoption.

## Native checks

Fresh source/shadow build:
`work/staged-load-267-20261101/source267-root.txt` and `build267-result.json`.
All runs use hidden native Qt/OpenGL windows and isolated settings/recovery paths.
No interactive owner editor or game was launched.

- `staged267-r2/stdout.txt`: **73 PASS**. Actual private zone3obr primary plus
  seven AddMob files;2019 authored/display positions match266 after asynchronous
  visual compositions settle; all8 no-op save candidates equal the raw files.
  Primary readiness before dependencies, GUI heartbeat, missing/standalone MOB,
  recursive/shared/cyclic/invalid dependencies, collection activation, dirty
  Cancel, cancellation mid-projection and script changes passed. Neighbor trace
  `[1,2,3,0]` proves touching terrain → touching scenery → remaining terrain;
  disabled display, warm cache ordering and toggle cancellation passed.
- `staged267-extra-final/stdout.txt`: **24 PASS** on the final source. A large
  dependency resumes after another explicit open; selected MOB and Undo history
  survive;10 no-op candidates remain exact. External source replacement and
  resource generation changes reject stale projections. Visual diagnostics wait
  for AddMob and resume after cancellation.
- Initial `staged267-r1` deliberately remains recorded as FAIL: the test used
  `QDialog::reject()` instead of clicking QMessageBox Cancel. Its synthetic
  cancellation driver was corrected; the real Cancel scenario then passed.

### Timing boundary

Same private inputs/providers/viewport as the266 probe, **with MPR already open**:

| Measurement | 267 |
| --- | ---: |
| Primary MOB load | 3321ms |
| First scene frame | 2473ms |
| Primary scene ready | 5794ms |
| All eight MOBs adopted | 9556ms |
| GUI heartbeat during AddMob | 66 callbacks |
| Largest observed background heartbeat gap | 495ms |

The earlier266 ready measurement was11881ms. A first267 cold run took8144ms
to the primary frame; these are local observations under variable host/cache
conditions, not a universal speed guarantee. A4ms slice budget does not interrupt
one expensive resource operation or final document adoption. First-frame resource
preparation and GPU publication can still cause pauses. Full pair-open time also
includes loading the active MPR.

## Export, package and installation

`work/zone9-export-255/staged267-export/stdout.txt`: **51 PASS** on final267,
normal test-process priority, unchanged120s export deadline. Full zone9 and all
layers, real tree ID29, ordinary editor capture, resource provenance and genuine
missing-resource refusal passed. All11 PNG files are byte-identical to266:
`work/staged-load-267-20261101/export-comparison.json`. Total **148 native checks**.

Guarded installation receipts will be recorded after packaging. Until then this
entry describes a tested implementation, not an installed release.

## Input guards

`work/staged-load-267-20261101/input-verification.json`:102 private inputs
unchanged; original foreign ROADMAP suffix and translation QM preserved.
Live `texts.res` differs from the old fixture snapshot because of an external
change already observed before this task; it was not overwritten.

Ordinary-window interaction/visual acceptance remains with the owner. The native
tests establish the requested stage order, preservation and cancellation behavior.
