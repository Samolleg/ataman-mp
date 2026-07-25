# EI Mapper: Endurance Edition roadmap

This is the living checklist for the private Endurance Edition line. Versioned
work is ordered from the newest planned minor line to the oldest completed one.
An item is checked only after its implementation has landed and received the
stated validation. Completed code should carry a commit, test, or release link.

Current development line: `0.3.0-alpha.18` focused closed testing. Alpha.11
was withdrawn after its temporary-resource bootstrap passed the fixed world
object's first word as the object pointer and crashed both normal and synthetic
Test Current Map launches. Alpha.12 corrects the pointer contract and adds an
uncompressed 8888 placeholder MMP with explicit runtime-compatible masks. Its
owned-process runtime smoke test must reach `GameplayReady`, remain stable for
15 seconds, and produce no native dump for both Zone 1 and the generated
empty-control fixture before packaging. Alpha.13 gates Play from Here on the
verified JMV autorun-MOB lifecycle and makes keyboard camera movement
frame-rate independent. Alpha.14 preserves the complete serialized Parts array
through editing/copying, restores renamed-primary validation overlays, and
completes Loaded Map MOB context actions. Alpha.15 adds source/state filtering,
sortable columns, saved Parts-window sizing, explicit unknown-token labeling,
and the missing canonical humanoid `hr` option. Alpha.16 keeps state aliases
filter-only, makes every Parts column resizable,
uses exact serialized snapshots for Parts undo/redo and cross-MOB paste, warns
when multiple structurally primary MOBs are loaded, and refuses to treat an
Autozone-only set as a valid Test Current Map composition. Alpha.17 corrects
that boundary: secondary-only mapped compositions remain
valid Test Current Map inputs, while only the SpellAddon autorun-dependent
Play from Here, Godmode, and time override controls are disabled. It also
makes all six Parts checkbox filter aliases reliable after sorting. This is a
version reclassification of the feature set that outgrew the `0.2.1` label,
not a serializer or compatibility reset. Alpha.18 removes the remaining
secondary-only launch guard: Test Current Map and Hot Reload fall back to the
selected normal entrance when a stale Play from Here marker cannot be applied,
while Play from Here, Godmode, and time override alone remain unavailable. It
also adds `enabled` and `disabled` as exact Parts checkbox-state filter aliases.
The remaining 0.3 release work is the
final runtime/corpus matrix, focused confirmation of the new live-clock and
startup-splash behavior, and version/tag/package housekeeping rather than
another editor architecture pass.

<details open>
<summary><strong>General wishlist — not assigned to a release</strong></summary>

## Compatibility and profile support

- [ ] Add an advanced, explicitly experimental runtime-variant management UI.
  It may let a user fingerprint a patched `game.exe`, compare its changed
  sections/ranges with a known layout, and locally approve the hash only after
  every required hook, caller, global, and PE-layout guard passes. A bare
  user-entered hash is never sufficient, locally working variants are not
  automatically promoted to supported, and this UI is not part of the current
  `0.3.0` release scope.
- [ ] **Post-1.0:** define an optional runtime-plugin contract for other mods
  only when their authors request direct-launch support.
  - Core editing, validation, collections, and saving never depend on a game
    executable or runtime plugin.
  - Test Current Map remains an optional bonus capability. JMV and the
    experimental Base provider do not imply support for other Starter builds.
  - A mod opts into only the runtime capabilities it implements; unsupported
    commands remain hidden or disabled with a reason.

## Optional editor integrations and accessibility

- [ ] Add an opt-in MCP server for EI Mapper automation and AI-assisted
  workflows. Start with read-only discovery, active-workspace inspection,
  validation, semantic diff, object queries, and diagnostics. Editing commands
  must use the same typed transactions, undo history, validation, dirty-state,
  backup, and save protections as interactive UI actions. Destructive edits,
  file writes, Test Current Map, hot reload, and process control require
  explicit capability grants and user confirmation; the server must never
  expose proprietary resources, scripts, local paths, or unsaved document data
  without an intentional request. Define a versioned tool/resource schema so
  external clients do not depend on internal C++ or widget structure.
- [ ] Add optional external MOB diff-tool integration. Pass two explicit files
  or exported snapshots to a configured executable without replacing the
  built-in deterministic diff or exposing private/unsaved data silently.
