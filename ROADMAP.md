# EI Mapper: Endurance Edition roadmap

Current development version: `0.3.0-alpha.26`.

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
- [ ] Add structured patrol/look-point and magic-trap attachment editing:
  owner/child lists, viewport selection, orphan/wrong-parent validation, and
  complete compound copy/remap transactions.
- [ ] Add Russian localization and an in-app language switch.
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
  affected-normal updates, and undoable sculpting.
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

- [ ] Integrate the approved modern painter as the third terrain mode: direct
  placement, existing-atlas smart painting, and generated-blend painting must
  share one atlas model.
- [ ] Complete semantic-atlas coverage and the explicit **Finalize Atlas** flow;
  do not import experimental generated imagery.
- [ ] Complete typed overlay-material editing and validated Base/JMV presets.
- [ ] Runtime-confirm landscape edit-mode neutral geometry, overlay reset after
  unload/reload, traversal, liquid interaction, and changed-map save/load.
- [ ] Offer explicit follow-up `map.txt` registration after new-MPR creation.
- [ ] Add cost/component inspection and deterministic loaded/regenerated graph
  diff, including the Zone 6 frozen/unfrozen source comparison.
- [ ] Track navigation-dirty regions incrementally and validate entrance
  reachability, asymmetry, isolation, clearance, and component changes.
- [ ] Runtime-validate representative Base/JMV traversal against generated and
  persisted graphs.

## Approved changelists and evidence

- Completed landscape/MPR integration: `8751b58`.
- Graph-aware Play from Here: `c369086`.
- [AI graph profile policy](docs/validation/AI_GRAPH_PROFILE_POLICY.md).
- [AI graph persistence validation](packaging/ALPHA26_AI_GRAPH_PERSISTENCE_R3.md).
- [AI graph runtime validation](packaging/ALPHA26_AI_GRAPH_RUNTIME_R1.md).
- [Public graph research reference](https://habr.com/ru/articles/769050/).
- [MPR settings and height-range contract](docs/MPR_SETTINGS_AND_HEIGHT_RANGE.md).
- [MPR settings validation](docs/validation/MPR_SETTINGS_HEIGHT_OPERATIONS_R1.md).
- [MPR transaction foundation](docs/validation/MPR_MAXZ_TRANSACTION_FOUNDATION.md).
- [Overlay authoring validation](docs/validation/OVERLAY_SURFACE_AUTHORING_R1.md).
- [New-MOB creation validation](docs/validation/NEW_MOB_CREATION_R1.md).
- [Terrain atlas review contract](docs/TERRAIN_ATLAS_REVIEW.md).
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

- [ ] Runtime-confirm Test Current Map and hot reload begin at the selected
  editor time while the game clock continues normally.
- [ ] Confirm splash/taskbar/setup focus on an existing profile, clean first
  run, and multi-display setup.
- [ ] Run the final representative Base/JMV editor corpus matrix: open,
  no-op/changed saves, undo/redo, autosave/recovery, validation, and shutdown.
- [ ] Run the final JMV runtime matrix: gameplay/dungeon/briefing maps,
  entrances/custom spawn, supplemental MOBs, generation Save/Load, hot reload,
  scripts, `drawall`, transitions, return, and graceful exit.
- [ ] Runtime-confirm immediate, conditional, and nested supplemental `AddMob`
  chains load exactly once when a deterministic fixture exists.
- [ ] Runtime-confirm World Object to Lever conversion with both previously
  shared and previously unused models.
- [ ] Determine the native Vulcansmoke runtime crash mechanism; retain the
  non-blocking Critical validation warning meanwhile.
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

- Keep this file goal-oriented and concise.
- Put unassigned requests in the wishlist; assign accepted work to a version.
- Keep version blocks in descending order and collapsible.
- Link an approved commit, changelist, test, or validation report for completed
  goals.
- Put alpha-by-alpha narratives, investigations, and rejected approaches in
  release notes, validation reports, or Git history—not in this roadmap.
- Reopen a checked goal only for a concrete regression or missing acceptance
  criterion.
