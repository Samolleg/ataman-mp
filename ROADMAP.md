# EI Mapper: Endurance Edition roadmap

This is the living project checklist for the private Endurance Edition line.
An item is checked only after its implementation has landed and received the
listed level of validation. New findings and wishlist requests belong here so
they do not get lost between test sessions.

Current development release: `0.1.0-alpha.49`.

## Implementation evidence

Completed code items below carry an inline commit link. Validation and packaging
evidence is linked separately where a commit alone does not prove runtime
behavior.

| Change group | Commit | Validation |
|---|---|---|
| Endurance identity, safe no-op documents, indexed resources, independent opening, outliner, external editor | [`054959b`](https://github.com/Ilufus/ei-mapper-endurance/commit/054959b) | [initial document tests](tests/endurance_document_tests.cpp) |
| NUL-delimited MOB validation | [`34b9ec8`](https://github.com/Ilufus/ei-mapper-endurance/commit/34b9ec8) | [document tests](tests/endurance_document_tests.cpp) |
| Crash diagnostics and rotating workflow logs | [`2568e38`](https://github.com/Ilufus/ei-mapper-endurance/commit/2568e38) | [bug-report workflow](docs/BUG_REPORTING.md) |
| Missing figures and unit path preservation | [`6454de7`](https://github.com/Ilufus/ei-mapper-endurance/commit/6454de7) | private JMV map checks |
| Generic JMV RES discovery | [`44ab888`](https://github.com/Ilufus/ei-mapper-endurance/commit/44ab888) | private JMV resource checks |
| SOUND/world serialization preservation | [`62b824a`](https://github.com/Ilufus/ei-mapper-endurance/commit/62b824a) | private changed-save comparisons |
| Unit guard/patrol height preservation | [`e0ac92a`](https://github.com/Ilufus/ei-mapper-endurance/commit/e0ac92a) | private Zone 1 changed-save comparison |
| Recoverable backups | [`8a7bc3c`](https://github.com/Ilufus/ei-mapper-endurance/commit/8a7bc3c) | [backup tests](tests/backup_manager_tests.cpp) |
| Model-qualified unit skins | [`35a43c8`](https://github.com/Ilufus/ei-mapper-endurance/commit/35a43c8) | [resource-name tests](tests/resource_name_tests.cpp) |
| Complete terrain atlases and internal map namespace | [`530bb16`](https://github.com/Ilufus/ei-mapper-endurance/commit/530bb16) | [resource-name tests](tests/resource_name_tests.cpp) |
| Consolidated duplicate-ID reporting | [`a39ef57`](https://github.com/Ilufus/ei-mapper-endurance/commit/a39ef57) | [duplicate-ID tests](tests/duplicate_id_report_tests.cpp) |
| Staged fresh-parser validation and alpha.11 preview | [`c8d8b3a`](https://github.com/Ilufus/ei-mapper-endurance/commit/c8d8b3a) | Historical alpha validation retired |
| Installation-based profiles | [`7b206c8`](https://github.com/Ilufus/ei-mapper-endurance/commit/7b206c8) | [resource-profile tests](tests/resource_profile_tests.cpp) |
| First-run and MOB-only viewport initialization | [`54d8070`](https://github.com/Ilufus/ei-mapper-endurance/commit/54d8070) | Historical alpha validation retired |
| Setup moved under Settings | [`8528799`](https://github.com/Ilufus/ei-mapper-endurance/commit/8528799) | Historical alpha validation retired |
| Database-backed humanoid candidate lookup | [`94e0b9d`](https://github.com/Ilufus/ei-mapper-endurance/commit/94e0b9d) | [resource-name tests](tests/resource_name_tests.cpp); base rendering remains unresolved |
| Alpha.14 testing-group release | [`800a011`](https://github.com/Ilufus/ei-mapper-endurance/commit/800a011) | Historical alpha validation retired |
| Capability profiles and fingerprinted Base/JMV runtime manifests | [`97b6991`](https://github.com/Ilufus/ei-mapper-endurance/commit/97b6991) | [resource-profile tests](tests/resource_profile_tests.cpp) |
| Strict raw-section MOB tree and deterministic semantic diff | [`aaef298`](https://github.com/Ilufus/ei-mapper-endurance/commit/aaef298) | [252-file corpus validation](docs/validation/MOB_HARDENING_VALIDATION.md) |
| Provenance-checked MOB leaf transactions | [`660a542`](https://github.com/Ilufus/ei-mapper-endurance/commit/660a542) | [document tests](tests/endurance_document_tests.cpp) |
| Fail-closed UI projection shadow transactions | [`3106fac`](https://github.com/Ilufus/ei-mapper-endurance/commit/3106fac) | [254-file corpus validation](docs/validation/MOB_SHADOW_TRANSACTION_VALIDATION.md) |
| Profile provider API and safe JMV test-run manifest contract | [`b467d87`](https://github.com/Ilufus/ei-mapper-endurance/commit/b467d87) | [runtime-provider tests](tests/runtime_provider_tests.cpp) |
| Filterable read-only MOB diff details dialog | [`fd30be0`](https://github.com/Ilufus/ei-mapper-endurance/commit/fd30be0) | production MinGW build and [semantic-diff tests](tests/endurance_document_tests.cpp) |
| Selection UX, recent files, open/save shortcuts, and editable model/texture filters | [`06d1c9b`](https://github.com/Ilufus/ei-mapper-endurance/commit/06d1c9b) | Clean production MinGW build, six regression suites, and Zone 1 no-op round trip |
| Semantic raw-object provenance and cached frustum culling | [`1ab2935`](https://github.com/Ilufus/ei-mapper-endurance/commit/1ab2935) | 254-file projection corpus and document/frustum tests |
| Generic topology-preserving MOB leaf saves | [`843ba9c`](https://github.com/Ilufus/ei-mapper-endurance/commit/843ba9c) | 254/254 Base/JMV projection corpus and 24 document tests |
| Exact object sequence and patrol/look structural saves | [`11698ad`](https://github.com/Ilufus/ei-mapper-endurance/commit/11698ad) | 254/254 Base/JMV corpus, 27 document tests, and six regression suites |
| MOB-parameter transactions, stable logic undo, repeated-save provenance, and typed property clipboard | [`f4b107c`](https://github.com/Ilufus/ei-mapper-endurance/commit/f4b107c) | Seven regression suites, UI widget checks, and Zone 1 no-op round trip |
| Debounced external-script import and row-based typed property clipboard | [`5192c4d`](https://github.com/Ilufus/ei-mapper-endurance/commit/5192c4d) | Extended UI clipboard checks, seven regression suites, and Zone 1 no-op round trip |
| Document-owned script workspace, independent script editor, first patrol creation, duplicate/history shortcuts, and persisted application keybinds | [`258893c`](https://github.com/Ilufus/ei-mapper-endurance/commit/258893c) | Production MinGW build and extended UI/settings checks |
| Non-interactive version probing and clean first-run distribution gate | [`b10261f`](https://github.com/Ilufus/ei-mapper-endurance/commit/b10261f), [`a3a416b`](https://github.com/Ilufus/ei-mapper-endurance/commit/a3a416b), [`b025265`](https://github.com/Ilufus/ei-mapper-endurance/commit/b025265) | Clean-package and Unicode-path-safe gates |
| Synchronized internal/external MOB scripts, explicit editor selection, script shortcuts, and MOB Parameters close lifecycle | [`607715a`](https://github.com/Ilufus/ei-mapper-endurance/commit/607715a) | Production MinGW build, seven regression suites, and headless UI check |
| Exact model-parts snapshot across multi-property paste undo/redo | [`749b850`](https://github.com/Ilufus/ei-mapper-endurance/commit/749b850) | Production MinGW build and focused regression check |
| Multi-field property context-menu copy/paste | [`7eba358`](https://github.com/Ilufus/ei-mapper-endurance/commit/7eba358) | Headless UI clipboard checks |
| Focus-aware external-script import and idempotent watcher markers | [`54e7cfb`](https://github.com/Ilufus/ei-mapper-endurance/commit/54e7cfb) | Production MinGW build and seven regression suites |
| Unified one-click MOB Parameters X/Close lifecycle | [`280470f`](https://github.com/Ilufus/ei-mapper-endurance/commit/280470f) | Production MinGW build and focused regression check |
| Coalesced external-script file watching without directory feedback | [`9b0f18a`](https://github.com/Ilufus/ei-mapper-endurance/commit/9b0f18a) | Production build and seven regression suites |
| Embedded-script and document-section MOB diff | [`0a4624f`](https://github.com/Ilufus/ei-mapper-endurance/commit/0a4624f) | Synthetic encrypted-script regression plus the supplied Zone 1/Zone 1 Test comparison |
| Redo-safe raw provenance, mixed model-parts editing, atomic logic-point placement, and heavy-history progress | [`db3fa31`](https://github.com/Ilufus/ei-mapper-endurance/commit/db3fa31) | Seven regression suites, UI widget checks, and [latest supplied-map validation](docs/releases/ALPHA41_VALIDATION.md) |

Alpha.34 packaging is withdrawn: it accidentally included a developer-local
`endurance-options.json`, which suppressed first-run setup and exposed local game
paths/recent files. Alpha.36 supersedes it with a clean-settings package gate
and the corrected script workflow.

## Product contract

- Normal editing must feel normal: no save preview, no per-field approval list,
  and no warning merely because the user moved a unit or changed several fields.
- Preserve every source MOB byte that the user did not intentionally change and
  propagate every intended edit into the saved MOB.
- Run raw-data checks, fresh reopen validation, backups, and atomic replacement
  automatically in the background.
- Refuse a save only when Endurance genuinely cannot represent the requested
  operation without risking unrelated MOB data. Treat that as an implementation
  gap to fix, not as a permanent user workflow.
- The game is the final compatibility target. Corpus checks and MobExplorer are
  development evidence; focused Base/JMV in-game tests decide release readiness.

## Current focus

- [x] Implement the final hardening-alpha save scope: normal mixed leaf edits,
  exact same-MOB copy/paste and deletion, known patrol/look-point structure, and
  selected same-type raw templates for object creation.
  ([`11698ad`](https://github.com/Ilufus/ei-mapper-endurance/commit/11698ad))
- [x] Package alpha.31 for normal and structural edit testing: move units, change mixed property
  fields, undo back to clean, save/reopen, and verify the resulting maps in JMV.
  The immutable source is tagged `v0.1.0-alpha.31`; the package includes source,
  symbols, checksums, rollback guidance, and the focused release test matrix.
- [x] Connect structural MOB saves for same-MOB copy/paste, object deletion, and
  adding/removing known patrol or look points. Reuse exact source subtrees for
  copies and require an explicit selected same-type template for new objects.
  ([`11698ad`](https://github.com/Ilufus/ei-mapper-endurance/commit/11698ad))
- [x] Verify structural saves with automated untouched-data comparison, Endurance
  reopen, MobExplorer comparison, and focused JMV `drawall`/unload/reload tests.
  Exact subtree/leaf regressions, the Base/JMV corpus, and closed-tester maps now
  cover copy/delete, patrol/look points, ordinary properties, diplomacy, scripts,
  repeated saves, and in-game `drawall`/load checks.
- [x] Decouple scripts from MOB Parameters: provide an independent internal
  editor, keep the external watcher alive for the loaded MOB document, and route
  internal/external imports through the main undo history with strict CP1251
  conversion. ([`258893c`](https://github.com/Ilufus/ei-mapper-endurance/commit/258893c))
- [ ] Complete tester coverage proving edited scripts and all other text change
  only their intended CP1251 leaves across Base/JMV saves.
- [ ] Stress repeated edit/undo/redo/save/open/close cycles on large JMV maps and
  fix any crash, stale-selection, or dirty-state regression found by testers.
  Alpha.41 fixes the first confirmed save/undo/save/redo provenance failure;
  closed-group repetition remains the release gate.
- [x] Route MOB Parameters through the main undo history; reload external-editor
  atomic replacements; remove stale patrol-node pointers from undo; and rebind
  saved raw-object provenance for subsequent saves.
  ([`f4b107c`](https://github.com/Ilufus/ei-mapper-endurance/commit/f4b107c))
- [x] Add destination-typed property clipboard validation. Copied values remain
  plain text for external editors, while Endurance rejects incompatible field
  pastes. Alpha.33 supersedes the original pending asset-paste behavior: an
  explicit paste commits immediately, while ordinary filter typing still waits
  for Enter or an explicit choice.
  ([`f4b107c`](https://github.com/Ilufus/ei-mapper-endurance/commit/f4b107c))
- [x] Coalesce external-script watcher notifications and prevent nested import
  prompts; move clipboard operations to property rows and labels; support typed
  multi-row and structured-value paste as one undo action; remove viewport
  Shift-click object shortcuts; and disable MOB Parameters without an active MOB.
  ([`5192c4d`](https://github.com/Ilufus/ei-mapper-endurance/commit/5192c4d))
- [ ] Finish lossless MPR archive preservation before enabling landscape editing.

MOB hardening implementation is complete for the supported edit classes. The
remaining gate is closed-group validation of alpha.40 and any concrete bugs it
finds. Lossless MPR editing is a separate future workstream; opening terrain for
MOB editing does not imply that landscape saving is supported.

## Alpha.14 testing campaign

- [x] Build and checksum-verify the private alpha.14 testing-group package. ([`800a011`](https://github.com/Ilufus/ei-mapper-endurance/commit/800a011))
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
  unknown payloads retained as opaque leaves. ([`aaef298`](https://github.com/Ilufus/ei-mapper-endurance/commit/aaef298), [validation](docs/validation/MOB_HARDENING_VALIDATION.md))
- [x] Pass byte-identical no-op checks across all 252 MOBs in the current Base/JMV
  Maps corpus. ([validation](docs/validation/MOB_HARDENING_VALIDATION.md))
- [x] Add the core provenance-checked `MobEditTransaction` leaf patcher with
  byte-exact untouched leaves, derived ancestor lengths, strict candidate reparse,
  stale-hash rejection, and strict CP1251 encoding. ([`660a542`](https://github.com/Ilufus/ei-mapper-endurance/commit/660a542), [document tests](tests/endurance_document_tests.cpp))
- [x] Connect existing topology-preserving UI edits to raw-tree output through
  the projection bridge. The internal legacy object model may remain as the UI
  representation as long as saved output is produced only by exact raw patches.
- [x] Add a fail-closed bridge for UI projection deltas. It binds changes to exact
  raw-source hashes, rejects canonicalized-source mismatches, preserves source-only
  records, and supplies the validated candidate to the normal save pipeline.
  ([`3106fac`](https://github.com/Ilufus/ei-mapper-endurance/commit/3106fac),
  [validation](docs/validation/MOB_SHADOW_TRANSACTION_VALIDATION.md))
- [x] Enable generic topology-preserving raw-leaf output. Mixed property and
  transform edits save normally without a preview or field whitelist, followed
  by fresh staged validation, backup, and atomic replacement.
  ([`843ba9c`](https://github.com/Ilufus/ei-mapper-endurance/commit/843ba9c))
- [x] Validate alpha.30 generic raw-leaf saving with ordinary mixed property and
  transform edits. There is no preview or field whitelist: the invariant is that
  every intended replacement is exact and all untouched leaves remain byte-identical.
  (historical alpha validation retired)
- [x] Disable the legacy changed-MOB serializer at the Save boundary; dirty MOB
  saves are refused until their UI edits have transaction provenance. Unchanged
  byte-identical Save As remains available. ([`912d00c`](https://github.com/Ilufus/ei-mapper-endurance/commit/912d00c))
- [x] Independently reparse transaction output and block any changed, missing, or
  added leaf outside the accepted provenance-checked edit. Empty structural
  containers are covered by a regression derived from the first tester report.
  ([`66fbca0`](https://github.com/Ilufus/ei-mapper-endurance/commit/66fbca0))
- [x] Save copy/paste and delete by inserting/removing the exact raw object
  subtree, then apply the pasted object's intended field changes as raw leaves.
  Save newly created objects from profile-approved raw templates. The document
  core already duplicates/removes exact object bytes and passed 252/252 installed
  MOBs containing objects; the remaining task is connecting those operations to
  the normal UI save path and validating them in JMV. ([`8013339`](https://github.com/Ilufus/ei-mapper-endurance/commit/8013339),
  [`f68ce87`](https://github.com/Ilufus/ei-mapper-endurance/commit/f68ce87),
  [`11698ad`](https://github.com/Ilufus/ei-mapper-endurance/commit/11698ad))
- [ ] Complete lossless MPR preservation for names, timestamps, alignment, and extra entries.
- [x] Preserve unchanged script bytes exactly and strictly reject edited text
  that cannot round-trip through CP1251. Internal and external imports are one
  undoable transaction; encrypted-script differences are now independently
  visible in MOB Diff. ([`258893c`](https://github.com/Ilufus/ei-mapper-endurance/commit/258893c),
  [`0a4624f`](https://github.com/Ilufus/ei-mapper-endurance/commit/0a4624f))
- [ ] Add optional explicit duplicate-ID Repair with reference and script auditing.
  This is a separate user-invoked repair tool, never part of ordinary open/save.
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
- [ ] Render humanoids in an appropriate database/profile-derived idle pose
  instead of the current null/T-pose fallback.
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
- [ ] Accept unpacked asset directories and ZIP archives containing loose MMPs
  as profile resource sources. Index ZIP central-directory entries without
  extracting the whole archive, apply the same normalized lookup/precedence
  rules as RES sources, and report malformed or shadowed assets. Primary repro:
  the successfully opened oversized custom MPR whose texture pack was supplied
  as a ZIP of MMP files rather than a RES archive.
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
- [ ] When opening an MPR while another workspace is loaded, offer to close and
  replace the current MPR/MOB set. Prompt once for all dirty documents, allow
  cancel, and clear document-bound undo state only after the close succeeds.
- [ ] Add optional Windows file associations for `.mpr` and `.mob`, registered
  only with explicit user consent and removable from Settings/uninstall.
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
- [x] Add a configurable external editor executable and customizable shortcut.
- [x] Export embedded CP1251 scripts to managed UTF-8 workspaces.
- [x] Watch external edits, coalesce atomic-save notifications, and require
  explicit import only when the text actually differs.
- [x] Make script import one undoable edit and reject non-CP1251 characters clearly.
- [ ] Recover unimported script workspaces after an abnormal exit.

## JMV setup and validation

- [x] Configure and test the development build against a confirmed JMV installation using settings outside upstream v0.3. ([`c8d8b3a`](https://github.com/Ilufus/ei-mapper-endurance/commit/c8d8b3a))
- [x] Keep all live maps untouched during automated tests; use hashed disposable copies.
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
- [x] Include non-object document sections in MOB Diff and decode embedded
  `SS_TEXT`/legacy script changes into readable line additions/removals in the
  UI and text/JSON exports. ([`0a4624f`](https://github.com/Ilufus/ei-mapper-endurance/commit/0a4624f))
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
- [x] Implement the first experimental JMV `MapTestLauncher` backend for one
  loaded MPR plus one active MOB. It stages the exact validated in-memory MOB,
  injects a disposable player bootstrap, writes uniquely named temporary map
  copies into the active JMV `Maps` folder. Alpha.42/43 proved staging and
  cleanup but rejected the attempted console route: `server` is not registered
  as an accessible command in the validated JMV runtime.
  It never replaces a source/live map or rewrites `texts.res`.
- [x] Choose a deterministic spawn from the map table's travel-zone deploy
  rectangles without assuming their centers are free. Score candidates against
  physical MOB object bounds and rectangle edges, fall back across travel zones,
  and refuse launch when no candidate has the required clearance.
- [x] Add a temporary, fingerprinted x86 JMV provider. Alpha.44 proved the ASI
  prototype and private autoexec marker can reach the internal handler, but
  Starter warns about unknown ASIs and a random outer filename is not a usable
  map identity.
- [x] Audit Starter's supported external-provider interface and call the handler
  with the real maps.txt map identity. Runtime evidence showed loose `.plg`
  files are not discovered by JMV's current configuration; use its declared,
  otherwise absent `DEV/Plugin=plugin.dll` slot. Redirect only exact source-map
  MPR/MOB opens to the hash-owned temporary files so the live map and embedded
  MP/SEC namespace remain untouched.
- [x] Retain backward-compatible recovery for interrupted alpha.43-alpha.46
  `2utoexec.cfg` markers, but stop modifying autoexec in current runs. Hash-check
  all temporary writes, remove only hash-matching provider/maps, retain run
  manifests/logs, and recover interrupted cleanup at the next editor start.
- [x] Alpha.46 runtime evidence proved the configured provider loads and both
  callbacks succeed, but its autoexec marker has already run before plugin
  readiness. Alpha.47 then proved the Starter callback thread does not pump
  timer messages: scheduling succeeded but the callback never fired. Wait for
  the visible game window from a bounded worker, subclass it only long enough
  to post one private message, restore its original window procedure, then
  install redirects and invoke the handler on the actual game-window thread.
- [ ] Perform the focused JMV runtime feasibility test: confirm that the engine
  accepts the temporary outer map name while preserving the MPR internal name,
  executes the MOB bootstrap exactly once, creates/controls the test player at
  the selected free position, supports `drawall` and traversal, and restores all
  temporary state after normal exit, launch failure, and a forced crash.
- [ ] Determine supplemental `Auto<map>*.mob` discovery/order and then prototype
  declared multi-MOB test loading through the existing convention or `AddMobEx`.
- [x] Add bridge readiness and request diagnostics to each retained test-run log.
- [ ] Add loaded-map identity reporting; readiness/request logs alone do not yet
  prove the requested map and MOB set loaded exactly once.
- [ ] Keep direct launch disabled for Base EI. JMV remains explicitly
  experimental until the focused runtime gate above passes.

## Release gates

- [x] `0.1.0-alpha.1` through `0.1.0-alpha.13`: initial safety, diagnostics, resources, serializer fixes, backups, model-qualified skins, terrain-atlas correctness, consolidated duplicate-ID reporting, staged-save validation, mod-maker preview reporting, installation-based reusable resource profiles, and corrected first-run/MOB-only viewport initialization. ([implementation evidence](#implementation-evidence))
- [x] `0.1.0-alpha.14`: testing-group build with Settings-based setup,
  explicit base-game humanoid known issue, database appearance diagnostics, and
  a coordinated test/report matrix. ([`800a011`](https://github.com/Ilufus/ei-mapper-endurance/commit/800a011))
- [x] `0.1.0-alpha.31`: final hardening tester build covering ordinary mixed
  property/transform saves, exact same-MOB copy/delete, and known patrol/look
  structural edits with invisible raw preservation checks.
- [x] `0.1.0-alpha.40`: MOB-hardening tester candidate incorporating all known
  alpha.14-group fixes, synchronized internal/external scripts, and readable
  embedded-script MOB diffs. The supported MOB edit implementation is complete;
  promotion now depends on closed-group regression and game testing.
- [x] `0.1.0-alpha.43`: experimental single-MPR/single-MOB JMV direct-launch
  candidate with lossless in-memory staging, obstruction-aware travel-zone
  spawn selection, hash-owned temporary Maps files, and recoverable autoexec
  restoration. Availability remains experimental pending the focused in-game gate.
- [x] `0.1.0-alpha.44`: replace the rejected public-console command with a
  temporary fingerprinted JMV bridge and run-specific bridge diagnostics.
  Availability remains experimental pending the focused in-game gate.
- [x] `0.1.0-alpha.45`: probe a loose mod-scoped `.plg` plus exact temporary-map
  file redirection. Three runtime attempts proved this JMV configuration does
  not discover loose `.plg` files; no provider callback or bridge log occurred.
- [x] `0.1.0-alpha.46`: correct the Starter plugin callback contract after
  static disassembly confirmed that `EntryBeforePatch`/`EntryAfterPatch` are
  one-argument `cdecl` callbacks where zero is success and nonzero enters the
  loader's error/assert path. Runtime evidence also showed loose `.plg` files
  are not discovered by this JMV profile: install temporarily into its declared,
  otherwise absent `DEV/Plugin=plugin.dll` slot without rewriting config.reg.
- [x] `0.1.0-alpha.47`: remove the failed autoexec timing dependency. Schedule
  the map request from a one-shot game-thread timer after `EntryAfterPatch`, and
  leave `2utoexec.cfg` untouched while retaining legacy interrupted-run recovery.
- [x] `0.1.0-alpha.48`: replace the non-firing callback-thread timer with a
  bounded game-window discovery worker and one-shot window-message dispatch.
  The engine handler still executes only on the game window's own thread.
- [x] `0.1.0-alpha.49`: alpha.48 found a game-owned window and queued its
  message before the final message path was stable, but the callback was never
  dispatched. Require the same visible window for two seconds and use bounded
  `SendMessageTimeout` dispatch so receipt or timeout is directly observable.
- [x] `0.1.0-alpha.41`: redo-safe raw provenance and the July 17 multi-edit/UI
  fixes passed owner testing and were handed to the closed tester group.
  ([`db3fa31`](https://github.com/Ilufus/ei-mapper-endurance/commit/db3fa31))
- [ ] Following private alpha: fix issues found by the closed test group and
  publish a concise Base/JMV coverage summary; no new hardening architecture.
- [ ] `0.1.0-beta.1`: correct indexed resources and responsive fast loading.
- [ ] `0.1.0-rc.1`: independent workspace workflow, UI, script editing, and completed safety validation.
- [ ] `1.0.0`: JMV-ready release passing the full corpus, MobExplorer, and in-game validation matrix.
- [ ] Package releases with executable, Qt runtime, source snapshot, GPL notices, build instructions, checksums, and rollback guidance.
- [ ] Consider Qt6/CMake modernization only after 1.0; use a major version if compatibility breaks.

## Wishlist inbox

Add requested enhancements here first. We can then assign each one to a release
or move it into the relevant section above without losing its history.

### High priority

- [x] Warn before changing or deleting an object ID that is referenced by the active MOB's
  embedded script.
  - Scan the decoded script for exact references to the old ID before an ID
    edit or explicit ID-repair transaction is committed.
  - Show the reference count and readable line/context examples so the user can
    identify the affected script logic; write the complete list to the workflow
    log when there are many matches.
  - Treat the result as a warning, not an automatic rewrite or save blocker.
    Script syntax and dynamically constructed IDs can be ambiguous, so only an
    explicit user-approved script edit may replace a referenced ID.
  - The warning is issued for the initiating user action, before the undoable
    transaction is created; undo/redo does not interrupt history playback with
    another prompt. Duplicate IDs are reported as references without assuming
    which duplicate the script resolves to at runtime.
  - Implemented by [the script-reference analyzer](source/documents/script_reference_analyzer.cpp)
    with [focused tests](tests/script_reference_analyzer_tests.cpp).
- [x] Add a hideable **Maps and Collections** browser before implementing the
  standalone custom-collection workflow.
  - Read the Base/JMV map list from the active profile's `texts.res` map-table
    member and show map codenames in a tree.
  - Loading a map entry resolves and opens its corresponding MPR plus primary
    MOB as one workspace operation, with a combined dirty-document prompt when
    replacing the current workspace.
  - Use top-level **Maps** and **Collections** groups; collections append to the
    same navigation model rather than creating a separate competing browser.
  - Place the browser in a left-side hideable panel and make `Ctrl+Space` its
    default customizable visibility shortcut.
  - Report missing/ambiguous map files without silently selecting a fallback.
  - The Maps MVP reads `map.txt` from the active profile's highest-priority
    `texts.res`, resolves the MPR/MOB pair case-insensitively, and leaves the
    Collections group ready for the later portable format.
    See [the map catalog](source/resources/map_catalog.cpp) and
    [focused tests](tests/map_catalog_tests.cpp).
- [ ] Populate database/prototype fields from the active profile database with
  prefix filtering and manual entry where the format permits it. Warn when a
  placed object's referenced entry is missing or was deleted; do not silently
  rewrite or clear the stored value.
- [ ] Make the main workspace panels dockable, detachable, and persistently
  rearrangeable. Support docking to window edges, beside other docks, and as
  tabs with another dock, approximating the useful UE5 layout behavior within
  Qt5's supported docking model.

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
  - Implement this on top of the **Maps and Collections** browser/navigation
    model so custom collections extend the same workflow as profile maps.
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
- [ ] Add a blocking, phase-aware progress dialog for MOB/MPR saves, including
  serialization, validation, backup, and atomic replacement. Never imply that
  replacement completed while validation or backup is still running.

### Medium priority

- [ ] Add optional external MOB diff-tool integration. Pass two explicit files
  or exported snapshots to a configured executable without replacing the
  built-in deterministic semantic diff or exposing unsaved/private data without
  confirmation.

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
- [ ] Correct spacing/alignment offsets for disabled menu items after the
  alpha.41 disabled-color fix.

### Completed from the July 17 intake

- [x] Disable **Create New Object** without an active MOB.
- [x] Use visibly muted, non-bold styling for disabled fields and menu items.
- [x] Remove the inactive context-help button from MOB Diff.
- [x] Keep Model Parts visible as undefined/different for mixed selections and
  allow compatible parts to be pasted across multiple objects.
- [x] Support compatible multi-field paste in **Create Object**.
- [x] Treat logic-point creation plus placement as one Undo/Redo operation.
- [x] Show a blocking progress indicator for long Undo/Redo operations and
  history jumps.
  ([`db3fa31`](https://github.com/Ilufus/ei-mapper-endurance/commit/db3fa31))

## Feature-development knowledge notes

These are implementation leads supplied by active mod development, not yet
universal Base/JMV engine guarantees. Confirm them against representative MOBs,
database entries, and the fingerprinted runtime profile before encoding them as
validation rules or save behavior.

- For humanoid units, the database/prototype name is reported to control visual
  appearance and equipment; the MOB texture field is reportedly ignored. The
  model name remains required and materially affects rendering.
- Do not conflate that required model name with the separate MOB `Template`
  field. The `Template` field, inner-map name, and `ParentID` are reported to
  have no normal runtime effect, but Endurance must still preserve them and must
  verify edge cases before hiding them or weakening validation.
- Model Parts can technically add character parts, but ordinary humanoids should
  retain the known-good/default parts copied from an existing unit. Weapons and
  other equipped visuals should come from the database/equipment resolver rather
  than being invented as model parts.
- `Import`/`Is need import = true` reportedly switches equipment, dropped items,
  and related unit data from database-derived values to MOB-embedded values.
- `InScript`/`Is use in script = true` is reported to matter primarily for
  dynamic non-character objects that scripts move or destroy. Do not generalize
  that interpretation without Base/JMV corpus checks.
- Navigation/movement graph data was historically stored in MOB files, but map
  modders lacked a graph-generation tool. A known workaround places a very large
  door over the map and opens it by script on first load to force runtime graph
  regeneration. Landscape editing and direct launch must detect/preserve this
  convention rather than deleting or "fixing" it automatically.
- A larger-than-normally-allowed MPR has opened successfully in Endurance. This
  is evidence that editor-side size assumptions should remain permissive, but
  runtime limits still require profile-specific in-game validation.

## Updating this checklist

Each completed code item should normally include a commit, test, or release link.
If testing exposes a regression, uncheck the item and add the concrete failing
case beneath it. Changes to this file are committed to `main`, so GitHub keeps a
complete history of what was added, completed, reopened, or reprioritized.