- [ ] Add configurable UI font-size scaling for 4K displays and accessibility.
- [ ] Add a Russian translation and an in-app language switch.
- [x] Render disabled menu items as a single ordinary grey label without
  Fusion's bright etched underlay or an alignment offset.
  ([`c38aaa7`](https://github.com/Ilufus/ei-mapper-endurance/commit/c38aaa7))

## MOB diff enhancements

- [ ] Add baseline/candidate swapping, original-versus-current entry points,
  persisted column widths, a read-only 3D perspective overlay, deterministic
  JSON export, and explicit unresolved handling for ambiguous duplicate-NID
  matches. These are useful extensions to the working file-to-file semantic
  diff, but are postponed beyond the 0.2.x stabilization line.
- [x] Make the major workspace panels dockable, detachable, recoverable from
  **View**, and persistently rearrangeable within Qt5's supported docking
  model. Object Outliner and Object Properties start as a right-edge vertical
  dock column; History and MOB Family Validation retain floating-window use
  with native minimize/maximize controls while also supporting docking.
  ([`a06e5fd`](https://github.com/Ilufus/ei-mapper-endurance/commit/a06e5fd))

## General validation backlog

- [ ] Decide and validate the recovery-versus-output policy for files containing
  known game-tolerated defects.
  - Opening must remain permissive enough to inspect and rescue recognized
    broken data, with every exception identified in validation and logs.
  - Repair is opt-in: opening a file must never silently normalize it or dirty
    the document.
  - Discuss whether every save of a document that still contains a recognized
    defect, including byte-identical Save As, must present a consolidated
    choice such as **Repair and save** (recommended), **Preserve known defects**
    (explicit consent), or **Cancel**.
  - Endurance must not silently create or propagate a known-invalid structure.
    Preserving one should require an explicit user decision recorded in the
    workflow log and validation report.
  - An explicit rescue/repair operation must preview its scope, be undoable,
    preserve a backup, reparse the result, and prove that unrelated bytes and
    semantics remain unchanged.
  - Apply this discussion first to legacy MapED `MT_AREAS`/`MT_TARGETS` lengths
    and then to any other narrowly recognized malformed structures. This
    exception exists because MapEd was a popular legacy map editor and its
    four-byte-short trap arrays are consequently present in maps shipped by
    multiple existing mods; it is compatibility with a known historical tool,
    not general acceptance of malformed MOB data.
- [ ] Stress repeated open/close/save/cancel and abnormal-exit behavior on large
  Base/JMV maps.
- [ ] Complete the broad game matrix: `drawall`, save/load, unload/reload,
  scripts, traversal, combat-relevant objects, and graceful exit.
- [ ] Convert concrete regressions into minimized synthetic fixtures whenever
  the format permits this without distributing game or JMV assets.

## Object authoring conveniences

- [x] Add an explicit **Convert World Object to Lever...** transaction.
  - Preserve the original NID, transform, model, textures, body parts, common
    object fields, directory membership, ordering position, and script-facing
    identity.
  - Use the corpus-proven Base/JMV lever schema directly: replace the raw
    `OBJECT` envelope with `LEVER`, insert the six standard lever records, patch
    `OBJTYPE` 54 to 60, and preserve every common/opaque target record. Never
    send the converted object through the legacy rebuilding serializer. See
    `docs/research/world-object-to-lever-audit.md`.
  - Show editable lever defaults before conversion (open mode/key/skill, current
    and total states, cycling, door, and graph-recalculation flags), scan the NID
    for script references, and apply the subtree replacement as one undoable
    transaction.
  - Refuse conversion only when the source is not a structurally proven world
    object, its raw provenance is unavailable/stale, or the active profile
    declares a different lever schema. A matching lever model/template is not
    required: shipped Base and JMV maps use multiple identical models as both
    ordinary objects and levers, including within the same MOB.
  - Implementation: [`a8f9cc1`](https://github.com/Ilufus/ei-mapper-endurance/commit/a8f9cc1).
  - [ ] Runtime-validate a converted already-shared model and a model never before
    used as a lever in disposable Base and JMV test maps before enabling the
    command by default for stable profile releases. The alpha command remains
    available for these disposable validation tests.

</details>

<details>
<summary><strong>1.0.x or later — distribution and legacy-platform compatibility</strong></summary>

## Distribution security and reputation

- [ ] Separate the experimental Base runtime injector from ordinary JMV tester
  packages, add stable Windows version metadata, evaluate code signing, and
  establish a vendor false-positive submission workflow for immutable release
  hashes. This is 1.0-era distribution hardening, not a 0.2.x feature gate.

## Windows 7 renderer compatibility

- [ ] Investigate a supported Windows 7 rendering fallback after the core
  editor reaches production maturity. This is explicitly outside `0.2.x` and is
  not currently a `1.0.0` release blocker.
  - The confirmed failure is an OpenGL context/renderer initialization problem,
    not an MPR/MOB parser failure.
  - Evaluate Qt ANGLE/D3D and other non-destructive renderer choices without
    weakening the default path on supported systems.
  - Keep renderer selection explicit and diagnostic; do not silently switch a
    user's backend after a failed launch.
  - Support only a path that can be built, packaged, and tested reliably on the
    legacy system; otherwise retain the documented unsupported status.

</details>

<details open>
<summary><strong>0.5.x — AI navigation graph visualization and regeneration</strong></summary>

## 0.5.0 release goal

Deliver profile-aware visualization, validation, and safe regeneration of the
AI navigation graph stored in a primary MOB or an external `.grp`. This work is
deliberately separated from the `0.4.x` landscape serializer: terrain editing
may warn that navigation is stale, but production graph replacement remains a
`0.5.0` capability.

Controlled Test Current Map validation confirmed that changed height and
texture data propagate at runtime and that the player can navigate correctly
around newly impassable terrain without an offline graph rebuild. Geometry,
liquid, and collision changes therefore mark navigation as potentially stale
for diagnostics and review, but graph regeneration is not a `0.4.0`
landscape-save gate unless later controlled evidence demonstrates a
graph-dependent case. MPR/SEC and AI-graph persistence remain separate
lossless transactions because the graph is not owned by the MPR.

## Confirmed format foundation

- [ ] Add `AiGraphDocument` with exact raw-byte retention for embedded
  `AI_GRAPH` records and external `.grp` files.
- [ ] Parse and validate the confirmed envelope: width, height, and
  `width * height * 152` bytes of graph data.
- [ ] Model the article-derived `8 * 19` subdivision conservatively:
  - eight neutral slots/layers per graph coordinate;
  - eight little-endian 16-bit directional values in each slot;
  - `0xFFFF` as an unavailable connection;
  - the remaining one-byte and two-byte planes retained as opaque until their
    engine or builder meaning is proven.
- [ ] Confirm graph-to-world orientation and the observed eight graph
  coordinates per terrain SEC across Base and JMV maps.
- [ ] Treat the published BMP converters as research evidence only. Their
  reverse conversion drops high bytes from finite 16-bit values, ignores the
  auxiliary planes, and is not a safe serializer or graph builder.
  ([Habr research article](https://habr.com/ru/articles/769050/))

## Visualization and validation

- [ ] Add a Navigation viewport mode with per-slot and combined overlays,
  directional edges, full 16-bit cost colouring, blocked connections, and a
  hover inspector exposing coordinates, source offsets, raw values, and hashes.
- [ ] Compare embedded MOB and external GRP sources without silently choosing
  precedence when both exist and differ.
- [ ] Add deterministic graph-to-graph diff, initially using Base Zone 6's
  unfrozen/frozen landscape pair as the controlled fixture.
- [x] Stage external MPR-basename `.grp` files losslessly and provide an
  explicit byte-guarded graphless test exception. Graphless testing loads an
  empty graph and is not a substitute for regeneration.
- [ ] Validate dimensions, complete byte coverage, boundary links, unexpected
  asymmetry, isolated regions/entrances, and opaque-plane changes. Existing
  graph defects and navigation-dirty edits remain diagnostic findings; Test
  Current Map may proceed with an explicit warning while runtime validation
  records whether the edited terrain actually causes a navigation defect.
- [ ] Track terrain/object edits as navigation-dirty regions and retain the
  oversized-door runtime-regeneration convention as an explicit diagnostic.
- [ ] Replace Play from Here's deliberately conservative terrain clearance
  probe with the active regenerated AI graph as the authoritative walkability
  and nearest-valid-position source. Automatic graph regeneration should make
  that graph available for every loaded MPR; terrain projection remains useful
  only for marker rendering and final Z display, not for deciding whether the
  player can stand there. The current probe may reject legitimate standable mesh
  objects and platforms; do not loosen it with unverified heuristics before the
  graph-backed route is available.

## Zone 6 source and state research

- [x] Follow the verified runtime read precedence for staging: embedded
  primary-MOB `AI_GRAPH` first, external MPR-basename `.grp` fallback. This
  does not enable graph writes or settle how a future editor should present
  conflicting sources for intentional editing.
- [ ] Base: correlate `zone6.mpr` + `zone6.grp` (unfrozen lake) with
  `zone6_2.mpr` + `zone6_2.grp` (frozen lake). Both graphs are `56x96`; they
  differ in 55,208 bytes across 2,642 graph coordinates.
- [ ] Base: explain the relationship between the graphless `zone6.mob`, the
  embedded graph in `zone6-lmp.mob`, and both external graph states.
- [ ] JMV: explain why `zone6.grp` and `zone6_2.grp` are byte-identical
  `64x96` graphs while `zone6.mob` also contains a differing embedded graph.
- [ ] Keep static MOB/GRP graphs distinct from the runtime navigation-grid
  representation serialized into saves until a controlled comparison proves
  their relationship.

## Regeneration

- [ ] Characterize the separate map editor's `AiGraphBaker` on disposable
  copies as a behavioral oracle, recording exact executable and output hashes.
- [ ] Derive terrain slope, height, liquid/ice, collision object, bridge,
  multi-level, door, and profile-specific graph-building rules through
  one-variable controlled fixtures.
- [ ] Implement full-map regeneration first. Partial dirty-region rebuilding is
  deferred until it is proven byte/semantic-equivalent to a full rebuild.
- [ ] Make regeneration explicit, previewed, staged, freshly reparsed,
  undoable, backed up, and atomic. Never invoke it as an ordinary Save side
  effect and never fall back to the legacy MOB rebuilding serializer.
- [ ] Validate generated graphs through deterministic rebuilds, graph/path
  probes, entrances, NPC traversal, water/ice state changes, dynamic doors,
  unload/reload, save/load, and separate fingerprinted Base/JMV runtime tests.

</details>

<details open>
<summary><strong>0.4.x — lossless MPR/SEC and landscape editing</strong></summary>

## 0.4.0 release goal

Deliver the first supported landscape-editing release. Merely displaying an MPR
does not qualify: changed terrain must use a lossless archive/sector document
path and must never fall back to the legacy rebuilding serializer.

## Lossless MPR/SEC foundation

The closed `feature/landscape-editing` baseline advances the original lossless
archive and canonical-lattice foundation into height sculpting/import,
semantic ground and overlay painting, undo batching, terrain diagnostics,
staged changed-MPR Save As, lossless RES member replacement, and a structured
`map.txt` catalog editor. Its focused automated suites and disposable manual
smoke matrix passed before this integration. The follow-up direct-save
increment enables replacement of the open dirty MPR only through the same
provenance-checked transaction: staged candidate, fresh reopen, semantic diff,
source-race refusal, pre-save backup, atomic replacement, post-commit byte and
reopen verification, and canonical document rebinding. The legacy rebuilding
serializer remains forbidden for every changed MPR.

- [ ] Preserve untouched archive members, internal names, member order,
  timestamps/metadata, alignment, unknown entries, and original MP/SEC
  namespaces.
- [ ] Make unchanged Save As byte-identical even when the outer filename differs
  from the embedded map name.
- [ ] Patch only edited sector/overlay payloads and derive affected lengths and
  metadata from emitted content.
- [x] Stage, freshly reopen, validate, back up, and atomically replace every
  changed MPR.
- [ ] Add deterministic MPR/SEC semantic diff and untouched-member equality
  checks.
- [ ] Validate changed MPRs on copied Base/JMV maps before enabling live-map
  replacement.

## New MPR creation

- [ ] Add a user-facing **Create New MPR...** workflow that is independent from
  `map.txt` registration. Creating terrain must not silently invent zone IDs,
  entrances, minimap, music, quest, objectives, or other catalog metadata.
- [ ] Let the author choose the map dimensions/sector layout, initial flat
  height, initial land material, active Base/JMV profile, and a supplied terrain
  atlas namespace. Endurance reads compatible atlases but does not author or
  bundle atlas imagery.
- [ ] Generate deterministic MP/SEC members with canonical 33x33 sector vertex
  ownership, synchronized shared seams, explicit land/overlay initialization,
  and names derived from the chosen internal MPR namespace.
- [ ] Construct a game-compatible MPR/RES member table. Lowercase each member
  name, encode it as CP1251, place it in its native hash slot, and preserve
  deterministic `NextIndex` collision chains; a linear member inventory alone
  is not runtime validation.
- [ ] Before packaging or Test Current Map, resolve every generated MP/SEC member
  through the runtime-style hash slot and chain. Reject case-insensitive
  duplicate names, cycles, out-of-range links, unreachable members, invalid
  dimensions/namespaces, and missing required sectors.
- [ ] Apply the changed-MPR save contract to the initial archive: stage outside
  the destination, freshly reopen, validate archive reachability and terrain
  semantics, create a backup when replacing an existing path, and publish with
  atomic replacement only after every check passes.
- [ ] Offer explicit follow-up registration through the `map.txt` editor after
  the MPR validates. Cancelling registration must leave the standalone MPR
  usable and must not create partial catalog or texture-resource changes.
- [ ] Add minimized synthetic tests for 1x1 and multi-sector maps, deterministic
  repeat generation, seam equality, initial height/material selection, supplied
  atlas namespaces, deliberate hash collisions, uppercase runtime lookup,
  unreachable/cyclic chain rejection, failed destination replacement, and
  byte-identical reopen. Keep proprietary Base/JMV runtime checks private and
  operate only on copied fixtures with recorded hashes.

## Heightmap editing

- [ ] Refactor terrain ownership around each sector's canonical 33x33 vertex
  lattice rather than independently duplicated tile vertices.
- [ ] Synchronize shared seams between adjacent sectors.
- [ ] Add undoable raise/lower, flatten, smooth, and ramp tools.
- [ ] Provide radius, strength, falloff, and numeric-height controls.
- [ ] Recalculate only affected normals and preserve untouched packed vertex
  bits.
- [ ] Warn before a height change requires lossy `maxZ` rescaling.

## Terrain texture painting

- [ ] Productionize the existing hidden tile brush with explicit atlas, layer,
  category, rotation, brush shape, preview, and validation controls.
- [ ] Implement visual blending through transition-tile painting and optional
  autotiling. SEC stores packed tile selection/rotation rather than arbitrary
  per-vertex blend weights.
- [ ] Keep the terrain/tile atlas bound to the MPR's actual embedded namespace
  and resource provenance.

## Overlay surfaces and materials

- [ ] Add creation, removal, sculpting, visibility, texture, and material tools
  for water, swamp, ice, lava, and other separate overlay surfaces.
- [ ] Provide validated Base and JMV material presets while retaining manual
  controls for mod-specific materials.
- [ ] Preserve unknown overlay records and extra archive members.

## Landscape safety and game validation

- [ ] Add a complete `map.txt` metadata editor for registered and new maps,
  including zone identity/type, entrances, destinations, minimap, music, and
  profile-specific metadata validation.

## Minimap generation and texture-resource export

- [ ] Extend the lossless `map.txt` model to preserve and edit the complete
  `#maps` block: sector width/height, minimap texture identity, and the separate
  objectives-screen texture identity. Never invent or overwrite objectives
  metadata as a minimap side effect.
- [ ] Add an existing-minimap viewer that resolves the referenced `.mmp`
  through the active Base/JMV resource profile and reports its archive
  provenance, format, dimensions, mip layout, and hash.
- [ ] Implement a deterministic orthographic minimap bake following the
  observed ZoneView contract: source BMP dimensions are sector width/height
  multiplied by 128 pixels per sector.
- [ ] Make an active collection the production bake input: its MPR, primary
  MOB, and ordered supplemental MOBs provide the complete authored static
  scene. Include terrain, overlays/liquids, lighting, and static geometry while
  excluding dynamic units. Permit an MPR-only terrain bake, but label it as
  incomplete when collection static geometry is absent.
- [ ] Retain the full-resolution bake for review and regeneration, then convert
  it to the game-facing 256x256 minimap MMP only after orientation, framing,
  filtering, and resampling are proven against matching stock and JMV
  ZoneView/MMP pairs.
- [ ] Provide one **Export Texture to RES...** workflow. The selected
  destination determines the operation:
  - a missing path creates a new RES archive;
  - an existing archive receives a new member after backup and staged rebuild;
  - an existing member requires explicit Replace, Rename, or Cancel handling.
  Allow any compatible user-selected archive, including the mod's
  `textures.res`, while warning about time and temporary-disk cost for large
  archives.
- [ ] Treat archive "append" as a semantic member addition, never an in-place
  byte append. Stage and freshly reopen the candidate, prove untouched member
  names/order/metadata/raw bytes are unchanged, verify the new/replaced MMP,
  then back up and atomically replace the destination.
- [ ] Coordinate the `map.txt` reference update only after the texture archive
  validates. A failed archive or metadata step must leave both destinations
  recoverable and must never publish a dangling minimap name.
- [ ] Prove profile-specific discovery and duplicate-name precedence for a
  newly created RES before recommending a dedicated archive. Prefer unique
  texture names so users do not accidentally replace another zone's minimap.
- [ ] Add synthetic tests for pixels-per-sector dimensions, orientation,
  deterministic repeat generation, static-geometry inclusion, dynamic-unit
  exclusion, new-archive creation, existing-archive addition/replacement,
  collision cancellation, untouched-member equality, failed validation, and
  rollback. Keep proprietary ZoneView/stock correlation as separate private
  runtime validation.

- [ ] Classify edits as visual-only or navigation-affecting. Height, liquid,
  collision, bridge, door, and multi-level changes mark explicit navigation-
  dirty regions; uncertain material/type changes fail conservatively to dirty.
- [ ] Keep MPR saving independent from graph regeneration while presenting the
  complete MPR + primary MOB/external GRP state as one map-validation result.
- [ ] Report navigation-dirty state and graph discrepancies without blocking
  landscape save or release readiness solely because an offline graph was not
  rebuilt; escalate only when diagnostics or runtime validation demonstrate an
  actual navigation defect.
- [ ] Detect and preserve the known oversized-door runtime graph-regeneration
  convention.
- [ ] Provide placement/navigation findings before save and test launch.
- [ ] Require in-game traversal, water/overlay interaction, unload/reload, and
  graceful-exit tests before `0.4.0` release.

</details>

<details open>
<summary><strong>0.3.x — integrated Endurance editor and direct-gameplay release</strong></summary>

## 0.3.0 release goal

Publish the current hardened editor as one coherent tester-facing release.
`0.3.0-alpha.1` promotes the accumulated `0.2.1-alpha.44` feature set because
it now contains several minor-release-sized capabilities beyond the original
0.2 direct-launch milestone. Existing MOB safety and compatibility contracts
remain unchanged.

### Included feature baseline

- [x] Lossless MOB editing for the supported property, transform, script,
  patrol/look-point, copy/paste, create/delete, and conversion workflows.
- [x] Runtime-family range/NID validation, copied-workspace source overlays,
  non-blocking findings, navigation to source objects, and conservative guided
  repair.
- [x] Independent MPR/MOB opening, collections, the dockable Loaded Map tree,
  one active editable MOB, locked-source inspection/copying, visibility, and
  safe targeted unload.
- [x] Preserve collection identity across sessions, offer an immediate safe
  reload after editing the active collection, and keep explicitly unmapped
  collections free of stale/inferred map.txt entrances.
- [x] Treat unmapped single-MPR/single-MOB collections as run-local synthetic
  maps for JMV testing: append one random map record to a losslessly staged
  private `texts.res`, redirect only that process's archive reads, and use the
  loaded MPR dimensions and Play from Here entrance without inheriting a
  borrowed zone's minimap or travel zones. The installed map table remains
  byte-identical.
- [x] Remove unsafe renaming/rebuilding of an already constructed runtime map
  record. The bridge now looks up the preloaded private record through the
  fingerprinted byte-guarded API and updates only its numeric dimensions and
  entrance rectangle. This supersedes both the rejected heap scan and the
  delayed allocator crash reproduced by `0.3.0-alpha.5`.
- `0.3.0-alpha.9` keeps a synthetic map's logical MPR/MOB identity stable when
  hot-reload generation files are named `map.mpr`/`map.mob`, and replaces the
  borrowed player-interface minimap with a run-local neutral checker resource.
- [x] Show collection display names from their editable `name` field, preserve
  new-collection drafts when Save As is cancelled, and offer to open both new
  and duplicated collections after saving.
- [x] Versioned recovery autosaves, atomic saves/backups, rotated workflow
  logs, crash dumps, interrupted-test recovery, and clean package validation.
- [x] Parameter-aware outliner filtering, dockable workspace panels, compound
  property/component copying, configurable shortcuts, camera speed, and
  external/internal script workflows.
- [x] Make `OBJBODYPARTS` editing lossless at the property/UI boundary: expose
  every serialized token (including unknown/profile-future values), retain
  exact order and duplicates, copy/paste the complete underlying array, and
  provide a display-only Parts filter. Evidence:
  [body-part array preservation](docs/validation/BODYPART_ARRAY_PRESERVATION.md)
  and [`53da556`](https://github.com/Ilufus/ei-mapper-endurance/commit/53da556).
- [x] Make Parts property operations transactional: exact-array cross-MOB
  paste and exact before/after undo/redo snapshots, with checkbox-only state
  display and resizable/persistent columns.
- [x] Use `SC_OBJECT_DB_FILE` to decide whether SpellAddon autorun-dependent
  Test controls are available. Renamed copies remain supported; secondary-only
  `PR_OBJECT_DB_FILE` sets can still run Test Current Map with mapped identity,
  while Play from Here, Godmode, and time override stay disabled. Multiple
  loaded primaries produce one non-blocking workspace validation warning.
- [x] Complete Loaded Map MOB context actions for parameters, script, targeted
  Save/Save As, and dirty-aware Save enablement; duplicated catalog maps now
  offer to open the collection they create.
- [x] Restore renamed-primary workspace overlay behavior so a loaded test copy
  such as `zone3obrtest.mob` does not also import the installed catalog primary
  into validation and generate an unbounded false-collision workload.
- [x] JMV Test Current Map, selected entrances, Play from Here, Godmode,
  supplemental MOB staging, generation-isolated saves, state-aware hot reload,
  orphan recovery, and same-map position/facing restoration.
- [x] Preserve the runtime graph source during Test Current Map and hot reload:
  retain embedded primary-MOB `AI_GRAPH` data, or validate and stage an
  unchanged sibling `.grp` in every immutable generation. Graph generation and
  editing remain in the 0.5.x workstream.
- [x] Rebuild the x86 runtime bridge and host as part of every release build,
  and reject stale bridges before a confirmed graphless launch can reach
  game.exe.
- [x] Profile-correct humanoid appearance/equipment/idle animation, the final
  texture/UV inspector, optimized render queues and culling, resource-format
  fixes, functional particle preview, ANGLE compatibility, and the integrated
  environment renderer.
- [x] Replace the inferred environment/local-light approximation with the
  fingerprinted native composition model: resource-driven weather curves,
  channel-wise maximum environment and point-light accumulation, native
  distance falloff, bounded projected-shadow opacity, authored torch lights,
  and a 64-light capacity covering the audited JMV corpus. The implementation
  and remaining evidence boundaries are recorded in
  [environment resource evidence](docs/ENVIRONMENT_RESOURCE_EVIDENCE.md),
  [environment rendering validation](docs/ENVIRONMENT_RENDERING_VALIDATION.md),
  and the [particle/runtime evidence ledger](docs/investigations/2026-07-23-particle-runtime.md).
  ([`37e0231`](https://github.com/Ilufus/ei-mapper-endurance/commit/37e0231),
  [`aa465c3`](https://github.com/Ilufus/ei-mapper-endurance/commit/aa465c3))
- [x] Separate always-available **Time of day** and **Lighting mode** viewport
  controls. JMV launch and hot reload now use `RunWorldTime` so the selected
  starting hour is applied without freezing normal clock progression.
  ([`7401874`](https://github.com/Ilufus/ei-mapper-endurance/commit/7401874),
  [`3af2099`](https://github.com/Ilufus/ei-mapper-endurance/commit/3af2099))
- [x] Add a taskbar-visible, focusable startup splash using the Endurance art,
  product name, edition, current version, and initialization status until the
  main window is ready. ([`3af2099`](https://github.com/Ilufus/ei-mapper-endurance/commit/3af2099))

### 0.3.0 release gate

- [ ] Runtime-confirm that Test Current Map and hot reload start at the editor
  time while the game clock continues normally, including a cave/briefing map.
- [ ] Confirm the startup splash/taskbar handoff on an existing profile and a
  clean first run whose setup dialog appears before the main window.
- [ ] Run the final representative Base/JMV editor corpus matrix: open,
  no-op round trip, intended changed save, undo/redo, autosave/recovery,
  validation, and graceful shutdown.
- [ ] Run the final JMV runtime matrix across normal gameplay, dungeon, and
  briefing maps: entrance/custom spawn, supplemental MOBs, Save/Load within one
  generation, same-map and different-map hot reload, scripts, `drawall`, map
  transition/return, and graceful exit. Retain the authoritative `result.json`
  and hashes.
- [ ] Reconfirm the clean distribution contract: no settings, logs, saves,
  dumps, private paths, or game assets; exactly one current changelist; matching
  executable/folder/source version; source/GPL notices and checksums included.
- [ ] Resolve any concrete release-blocking regression found by the closed
  group, then create the immutable `v0.3.0` tag and tester package.

### Explicitly non-blocking follow-ups

- [ ] Confirm immediate, conditional, and nested supplemental `AddMob` chains
  load exactly once when a deterministic fixture becomes available.
- [ ] Runtime-confirm World Object to Lever conversion for both an already
  shared model and a model never previously used as a lever.
- [ ] Extend guided NID repair to explicit per-script-reference assignment and
  coordinated multi-MOB save cohorts.
- [ ] File associations, a dockable workflow-log panel, deeper persistent RES
  indexing, and broader diff presentation remain useful follow-ups but do not
  hold the 0.3.0 safety/runtime release gate.

</details>

<details>
<summary><strong>0.2.x — hardened MOB editor and JMV direct-gameplay development history</strong></summary>

## 0.2.x outcome

The 0.2.x prerelease line established the hardened MOB editor, profile-aware
resources, direct JMV gameplay, and hot reload. It closed without pretending
that the much larger accumulated `0.2.1` feature set was merely a patch; that
working baseline is promoted intact to `0.3.0-alpha.1` above.

## Historical 0.2.0 release gate

`0.1.0-alpha.58` is the immutable selected-zone/save-isolation test build.
`0.2.0-alpha.1` moves the selected zone and entrance into the engine's New
Game player record before transition, permits authored entrances when the
editor's conservative clearance check cannot prove them clear, and introduced
profile-isolated test saves. Test Save/Load/Main Menu remain available through
run-local generation namespaces: every initial launch and hot reload starts a
new empty save generation, so saves remain useful within one exact staged-map
state and can never reach live campaign saves or a later map generation.
This historical gate targeted the first immutable `0.2.0` package after the
following runtime contract was demonstrated on normal gameplay, dungeon, and
briefing maps. The larger resulting baseline is now tracked under 0.3 above.

### Final 0.2.x status before promotion

- **Final packaged baseline:** `0.2.1-alpha.44`. Alpha.44 integrates the
  committed profile-correct render pipeline, environment renderer, particles,
  ANGLE support, and current runtime/editor safeguards. Alpha.42 completed native
  clipboard behavior for individual Vector3 components without selecting or
  flashing the parent compound property. Alpha.41/42 make component context
  selection exclusive and route child-label context menus directly. Alpha.38
  also fixes the shutdown crash seen in alpha.34 by retaining the diagnostic
  OpenGL timer query until process/context destruction instead of destroying it
  through a dead Qt surface.
  ([`0fa4708`](https://github.com/Ilufus/ei-mapper-endurance/commit/0fa4708),
  [`3d9c4e8`](https://github.com/Ilufus/ei-mapper-endurance/commit/3d9c4e8))
- **Recent confirmed baseline:** Alpha.32 adds a persistent
  0.1x-10x viewport camera-speed control, a configurable Ctrl+mouse-wheel
  gesture, and a cancellable 15-second notice before each periodic recovery
  autosave. Selection-only undo steps no longer rebuild a full lossless MOB
  candidate or schedule family validation, removing the observed selection
  hitches. ([`c5feca5`](https://github.com/Ilufus/ei-mapper-endurance/commit/c5feca5))
  Alpha.31 corrects the lever
  eligibility rule using the complete Base/JMV corpus: basic `OBJECT` records
  with proven subtypes 53 (flora), 54 (ordinary scenery), and 55
  (stone/scenery) can be converted; malformed, duplicate, and unproven subtype
  records still fail closed. Alpha.30 fixes the remaining
  runtime-process ownership boundary: an unrelated `game.exe` can no longer
  make an empty/legacy recovery journal claim an active Endurance test, and the
  editor accepts alpha.29's accidental UTF-16LE `game.pid` while the rebuilt
  host writes the protocol as ASCII. This restores Hot Reload readiness against
  the hosted child `game.exe`. ([`e18f868`](https://github.com/Ilufus/ei-mapper-endurance/commit/e18f868))
  Alpha.30 also adds the lossless, one-step World Object to Lever transaction
  with editable defaults and script-reference warning.
  ([`a8f9cc1`](https://github.com/Ilufus/ei-mapper-endurance/commit/a8f9cc1))
  Alpha.29 fixes hosted JMV hot
  reload dispatch: readiness already resolved the bridge thread against the
  owned `game.exe`, but dispatch incorrectly compared that thread with the
  separate injector-host PID. Runtime process identity now has one shared
  resolver and a regression test. Alpha.28 retained the approved
  alpha.24 character/render pipeline while reverting the unfinished environment,
  weather, sky, and shadow prototype. Game & Mod Setup treats any effective
  resource-root change like a profile switch: dirty documents are offered for
  saving, the workspace is unloaded, resources are reinitialized, and Maps &
  Collections is refreshed. Owned JMV tests virtualize both Nival and Starter
  registry roots and guard stale same-mod absolute paths, so two installations
  cannot silently mix the selected `game.exe` with another installation's JMV
  maps or database. ([`b76fc3e`](https://github.com/Ilufus/ei-mapper-endurance/commit/b76fc3e))
  Alpha.27 evidence also proved that the owned injector activated Endurance's
  engine detours before JMV `AddonMain` began its own patch phase. Alpha.28
  prepares path isolation before resume but defers engine detours until observed
  executable patch activity becomes quiescent; no addon/ASI hash becomes a
  manifest dependency.
  Alpha.28 dual-install evidence shows the selected executable and virtualized
  registry roots are correct, but cross-install stale-path redirection does not
  reliably replace all Starter/JMV state. Treat multiple independent Starter
  roots as unsupported pending a product decision; the proposed 0.2.x behavior
  is a non-destructive preflight warning instead of broader runtime hooks.
  Alpha.26 retained the alpha.24 render and
  autosave baseline while hardening validation navigation and the NVIDIA test
  overlay. JMV direct-launch compatibility now depends only on the exact
  fingerprinted Starter `game.exe`; Starter addon.dll, SpellAddonX.asi, JMV
  addon.dll, mutable Lua, and SpellAddon configuration no longer gate editor
  discovery or Test Current Map. Optional autorun-MOB conveniences report their
  own runtime evidence instead of defining global compatibility.
  ([`eff6f99`](https://github.com/Ilufus/ei-mapper-endurance/commit/eff6f99))
  Alpha.24 integrated the render pipeline
  through [`191ac85`](https://github.com/Ilufus/ei-mapper-endurance/commit/191ac85e1990d9f7eddfef7d7cdef1a29aa67930)
  and delivering the verified live autosave-cadence signal
  ([`269c807`](https://github.com/Ilufus/ei-mapper-endurance/commit/269c80759a334174eca26eec7022524ffff4ea64)).
  Post-alpha.24 stabilization distinguishes exact-hash code components from
  fingerprinted mutable JMV scripts/configuration, accepts the known patched
  JMV addon variant, and identifies a mismatched component/path/hash instead of
  issuing only a generic manifest rejection. Validation object actions now load
  an unloaded source MOB before selecting its proven object. Two supplied
  NVIDIA crashes were traced to entrance-overlay draws inheriting stale mesh
  vertex attributes; the overlay now resets VAO/VBO attribute state first.
  ([`0b206e5`](https://github.com/Ilufus/ei-mapper-endurance/commit/0b206e5))
  Alpha.23 added explicit autosave scheduling telemetry, Autosave Now, and Open
  Backup Folder, but tester logs proved that changing the cadence still required
  a restart. Alpha.22 fixed the General-settings
  registration that left alpha.21's visible autosave controls disconnected
  from persistence and prevented its timer from starting. Alpha.21 completed the remaining
  release-facing editor priorities: exact duplicate-MOB detection, guarded
  workspace replacement for manual MPR opens, named save/load progress,
  versioned recoverable autosaves, and the optimized profile-correct humanoid
  rendering merge. Alpha.20 hardened the dockable Loaded Map
  tree, explicit active/locked MOB ownership, independent MPR/MOB visibility,
  safe targeted MOB unload, all-loaded-MOB outliner grouping, locked-source
  inspection/copying, Ctrl+T owner activation, and validated numeric component
  copying. Undo/redo now reveals the MOB owning the applied document change.
  Implementations: [`517cc1f`](https://github.com/Ilufus/ei-mapper-endurance/commit/517cc1f),
  [`90eb899`](https://github.com/Ilufus/ei-mapper-endurance/commit/90eb899),
  [`8a0badd`](https://github.com/Ilufus/ei-mapper-endurance/commit/8a0badd).
  Alpha.19 fixes the unload-time stale-MOB lookup found in alpha.18 and adds
  explicit selection ownership, persistent cross-MOB selection, locked-MOB
  viewport colouring, outliner ancestry, and multi-row unload. Alpha.20 fixes
  Ctrl+A ownership counters, aligns and drag-batches MPR/MOB visibility,
  colours locked ancestry amber, and coalesces expensive family validation
  outside active edit gestures. The focused Loaded Map interaction pass is
  confirmed. Current source also mutes the complete outliner hierarchy of a
  hidden MOB while retaining expansion and visibility controls
  ([`807518c`](https://github.com/Ilufus/ei-mapper-endurance/commit/807518c)).
  Alpha.21 activates an already loaded MOB instead of loading the same canonical
  file twice; paths are canonicalized and compared case-insensitively.
  Implementations: [`87c36b1`](https://github.com/Ilufus/ei-mapper-endurance/commit/87c36b1),
  [`23cd7c4`](https://github.com/Ilufus/ei-mapper-endurance/commit/23cd7c4),
  [`8200aaa`](https://github.com/Ilufus/ei-mapper-endurance/commit/8200aaa).
- **Alpha.13 validation/UI changes:** validation no longer performs expensive automatic
  replacement-ID scans for fully shared ranges, map switches refresh their
  runtime-family identity, missing `AddMob(...)` dependencies identify the
  exact source MOB/script line and explicitly state that the file was not
  found, and the main Outliner/Properties/History/Validation panels share a
  dockable View-menu lifecycle.
- **MOB hardening:** complete for the supported editing surface. Remaining work
  is validation breadth and new editing features, not another serializer
  rewrite.
- **Direct JMV gameplay:** working on tested normal, dungeon, and briefing maps,
  including selected entrances, zone metadata, startup-movie suppression,
  Godmode, supplemental `AddMob` loading, and temporary-map staging.
- **0.2.x runtime release blockers:** generation-isolated Save/Load,
  authoritative gameplay/Main Menu/inventory/store/world-map/transition state
  handling, unsafe-transition deferral, same-map position/facing restoration,
  and cross-MOB hot reload are runtime-confirmed. Different-map anchor reset
  and authoritative result reporting are implemented; run the final focused
  gameplay matrix.
- **0.2.1 hot reload:** protocol v2, immutable map/save generations, normal
  Main Menu/New Game rebootstrap, responsive stale-transition world-map
  classification, orphan monitoring, and same-position restoration are
  implemented and runtime-confirmed. Nested/conditional supplemental loading
  remains an explicit confirmation item because no controlled fixture exists.
- **Stable 0.2.0 editor gate:** parameter-aware outliner filtering with strict
  contiguous matching by default and explicit `~` subsequence fuzziness.
  Unreal-style MOB ownership/locking remains the larger workspace follow-up.

### Critical: structural zone-family NID validation

- [x] **HIGH — alpha.25 load re-entrancy:** prevent a second map load from
  starting while the first MOB is still finalising.
  - The supplied log shows Zone 4 starting at 04:01:21 while Zone 17 visual
    preloading continued until 04:01:30. Only the Zone 17 landscape was
    unloaded because its MOB had not yet been registered, leaving both map
    sets loaded together.
  - Block map/collection activation throughout every load-finalisation phase;
    replace unrestricted event pumping with input-excluding progress updates.
  - Validation must still report real overlaps in this state, but label them as
    cross-family workspace findings rather than claiming Zone 4 and Zone 17
    are one runtime family.
  - Alpha.26 makes load progress input-blocking, excludes user input from all
    explicit progress event pumping, rejects nested map/collection/document
    activation, and disables open/save/test actions until finalisation ends.

- [x] **RELEASE BLOCKER — alpha.25 JMV launch transport regression:** diagnose
  runs where staging and `game.exe` launch succeed but the temporary bridge
  entry point is never invoked.
  - Run `20260721-190712-5a815226` reached the normal main menu and exited
    cleanly, but produced neither `runtime.log` nor `bridge-result.log`.
  - The staged files, JMV DEV `plugin.dll` slot, bridge exports, and exact
    supported `game.exe` hash were all present; this is not an entrance, map,
    or relaxed-manifest rejection.
  - Alpha.26 removes the DEV plugin slot from Endurance delivery. Both JMV and
    Base now use the editor-owned suspended-process host, run-local bridge DLL,
    and explicit `EnduranceInitialize` export. JMV addon/ASI/Lua remain ordinary
    mod gameplay components rather than bridge-delivery dependencies.
  - The selected game root is virtualized inside the owned process so Evil
    Islands' shared registry paths cannot redirect one Base installation into
    another. Two exact Base executable hashes are admitted; all detour bytes
    remain independently guarded.
- [x] Detect intra-file duplicates and cross-file NID collisions across the
  complete active zone family before more multi-MOB editing is released.
  This is critical because the confirmed `zone7.mob` / `Autozone7altar.mob`
  cross-type collisions break active mod work and correlate strongly with an
  unchecked game lookup crash and an invisible enemy.
  - Use `CMobDocument` object snapshots and section provenance, never raw
    `NewObject` marker boundaries.
  - Runtime families now start with the selected `map.txt` primary or explicit
    collection members and follow transitive, case-insensitive `AddMob(...)`
    script edges. Constant string concatenation is supported; unresolved
    dynamic calls are reported as incomplete instead of guessed.
  - Report NID, source MOB, object name, section/OBJTYPE, prototype, source
    path, and incompatible-type severity.
  - Debounced read-only validation runs after loaded-MOB edits/undo/redo and a
    fresh whole-family preflight runs before Save, Save As, Save All, Test
    Current Map, and hot reload. Findings are consolidated and never block
    editing, saving, or runtime testing.
  - Preserve established base-map IDs. Suggest deterministic unused IDs for
    newly added or currently edited objects; never silently renumber a base
    MOB.
  - [x] Add deterministic range-aware suggestions and a conservative guided
    repair dialog. Selected loaded objects are renumbered in one undoable
    action; the primary MOB is preserved by default. Script and `ParentID`
    evidence disables automatic repair rather than guessing.
  - [ ] Add explicit per-reference assignment and coordinated multi-MOB Save
    All for repairs that genuinely cross document boundaries.
  - [x] Regression-test the supplied Zone 7 fixture: six resolved family
    members, six legacy range overlaps, all eight incompatible NID collisions,
    and deterministic Autozone suggestions `1000321`-`1000328`.
  - [ ] Freeze the complete 162-MOB JMV family manifest and add synthetic
    intra-file, incomplete-family, undo/redo, and reference-refusal fixtures.
  - [x] Resolve copied workspaces as logical-filename overlays over installed
    Maps sources. One logical MOB contributes objects and ranges once; the
    active/in-memory, collection, workspace, installed-fallback, and
    profile-wide precedence is visible with full paths and shadowed sources.
  - [x] Run family parsing, script/reference indexing, and collision analysis
    asynchronously with debouncing, stale-generation rejection, and bounded
    validation-panel rendering. Save and Test Current Map remain available.
  - [x] Preserve structurally bounded unknown root records as opaque bytes and
    warn once instead of rejecting the editable known projection. Failed or
    unsafe MOB projections are discarded before they can become active or
    dirty. The JMV `Autozone7chest.mob` `0xACCEECCC` fixture is covered by a
    byte-preservation regression test.
  - Implementation and focused corpus harness: commit `9ddc802`;
    [alpha.5 validation](docs/releases/0.2.1-alpha.5-validation.md).
  - Source/projection hardening and focused tests:
    [alpha.6 validation](docs/releases/0.2.1-alpha.6-validation-hardening.md).
  - [x] Alpha.7 shows only three concise, truncated findings in warning
    dialogs; the complete report remains in the validation panel and log.
  - [x] Start MOB Family Validation as a detached, dockable native window,
    keep it reopenable after closing, and provide direct selection of loaded
    objects from NID-backed finding rows.
  - [x] Remove eager replacement-ID searches from automatic panel rendering.
    Fully shared million-ID ranges in the Zone 19 briefing/gameplay family
    previously made the GUI thread consume a full CPU core for minutes. The
    structural family pass itself measures about 0.20 seconds; replacement
    allocation now runs only from the explicit repair workflow. Identical
    queued validations are coalesced, map/collection loading has a blocking
    progress dialog, and worker/UI timings are logged. ([`6286325`](https://github.com/Ilufus/ei-mapper-endurance/commit/6286325))
  - [x] Refresh the map/test identity after a complete map pair has loaded.
    Alpha.12 could retain the previous map's primary MOB during a quick switch
    (observed `bz19g` metadata with active `gz19g.mob`), combining two mutually
    exclusive families and reporting false collisions. ([`4c833d9`](https://github.com/Ilufus/ei-mapper-endurance/commit/4c833d9))
  - [x] Make incomplete-family diagnostics name the missing MOB file and retain
    parser-derived dependency provenance: referring MOB path, script line, and
    exact `AddMob(...)` expression. The real missing Zone 14 dependency is
    covered by a focused synthetic test and corpus probe.
    ([`263dea8`](https://github.com/Ilufus/ei-mapper-endurance/commit/263dea8))
  - Detailed design: [structural MOB NID validation plan](docs/research/structural-mob-nid-validation-plan.md).

### P0: direct-launch stabilization

- [x] Add horizontal viewport operation controls and a top map-test toolbar.
  - Select/Move/Rotate/Scale/Tile Brush share configurable actions with
    `Esc`/`G`/`R`/`T`/`B`, and toolbar clicks cannot become world anchors.
  - Test/Stop, authored entrance selection, Play from Here, and Godmode are
    presented beside the viewport.
- [x] Add selected-entrance and borrowed-identity launch preparation.
  - Preserve `map.txt` source order and include inactive authored entrances.
  - Unregistered MPR identities are explicit, warned, and retained only for
    the currently loaded terrain.
- [x] Add a non-destructive collision-aware Play from Here picker and requested/resolved marker.
  - `0.2.0-alpha.4` replaces the framebuffer/depth redraw picker that could
    flicker and corrupt render state with a CPU ray/heightfield intersection.
  - Source after alpha.8 restores the established P behavior after corpus review
    (held for the next combined test build; no alpha.9 package was published):
    selecting a Radius unit and pressing P inserts a new first patrol point and
    changes the behavior to Path. Any dormant stored route is preserved and
    becomes visible; it is not silently reused or replaced.
  - `0.2.0-alpha.8` hardens logic-point editing: selected patrol/look markers
    render last, new look points inherit timing settings, and G/Move recognizes
    patrol, look, activation, and cast-point selections. Its experimental
    retained-point reuse policy is superseded by alpha.9.
  - `0.2.0-alpha.7` replaces the screen crosshair with a terrain-projected hero
    outline and uses the process-owned JMV autorun bootstrap to apply the chosen
    position with `SetCPFast` after normal player/session initialization.
- [x] Validate temporary JMV Godmode delivery through a run-local SpellAddon
  autorun MOB.
  - Runtime testing proved that the safely installed temporary `autoexec.cfg`
    was ignored. `0.2.0-alpha.4` instead losslessly patches a run-local copy of
    `AutoRunSpellAddon123.mob` with `GodMode(0,3)` and redirects only the owned
    test process. The installed autorun MOB remains untouched.
  - Alpha.6 proved the redirect happened but the appended command did not run.
    Alpha.7 injects both spawn placement and Godmode at the beginning of the
    existing `WorldScript` body, before early returns or `KillScript` paths.
  - The final path preserves the MOB's leading initialization delay, uses the
    initialized `GetLeader()` object, and removes invalid C-style semicolons
    from EI script statements. User runtime testing confirmed Godmode works.
    ([`c1ea993`](https://github.com/Ilufus/ei-mapper-endurance/commit/c1ea993))
- [x] Implement the exact-hash Base EI feasibility transport and four-level
  probe ladder. See [the static compatibility record](docs/base-runtime-probe.md).
  - Base is launched suspended by an editor-owned 32-bit helper; the run-local
    bridge initializes outside `DllMain` and resumes only after hash/byte guards.
  - Observation, pass-through, byte-identical redirect, and edited-state
    redirect results remain separate. General Base support is not claimed.
  - `0.2.0-alpha.3` never entered the ladder because the packaged x86 bridge
    exposed only the decorated initialization name. `alpha.4` exports the
    stable protocol name. Local alpha.6 testing confirmed probe levels 3 and 4
    both load the staged Base map pair; Base remains exact-hash Experimental
    pending a recorded full-ladder validation package.

- [x] Visualize authored entrance deploy rectangles in the viewport and add
  **Go to Entrance** camera navigation.
  - Use the overlay to distinguish a wrong entrance ID from normal engine
    placement within a deploy rectangle or an obstructed authored entrance.
  - `0.2.0-alpha.5` fixes the overlay's OpenGL/QPainter composition after
    `alpha.4` caused a continuously cleared/flickering viewport.
  - `0.2.0-alpha.6` removes same-framebuffer QPainter composition entirely
    after alpha.5 still produced alternating dark frames on NVIDIA. Entrance
    areas and the custom-spawn marker now stay in the native OpenGL pass.
  - Alpha.7 corrects label texture orientation, locks the authored entrance
    selector while a custom entrance is active, and makes Go to Entrance focus
    either the selected authored entrance or the custom anchor.
  - Current source isolates custom-entrance clicks from object selection, lets
    the transient hero marker be selected and moved with G, maps Delete to
    Clear entrance, and renders the selected authored entrance in green only
    while no custom marker is active. This remains unnumbered pending the next
    combined test build. ([`4e62424`](https://github.com/Ilufus/ei-mapper-endurance/commit/4e62424))
  - Runtime evidence from zone1 and zone2 proved the engine player record uses
    a zero-based entrance-array index, not the one-based `#exit` ID. Current
    source translates ID to index explicitly; this is the focused fix for
    Entrance 1 selecting Entrance 2 and the final entrance falling out of
    range. ([`4e62424`](https://github.com/Ilufus/ei-mapper-endurance/commit/4e62424))

- [x] Make profile context explicit and safe to change.
  - The active editing profile appears in the window title and is preselected
    in Game and Mod Setup.
  - Current source prevents the setup dialog's initial focus change from
    silently recommending and selecting the first valid profile over the
    configured profile. ([`4e62424`](https://github.com/Ilufus/ei-mapper-endurance/commit/4e62424))
  - Switching Base/JMV profiles closes loaded documents first so resource-backed
    scene state cannot survive into the new profile.

- [x] Keep **Main Menu**, **Save**, and **Load** available without exposing live
  campaign saves.
  - Redirect the exact owned game process to generation `000000` on initial
    launch and a new empty immutable namespace on every committed hot reload.
  - Retire older generations immediately from the game namespace, retain them
    until process exit, and remove them during normal or recovered cleanup.
- [x] Expose **Stop JMV Map Test** in Endurance while its owned game process is
  active, with customizable `Shift+Esc` as the default shortcut
  ([implementation](https://github.com/Ilufus/ei-mapper-endurance/commit/5f693b2)).
- [x] Runtime-validate the guarded runtime-readiness/state gate.
  - A completed main-menu map load does not prove UI/session initialization is
    finished on all hardware.
  - Observe a fingerprinted engine state; do not add another fixed delay.
- [x] Prove exactly one controlled player in the Alpha.58 runtime tests.
  - Alpha.58 removed temporary MOB script injection and now stages the exact
    validated in-memory MOB candidate.
  - Detect missing or duplicate controlled characters and fail clearly.
- [x] Apply the selected map-table entrance during runtime initialization.
  - Prefer the first hand-placed entrance center, including inactive entrances.
  - Ground- and collision-check it; use a nearby point inside the same entrance
    only when its center is blocked.
  - Alpha.58 selected and recorded the entrance but did not apply it.
  - `0.2.0-alpha.1` writes the selected zone and entrance to the fingerprinted
    New Game player record before transition; runtime testing confirmed correct
    entry behavior across the tested map set.
- [x] Validate or correct selected-zone session metadata.
  - Correct zone identity, minimap, music, and gameplay UI.
  - Prevent zone1 quest-log leakage. Use a blank test state when a meaningful
    quest state cannot be constructed without a selected save.
  - Do not invent minimap/music/quest metadata for a new map silently.
- [x] Initialize briefing maps through their briefing path.
  - Read zone type from `map.txt` rather than assuming every staged pair is an
    ordinary gameplay zone.
  - Add explicit metadata for new maps that do not yet have a map-table entry.
- [x] Runtime-prove Save/Load inside one generation and the empty Load list after
  hot reload. Saves from an older staged-map generation must remain invisible.
- [x] Verify startup-movie suppression for root and active-mod movie paths.
  - Alpha.58 hides `1c.bik` and `nival.bik` in-process without renaming them.
  - Preserve map-specific cutscenes; only startup movies are skipped by default.
- [x] Add authoritative bridge results.
  - Report actual loaded zone, MPR/MOB hashes, player count, selected entrance,
    save namespace, readiness, and cleanup outcome.
  - Distinguish a genuine crash from the game's known abnormal-looking but
    user-initiated exit path.
  - Present post-test warnings/errors in a non-modal PIE-style notification
    that auto-dismisses after a few seconds when untouched, with **Dismiss**
    and **Open Log** actions. Keep the complete text in the workflow log now
    and the future message-log panel later.
  - Alpha.15 writes a structured `result.json` after every owned test process,
    correlates observed loader paths with the hash-verified staged payload,
    distinguishes user Stop Test from a crash exit, and records source-file and
    hash-owned cleanup results.
  - The editor validates deterministic evidence automatically and logs the
    complete report. Warnings/errors use the non-modal notification above;
    successful runs remain log-only.
  - `EIMapperEndurance.exe --test-run-report <run-root> [--json]` provides the
    same deterministic check for closed-test collection and automation.
    Implementation: [`7677c60`](https://github.com/Ilufus/ei-mapper-endurance/commit/7677c60).
- [ ] Complete the focused 0.2.x release matrix and publish `0.2.0`.
  - Normal JMV gameplay map.
  - Zone17/zone18 or another map that previously refused script injection.
  - Dungeon map and `bz` briefing map.
  - `drawall`, traversal, generation-local Save/Load/Main Menu, return/stop, and
    graceful exit.
  - Automate staging/hash/source-preservation checks, launch readiness,
    reported map/player/entrance/generation identity, same/different-map reload,
    stop, and cleanup. Retain manual checks only for rendering, traversal,
    `drawall`, and in-game Save/Load interaction.
  - [x] Implement the automatic per-run evidence collector and offline report
    validator. It evaluates every completed user-driven launch/reload without
    adding extra runtime actions or touching game state.
  - [ ] Exercise the matrix scenarios and archive their passing `result.json`
    reports; rendering, traversal, `drawall`, and Save/Load behavior remain the
    intentionally manual observations.

### Confirmed runtime behavior and research risks

- [x] Confirm that the staged edited map remains mounted for the lifetime of a
  single Ctrl+F5 run: travelling into a connected installed zone and returning
  reloads the staged edited version with its changes intact.
- [x] Audit scripts that depend on exact zone IDs, primary MOB names, or
  `Auto<zone>*.mob` naming/discovery.
  - Corpus inspection confirms `map.txt` maps the primary MPR/MOB identity,
    while main and supplemental MOB scripts invoke exact filenames through
    conditional `AddMob("...")` calls. Nested supplemental loads also occur.
  - Runtime logs independently show those filenames being opened after the
    primary MOB and SpellAddon autorun MOB. `AddMobEx` exists in Starter's
    addon exports but is not used by the inspected JMV MOB corpus.
  - Endurance preserves every source script unchanged. MOBs explicitly listed
    in a collection are loaded in collection order through a run-owned autorun
    bootstrap; matching runtime requests are redirected to validated staged
    copies. Unlisted nested/conditional `AddMob` calls retain installed behavior.
  - Research record: [JMV supplemental MOB loading](docs/research/jmv-supplemental-mob-loading.md).

### Implemented in Alpha.58, awaiting runtime confirmation

- [x] Remove disposable-player script injection; stage the exact validated
  in-memory MOB. ([`1810546`](https://github.com/Ilufus/ei-mapper-endurance/commit/18105469f0b0f5cd46189824ca65dbdde3f3ce23))
- [x] Substitute the selected `map.txt` zone during the normal New Game
  bootstrap. ([`1810546`](https://github.com/Ilufus/ei-mapper-endurance/commit/18105469f0b0f5cd46189824ca65dbdde3f3ce23))
- [x] Prototype redirecting recognized runtime save filesystem operations to a
  retained AppData tree. Cross-run persistence was rejected; protocol v2 now
  uses disposable per-run/per-reload generation namespaces instead.
  ([`1810546`](https://github.com/Ilufus/ei-mapper-endurance/commit/18105469f0b0f5cd46189824ca65dbdde3f3ce23))
- [x] Suppress the two known startup movies without renaming live files.
  ([`1810546`](https://github.com/Ilufus/ei-mapper-endurance/commit/18105469f0b0f5cd46189824ca65dbdde3f3ce23))
- [x] Show launch-preparation progress immediately and allow the safety warning
  to be acknowledged once. ([`1810546`](https://github.com/Ilufus/ei-mapper-endurance/commit/18105469f0b0f5cd46189824ca65dbdde3f3ce23))
- [x] Reset per-run launcher state so a failed staging attempt cannot reuse an
  earlier manifest. ([`1810546`](https://github.com/Ilufus/ei-mapper-endurance/commit/18105469f0b0f5cd46189824ca65dbdde3f3ce23))

## 0.2.1: JMV hot reload and test-session continuity

- [x] Upgrade the hot-reload transport to protocol v2 with map generation,
  save namespace, observed runtime-state sequence, source/target zone identity,
  and bootstrap mode.
- [x] Create generation `000000` for initial launch and atomically commit a new
  empty save namespace only when a reload transition begins. Failed staging
  keeps the active generation; uncertain post-transition failures keep the new
  namespace.
- [x] Use a state-aware normal Main Menu/New Game rebootstrap for every reload.
  - Alpha.10 proved direct gameplay loader substitution changes terrain/objects
    but retains the previous map's minimap, quest state, and entrances.
  - Alpha.10's generic UI-control detour crashed when inventory/store opened
    before it could publish a state event; alpha.11 removes that hook entirely.
  - Gameplay, inventory, store, and other responsive states now use the same
    normal Main Menu transition, then the proven complete New Game bootstrap.
  - A short-lived Transitioning state defers. If the game thread is responsive
    after the transition state has remained unchanged, classify the observed
    loader-less map-screen path as WorldMapReady and rebootstrap safely.
- [x] Journal process ownership by PID, executable path, creation time, runtime
  hash, and run ID. On restart, clean dead runs immediately; monitor a verified
  surviving orphan without attaching to or killing it, then clean after exit.
- [x] Keep monitoring across the short Windows process-exit/locked-plugin race,
  refresh Test Current Map immediately after cleanup, and offer an explicit
  close request for a verified orphan with an unsaved-progress warning.
- [x] Maximize and focus the verified Endurance-owned game window after a
  successful hot reload so the completed state switch is immediately visible.
  Cleanup is hash-guarded and keeps externally modified artifacts for review.

- [x] Define a versioned, hash-carrying hot-reload request schema that rejects
  payloads outside the Endurance-owned test-run directory.
  ([`4e62424`](https://github.com/Ilufus/ei-mapper-endurance/commit/4e62424))

- [x] Make repeated Ctrl+F5 hot-reload the Endurance-owned game process.
  - [x] Validate and restage the exact current in-memory pair under the current
    run, with fresh hashes and a losslessly patched autorun bootstrap.
  - [x] Keep a process-private message hook on the captured game thread and
    accept only monotonic, owned, hash-matching control requests.
  - [x] Stage every reload as a new immutable, hash-verified generation under
    the run directory. Retain older generations until exit so Windows file
    locks cannot force replacement of the active pair.
  - [x] Restage the collection's supplemental MOBs under stable original
    filenames so repeated script-driven `AddMob` calls resolve to the current
    editor versions after hot reload. ([`c1ea993`](https://github.com/Ilufus/ei-mapper-endurance/commit/c1ea993))
  - [x] Never attach to or stop an independently launched game.
  - [x] Report readiness only after the first staged loader completed; report
    each reload completion/failure back to the editor with a 20-second timeout.
  - [x] Runtime-prove that the verified normal zone-transition controller safely
    unloads and reloads the same staged pair without restarting JMV. Exact-once
    supplemental/collection confirmation remains the only unchecked child.
  - [x] Re-test cross-MOB copied objects during hot reload with alpha.4 or later;
    alpha.4 fixed the provenance rejection that previously stopped trees copied
    from `zone1.mob` into `Autozone1cyclopus.mob` from staging.
  - Implementation: [`5011954`](https://github.com/Ilufus/ei-mapper-endurance/commit/5011954).
- [x] Preserve position on same-map reload.
  - Runtime testing confirms position and facing are captured before teardown,
    restored after safety checks, and fall back to the selected entrance when
    restoration is unsafe.
- [x] Use normal entrance deployment when the editor switches to a different
  map set before Ctrl+F5.
  - [x] Bind Play-from-Here state to the exact loaded MPR/map-table context and
    invalidate every picking/movement/selection remnant when that context
    changes. Stale anchors are ignored at launch even if retained accidentally;
    alpha.14 runtime testing confirms map B no longer inherits map A's anchor.
    ([`3f658af`](https://github.com/Ilufus/ei-mapper-endurance/commit/3f658af))
  - [x] Clear the previous generation's optional bootstrap MOB when the new
    reload request contains no Play from Here/Godmode/supplemental bootstrap.
    This prevents map A's cleared custom spawn from being replayed after map B
    selects a normal entrance. Runtime confirmation is pending the next tester
    build. ([`6286325`](https://github.com/Ilufus/ei-mapper-endurance/commit/6286325))
- [x] Keep the staged version mounted when travelling away and returning.
  - Returning to the edited map must not silently load the installed original.
  - Record every active staged map identity and hash.
- [x] Reject or defer hot reload while the runtime is between zones/on a map
  loading screen. Runtime testing confirms the bridge now defers until a stable
  state instead of delivering into the unsafe transition window.
- [x] Document the current staging split: complete immutable payloads and
  reload generations live under AppData, while JMV additionally requires one
  randomly named, hash-owned MPR/MOB alias in the active mod `Maps` directory.
  Cleanup removes only byte-identical owned aliases and interrupted-run
  recovery retries removal; source maps are never replaced.
- [x] Keep Endurance's **Stop JMV Map Test** control available during hot reload
  and restore editor focus only after exit or confirmed termination. No in-game
  End Test integration is required; normal `Alt+F4` remains available.

## 0.2.x: multi-MOB and workspace workflow

- [x] Preserve exact raw object templates when copying between different MOBs.
  Cross-MOB paste now carries the donor source and clean-projection subtrees,
  validates their hashes, and can save or hot-reload the destination without
  falling back to the rebuilding serializer. Runtime testing with cross-MOB
  copied objects through hot reload is confirmed.
- [x] Replace the secondary-MOB chooser with a dockable **Loaded Map** panel.
  - Present the workspace as a tree. The loaded MPR is the root and the loaded
    primary/supplemental MOBs are its children. A MOB-only workspace uses an
    explicit `No MPR (neutral grid)` root rather than implying that a terrain
    document is loaded.
  - The MPR root has its own visibility toggle. Hiding it suppresses terrain
    rendering and normal terrain picking only: the MPR remains attached and
    continues to supply projection, height, entrance, validation, collection,
    and Test Current Map data. Object coordinates and heights never change when
    terrain visibility changes. Terrain-dependent interactive tools that would
    be ambiguous against an invisible surface are disabled until it is shown.
  - Endurance does not support simultaneous editing of multiple MOBs. Exactly
    one MOB is active/unlocked; every other loaded MOB is locked against edits
    but its visible objects remain selectable for inspection and copying.
  - Double-clicking a MOB row in the panel switches the active destination;
    single-click remains ordinary row selection for multi-row operations.
    Switching locks the previous MOB and unlocks the selected MOB. The former
    Ctrl+T owner-MOB command was removed after the persistent Loaded Map panel
    made it redundant.
  - Each row can independently hide/show its MOB without unloading it.
    Visibility is editor-rendering/picking state only: hidden MOBs remain loaded
    and continue to participate in validation, Save All, collections, and Test
    Current Map. The active MOB may also be hidden; doing so clears its current
    selection and prevents its objects from being selected in either the
    viewport or outliner until shown again. Its complete outliner hierarchy is
    visually muted but remains expandable.
  - Show objects from every loaded MOB in the outliner, grouped under top-level
    MOB nodes. Objects from visible locked MOBs may be selected and inspected,
    but property edits, transforms, deletion, patrol/look editing, and other
    mutations remain disabled. The UI identifies their owning locked MOB.
  - Copy is allowed from a selected locked-MOB object. Paste and Ctrl+D
    duplicate always create the new object in the active MOB, never in the
    selected source object's locked owner. Double-clicking a locked-MOB object
    activates its owner and keeps that object selected.
  - Show the owning MOB for the selected object. Reuse the already working
    cross-MOB copy/paste path; no coordinated simultaneous-edit transaction or
    multi-document Save All redesign is required for this feature.
  - Switching MOBs cancels any uncommitted placement/transform and clears the
    old selection without dirtying either document. Undo/redo remains global;
    when its next command belongs to another loaded MOB, Endurance activates
    that owner before applying the command rather than mutating a locked MOB
    invisibly. Map/MOB open, close, visibility, and active-MOB switching remain
    workspace UI state, not document undo commands.
  - Provide **Unload MOB** on MOB rows. It runs the normal dirty-document
    Save/Discard/Cancel check, closes or resolves document-owned parameter and
    script windows, cancels pending transforms, clears selection, and prevents
    stale undo/redo commands from referencing the unloaded document. A
    successfully unloaded MOB is removed from the tree and from the workspace,
    validation, Save All, collection snapshot, and Test Current Map document
    sets. If the active MOB is unloaded, choose the next loaded MOB
    deterministically; if no MOB remains, enter terrain-only mode and disable
    MOB editing actions.
  - Do not add drag reordering in the first version. Preserve collection/runtime
    load order, show the primary/supplemental role, and keep unloading distinct
    from hiding so a visibility click can never change the test-run document set.
  - Implementations: [`517cc1f`](https://github.com/Ilufus/ei-mapper-endurance/commit/517cc1f),
    [`90eb899`](https://github.com/Ilufus/ei-mapper-endurance/commit/90eb899),
    [`8a0badd`](https://github.com/Ilufus/ei-mapper-endurance/commit/8a0badd).
- [ ] Runtime-validate deterministic multi-MOB JMV test loading.
  - [x] Identify the primary MOB from `map.txt` (falling back to collection
    order), validate every loaded MOB, and inventory all source/staged hashes.
  - [x] Preserve original supplemental filenames and explicitly load collection
    members in their declared order through the run-owned bootstrap.
  - [x] Redirect only matching run-owned supplemental files; unlisted calls
    continue to the installed JMV file. No `AddMobEx` assumption is required.
  - [ ] Runtime-test immediate, conditional, and nested supplemental loads,
    then repeat after Ctrl+F5 hot reload and confirm no duplicate instances.
    There is no current deterministic nested/conditional fixture; retain this
    as a confirmation task and do not hold unrelated 0.2.x work on it.
  - Implementation: [`c1ea993`](https://github.com/Ilufus/ei-mapper-endurance/commit/c1ea993).
- [x] Add portable Endurance `.collection` files.
  - Store one optional MPR and one or more ordered MOB references; never embed
    or copy game data.
  - Add **Open Collection**, **Save Collection**, and **Save Collection As**.
  - Store them under `collections/` by default.
  - Prefer profile/map-root-relative references and report unresolved paths
    together before opening.
  - Current source implements the UTF-8 `key=value` v1 document, ordered MOB
    list, create/edit/open/save/save-as UI, grouped missing-reference errors,
    browser population, and explicit refresh.
    ([`4e62424`](https://github.com/Ilufus/ei-mapper-endurance/commit/4e62424))
  - Edit/reorder without loading is included in the editor dialog rather than
    deferred.
- [x] Add the hideable **Maps and Collections** browser with a `map.txt`-based
  Maps MVP and customizable Ctrl+Space shortcut. ([`a8e1ac7`](https://github.com/Ilufus/ei-mapper-endurance/commit/a8e1ac7), [`d86188f`](https://github.com/Ilufus/ei-mapper-endurance/commit/d86188f))
  - Map rows now expose **Open Map**, catalog editing with the matching
    `map.txt` entry preselected, and **Duplicate as Collection**. Collection
    rows expose open, edit, duplicate, save-as, and guarded deletion; a
    persistent **New Collection** shortcut sits above Refresh.
    ([`51d9ad0`](https://github.com/Ilufus/ei-mapper-endurance/commit/51d9ad0))

## 0.2.x: MOB editing workflow and recovery

- [x] **Stable 0.2.0 gate:** add a parameter-aware outliner filter.
  - Search object name, NID, type, model, texture, database/prototype name,
    MOB ownership, and validation state rather than only visible tree labels.
  - A query such as `unhufe` must return objects whose model is `unhufe`.
  - Preserve selection and hierarchy context, show match counts, debounce large
    maps, and keep filtering independent from viewport selection/scrolling.
  - Current source adds 150 ms debouncing, strict contiguous bare terms, quoted
    phrases, AND-combined terms, exact `field=value`, explicit `~` subsequence
    fuzziness, and `name:`, `id:`, `type:`, `model:`, `texture:`,
    `db:`/`prototype:`, `mob:`, and `validation:` fields.
  - Initial implementation: local commit `1685bdd`; alpha.9 carries the strict
    matching correction and awaits focused tester validation.
- [x] Make manual **Open MPR**, drag/drop, recent-file, and command-line MPR
  opening reuse the guarded replacement already used by the Maps browser:
  run the existing dirty-state Save/Discard/Cancel check, then unload every MOB
  and the old MPR, clear document-bound undo state, and load the requested
  terrain. Opening additional MOBs remains independent and is not affected.
- [x] Prevent the same canonical MOB file from being loaded more than once.
  Reopening it activates its existing document and reports that fact without
  duplicating objects, validation candidates, or undo state.
- [x] Persist the expanded/collapsed state of the **Maps** and **Collections**
  roots in the browser across refreshes and editor restarts.
- [x] Add an opt-in **Open last session on startup** General setting that invokes
  the existing File-menu session loader. It defaults off and never restores
  unsaved edits. Session data now preserves whether the workspace was opened as
  a catalog map or collection, including the exact zone/collection identity;
  restored workspaces no longer degrade into anonymous loose-file sets.
- [x] Show the active catalog map or collection identity in the main window
  title alongside the editing profile and active MPR/MOB.
- [x] On first-run setup, explicitly show Maps and Collections docked left and
  Object Outliner above Object Properties on the right. Subsequent launches
  respect the user's saved dock layout.
- [x] Keep disabled menu entries aligned with enabled entries by using the same
  font metrics; disabled state changes color only.
- [ ] Add opt-in Windows file associations for `.mpr`, `.mob`, and
  `.collection`, with correct single-instance/open-argument behavior and an
  explicit removal path.
- [x] **High priority:** add periodic autosave with versioned, recoverable
  backup states soon after the RC gate. Autosaves must never overwrite live
  source maps directly and recovery must identify the source hash/generation.
  Each scheduled occurrence now gives a non-activating 15-second countdown and
  can be skipped without disabling the autosave schedule.
- [x] Add a viewport camera-speed multiplier from 0.1x to 10x, persisted in
  Endurance settings and adjustable through a configurable Ctrl+mouse-wheel
  gesture.
- [ ] Add a resizable bottom workflow-log panel with filtering, copy/export, and
  quick access to the full rotated log.
- [x] Show named, blocking save phases—serialize, validate, create backup,
  re-open staged output, and atomically replace—so a large safe save cannot look
  like a frozen editor. Save progress is deliberately non-cancellable.
- [x] Move manual MPR/MOB loading phases behind the existing
  blocking modal loading UI and expose a visible busy state. Loading is not
  cancellable; the purpose is to prove the editor is working and prevent a
  healthy long operation from looking frozen. This is responsiveness UX, not a
  parser or serializer redesign. Deeper asynchronous resource indexing remains
  tracked separately under resources and performance.
- [ ] Stress large-map edit/undo/redo/save/open/close cycles with scripts,
  model parts, multi-selection, copy/delete, and patrol/look structures.
- [ ] Finish closed-group coverage proving scripts and all CP1251 text edits
  change only their intended leaves.
- [x] Keep selection changes in undo history and group one user action into one
  undo/redo step.
- [x] Keep map open/close outside object-edit undo history and clear/protect
  history when documents unload.
- [x] Support typed multi-field property copy/paste, including model parts and
  Create Object, with destination compatibility checks.
- [x] **0.2.x release task:** allow copying/pasting one selected component of a
  compound property value—initially individual X/Y/Z vector components. A
  copied component is an ordinary numeric scalar (for Vector3, a float), so it
  may be pasted into any numeric destination that can represent it safely; it
  is not restricted to the same component name or compound-property type.
  Integer destinations round the numeric value to the nearest integer and then
  apply the destination's normal range/validity checks; they do not require an
  already-integral source value.
  ([`517cc1f`](https://github.com/Ilufus/ei-mapper-endurance/commit/517cc1f))
- [x] Provide independent internal/external MOB script editing with document-
  owned workspaces, undoable imports, recovery, and configurable editor path.
- [x] Warn before changing/resetting/deleting object IDs referenced by the
  active MOB script. ([`a8e1ac7`](https://github.com/Ilufus/ei-mapper-endurance/commit/a8e1ac7))

## 0.2.x: resources, appearance, and performance

Database-driven humanoid appearance, equipment, standing animation, spatial
candidate queries, and bounded render queues are merged into the active 0.2.x
line. Profile/runtime validation remains part of the RC matrix.

- [ ] Complete cancellable asynchronous RES indexing, MOB parsing, and CPU
  decoding while keeping OpenGL creation in bounded GUI-thread batches.
- [ ] Persist resource indexes by archive path, size, modification time, and
  header hash.
- [ ] Add a diagnostics panel for phase timings, shadowed assets, missing
  resources, and decode failures.
- [ ] Cover every MMP format found in the JMV corpus. Unsupported or malformed
  textures display a diagnostic placeholder with asset, archive, format, and
  failure reason.
- [ ] Accept explicitly configured ZIP archives as read-only loose resource
  sources for supplied `.mmp` packs. Index entries without extracting into the
  game tree, retain deterministic precedence/provenance, reject unsafe paths,
  and keep RES handling unchanged.
- [ ] Re-run cold/warm performance acceptance on representative JMV maps and
  keep the post-index target at least 10x faster than original v0.3 where the
  current machine permits it.
- [x] Merge the optimized profile-correct visuals branch, including applicable
  humanoid appearance from active-profile database/template data.
- [x] Merge database-equipped armor and weapon rendering.
  - Base scope: one one-handed or one ranged weapon.
  - JMV profile scope: armor and ordinary equipped visuals.
  - JMV stretch: 1H+shield, dual 1H, 2H, dual 2H with perk, and 2H+shield with
    the applicable perk.
- [x] Merge profile-correct standing idle animation selection and its
  diagnostic fallback instead of leaving characters in the null/T pose.
- [ ] Populate prototype/model/texture fields from the active database with
  prefix filtering and manual entry where valid. Warn on missing entries rather
  than silently clearing stored values.
- [x] Normalize archive keys case-insensitively, cache misses, and resolve
  generic configured archives by deterministic priority.
- [x] Add cached CPU frustum culling for object geometry while keeping logic
  helpers independently visible. ([`1ab2935`](https://github.com/Ilufus/ei-mapper-endurance/commit/1ab2935))
- [x] Correct internal MPR atlas namespace handling and model-qualified JMV unit
  skins. ([`530bb16`](https://github.com/Ilufus/ei-mapper-endurance/commit/530bb16), [`35a43c8`](https://github.com/Ilufus/ei-mapper-endurance/commit/35a43c8))

## 0.2.x: MOB diff and validation tools

- [x] Add deterministic file-to-file MOB semantic diff, including embedded
  script and document-section differences. ([`fd30be0`](https://github.com/Ilufus/ei-mapper-endurance/commit/fd30be0), [`0a4624f`](https://github.com/Ilufus/ei-mapper-endurance/commit/0a4624f))

## 0.2.x capability boundaries

- [x] Fingerprint Base and JMV runtime stacks and expose profile capabilities.
  ([`97b6991`](https://github.com/Ilufus/ei-mapper-endurance/commit/97b6991))
- [x] Define profile-scoped appearance, equipment, prototype, semantic
  validation, and map-test provider interfaces. ([`b467d87`](https://github.com/Ilufus/ei-mapper-endurance/commit/b467d87))
- [x] Keep JMV DLL-defined behavior out of the editor process; use a
  fingerprinted isolated runtime provider.
- [x] Keep generalized Base direct launch disabled while exposing only the
  fingerprinted experimental Base provider. Local testing confirmed exact-hash
  probe levels 3 and 4; this does not claim support for arbitrary Base
  executables or other mods.
- [x] Scope JMV bridge compatibility to the exact Starter `game.exe` code
  layout actually hooked by Endurance. Frequently updated JMV/Starter runtime
  extensions are mod-owned behavior, not editor dependencies or manifest
  gates. ([`eff6f99`](https://github.com/Ilufus/ei-mapper-endurance/commit/eff6f99))
- [ ] **Post-1.0, on request:** support other mods through optional,
  fingerprinted runtime plugins owned by their profiles. Do not generalize
  unsafe memory hooks merely to advertise universal Test Current Map support.

</details>

<details>
<summary><strong>0.1.x — completed foundation and MOB hardening history</strong></summary>

## 0.1.x outcome

The supported MOB hardening implementation is complete. Normal properties,
transforms, scripts, diplomacy, model parts, same-MOB copy/delete, and known
patrol/look structures save through provenance-checked raw patches. Endurance
does not rebuild an edited MOB through the legacy serializer when it cannot map
an operation safely.

## Core product and repository foundation

- [x] Create an independent private repository with complete upstream history,
  GPL-3.0 attribution, `NOTICE.md`, and an `upstream` remote.
  ([`054959b`](https://github.com/Ilufus/ei-mapper-endurance/commit/054959b))
- [x] Brand the product/executable as EI Mapper: Endurance Edition and isolate
  settings/log identifiers. ([`054959b`](https://github.com/Ilufus/ei-mapper-endurance/commit/054959b))
- [x] Organize tracked C++ under `source/app`, `core`, `documents`, `mob`,
  `objects`, `render`, `resources`, `runtime`, and `ui`; move assets/docs/scripts
  out of the root and retire superseded alpha validation files.
  ([`a8e1ac7`](https://github.com/Ilufus/ei-mapper-endurance/commit/a8e1ac7))
- [x] Keep game assets, private paths, dumps, saves, and integration fixtures out
  of Git and distribution packages.

## MOB safety and lossless editing

- [x] Stop automatic duplicate-ID repair on open and report duplicates without
  dirtying the document. ([`054959b`](https://github.com/Ilufus/ei-mapper-endurance/commit/054959b), [`34b9ec8`](https://github.com/Ilufus/ei-mapper-endurance/commit/34b9ec8))
- [x] Add strict raw-section MOB parsing with complete byte coverage and
  deterministic semantic diff. ([`aaef298`](https://github.com/Ilufus/ei-mapper-endurance/commit/aaef298))
- [x] Add provenance-checked leaf transactions and fail-closed UI projection
  shadow transactions. ([`660a542`](https://github.com/Ilufus/ei-mapper-endurance/commit/660a542), [`3106fac`](https://github.com/Ilufus/ei-mapper-endurance/commit/3106fac))
- [x] Preserve unchanged MOB/MPR Save As bytes and use staged fresh-parser
  validation, backups, and atomic replacement.
- [x] Preserve SOUND/world fields, CP1251 lengths, figure/texture/database
  strings, guard/patrol height, model parts, scripts, diplomacy, and unknown
  raw payloads for supported edits.
- [x] Add exact same-MOB raw subtree copy/delete and known patrol/look structural
  edits. ([`11698ad`](https://github.com/Ilufus/ei-mapper-endurance/commit/11698ad))
- [x] Generalize topology-preserving raw leaf saves beyond a permanent field
  whitelist. ([`843ba9c`](https://github.com/Ilufus/ei-mapper-endurance/commit/843ba9c))
- [x] Rebind raw provenance after save and protect repeated undo/redo/save
  sequences. ([`db3fa31`](https://github.com/Ilufus/ei-mapper-endurance/commit/db3fa31))
- [x] Validate the Base/JMV projection corpus and focused in-game changed-map
  tests. ([MOB hardening validation](docs/validation/MOB_HARDENING_VALIDATION.md), [shadow transaction validation](docs/validation/MOB_SHADOW_TRANSACTION_VALIDATION.md))

## Backups, diagnostics, and recovery

- [x] Create deduplicated open snapshots and timestamped pre-save revisions;
  refuse replacement when the required backup fails. ([`8a7bc3c`](https://github.com/Ilufus/ei-mapper-endurance/commit/8a7bc3c))
- [x] Add retention, metadata/hashes, and **Tools > Open Backup Folder**.
- [x] Start a clean workflow log on launch and rotate the prior log with a
  timestamp. ([`2568e38`](https://github.com/Ilufus/ei-mapper-endurance/commit/2568e38))
- [x] Add crash records/dumps, application version, active documents, rendering
  state, and workflow phases to diagnostics.
- [x] Ensure child windows close with the main editor and remove build-time
  interactive version probes.

## Resource profiles and loading

- [x] Add installation-based Base/JMV profiles with first-run setup, automatic
  RES discovery, deterministic source order, and Settings-based reconfiguration.
  ([`7b206c8`](https://github.com/Ilufus/ei-mapper-endurance/commit/7b206c8), [`8528799`](https://github.com/Ilufus/ei-mapper-endurance/commit/8528799))
- [x] Add generic JMV RES discovery, case-normalized lookup, lazy indexed
  payload reads, and negative caching. ([`44ab888`](https://github.com/Ilufus/ei-mapper-endurance/commit/44ab888))
- [x] Fix terrain-atlas/internal-map naming and model-qualified skin lookup.
- [x] Add database-backed humanoid appearance candidates while retaining the
  unresolved Base rendering task for the later full resolver.

## Independent editor workflow and UI

- [x] Open MPR and MOB independently, including MOB-only neutral-grid rendering.
- [x] Add resizable outliner/property layouts, grouped object navigation,
  recent files, independent shortcuts, and persisted UI state. Full fuzzy
  parameter filtering remains a high-priority `0.2.x` task.
- [x] Add model/texture prefix filtering with explicit commit and manual entry.
- [x] Add configurable undo-history limit, selection undo, atomic multi-object
  actions, and protected document lifecycles.
- [x] Add first patrol/look point creation, duplicate shortcut, history shortcut,
  and customizable application keybindings.
- [x] Add external-editor selection and an independent internal script editor
  with syntax highlighting, CP1251 round-trip enforcement, undo, diff/import,
  recovery, and document-owned watching.
- [x] Add readable MOB diff details including scripts and raw sections.

## Direct-launch feasibility history, newest first

- [x] Alpha.58: selected-zone transition, exact staged MOB, save redirection,
  startup-movie interception, immediate progress, and clean per-run state.
  ([`1810546`](https://github.com/Ilufus/ei-mapper-endurance/commit/18105469f0b0f5cd46189824ca65dbdde3f3ce23))
- [x] Alpha.57: invoke the complete fingerprinted New Game bootstrap. Runtime
  testing proved staged terrain/MOB loading and a playable session, while also
  exposing duplicate-player and zone1-metadata defects.
  ([`28300cb`](https://github.com/Ilufus/ei-mapper-endurance/commit/28300cb))
- [x] Alpha.56: prove the partial transition had no missing asynchronous loader.
- [x] Alpha.55: capture the real transition controller and prove mode 0 enters
  only an incomplete gameplay shell without full session state.
- [x] Alpha.54: identify mode 2 as the dedicated main-menu transition.
- [x] Alpha.53: disprove the earlier startup/menu initializer as New Game.
- [x] Alpha.52/51/50: observe and fingerprint the real single-player map-loader
  entry and callers without redirecting files.
- [x] Alpha.49 through Alpha.43: retire failed console/server, autoexec, timer,
  window-message, loose-plugin, and incomplete transition approaches while
  preserving their evidence in Git history.

## Notable implementation evidence

| Change group | Evidence |
|---|---|
| Initial Endurance identity/documents/resources/workflow | [`054959b`](https://github.com/Ilufus/ei-mapper-endurance/commit/054959b) |
| Crash diagnostics and rotating logs | [`2568e38`](https://github.com/Ilufus/ei-mapper-endurance/commit/2568e38) |
| Recoverable backups | [`8a7bc3c`](https://github.com/Ilufus/ei-mapper-endurance/commit/8a7bc3c) |
| Capability profiles and runtime manifests | [`97b6991`](https://github.com/Ilufus/ei-mapper-endurance/commit/97b6991) |
| Raw-section MOB tree and semantic diff | [`aaef298`](https://github.com/Ilufus/ei-mapper-endurance/commit/aaef298) |
| Generic raw leaf saves | [`843ba9c`](https://github.com/Ilufus/ei-mapper-endurance/commit/843ba9c) |
| Exact copy/delete/patrol/look saves | [`11698ad`](https://github.com/Ilufus/ei-mapper-endurance/commit/11698ad) |
| MOB parameters, scripts, clipboard, and undo integration | [`f4b107c`](https://github.com/Ilufus/ei-mapper-endurance/commit/f4b107c), [`5192c4d`](https://github.com/Ilufus/ei-mapper-endurance/commit/5192c4d), [`258893c`](https://github.com/Ilufus/ei-mapper-endurance/commit/258893c) |
| Redo-safe provenance and July UI fixes | [`db3fa31`](https://github.com/Ilufus/ei-mapper-endurance/commit/db3fa31) |

</details>

<details>
<summary><strong>Project contract and feature-development knowledge</strong></summary>

## Product contract

- Normal editing must feel normal: no save preview, per-field approval list, or
  warning merely because a normal supported field changed.
- Preserve every source byte the user did not intentionally change and propagate
  every intended edit.
- Run raw-data checks, fresh reopen validation, backups, and atomic replacement
  automatically.
- Refuse only operations Endurance genuinely cannot represent safely; treat the
  refusal as an implementation gap rather than a permanent workflow.
- The game is the final compatibility target. Corpus checks and MobExplorer are
  supporting development evidence, not substitutes for Base/JMV runtime tests.

## Compatibility evidence levels

1. **Structurally valid:** complete byte coverage and valid encoding/lengths.
2. **Profile compatible:** passes Base or JMV semantic/resource rules.
3. **Runtime validated:** passes recorded game tests on fingerprinted components.

## Feature-development knowledge notes

- Database/prototype identity reportedly controls normal humanoid appearance and
  equipment; the MOB texture field may be ignored at runtime for those units.
  Keep preserving the field until profile-specific evidence proves otherwise.
- Do not conflate model name with the MOB `Template` field. Preserve template,
  inner-map name, and `ParentID` even where their ordinary runtime effect is
  unclear.
- Ordinary humanoids should retain known-good model parts. Equipped visuals
  should come from the appearance/equipment resolver rather than invented model
  parts.
- `Import`/`Is need import` reportedly switches equipment, dropped items, and
  related unit data from database-derived to MOB-embedded values.
- `InScript`/`Is use in script` reportedly matters mainly for dynamic objects
  scripts move or destroy; do not generalize this without corpus checks.
- Navigation data was historically stored in MOB files, but map makers used a
  large scripted door to force runtime graph regeneration. Preserve and detect
  this convention.
- Larger-than-normal MPRs can open in Endurance. Keep editor-side size handling
  permissive, while treating runtime limits as profile-specific validation.

## Roadmap maintenance

- Put unassigned requests in the General wishlist first.
- Assign accepted work to the newest applicable version block.
- Order version blocks and release-specific tasks from newest to oldest.
- Keep major blocks collapsible with `<details>`.
- Add a commit, test, or release link when checking off implementation work.
- If testing exposes a regression, reopen the item and record the concrete case.

</details>
- [ ] Runtime-confirm the complete synthetic `game` identity used by unmapped
  MPR/MOB testing. The alpha.7 generated-graph control still crashed after
  `GameplayReady`, disproving the graph-only explanation. Alpha.8 now supplies
  the loaded MPR dimensions, safe built-in minimap/objectives placeholders,
  and one private deploy entrance because the normal mission UI and player
  bootstrap consume those fields. The installed map table and source files
  remain unchanged.
- [ ] Determine the native Vulcansmoke (8197 / `0x2005`) crash mechanism with
  the isolated exact-name `Vulkan.mmp` run-local alias control and dump/runtime
  analysis. Keep the editor's Critical validation notice in place meanwhile;
  do not install or treat a borrowed atlas as visual-fidelity evidence.
