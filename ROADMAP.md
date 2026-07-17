# EI Mapper: Endurance Edition roadmap

This is the living project checklist for the private Endurance Edition line.
An item is checked only after its implementation has landed and received the
listed level of validation. New findings and wishlist requests belong here so
they do not get lost between test sessions.

Current development release: `0.1.0-alpha.29`.

## Implementation evidence

Completed code items below carry an inline commit link. Validation and packaging
evidence is linked separately where a commit alone does not prove runtime
behavior.

| Change group | Commit | Validation |
|---|---|---|
| Endurance identity, safe no-op documents, indexed resources, independent opening, outliner, external editor | [`054959b`](https://github.com/Ilufus/ei-mapper-endurance/commit/054959b) | [initial document tests](tests/endurance_document_tests.cpp) |
| NUL-delimited MOB validation | [`34b9ec8`](https://github.com/Ilufus/ei-mapper-endurance/commit/34b9ec8) | [document tests](tests/endurance_document_tests.cpp) |
| Crash diagnostics and rotating workflow logs | [`2568e38`](https://github.com/Ilufus/ei-mapper-endurance/commit/2568e38) | [bug-report workflow](BUG_REPORTING.md) |
| Missing figures and unit path preservation | [`6454de7`](https://github.com/Ilufus/ei-mapper-endurance/commit/6454de7) | private JMV map checks |
| Generic JMV RES discovery | [`44ab888`](https://github.com/Ilufus/ei-mapper-endurance/commit/44ab888) | private JMV resource checks |
| SOUND/world serialization preservation | [`62b824a`](https://github.com/Ilufus/ei-mapper-endurance/commit/62b824a) | private changed-save comparisons |
| Unit guard/patrol height preservation | [`e0ac92a`](https://github.com/Ilufus/ei-mapper-endurance/commit/e0ac92a) | private Zone 1 changed-save comparison |
| Recoverable backups | [`8a7bc3c`](https://github.com/Ilufus/ei-mapper-endurance/commit/8a7bc3c) | [backup tests](tests/backup_manager_tests.cpp) |
| Model-qualified unit skins | [`35a43c8`](https://github.com/Ilufus/ei-mapper-endurance/commit/35a43c8) | [resource-name tests](tests/resource_name_tests.cpp) |
| Complete terrain atlases and internal map namespace | [`530bb16`](https://github.com/Ilufus/ei-mapper-endurance/commit/530bb16) | [resource-name tests](tests/resource_name_tests.cpp) |
| Consolidated duplicate-ID reporting | [`a39ef57`](https://github.com/Ilufus/ei-mapper-endurance/commit/a39ef57) | [duplicate-ID tests](tests/duplicate_id_report_tests.cpp) |
| Staged fresh-parser validation and alpha.11 preview | [`c8d8b3a`](https://github.com/Ilufus/ei-mapper-endurance/commit/c8d8b3a) | [alpha.11 validation](ALPHA11_VALIDATION.md) |
| Installation-based profiles | [`7b206c8`](https://github.com/Ilufus/ei-mapper-endurance/commit/7b206c8) | [resource-profile tests](tests/resource_profile_tests.cpp) |
| First-run and MOB-only viewport initialization | [`54d8070`](https://github.com/Ilufus/ei-mapper-endurance/commit/54d8070) | [alpha.13 validation](ALPHA13_VALIDATION.md) |
| Setup moved under Settings | [`8528799`](https://github.com/Ilufus/ei-mapper-endurance/commit/8528799) | [alpha.13 validation](ALPHA13_VALIDATION.md) |
| Database-backed humanoid candidate lookup | [`94e0b9d`](https://github.com/Ilufus/ei-mapper-endurance/commit/94e0b9d) | [resource-name tests](tests/resource_name_tests.cpp); base rendering remains unresolved |
| Alpha.14 testing-group release | [`800a011`](https://github.com/Ilufus/ei-mapper-endurance/commit/800a011) | [alpha.14 validation](ALPHA14_VALIDATION.md) |
| Capability profiles and fingerprinted Base/JMV runtime manifests | [`97b6991`](https://github.com/Ilufus/ei-mapper-endurance/commit/97b6991) | [resource-profile tests](tests/resource_profile_tests.cpp) |
| Strict raw-section MOB tree and deterministic semantic diff | [`aaef298`](https://github.com/Ilufus/ei-mapper-endurance/commit/aaef298) | [252-file corpus validation](MOB_HARDENING_VALIDATION.md) |
| Provenance-checked MOB leaf transactions | [`660a542`](https://github.com/Ilufus/ei-mapper-endurance/commit/660a542) | [document tests](tests/endurance_document_tests.cpp) |
| Fail-closed UI projection shadow transactions | [`3106fac`](https://github.com/Ilufus/ei-mapper-endurance/commit/3106fac) | [254-file corpus validation](MOB_SHADOW_TRANSACTION_VALIDATION.md) |
| Profile provider API and safe JMV test-run manifest contract | [`b467d87`](https://github.com/Ilufus/ei-mapper-endurance/commit/b467d87) | [runtime-provider tests](tests/runtime_provider_tests.cpp) |
| Filterable read-only MOB diff details dialog | [`fd30be0`](https://github.com/Ilufus/ei-mapper-endurance/commit/fd30be0) | production MinGW build and [semantic-diff tests](tests/endurance_document_tests.cpp) |
| Selection UX, recent files, open/save shortcuts, and editable model/texture filters | [`06d1c9b`](https://github.com/Ilufus/ei-mapper-endurance/commit/06d1c9b) | clean production MinGW build, six regression suites, Zone 1 no-op round trip, and [alpha.22 manual checks](ALPHA22_VALIDATION.md) |
| Semantic raw-object provenance and cached frustum culling | [`1ab2935`](https://github.com/Ilufus/ei-mapper-endurance/commit/1ab2935) | 254-file projection corpus, document/frustum tests, and [alpha.28 checks](ALPHA28_VALIDATION.md) |

## Current focus

- [x] Analyze the first structured alpha.14 testing-group changed-save report,
  including workflow/hash correlation, independent MobExplorer comparison, runtime
  resource evidence, and raw structural preservation. ([Zone 5 report](ALPHA14_TESTER_ZONE5_VALIDATION.md))
- [ ] Continue collecting structured testing-group results and focused in-game
  outcomes across additional maps and edit types.
- [x] Validate an alpha.25 real-UI diplomacy edit (`OBJ_PLAYER` 4 -> 12) against
  an independently generated MobExplorer candidate: exact full-file SHA-256 match,
  one modified object, no added/removed objects, and no unrelated field changes.
  ([validation](MOB_SHADOW_TRANSACTION_VALIDATION.md))
- [x] Ensure MOB Parameters and every other editor-created top-level window close
  with the main editor. Auxiliary windows now have explicit owners, shutdown has
  a fallback top-level sweep, and the headless widget check covers both owned and
  accidentally orphaned windows. ([`f9fa250`](https://github.com/Ilufus/ei-mapper-endurance/commit/f9fa250))
- [ ] Expand changed-save regression coverage across the private JMV map corpus.
- [x] Replace fragile index-based raw-object provenance with deterministic
  semantic alignment, including equal-count byte-identical duplicate subtrees.
  The installed Base/JMV projection corpus passes 254/254 and mismatches still
  fail closed. ([`1ab2935`](https://github.com/Ilufus/ei-mapper-endurance/commit/1ab2935))
- [x] Validate alpha.28 cached frustum culling on large JMV maps. Normal object
  rendering and GPU picking skip objects whose cached bounds are fully outside
  the camera frustum. Zone 18 improved from the former severe slowdown to a
  visually correct stable 60+ FPS in tester observation. The test exposed that
  alpha.28 disabled all culling in Logic mode and that the modal Settings owner
  was accidentally disabled; both regressions move to alpha.29.
  ([validation](ALPHA28_VALIDATION.md))
- [ ] Validate alpha.29 split object/logic rendering. Off-screen object geometry
  remains culled in Logic mode while unit paths, patrol/look points, trap cast
  points, and activation zones retain their existing visualization behavior.
  Settings now uses Qt window modality without disabling its own owner chain, so
  every tab remains selectable. ([validation](ALPHA29_VALIDATION.md))
- [ ] Migrate changed MOB saves from the legacy serializer to raw-tree transactions;
  no-op raw preservation is validated, leaf-only UI deltas run through a fail-closed
  transaction bridge, and independently validated `OBJ_PLAYER` edits now use the
  staged backup/atomic-save pipeline. Other leaf classes and all structural edits
  remain blocked. ([`3106fac`](https://github.com/Ilufus/ei-mapper-endurance/commit/3106fac),
  [`3cd2b3c`](https://github.com/Ilufus/ei-mapper-endurance/commit/3cd2b3c),
  [validation](MOB_SHADOW_TRANSACTION_VALIDATION.md))
- [x] Recompute MOB dirty state against its loaded projection after edit and
  undo/redo, allowing byte-identical Save As once every projected change is
  undone. ([`d7feaab`](https://github.com/Ilufus/ei-mapper-endurance/commit/d7feaab))
- [x] Validate exact snapshot-based undo for compound unit, patrol, and look
  point movement across repeated undo/redo cycles. Implementation:
  [`7a9bed4`](https://github.com/Ilufus/ei-mapper-endurance/commit/7a9bed4);
  confirmed in alpha.17 testing.
- [ ] Validate alpha.18 selection-history behavior and exact-instance
  create/delete redo across repeated copy/paste cycles. Implementation:
  [`99f841f`](https://github.com/Ilufus/ei-mapper-endurance/commit/99f841f);
  focused test instructions are in [ALPHA18_VALIDATION.md](ALPHA18_VALIDATION.md).
- [ ] Validate alpha.19 gesture-level undo, pasted-selection restoration,
  selected-object overlap rendering, and cancellation of unfinished transforms
  before Close All. Implementation:
  [`95c9094`](https://github.com/Ilufus/ei-mapper-endurance/commit/95c9094);
  focused test instructions are in [ALPHA19_VALIDATION.md](ALPHA19_VALIDATION.md).
- [ ] Validate alpha.20 cancellation of unfinished movement and the document
  lifecycle boundary. Open/close operations are no longer undo commands, and
  history is cleared before documents are destroyed or terrain is replaced.
  Implementation:
  [`554b0d4`](https://github.com/Ilufus/ei-mapper-endurance/commit/554b0d4);
  focused test instructions are in [ALPHA20_VALIDATION.md](ALPHA20_VALIDATION.md).
- [ ] Validate alpha.21 deferred paste transactions. Unconfirmed copies are
  preview-only and cannot create a dirty empty Redo entry; confirmed placement
  creates distinct Paste and Move entries. Implementation:
  [`ff043cf`](https://github.com/Ilufus/ei-mapper-endurance/commit/ff043cf);
  viewport selections also reveal their exact pointer-backed outliner entry,
  including duplicate-NID objects
  ([`193966e`](https://github.com/Ilufus/ei-mapper-endurance/commit/193966e)).
  Focused test instructions are in [ALPHA21_VALIDATION.md](ALPHA21_VALIDATION.md).
- [ ] Validate alpha.22 selection UX, persistent recent-file list, open/save
  shortcuts, and keyboard-filterable model/texture fields. Implementation:
  [`06d1c9b`](https://github.com/Ilufus/ei-mapper-endurance/commit/06d1c9b);
  the reported property-panel selection slowdown, unintended first-match
  application, contains-style filtering, and MPR/MOB menu ordering are fixed in
  [`0436992`](https://github.com/Ilufus/ei-mapper-endurance/commit/0436992).
  The remaining Qt combo auto-completion path and outliner-originated automatic
  scrolling are corrected in
  [`c5d785a`](https://github.com/Ilufus/ei-mapper-endurance/commit/c5d785a).
  Typed model/texture previews now commit only on Enter or an explicit dropdown
  activation; focus loss and Escape restore the accepted value
  ([`bcab5cb`](https://github.com/Ilufus/ei-mapper-endurance/commit/bcab5cb)).
  Latest focused test instructions are in
  [ALPHA24_VALIDATION.md](ALPHA24_VALIDATION.md).
- [ ] Finish lossless preservation of MPR archive details.

These are the remaining safety gates before editing live maps can be recommended.

## Alpha.14 testing campaign

- [x] Build and checksum-verify the private alpha.14 testing-group package. ([`800a011`](https://github.com/Ilufus/ei-mapper-endurance/commit/800a011), [validation](ALPHA14_VALIDATION.md))
- [x] Include source, symbols, GPL notices, validation notes, safe-test rules,
  bug-report instructions, and a shared test matrix. ([`800a011`](https://github.com/Ilufus/ei-mapper-endurance/commit/800a011))
- [x] Record base-game `zone3obr.mob` humanoid textures as a known unresolved
  issue instead of treating candidate-name lookup as a rendering fix. ([`800a011`](https://github.com/Ilufus/ei-mapper-endurance/commit/800a011))
- [ ] Collect coverage for MPR-first, MOB-first, MOB-only, terrain
  attach/detach, repeated open/close, and cold/warm loading.
- [ ] Collect narrow changed-save tests for properties, copied objects, patrol
  points, scripts, and terrain, followed by Endurance reopen, MobExplorer
  comparison, `drawall`, unload/reload, and graceful exit.
- [ ] Triage every crash against its matching rotated workflow log and exact
  reproduction steps; do not infer one cause from a shared crash address.
- [ ] Convert confirmed regressions into minimized synthetic fixtures where the
  format permits it without distributing game or JMV assets.
- [ ] Publish the next private alpha only after fixes and package validation;
  keep alpha.14 immutable once it has been handed to testers.

## Foundation and identity

- [x] Create an independent private repository with complete upstream history. ([`054959b`](https://github.com/Ilufus/ei-mapper-endurance/commit/054959b))
- [x] Keep the public project as the read-only `upstream` remote. ([`054959b`](https://github.com/Ilufus/ei-mapper-endurance/commit/054959b))
- [x] Tag the imported upstream baseline at commit `1cebe83d`. ([baseline](https://github.com/Ilufus/ei-mapper-endurance/tree/upstream-1cebe83d))
- [x] Brand the product and executable as EI Mapper: Endurance Edition. ([`054959b`](https://github.com/Ilufus/ei-mapper-endurance/commit/054959b))
- [x] Start independent SemVer at `0.1.0` and separate Endurance settings/log paths. ([`054959b`](https://github.com/Ilufus/ei-mapper-endurance/commit/054959b))
- [x] Preserve GPL-3.0, upstream attribution, history, and `NOTICE.md`. ([`054959b`](https://github.com/Ilufus/ei-mapper-endurance/commit/054959b))
- [x] Keep game assets, private paths, dumps, and integration fixtures out of Git. ([repository tree](https://github.com/Ilufus/ei-mapper-endurance/tree/main))

## Stability and safe saving

- [x] Stop automatic duplicate-ID repair while opening a MOB. ([`054959b`](https://github.com/Ilufus/ei-mapper-endurance/commit/054959b))
- [x] Report duplicate IDs without changing the source or dirty state. ([`054959b`](https://github.com/Ilufus/ei-mapper-endurance/commit/054959b), [`34b9ec8`](https://github.com/Ilufus/ei-mapper-endurance/commit/34b9ec8))
- [x] Preserve exact bytes for unchanged MOB/MPR Save As operations. ([`054959b`](https://github.com/Ilufus/ei-mapper-endurance/commit/054959b))
- [x] Use `QSaveFile` for atomic replacement. ([`054959b`](https://github.com/Ilufus/ei-mapper-endurance/commit/054959b))
- [x] Fix sunlight/ambient serialization confusion. ([`054959b`](https://github.com/Ilufus/ei-mapper-endurance/commit/054959b))
- [x] Preserve unit prototype trailing bytes required by existing maps. ([`6454de7`](https://github.com/Ilufus/ei-mapper-endurance/commit/6454de7))
- [x] Keep patrol/look coordinates and unit height in stored map coordinates. ([`e0ac92a`](https://github.com/Ilufus/ei-mapper-endurance/commit/e0ac92a))
- [x] Correct SOUND field ordering, wind float preservation, and optional VSS handling. ([`62b824a`](https://github.com/Ilufus/ei-mapper-endurance/commit/62b824a))
- [x] Add deduplicated open snapshots and timestamped pre-save backups. ([alpha.7](https://github.com/Ilufus/ei-mapper-endurance/commit/8a7bc3c))
- [x] Refuse replacement when the required pre-save backup fails. ([`8a7bc3c`](https://github.com/Ilufus/ei-mapper-endurance/commit/8a7bc3c))
- [x] Add a rotating backup retention policy and **Tools > Open Backup Folder**. ([`8a7bc3c`](https://github.com/Ilufus/ei-mapper-endurance/commit/8a7bc3c))
- [x] Verify backup creation, metadata, hashes, and source preservation in a disposable end-to-end test. ([backup tests](tests/backup_manager_tests.cpp))
- [x] Validate the staged document with a fresh parser before committing it. ([`c8d8b3a`](https://github.com/Ilufus/ei-mapper-endurance/commit/c8d8b3a))
- [x] Parse every MOB section with strict bounds and complete byte coverage;
  preserve numeric IDs, occurrences, ordering, raw bytes, paths, and hashes, with
  unknown payloads retained as opaque leaves. ([`aaef298`](https://github.com/Ilufus/ei-mapper-endurance/commit/aaef298), [validation](MOB_HARDENING_VALIDATION.md))
- [x] Pass byte-identical no-op checks across all 252 MOBs in the current Base/JMV
  Maps corpus. ([validation](MOB_HARDENING_VALIDATION.md))
- [x] Add the core provenance-checked `MobEditTransaction` leaf patcher with
  byte-exact untouched leaves, derived ancestor lengths, strict candidate reparse,
  stale-hash rejection, and strict CP1251 encoding. ([`660a542`](https://github.com/Ilufus/ei-mapper-endurance/commit/660a542), [document tests](tests/endurance_document_tests.cpp))
- [ ] Route all existing UI edit commands through `MobEditTransaction`; the core
  patcher exists, but the UI edit model still projects through legacy objects.
- [x] Add a fail-closed shadow bridge for leaf-only UI projection deltas. It binds
  changes to exact raw-source hashes, rejects structural and canonicalized-source
  changes, preserves source-only records, and never writes its candidate.
  ([`3106fac`](https://github.com/Ilufus/ei-mapper-endurance/commit/3106fac),
  [validation](MOB_SHADOW_TRANSACTION_VALIDATION.md))
- [x] Promote the independently cross-validated one-byte `OBJ_PLAYER` edit class
  into real changed output through the raw transaction, fresh staged validation,
  pre-save backup, and atomic replacement path. Mixed or unvalidated edit sets
  remain wholly blocked. ([`3cd2b3c`](https://github.com/Ilufus/ei-mapper-endurance/commit/3cd2b3c))
- [x] Disable the legacy changed-MOB serializer at the Save boundary; dirty MOB
  saves are refused until their UI edits have transaction provenance. Unchanged
  byte-identical Save As remains available. ([`912d00c`](https://github.com/Ilufus/ei-mapper-endurance/commit/912d00c))
- [x] Independently reparse transaction output and block any changed, missing, or
  added leaf outside the accepted provenance-checked edit. Empty structural
  containers are covered by a regression derived from the first tester report.
  ([`66fbca0`](https://github.com/Ilufus/ei-mapper-endurance/commit/66fbca0))
- [ ] Copy/delete exact raw object subtrees and create new objects only from
  profile-approved raw templates. The provenance-checked document core now
  duplicates/removes exact object bytes and passed 252/252 installed MOBs that
  contain objects. Loaded UI objects are now bound to source-document, section,
  and subtree hashes, and same-MOB clipboard copies carry that identity through
  undo/redo. Replaying copied-leaf edits, final semantic authorization, and
  profile-approved template creation remain. ([`8013339`](https://github.com/Ilufus/ei-mapper-endurance/commit/8013339),
  [`f68ce87`](https://github.com/Ilufus/ei-mapper-endurance/commit/f68ce87))
- [ ] Complete lossless MPR preservation for names, timestamps, alignment, and extra entries.
- [ ] Verify CP1251 script byte lengths and round trips across the corpus.
- [ ] Add an explicit duplicate-ID Repair preview with reference and script auditing.
- [ ] Add in-app backup browsing and one-click restore to a user-selected destination.
- [ ] Add integrated failure-injection tests for backup, staging, validation, and replacement failures.
- [ ] Stress repeated open/close/save/cancel and abnormal-exit behavior on large maps.
- [ ] Complete the game test matrix: `drawall`, save/load, unload/reload, scripts, traversal, and exit.

## Diagnostics and crash recovery

- [x] Put workflow logs in a dedicated Endurance logs folder. ([`2568e38`](https://github.com/Ilufus/ei-mapper-endurance/commit/2568e38))
- [x] Start a clean current log and timestamp the previous log on launch. ([`2568e38`](https://github.com/Ilufus/ei-mapper-endurance/commit/2568e38))
- [x] Record file-open phases and last-operation breadcrumbs for hard-crash investigation. ([`2568e38`](https://github.com/Ilufus/ei-mapper-endurance/commit/2568e38))
- [x] Add crash handling that preserves useful diagnostic context where Windows permits it. ([`2568e38`](https://github.com/Ilufus/ei-mapper-endurance/commit/2568e38))
- [ ] Add a diagnostics panel for phase timings, validation findings, and missing resources.
- [ ] Add an opt-in crash bundle creator that removes private paths and game assets.
- [ ] Exercise hard-crash logging with controlled parser, decoder, and OpenGL failures.
- [x] Correct crash fault-module attribution, capture access details/registers
  and a normal minidump, and add phase-specific MPR/OpenGL breadcrumbs.
  ([`4991011`](https://github.com/Ilufus/ei-mapper-endurance/commit/4991011))
- [ ] Resolve the alpha.14 Windows 7 MPR-open crash using an alpha.15 normal vs
  ANGLE controlled retry; retain the exact map hash, GPU/driver, workflow log,
  and private minidump before assigning a root cause.

## Resources, figures, textures, and performance

- [x] Add optional cached CPU frustum culling for normal rendering and picking,
  with conservative handling of rotations, selection, and logic visualization.
  ([`1ab2935`](https://github.com/Ilufus/ei-mapper-endurance/commit/1ab2935),
  [`ff1c9a0`](https://github.com/Ilufus/ei-mapper-endurance/commit/ff1c9a0))
- [ ] Profile remaining large-map bottlenecks after alpha.28 field measurements.
  Consider spatial partitioning, batching, level-of-detail, and only then
  occlusion culling; avoid per-object GPU queries on the legacy renderer without
  evidence because query synchronization can reduce performance.

- [x] Index RES tables and lazily read payloads instead of expanding whole archives repeatedly. ([`054959b`](https://github.com/Ilufus/ei-mapper-endurance/commit/054959b))
- [x] Normalize mixed-case archive lookups and cache missing-resource results. ([`054959b`](https://github.com/Ilufus/ei-mapper-endurance/commit/054959b))
- [x] Treat configured RES archives as generic sources with deterministic priority. ([`44ab888`](https://github.com/Ilufus/ei-mapper-endurance/commit/44ab888))
- [x] Discover JMV archives beyond the old figure/texture category restrictions. ([`44ab888`](https://github.com/Ilufus/ei-mapper-endurance/commit/44ab888))
- [x] Resolve the previously missing representative Zone 3/18 JMV figures. ([`6454de7`](https://github.com/Ilufus/ei-mapper-endurance/commit/6454de7))
- [x] Preserve unit figure, texture, and database fields during changed saves. ([`6454de7`](https://github.com/Ilufus/ei-mapper-endurance/commit/6454de7))
- [x] Resolve model-qualified `_xN` unit skins such as Zone 12 skeleton textures. ([`35a43c8`](https://github.com/Ilufus/ei-mapper-endurance/commit/35a43c8))
- [x] Parse human and orc prototype skin indices from the active `units.udb`
  and produce model-qualified candidate texture names. ([`94e0b9d`](https://github.com/Ilufus/ei-mapper-endurance/commit/94e0b9d))
- [ ] Complete database/template-driven character rendering, including race
  texture lists, secondary layers, and equipped visuals. Base-game
  `zone3obr.mob` remains the known primary-skin regression case; JMV rendering
  remains the current priority.
- [x] Prefer an active mod `database.res` over adjacent backup or
  `original-unmodified` database archives for unit appearance lookup. ([`94e0b9d`](https://github.com/Ilufus/ei-mapper-endurance/commit/94e0b9d))
- [x] Log and display the nearest existing family variation when a MOB references an absent skin, without changing the stored field. ([`35a43c8`](https://github.com/Ilufus/ei-mapper-endurance/commit/35a43c8))
- [x] Resolve complete terrain-atlas sets case-insensitively with deterministic JMV-first provenance. ([`530bb16`](https://github.com/Ilufus/ei-mapper-endurance/commit/530bb16))
- [x] Refuse partial terrain-atlas assembly and use a complete diagnostic fallback instead. ([`530bb16`](https://github.com/Ilufus/ei-mapper-endurance/commit/530bb16))
- [x] Use the embedded MPR map namespace for Tile Parameters, adapting upstream PR #19. ([`530bb16`](https://github.com/Ilufus/ei-mapper-endurance/commit/530bb16))
- [ ] Persist catalog indexes and invalidate them by path, size, timestamp, and header hash.
- [ ] Inventory and support every MMP format found in the private JMV corpus.
- [ ] Show diagnostic placeholders with asset, archive, format, and failure reason.
- [ ] Move indexing, parsing, and CPU decoding to cancellable worker tasks.
- [ ] Batch OpenGL uploads on the GUI thread without freezing the editor.
- [ ] Report archive precedence, collisions, and shadowed assets.
- [ ] Meet the warm-load target: at least 10x faster than v0.3 and within 15 seconds on representative maps.

## Document workflow

- [x] Provide separate Open MPR and Open MOB commands. ([`054959b`](https://github.com/Ilufus/ei-mapper-endurance/commit/054959b))
- [x] Allow basic MOB-only opening and rendering without automatically loading terrain. ([`054959b`](https://github.com/Ilufus/ei-mapper-endurance/commit/054959b), [`54d8070`](https://github.com/Ilufus/ei-mapper-endurance/commit/54d8070))
- [x] Keep an MPR open while independently choosing a MOB. ([`054959b`](https://github.com/Ilufus/ei-mapper-endurance/commit/054959b))
- [ ] Introduce the full `MapWorkspace` model: optional MPR, multiple MOBs, explicit active document.
- [ ] Add Close MPR and Close Active MOB commands.
- [ ] Add an explicit Open Matching MOB convenience command.
- [x] Reproject display positions safely when terrain is attached or detached
  without changing stored MOB coordinates. ([`54d8070`](https://github.com/Ilufus/ei-mapper-endurance/commit/54d8070))
- [ ] Disable only terrain-dependent tools in MOB-only mode.
- [ ] Add independent `.mpr` and `.mob` drag-and-drop behavior.
- [x] Add headless `--profile-load` discovery and JSON output. ([`7b206c8`](https://github.com/Ilufus/ei-mapper-endurance/commit/7b206c8))
- [x] Add explicit profile capabilities and fingerprinted Base/JMV runtime-stack
  manifests. ([`97b6991`](https://github.com/Ilufus/ei-mapper-endurance/commit/97b6991))
- [x] Add deterministic headless MOB comparison with `--mob-diff <baseline>
  --against <candidate>` and JSON output. ([`aaef298`](https://github.com/Ilufus/ei-mapper-endurance/commit/aaef298))
- [ ] Add Open Workspace support through portable `.collection` manifests
  (high-priority scope is tracked in the wishlist below).

## UI and script workflow

- [x] Make the object outliner resizable and persist its splitter layout. ([`054959b`](https://github.com/Ilufus/ei-mapper-endurance/commit/054959b))
- [x] Enable the external-script-editor button with OS association fallback. ([`054959b`](https://github.com/Ilufus/ei-mapper-endurance/commit/054959b))
- [ ] Convert the outliner to a resizable/detachable dock.
- [ ] Add stable filtering by text, type, ID, model, database name, and validation state.
- [ ] Add a configurable external editor executable.
- [ ] Export embedded CP1251 scripts to managed UTF-8 workspaces.
- [ ] Watch external edits, display a diff, and require explicit import.
- [ ] Make script import one undoable edit and reject non-CP1251 characters clearly.
- [ ] Recover unimported script workspaces after an abnormal exit.

## JMV setup and validation

- [x] Configure and test the development build against a confirmed JMV installation using settings outside upstream v0.3. ([`c8d8b3a`](https://github.com/Ilufus/ei-mapper-endurance/commit/c8d8b3a), [validation](ALPHA11_VALIDATION.md))
- [x] Keep all live maps untouched during automated tests; use hashed disposable copies. ([validation](ALPHA11_VALIDATION.md))
- [x] Provide `--validate`, `--roundtrip-check`, and JSON output for automation. ([`054959b`](https://github.com/Ilufus/ei-mapper-endurance/commit/054959b))
- [x] Add the JMV setup wizard with auto-discovery and explicit confirmation. ([`7b206c8`](https://github.com/Ilufus/ei-mapper-endurance/commit/7b206c8))
- [x] Move Game and Mod Setup under Settings and suppress automatic legacy
  resource dialogs and warnings. ([`8528799`](https://github.com/Ilufus/ei-mapper-endurance/commit/8528799))
- [x] Use the selected profile's preferred Maps directory for Open dialogs. ([`7b206c8`](https://github.com/Ilufus/ei-mapper-endurance/commit/7b206c8))
- [x] Refresh resource state and viewport geometry after setup without requiring
  an editor restart. ([`54d8070`](https://github.com/Ilufus/ei-mapper-endurance/commit/54d8070))
- [ ] Validate archive signatures and show source precedence during setup.
- [ ] Offer a read-only, previewed import of compatible upstream `options.json` values.
- [ ] Maintain a hash inventory for the private Zone 1/3/4/6/12/18/19 integration corpus.
- [ ] Automate semantic comparison with MobExplorer where its interfaces permit it.
- [ ] Compare ID-preserving and explicit-repair builds before linking ID changes to earlier game crashes.

## Perspective MOB diff

- [x] Build immutable object snapshots from raw subtrees and match exact subtree
  hashes, unique family-scoped NIDs, then unique type/prototype/name identity.
  Ambiguous identities remain unresolved. ([`aaef298`](https://github.com/Ilufus/ei-mapper-endurance/commit/aaef298))
- [x] Classify unchanged, added, removed, modified, moved, and ambiguous entries;
  export deterministic text/JSON reports. ([`aaef298`](https://github.com/Ilufus/ei-mapper-endurance/commit/aaef298))
- [x] Add **Tools > Compare MOB Files** with strict validation, filterable
  classification/match/confidence/NID/name/prototype/path/hash details, and
  deterministic text/JSON export. ([`fd30be0`](https://github.com/Ilufus/ei-mapper-endurance/commit/fd30be0))
- [ ] Add profile-compatibility findings to each in-app diff entry after concrete
  runtime semantic validators exist.
- [ ] Add the read-only 3D overlay with red removals, green additions, amber
  modifications, movement vectors, terrain/grid selection, filters, and isolated picking.
- [ ] Add original-versus-current and candidate-save-versus-transaction entry points.
- [ ] Keep diff-to-merge deferred until the read-only workflow is validated.

## Profile-specific runtime and JMV test launch

- [x] Declare profile-scoped capability states for lossless editing, perspective
  diff, direct/multi-MOB launch, appearance/equipment resolution, and semantic
  validation. ([`97b6991`](https://github.com/Ilufus/ei-mapper-endurance/commit/97b6991))
- [x] Fingerprint the approved Base executable and complete JMV Starter/addon/
  SpellAddon/JMV addon/Lua/config stack without loading runtime DLLs in-process.
  ([`97b6991`](https://github.com/Ilufus/ei-mapper-endurance/commit/97b6991))
- [x] Define profile-scoped provider interfaces for appearance, equipment,
  prototypes, semantic validation, and map test launch. ([`b467d87`](https://github.com/Ilufus/ei-mapper-endurance/commit/b467d87))
- [x] Define and validate the versioned JMV test-run request manifest, including
  ordered MOBs, active MOB, runtime/source/staged hashes, logs/results, safe run
  IDs, JMV-only scope, and mandatory containment inside the disposable run root.
  ([`b467d87`](https://github.com/Ilufus/ei-mapper-endurance/commit/b467d87), [runtime-provider tests](tests/runtime_provider_tests.cpp))
- [ ] Implement concrete provider backends; interface and manifest contracts are
  present, but appearance/equipment/semantic/launch providers remain disabled.
- [ ] Run the non-destructive JMV feasibility ladder: `server <map>`, supplemental
  `Auto<map>*.mob` discovery/order, then autorun-script `AddMobEx`.
- [ ] Implement manifest staging and disposable test state only after one
  mechanism proves exact ordered loading, readiness reporting, and cleanup.
- [ ] Keep direct launch disabled for Base EI and unavailable for JMV until the
  feasibility gate passes; do not redirect live Maps or Starter configuration.

## Release gates

- [x] `0.1.0-alpha.1` through `0.1.0-alpha.13`: initial safety, diagnostics, resources, serializer fixes, backups, model-qualified skins, terrain-atlas correctness, consolidated duplicate-ID reporting, staged-save validation, mod-maker preview reporting, installation-based reusable resource profiles, and corrected first-run/MOB-only viewport initialization. ([implementation evidence](#implementation-evidence))
- [x] `0.1.0-alpha.14`: testing-group build with Settings-based setup,
  explicit base-game humanoid known issue, database appearance diagnostics, and
  a coordinated test/report matrix. ([`800a011`](https://github.com/Ilufus/ei-mapper-endurance/commit/800a011), [validation](ALPHA14_VALIDATION.md))
- [ ] Next private alpha: address confirmed alpha.14 regressions and publish a
  coverage summary from the group test matrix.
- [ ] `0.1.0-beta.1`: correct indexed resources and responsive fast loading.
- [ ] `0.1.0-rc.1`: independent workspace workflow, UI, script editing, and completed safety validation.
- [ ] `1.0.0`: JMV-ready release passing the full corpus, MobExplorer, and in-game validation matrix.
- [ ] Package releases with executable, Qt runtime, source snapshot, GPL notices, build instructions, checksums, and rollback guidance.
- [ ] Consider Qt6/CMake modernization only after 1.0; use a major version if compatibility breaks.

## Wishlist inbox

Add requested enhancements here first. We can then assign each one to a release
or move it into the relevant section above without losing its history.

### High priority

- [ ] Add a supported **Landscape Editing** mode for MPR/SEC terrain.
  - Gate editing on lossless MPR/SEC saves: preserve untouched archive members,
    sector bytes, names, ordering, alignment, and metadata, then stage, reopen,
    and validate every save before replacing the destination.
  - Refactor terrain ownership around each sector's canonical 33x33 vertex
    lattice instead of independently duplicated tile vertices, including seam
    synchronization between neighboring sectors.
  - Add undoable raise/lower, flatten, smooth, and ramp height tools with brush
    radius, strength, falloff, and numeric-height controls.
  - Recalculate only affected normals, preserve untouched packed vertex bits,
    and warn before height changes require lossy `maxZ` rescaling.
  - Productionize the existing hidden tile brush with explicit layer, atlas,
    category, rotation, brush-shape, preview, and validation controls.
  - Implement visual texture blending through transition-tile painting and
    optional autotiling; the SEC format has packed tile selection/rotation but
    no arbitrary per-vertex texture-blend weights.
  - Add creation, removal, sculpting, visibility, texture, and material editing
    for the separate overlay surface used by water, swamp, ice, and lava.
  - Provide validated material presets for base EI and JMV while retaining
    manual controls for mod-specific terrain materials.
  - Warn when terrain edits can invalidate MOB object placement or the AI
    navigation graph, and provide validation reports before save/test launch.
  - Require copied-map round-trip checks, untouched-member/sector diffs, and
    focused in-game traversal tests before enabling this for live maps.
- [ ] Add a portable Endurance `.collection` format for reusable MPR/MOB sets.
  - A collection is a small manifest listing one optional MPR and one or more
    MOB files; it never embeds, copies, or modifies the referenced map assets.
  - Load a collection and open its complete MPR/MOB set without selecting each
    file manually.
  - Add **Save Collection** and **Save Collection As** for the currently loaded
    set.
  - Store collections by default in the editor installation's `collections/`
    folder so files can be copied and shared with other mod makers.
  - Use portable profile/map-root-relative references where possible; do not
    write user-specific absolute paths into shared collection files.
  - Report missing or ambiguous files together and let the user review the
    resolved paths before opening the set.
  - Stretch goal: add an **Edit Collection** submenu for adding, removing,
    reordering, and replacing entries without first loading the whole set.
- [ ] Replace the secondary-MOB workflow with Unreal-style multi-MOB loading and editing.
  - Show all open MOBs in a **Loaded MOBs** tab/list.
  - Allow individual MOBs to be locked against selection and accidental edits.
  - Make the currently selected/active MOB explicit; new objects and pasted objects go there.
  - Show which loaded MOB owns the currently selected object.
  - Treat MOBs similarly to Unreal Engine sublevels within the loaded map workspace.
- [x] Allow model and unit texture fields to support both dropdown selection and
  manual keyboard entry with case-insensitive filtering.
  ([`06d1c9b`](https://github.com/Ilufus/ei-mapper-endurance/commit/06d1c9b))
- [ ] Add periodic autosave with recoverable, versioned backup states.
- [ ] Add a command to launch the currently loaded MPR and MOB set directly in
  game for testing (**JMV-only stretch goal**; gated by the profile provider and
  non-destructive feasibility protocol above).
- [ ] Add a resizable workflow-log panel along the bottom of the viewport, similar to the UE4 output log.

### Medium priority

- [ ] Load database-defined humanoid textures for humans, skeletons, and orcs where applicable.
  - Prototype skin-index parsing and candidate lookup are implemented.
  - JMV remains the working visual baseline; base-game `zone3obr.mob` is still
    incorrect and must be retested against the full database/template pipeline.
  - Keep this open until visual corpus testing covers primary and secondary
    layers for all supported humanoid models.
- [ ] Render every applicable humanoid with the weapons and armor assigned by
  its database setup, including humans, orcs, undead, and any other humanoid
  race that equips items in game.
  - Core scope: reproduce the normal equipped armor and weapon visuals defined
    by each supported database/template entry.
  - **Stretch goal (JMV only):** correctly assemble one-handed weapon plus
    shield, dual-wield, and two-handed configurations.

### Low priority

- [ ] Add configurable UI font-size scaling for 4K displays and accessibility.
- [ ] Add a Russian translation and an in-app language switch.

## Updating this checklist

Each completed code item should normally include a commit, test, or release link.
If testing exposes a regression, uncheck the item and add the concrete failing
case beneath it. Changes to this file are committed to `main`, so GitHub keeps a
complete history of what was added, completed, reopened, or reprioritized.
