# EI Mapper: Endurance Edition roadmap

Current development version: `0.5.0-alpha`.

This file tracks product goals, not development diaries. Completed goals link
to the approved commits, changelists, or validation reports that implemented
them. Investigation notes and rejected experiments belong in `docs/` and Git
history. The permanent safety and compatibility rules are in
[Project contract](docs/PROJECT_CONTRACT.md).

<details open>
<summary><strong>General wishlist — not assigned to a release</strong></summary>

## Authoring and workflow

- [x] Complete the object-property label/tooltip audit and expose previously
  hidden safe fields plus all five unit logic slots. Serialized names and values
  remain unchanged; structural object type stays read-only
  ([`55b8510`](https://github.com/Ilufus/ei-mapper-endurance/commit/55b8510)).
- [ ] Finish structural validation and remapping for Behavior attachments:
  detect orphan/wrong-parent relationships and complete cross-owner compound
  copy/remap transactions without weakening the implemented authoring UI.
- [x] Add Russian localization and an in-app language switch
  ([`b3854b0e`](https://github.com/Ilufus/ei-mapper-endurance/commit/b3854b0e)).
- [ ] Add opt-in Windows associations for `.mpr`, `.mob`, and `.collection`.
- [ ] Add a viewport action that copies the current camera transform as
  ready-to-paste game script calls.
- [ ] Add baseline/candidate swapping, 3D perspective comparison, deterministic
  JSON export, and explicit ambiguous-match handling to MOB Diff.
- [ ] Add optional external MOB diff-tool integration.
- [ ] Add optional MPR-only Test Current Map by staging a structurally valid
  empty MOB; keep profile-specific spawn requirements explicit.
- [ ] Add an opt-in MCP server whose editing operations use normal Endurance
  transactions, undo, validation, backup, and confirmation boundaries.

## Resources, performance, and diagnostics

- [ ] Move the remaining cold RES index/decode work off the GUI thread and
  persist archive indexes by path, size, timestamp, and header hash.
- [ ] Surface existing phase timings, shadowed assets, missing resources, and
  decode failures through the workflow/message-log panel.
- [ ] Cover every observed Base/JMV MMP format with either correct decoding or
  an explicit asset/archive/format diagnostic.
- [ ] Support explicitly configured ZIP archives as read-only loose texture
  sources without extracting into the game tree.
- [ ] Continue measured heavy-map loading and renderer optimization; retain the
  current five-second complete-workspace target on the reference machine.

## Validation and recovery policy

- [x] Add profile/assembly-aware, non-blocking character-mask diagnostics for
  the verified helmet-hair algorithm plus model-local FIG/BON crown evidence
  on every database-equipped character,
  dangerous wearable empty/missing masks,
  missing core anatomy, dotted alternative overlap, and duplicates while
  preserving unknown tokens and creature conventions
  ([validation](docs/validation/CHARACTER_BODYPART_VALIDATION.md)).
- [ ] Design an explicit rescue workflow for known game-tolerated malformed
  records. Opening stays non-destructive; repair is opt-in; knowingly preserving
  a defect requires explicit consent and logging.
- [ ] Extend guided NID repair to per-script-reference assignment and
  coordinated multi-MOB save cohorts.
- [ ] Stress repeated large-map open/edit/undo/redo/save/close and abnormal-exit
  recovery across scripts, model parts, multi-selection, and compound objects.
- [ ] Convert every reproducible format regression into a minimized synthetic
  fixture where licensing permits.

## Long-term integrations

- [ ] Add advanced local runtime-variant approval based on complete guarded
  layout/signature validation, never a user-entered hash alone.
- [ ] Support other mods through optional profile/runtime plugins only when
  requested by their authors.
- [ ] Restore MOB VSS as a post-baseline feature: lossless parser/viewer first,
  then proven VSS/script synchronization, visual editing, and transactional
  conflict handling.

</details>

<details>
<summary><strong>1.0.x and later — production release and compatibility</strong></summary>

## Goals

- [ ] Publish the first stable JMV-ready release with complete source, GPL and
  attribution notices, reproducible build instructions, validation report,
  checksums, and rollback guidance.
- [ ] Complete the representative Base/JMV editor and runtime matrices.
- [ ] Establish release signing/reputation guidance for the unsigned editor and
  its process-owned runtime bridge.
- [x] Confirm Windows 7 compatibility for the current Qt5 editor build without
  weakening the default modern-Windows renderer path.
- [ ] Keep Base support independent from JMV behavior and keep Test Current Map
  optional for profiles without a validated provider.
- [ ] Consider Qt6/CMake modernization only after 1.0; use a major version if
  compatibility breaks.

</details>

<details open>
<summary><strong>0.5.x — beta content and resource authoring</strong></summary>

## Release goal

Promote Endurance into beta with production terrain painting and integrated,
profile-aware database/resource authoring. Database and RES changes must retain
the same review, validation, preservation, backup, and atomic-publication
boundaries as MOB and MPR authoring.

## Completed foundation

- [x] Add a schema-aware Database Editor for all 26 physical Evil Islands
  tables, including typed and structured fields, lossless opaque-field
  preservation, clone/delete, undo/redo, review, validation, XLSX interchange,
  and native database publication.
- [x] Add a dockable Resource Browser for active-profile and external archives
  with provider provenance, search/filtering, metadata and format-aware
  previews, export, and reviewed Save As mutation transactions.
- [x] Integrate both tools: protected `database.res` opens in the Database
  Editor, while established database resource fields open the exact winning
  active-profile member in the Resource Browser.
- [x] Preserve dock ownership and multi-window stacking during cross-tool
  navigation; bring the destination tool and its host window forward without
  silently undocking either tool.
- [x] Make wide database tables interactive at normal UI latency, including
  Race Models: keep the literal `#` column frozen by default, preserve explicit
  freeze/layout state, softly highlight the selected row, and avoid full-grid
  work during paint, selection, context-menu, and horizontal-scroll events.
- [x] Consolidate viewport visibility into a collapsed-by-default **Visuals**
  switcher with synchronized entrance, static-object, dynamic-object,
  object-helper, scene-light, terrain, overlay, object-shadow, environment,
  particle, and animation-freeze controls.

## Remaining beta gates

- [x] Ship the generated-blend terrain painter and explicit **Finalize Atlas**
  workflow tracked in 0.4.x.
- [ ] Run the representative Base/JMV beta acceptance matrix for database and
  RES review, mutation, reopen, preservation, and cross-tool navigation.

## 0.5 release-candidate known issues and boundaries

- [ ] **Lava + Resolve Blends:** direct Modern painting follows the selected
  Lava material, but **Shift+R** cannot yet preserve one visually consistent
  route owner across every rare connected Lava-on-Water transition without
  regressing ordinary Ground/Overlay repair. The bounded generic resolver is
  retained while this remains under review.
- The shipped 32-bit runtime is validated through **4,096 terrain-atlas slots / 64
  pages**. Endurance can represent the packed-format maximum of 16,384 slots,
  but sizes above 4,096 are not release-supported: the controlled 8,192-slot
  run failed from 32-bit address-space pressure.
- Test Current Map hot-reloads ordinary MPR/MOB changes in-process. A terrain
  atlas byte/page/slot change deliberately restarts only the owned test process
  so the game cannot retain a stale resource identity.
- [ ] Clear or explicitly waive the remaining headless UI-check failures for
  compact Landscape Tools layout and floating-dock native-window identity.
  Their corresponding interactive workflows have passed tester review, but the
  automated release gate is not yet clean.
- [ ] Isolate the recurring ANGLE compatibility error after
  `frame/overlays-complete`; no equivalent default desktop-OpenGL defect is
  currently confirmed.

## Approved changelists and evidence

- [Database and resource authoring beta foundation](docs/releases/0.5.0-beta-database-resource-foundation.md).
- Database/Resource Browser integration and navigation: `6940613d` through
  `4038ba23`.
- Frozen-column, paint-latency, and multi-window fixes: `e0873c97` and
  `0eebd4b1`; the r8 tester build was user-confirmed.
- [Integrated Database Editor contract](docs/DATABASE_EDITOR_VERTICAL_SLICE.md).
- [Viewport Visuals, Noise sculpt, and live-stroke responsiveness](packaging/ALPHA26_SCULPT_PERFORMANCE_R4.md).
- [Full database editor coverage](docs/releases/0.3.0-alpha.26-full-database-editor.md).
- [Expanded terrain-atlas shipped-runtime boundary](docs/validation/EXPANDED_TERRAIN_ATLAS_RUNTIME.md).
- Resource Browser validation:
  [r1](packaging/ALPHA26_RESOURCE_BROWSER_R1.md),
  [r2](packaging/ALPHA26_RESOURCE_BROWSER_R2.md), and
  [r3](packaging/ALPHA26_RESOURCE_BROWSER_R3.md).

</details>

<details open>
<summary><strong>0.4.x — lossless landscape and AI navigation</strong></summary>

## Release goal

Deliver safe production landscape editing and profile-aware AI navigation
authoring. Changed MPRs must use the lossless document path and must never fall
back to the legacy rebuilding serializer; graph persistence remains explicit.

## Completed foundation

- [x] Preserve untouched archive members, names, order, metadata, alignment,
  namespaces, and raw bytes.
- [x] Patch only edited MP/SEC/overlay payloads and verify untouched-member
  equality.
- [x] Stage, freshly reopen, semantically validate, back up, atomically replace,
  and rebind every changed MPR save.
- [x] Add canonical 33x33 sector ownership, shared-seam synchronization,
  affected-normal updates, undoable sculpting, and a deterministic per-stroke
  Noise brush for terrain and overlays.
- [x] Add MPR settings, global height operations, `maxZ` handling, crop/expand,
  object translation, new-MPR creation, and MPR recovery autosaves.
- [x] Add direct tile placement and smart painting constrained to the existing
  atlas.
- [x] Add undoable overlay creation/removal/sculpting/material assignment with
  exact neutral geometry during editing.
- [x] Add `map.txt` catalog editing, new-MOB creation, minimap generation,
  grading, export, viewport navigation, and runtime staging.
- [x] Add AI graph preview/regeneration/staging/persistence foundations and
  graph-aware Play from Here.
- [x] Preserve and parse embedded `AI_GRAPH` and external `.grp` sources with
  explicit ownership and lossless Test Current Map staging.
- [x] Add Navigation viewport layer selection, node/link/isolation preview, and
  loaded-versus-regenerated source override.
- [x] Implement deterministic terrain/liquid/profile regeneration for all eight
  movement profiles with authored object/bridge navigation rasterization.
- [x] Make graph persistence an explicit previewed, backed-up, freshly
  reparsed, atomic bake/export/ownership-switch operation.
- [x] Add deterministic regeneration, component recoloring, structural reopen,
  and graph-aware spawn/path projection tests.

## Remaining goals

- [x] Integrate the approved three-mode terrain painter: Modern resolves exact
  authored identities before deterministic generation, Classic Auto remains
  non-mutating with Safe enabled by default, and Manual writes the selected
  physical slot/rotation directly. All modes share the provenance-aware working
  atlas and one undoable stroke contract.
- [x] Complete the reviewed semantic-atlas authority and explicit draft/finalize
  publication flow, including paired MPR+RES identity, expanded addressing,
  deterministic resolve, undo/redo persistence, and runtime hot reload.
- [x] Add typed overlay-material editing with live preview, lossless undoable
  application, animation records, and per-cell material-slot inspection.
- [ ] Add validated Base/JMV surface-material presets.
- [ ] Runtime-confirm landscape edit-mode neutral geometry, overlay reset after
  unload/reload, traversal, liquid interaction, and changed-map save/load.
- [ ] Offer explicit follow-up `map.txt` registration after new-MPR creation.
- [x] Add cost/component inspection and deterministic loaded/regenerated graph
  comparison, backed by the native graph parity audit.
- [x] Runtime-validate representative Base/JMV traversal against generated and
  persisted graphs.

## Approved changelists and evidence

- Completed landscape/MPR integration: `8751b58`.
- Graph-aware Play from Here: `c369086`.
- [AI graph profile policy](docs/validation/AI_GRAPH_PROFILE_POLICY.md).
- [AI graph persistence validation](packaging/ALPHA26_AI_GRAPH_PERSISTENCE_R3.md).
- [AI graph runtime validation](packaging/ALPHA26_AI_GRAPH_RUNTIME_R1.md).
- [AI graph parity validation](packaging/ALPHA26_AI_GRAPH_PARITY_R4.md) and the
  preserved 47-graph native audit corpus (`9739e1e`).
- [Public graph research reference](https://habr.com/ru/articles/769050/).
- [MPR settings and height-range contract](docs/MPR_SETTINGS_AND_HEIGHT_RANGE.md).
- [MPR settings validation](docs/validation/MPR_SETTINGS_HEIGHT_OPERATIONS_R1.md).
- [MPR transaction foundation](docs/validation/MPR_MAXZ_TRANSACTION_FOUNDATION.md).
- [Overlay authoring validation](docs/validation/OVERLAY_SURFACE_AUTHORING_R1.md).
- [New-MOB creation validation](docs/validation/NEW_MOB_CREATION_R1.md).
- [Terrain atlas review contract](docs/TERRAIN_ATLAS_REVIEW.md).
- [Modern terrain editor contract](docs/MODERN_TERRAIN_EDITOR_MVP.md) and
  [R180 integration test scope](packaging/ALPHA26_TERRAIN_PAINT_MODES_R180_MODERN_DRAFT_UI_SEMANTIC_TEST.md).
- Minimap changelists:
  [generation](docs/releases/0.3.0-alpha.19-minimap-generation.md),
  [viewport/export](docs/releases/0.3.0-alpha.20-minimap-viewport-and-export.md),
  [fidelity/reload](docs/releases/0.3.0-alpha.21-minimap-fidelity-and-reload.md),
  [authored profiles](docs/releases/0.3.0-alpha.22-minimap-authored-profile.md),
  [runtime controls](docs/releases/0.3.0-alpha.23-minimap-control-and-runtime-header.md),
  [grading](docs/releases/0.3.0-alpha.24-modeless-minimap-grading.md),
  [navigation/locking](docs/releases/0.3.0-alpha.25-minimap-navigation-and-locking.md),
  and [polish](docs/releases/0.3.0-alpha.26-minimap-polish.md).

</details>

<details open>
<summary><strong>0.3.x — integrated editor and direct-gameplay release</strong></summary>

## Release goal

Publish the hardened editor, profile-correct rendering, collections, runtime
validation, Test Current Map, hot reload, and the integrated landscape
foundation as one coherent `0.3.0` release.

## Completed goals

- [x] Lossless MOB editing for supported properties, transforms, scripts,
  model parts, patrol/look points, copy/paste, create/delete, and conversion.
- [x] Runtime-family range/NID validation, copied-workspace overlays,
  non-blocking findings, source-object navigation, and conservative repair.
- [x] Independent MPR/MOB opening, portable collections, Maps and Collections,
  Loaded Map ownership/visibility/locking, and safe targeted unload.
- [x] Versioned autosaves, backups, atomic saves, crash diagnostics, interrupted
  test recovery, and clean-package validation.
- [x] Parameter-aware outliner search, dockable workspace panels, transactional
  property/component/model-part copy, configurable shortcuts, and internal/
  external script workflows.
- [x] Structured Behavior-mode authoring for all unit Logic slots, patrol/look
  hierarchies, and magic-trap activation/cast arrays: outliner and property
  collections, viewport selection, disabled-slot preview, contextual creation,
  copy/paste/duplicate, drag reordering, parent/sibling navigation, and a
  diplomacy-derived read-only trap-target table with a symbolic
  Ally/Neutral/War MOB diplomacy matrix.
- [x] Profile-correct humanoid appearance, equipment, animation, texture/UV
  inspection, camera-priority preparation, session caches, culling, and render
  queues.
- [x] Add indexed RES lookup, negative resource caches, asynchronous character
  preparation, persistent derived visual caches, bounded GUI-thread GPU work,
  and load/render phase timings.
- [x] Resource-driven environment lighting, authored point lights, particle
  reconstruction/batching, liquid material animation, time-of-day controls,
  and desktop OpenGL overlay confirmation.
- [x] JMV Test Current Map with selected entrances, synthetic unmapped-map
  identity, supplemental MOBs, graph/resource staging, isolated saves,
  Godmode, Play from Here, and exact runtime ownership.
- [x] State-aware hot reload with immutable generations, same-map position/
  facing restoration, transition deferral, orphan recovery, and cleanup.
- [x] Startup splash, UI scaling, accessibility fixes, camera-speed controls,
  frame-rate-independent movement, and clean first-run setup.
- [x] Dockable, filterable Message Log and MOB Family Validation with shared
  dock/floating tabs, persistent multi-monitor layout, correct View toggles,
  newest-message following, and main-window-owned shutdown.
- [x] Integrate the completed lossless landscape/MPR/minimap/AI graph foundation.

## Release gate

- [x] Runtime-confirm Test Current Map and hot reload begin at the selected
  editor time while the game clock continues normally
  ([`7401874`](https://github.com/Ilufus/ei-mapper-endurance/commit/7401874)).
- [x] Confirm splash/taskbar/setup focus on an existing profile, clean first
  run, and multi-display setup
  ([`3af2099`](https://github.com/Ilufus/ei-mapper-endurance/commit/3af2099)).
- [ ] Run the final representative Base/JMV editor corpus matrix: open,
  no-op/changed saves, undo/redo, autosave/recovery, validation, and shutdown.
- [ ] Run the final JMV runtime matrix: gameplay/dungeon/briefing maps,
  entrances/custom spawn, supplemental MOBs, generation Save/Load, hot reload,
  scripts, `drawall`, transitions, return, and graceful exit.
- [ ] Runtime-confirm immediate, conditional, and nested supplemental `AddMob`
  chains load exactly once when a deterministic fixture exists.
- [ ] Runtime-confirm World Object to Lever conversion with both previously
  shared and previously unused models.
- [x] Complete the native Vulcansmoke runtime-failure investigation and retain
  the non-blocking Critical validation warning; the controlled A/B identifies
  the hazardous record while the absent `Vulkan.mmp` remains the leading, not
  proven, mechanism
  ([investigation](docs/investigations/2026-07-23-particle-runtime.md#full-catalog-editor-freeze-and-vulcansmoke-runtime-control),
  [status](docs/PARTICLE_RENDERING_STATUS.md#runtime-safety-and-performance)).
- [ ] Runtime-confirm the complete synthetic map identity used by unmapped
  MPR/MOB testing.
- [ ] Reconfirm the distribution contract, resolve concrete closed-group
  blockers, then tag and package immutable `v0.3.0`.

## Non-blocking follow-ups

- [ ] Isolate the recurring ANGLE error after `frame/overlays-complete` before
  attributing it to any renderer subsystem.
- [ ] Continue the measured renderer plan: persistent/GPU-driven submission,
  dirty-state preparation, cached static/dynamic shadows, and evidence-gated
  raster reduction.
- [ ] Finish deeper persistent resource indexing and broader diff presentation.

## Approved changelists and evidence

- MOB hardening: [`aaef298`](https://github.com/Ilufus/ei-mapper-endurance/commit/aaef298),
  [`843ba9c`](https://github.com/Ilufus/ei-mapper-endurance/commit/843ba9c),
  [`11698ad`](https://github.com/Ilufus/ei-mapper-endurance/commit/11698ad),
  and [`db3fa31`](https://github.com/Ilufus/ei-mapper-endurance/commit/db3fa31).
- Runtime capabilities and providers:
  [`97b6991`](https://github.com/Ilufus/ei-mapper-endurance/commit/97b6991),
  [`b467d87`](https://github.com/Ilufus/ei-mapper-endurance/commit/b467d87),
  and [`eff6f99`](https://github.com/Ilufus/ei-mapper-endurance/commit/eff6f99).
- Collections and workspace UI:
  [`4e62424`](https://github.com/Ilufus/ei-mapper-endurance/commit/4e62424),
  [`51d9ad0`](https://github.com/Ilufus/ei-mapper-endurance/commit/51d9ad0),
  [`517cc1f`](https://github.com/Ilufus/ei-mapper-endurance/commit/517cc1f),
  and [`a06e5fd`](https://github.com/Ilufus/ei-mapper-endurance/commit/a06e5fd).
- Behavior hierarchy and collection authoring:
  [`e2015b1`](https://github.com/Ilufus/ei-mapper-endurance/commit/e2015b1),
  [`0b220d9`](https://github.com/Ilufus/ei-mapper-endurance/commit/0b220d9),
  [`36230d4`](https://github.com/Ilufus/ei-mapper-endurance/commit/36230d4),
  [`a4bb00e`](https://github.com/Ilufus/ei-mapper-endurance/commit/a4bb00e),
  and [`86e6ae0`](https://github.com/Ilufus/ei-mapper-endurance/commit/86e6ae0)
  ([interaction validation](packaging/ALPHA26_OBJECT_UI_REVIEW_R20.md),
  [trap-target table](packaging/ALPHA26_TRAP_TARGET_TABLE_R23.md),
  [diplomacy labels](packaging/ALPHA26_DIPLOMACY_LABELS_R24.md),
  [directional diplomacy](packaging/ALPHA26_DIPLOMACY_DIRECTION_R25.md),
  [diplomacy display](packaging/ALPHA26_DIPLOMACY_DISPLAY_R26.md),
  [diplomacy layout](packaging/ALPHA26_DIPLOMACY_LAYOUT_R27.md),
  [responsive diplomacy](packaging/ALPHA26_DIPLOMACY_RESPONSIVE_R28.md)).
- Profile visuals, particles, and environment:
  [`37e0231`](https://github.com/Ilufus/ei-mapper-endurance/commit/37e0231),
  [`aa465c3`](https://github.com/Ilufus/ei-mapper-endurance/commit/aa465c3),
  [`13639ca`](https://github.com/Ilufus/ei-mapper-endurance/commit/13639ca),
  and [`2994805`](https://github.com/Ilufus/ei-mapper-endurance/commit/2994805).
- Heavy-map loading and visual-cache work: `ab09e34`, `89405d0`, `0d3676d`,
  and `e24e31b`.
- Time, splash, and startup:
  [`7401874`](https://github.com/Ilufus/ei-mapper-endurance/commit/7401874)
  and [`3af2099`](https://github.com/Ilufus/ei-mapper-endurance/commit/3af2099).
- Message Log and validation-window workflow:
  [`fb2a854`](https://github.com/Ilufus/ei-mapper-endurance/commit/fb2a854),
  [`42949a2`](https://github.com/Ilufus/ei-mapper-endurance/commit/42949a2),
  [`c9d5214`](https://github.com/Ilufus/ei-mapper-endurance/commit/c9d5214),
  [`3049773`](https://github.com/Ilufus/ei-mapper-endurance/commit/3049773),
  and [`14e972a`](https://github.com/Ilufus/ei-mapper-endurance/commit/14e972a).
- Current landscape/MPR integration: `8751b58`.
- [MOB hardening validation](docs/validation/MOB_HARDENING_VALIDATION.md).
- [Particle rendering status](docs/PARTICLE_RENDERING_STATUS.md).
- [Environment rendering validation](docs/ENVIRONMENT_RENDERING_VALIDATION.md).
- [Overlay animation evidence](docs/investigations/2026-07-26-overlay-material-animation.md).

</details>

<details>
<summary><strong>0.2.x — completed hardened workflow and direct-gameplay milestone</strong></summary>

## Goals delivered

- [x] Capability-based Base/JMV profiles and deterministic resource discovery.
- [x] Runtime-family NID/range validation and workspace-overlay resolution.
- [x] Portable collections, multi-MOB viewing, one active editable MOB, and
  cross-MOB copy into the active destination.
- [x] JMV direct gameplay, process-owned bridge/host, isolated staging, and
  generation-scoped saves.
- [x] State-aware hot reload, cleanup journals, orphan detection, and Stop Test.
- [x] Transactional script/model-part/property workflows, undo/redo safety,
  autosave, backups, loading/saving progress, and outliner filtering.
- [x] Profile-correct resources/rendering baseline and deterministic MOB Diff.

## Approved changelists

- [`97b6991`](https://github.com/Ilufus/ei-mapper-endurance/commit/97b6991) — profile capabilities.
- [`fd30be0`](https://github.com/Ilufus/ei-mapper-endurance/commit/fd30be0) and
  [`0a4624f`](https://github.com/Ilufus/ei-mapper-endurance/commit/0a4624f) — MOB Diff.
- [`4e62424`](https://github.com/Ilufus/ei-mapper-endurance/commit/4e62424) — collections.
- [`c1ea993`](https://github.com/Ilufus/ei-mapper-endurance/commit/c1ea993) — supplemental MOB staging.
- [`517cc1f`](https://github.com/Ilufus/ei-mapper-endurance/commit/517cc1f),
  [`90eb899`](https://github.com/Ilufus/ei-mapper-endurance/commit/90eb899), and
  [`8a0badd`](https://github.com/Ilufus/ei-mapper-endurance/commit/8a0badd) — Loaded Map workflow.

</details>

<details>
<summary><strong>0.1.x — completed repository and MOB-safety foundation</strong></summary>

## Goals delivered

- [x] Independent private Endurance repository, branding, versioning, GPL-3.0
  attribution, isolated settings, and upstream history.
- [x] Complete raw-section MOB parsing, unknown-data preservation, semantic
  diff, provenance-checked edits, exact subtree copy/delete, and fail-closed
  saving.
- [x] Fresh reopen validation, backup, atomic replacement, crash diagnostics,
  rotating logs, and child-window lifecycle safety.
- [x] Installation-based Base/JMV profiles, indexed resources, independent
  MPR/MOB opening, neutral-grid MOB mode, configurable undo, and script tools.

## Approved changelists

- [`054959b`](https://github.com/Ilufus/ei-mapper-endurance/commit/054959b) — product/repository foundation.
- [`2568e38`](https://github.com/Ilufus/ei-mapper-endurance/commit/2568e38) — diagnostics and rotating logs.
- [`8a7bc3c`](https://github.com/Ilufus/ei-mapper-endurance/commit/8a7bc3c) — recoverable backups.
- [`aaef298`](https://github.com/Ilufus/ei-mapper-endurance/commit/aaef298) — raw-section MOB document and diff.
- [`843ba9c`](https://github.com/Ilufus/ei-mapper-endurance/commit/843ba9c) — generic raw-leaf saves.
- [`11698ad`](https://github.com/Ilufus/ei-mapper-endurance/commit/11698ad) — exact copy/delete/patrol/look saves.
- [`db3fa31`](https://github.com/Ilufus/ei-mapper-endurance/commit/db3fa31) — redo-safe provenance.

</details>

## Roadmap maintenance

### Authorized implementation: EI-ADD-FROM-DB-001

- Original problem: Create Object requires technical fields and a selected
  same-type raw MOB template; there is no complete database-to-map workflow.
- Expected result: all three searchable categories (monster-prototype creatures,
  DB items as ordinary objects (latest user decision), usable objects/levers), names/models/preview, supported
  defaults, placement, undo/redo, guarded save/reopen; technical options secondary.
- Authority: dedicated contributor branch, project code in this worktree only;
  read-only installed-data research; tests/build, scoped commit/push/PR, permanent
  unpacked tester folder. No main merge, live game/mod/settings changes, or private
  source/data in repository, package, or publication.
- Base: `contrib/Samolleg/add-from-db` from fetched `origin/main`
  `ffdc28ea9f749896ddffdd583e2a6a5e44d5201a` (2026-09-11).
- Status: partial implementation on 2026-09-11: active database catalog,
  searchable picker, creature/item/lever preview and click placement, fresh validated
  creation records, and undo/save/redo retention implemented. Item placement is now
  an ordinary object; the user owns lever conversion and the interaction script.
  This explicitly supersedes R2's native pickup requirement. Complete automatic
  lever appearance and all-entry/gameplay acceptance remain open.
  Parent remains open until all three categories and their
  persistence/regression checks are delivered. Canonical task read from the
  main checkout roadmap; its local changes have not been overwritten.
- Original cause: the creation form had no database selection and required a
  selected same-type raw template. Database mode now uses its own independently
  generated, parsed/reopened record; the save guard still requires hashed bytes.
- Evidence and repeatable acceptance cases:
  [creation research](docs/research/add-from-database-001.md). Read-only traversal
  passed on 252 installed MOBs; both databases contain 37 lever prototype rows.
  [validation checkpoint](docs/validation/add-from-database-001.md): seven focused
  tests pass, including installed Base/JMV catalog checks and archive identity;
  the creation diagnostic passes real undo/redo and save/undo/save/redo/save,
  multiple fresh IDs, editable reopen and corrupt-hash rejection.
- R2 (resolved 2026-09-11): native item/bag pickup, no new scenario. The proposed
  generated-script route is rejected; all three categories remain required.
  R3 (necessary research): validated fresh raw records and
  creature appearance/lever defaults; current renderer forbids blind DB fallback.
- R4 (research gap, not an outstanding product decision): independent native
  item pickup remains unverified. The user reaffirmed creating the selected
  database object on the map; the proposed creature-loot substitution is not
  authorized and the follow-up choice is withdrawn. Persistence format is an
  implementation concern, not a prerequisite the user must resolve.
- R5 (necessary regression, fixed): a creation command lost its source after
  save/undo/save, so redo could not be saved. The original failure was reproduced;
  undo now retains the exact current source/clean bytes before removal, and the
  full sequence passes for creatures and levers.
- Delivery checkpoint: unpacked tester folder finalized outside this worktree;
  distribution validation passes and executable hash matches the tested build.
  [Tester notes](packaging/ADD_FROM_DATABASE_TEST.md) explicitly describe the
  incomplete scope; this is a draft contributor handoff, not parent closure.
- Next: validate the delivered ordinary-item workflow, supported automatic lever appearance,
  and interactive/gameplay validation. This checkpoint does not close the parent.
  No installed data or user settings changed; no game assets or private source
  were copied into implementation or distribution.
- R6/R7/R8 (fixed for the recorded cases, 2026-09-11 user video): JMV AncientCurseM25 and
  ArmoredTurtle110 have DB texture names but render as cyan silhouettes; the
  item `2h long spear` has no preview; Place leaves the window open. Expected:
  textured previews, including weapons, and immediate cursor-bound placement
  with the picker hidden, click to commit and Escape to cancel. Decoder installed
  only in ignored build-video-tools; sampled the supplied 30-second video.
  First incorrect decisions: preview reuses viewport shader uniforms (including
  solid customColor); placement checks a PNT3-only CPU atlas decoder for ordinary
  DXT textures; item placement eligibility suppresses preview; the custom placement
  event filter never draws a pending object. Reproduction and checks tracked in
  docs/validation/add-from-database-video-regressions.md. These are necessary
  fixes within EI-ADD-FROM-DB-001, not a change to its native-pickup requirement.
  The original shader regression was rebuilt and failed (two-color silhouette);
  the fixed renderer passes visually inspected JMV desktop/ANGLE and base-resource
  previews. The production picker/viewport transaction passes immediate placement,
  pointer motion, cancellation and one-action undo/redo. Thirteen focused tests
  and the original creation/save diagnostic pass. r2 preserves the user's used r1
  folder. Native item placement and all-entry/gameplay acceptance remain open.
  r2 is finalized in the permanent tester location; executable hashes match the
  tested build. The existing draft PR #6 remains the contributor review target.
  User acceptance on 2026-09-11: NPC placement works in r2. The supplied screenshot
  confirms the selected weapon/material preview, but item placement is disabled.
  This is the existing implementation gap R4: the catalog rejects all item
  creation and the form never enables its Place button for items. It is not a
  reported missing texture or failed user setup. Next remains native collectible
  creation and validation without a new scenario; parent scope is unchanged.
- R4 continuation (2026-09-11): user explicitly authorized item implementation.
  The screenshot acceptance case and first rejecting editor decisions are recorded
  in [native item checkpoint](docs/research/database-native-item-placement.md).
  Additional read-only research did not establish an independent native pickup
  path in the inspected Base/JMV facilities; the collection routes examined
  require a unit target. A proposed runtime extension changes map compatibility
  and remains unproven. Next: resolve whether a matching game runtime dependency
  is acceptable, then prove actual pickup and persistence before enabling the
  picker. Ordinary game compatibility cannot be silently dropped. No production
  code or installed files changed at this checkpoint; parent remains open.
- R4 scope change explicitly requested by user (2026-09-11): place the selected
  database item as an ordinary world object with its model/material. The user
  will convert it to a lever and attach an existing interaction script. This
  supersedes the native-pickup requirement for items; no runtime dependency or
  generated script is needed. Implement enabled Place, immediate mouse ghost,
  cancel/commit, history, save/reopen and retained appearance. Preserve the
  earlier requirement/research above as history. Status: implemented and checked
  for Base/JMV weapons; original disabled button reproduced before the fix.
  Pointer/cancel/commit/history and save/reopen pass; converting the placed sword
  to a lever retains model, selected parts and material, with identical rendered
  images. See [r3 validation](docs/validation/database-item-placement-r3.md).
  r3 is finalized in the permanent tester location; staged and delivered
  executable hashes match. Thirteen focused tests and Base/JMV desktop/ANGLE
  placement checks pass. The used r2 folder is preserved. Gameplay with the
  user's interaction script remains a user check, not automatic native pickup.
- R9 (2026-09-11, necessary regression): user placed `cheat dagger.bronze` on
  zone1; editor textured, game white. Read-only inspection confirms both texture
  fields include a terminal `.mmp`. The game appends the extension while the
  editor's permissive lookup strips it, masking the invalid saved identifier.
  Expected: persist extensionless texture identifiers while preserving `.x2`
  and material suffixes. New item creation is fixed; the red test reproduced the
  invalid filename before the fix. Thirteen focused tests, saved primary/secondary
  fields, exact resource lookup and Base/JMV desktop/ANGLE checks pass. r4 is
  finalized in staging and the permanent tester location, with matching hashes.
  See [r4 validation](docs/validation/database-item-texture-r4.md). The existing user map
  remains unchanged; its two texture fields can be corrected in the editor.
  Next: user reloads the corrected map for in-game appearance acceptance. This
  checkpoint does not claim a corrected runtime screenshot or close parent gaps.
- Related gap EI-ADD-FROM-DB-001-G1: prior offscreen `--ui-widget-check` crashed
  with 0xC0000005; reproduced under the minimal Qt platform. Interactive UI
  acceptance remains unverified, independently of the passing creation checks.
- Related research EI-ADD-FROM-DB-001-R1: named EI safety/reference skills absent
  from listed skills and searched local skill/plugin locations. Research followed
  explicit user authorization for read-only inspection; no private contents or
  provenance may enter the repository, package, or publication.

- Keep this file goal-oriented and concise.
- Put unassigned requests in the wishlist; assign accepted work to a version.
- Keep version blocks in descending order and collapsible.
- Link an approved commit, changelist, test, or validation report for completed
  goals.
- Put alpha-by-alpha narratives, investigations, and rejected approaches in
  release notes, validation reports, or Git history—not in this roadmap.
- Reopen a checked goal only for a concrete regression or missing acceptance
  criterion.
