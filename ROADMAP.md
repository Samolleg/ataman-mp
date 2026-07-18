# EI Mapper: Endurance Edition roadmap

This is the living checklist for the private Endurance Edition line. Versioned
work is ordered from the newest planned minor line to the oldest completed one.
An item is checked only after its implementation has landed and received the
stated validation. Completed code should carry a commit, test, or release link.

Current development line: `0.2.0`.

<details open>
<summary><strong>General wishlist — not assigned to a release</strong></summary>

## Compatibility and profile support

- [ ] Define a documented provider/extension contract for other mods.
  - A mod opts into only the capabilities it actually implements.
  - Unsupported profile features remain hidden or disabled with a reason.

## Optional editor integrations and accessibility

- [ ] Add optional external MOB diff-tool integration. Pass two explicit files
  or exported snapshots to a configured executable without replacing the
  built-in deterministic diff or exposing private/unsaved data silently.
- [ ] Add configurable UI font-size scaling for 4K displays and accessibility.
- [ ] Add a Russian translation and an in-app language switch.
- [ ] Correct the remaining spacing/alignment offsets for disabled menu items.
- [ ] Make all major workspace panels dockable, detachable, tab-compatible, and
  persistently rearrangeable within Qt5's supported docking model.

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
- [ ] Define object-aware duplicate-ID validation and repair policy.
  - Do not assign one severity to every duplicate: duplicated static decorative
    objects such as bushes may be harmless, while units, logic-bearing objects,
    or IDs referenced from scripts can be unsafe.
  - Classify findings using object family, logic/script references, and active
    runtime profile. Keep uncertain cases visible rather than guessing.
  - Opening preserves all IDs. Any repair remains an explicit previewed
    operation that updates only unambiguous structured references and reports
    ambiguous/script references for manual review.
  - Decide whether saving unresolved high-risk duplicates requires the same
    explicit preserve/repair/cancel consent as other known defects.
- [ ] Stress repeated open/close/save/cancel and abnormal-exit behavior on large
  Base/JMV maps.
- [ ] Complete the broad game matrix: `drawall`, save/load, unload/reload,
  scripts, traversal, combat-relevant objects, and graceful exit.
- [ ] Convert concrete regressions into minimized synthetic fixtures whenever
  the format permits this without distributing game or JMV assets.

</details>

<details>
<summary><strong>1.0.x or later — optional legacy-platform compatibility</strong></summary>

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
<summary><strong>0.3.x — lossless MPR/SEC and landscape editing</strong></summary>

## 0.3.0 release goal

Deliver the first supported landscape-editing release. Merely displaying an MPR
does not qualify: changed terrain must use a lossless archive/sector document
path and must never fall back to the legacy rebuilding serializer.

## Lossless MPR/SEC foundation

Isolated implementation status: `feature/landscape-editing` has clean commits
`201b3ff` (lossless archive document) and `195e469` (canonical terrain lattice
and undoable sculpt-command foundation). Its focused suite passes 38/38 and a
copied 8-Base/32-JMV corpus round-trips byte-identically. These commits are not
merged; packed-normal recalculation and renderer/UI integration remain the
next gate, and changed landscape saving remains disabled.

- [ ] Preserve untouched archive members, internal names, member order,
  timestamps/metadata, alignment, unknown entries, and original MP/SEC
  namespaces.
- [ ] Make unchanged Save As byte-identical even when the outer filename differs
  from the embedded map name.
- [ ] Patch only edited sector/overlay payloads and derive affected lengths and
  metadata from emitted content.
- [ ] Stage, freshly reopen, validate, back up, and atomically replace every
  changed MPR.
- [ ] Add deterministic MPR/SEC semantic diff and untouched-member equality
  checks.
- [ ] Validate changed MPRs on copied Base/JMV maps before enabling live-map
  replacement.

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

- [ ] Warn when terrain edits can invalidate MOB object heights or the AI
  navigation graph.
- [ ] Detect and preserve the known oversized-door runtime graph-regeneration
  convention.
- [ ] Provide placement/navigation findings before save and test launch.
- [ ] Require in-game traversal, water/overlay interaction, unload/reload, and
  graceful-exit tests before `0.3.0` release.

</details>

<details open>
<summary><strong>0.2.x — hardened MOB editor and JMV direct gameplay</strong></summary>

## 0.2.0 release gate

`0.1.0-alpha.58` is the immutable selected-zone/save-isolation test build.
`0.2.0-alpha.1` moves the selected zone and entrance into the engine's New
Game player record before transition, permits authored entrances when the
editor's conservative clearance check cannot prove them clear, and introduced
profile-isolated test saves. Runtime testing subsequently rejected persistent
Save/Load for temporary map instances; active Ctrl+F5 sessions will disable
those commands instead.
Publish the first immutable `0.2.0` package after the following runtime contract
is demonstrated on normal gameplay, dungeon, and briefing maps.

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
- [ ] Validate temporary JMV Godmode delivery through a run-local SpellAddon
  autorun MOB.
  - Runtime testing proved that the safely installed temporary `autoexec.cfg`
    was ignored. `0.2.0-alpha.4` instead losslessly patches a run-local copy of
    `AutoRunSpellAddon123.mob` with `GodMode(0,3)` and redirects only the owned
    test process. The installed autorun MOB remains untouched.
  - Alpha.6 proved the redirect happened but the appended command did not run.
    Alpha.7 injects both spawn placement and Godmode at the beginning of the
    existing `WorldScript` body, before early returns or `KillScript` paths.
  - Current run logs prove the redirected autorun MOB is opened, but its
    commands were inserted before the MOB's own leading initialization delay.
    Current source preserves that delay and injects `SetCPFast`/`GodMode`
    immediately after it. Runtime confirmation is still required.
    ([`4e62424`](https://github.com/Ilufus/ei-mapper-endurance/commit/4e62424))
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

- [ ] Block **Main Menu** after the staged map becomes active.
  - The initial main menu remains part of bootstrap, so arm the restriction only
    after the staged loader succeeds.
  - Disable the existing pause-menu button using the engine's normal disabled
    action state; no replacement in-game command is needed.
  - Endurance **Stop Test** and normal process exit must remain functional.
- [x] Expose **Stop JMV Map Test** in Endurance while its owned game process is
  active, with customizable `Shift+Esc` as the default shortcut
  ([implementation](https://github.com/Ilufus/ei-mapper-endurance/commit/5f693b2)).
- [ ] Replace loader-return timing with a reliable runtime-readiness gate.
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
- [ ] Disable **Save** and **Load** after the staged map becomes active.
  - The temporary map instance and its staged identity are valid only for one
    Ctrl+F5 run; saves from an earlier run are deliberately unsupported.
  - Disable the existing in-game actions using their normal disabled state,
    alongside **Main Menu**. No replacement save namespace is required.
  - Runtime evidence: loading a save associated with an earlier Ctrl+F5 run
    crashed after the temporary map instance had been replaced.
- [x] Verify startup-movie suppression for root and active-mod movie paths.
  - Alpha.58 hides `1c.bik` and `nival.bik` in-process without renaming them.
  - Preserve map-specific cutscenes; only startup movies are skipped by default.
- [ ] Add authoritative bridge results.
  - Report actual loaded zone, MPR/MOB hashes, player count, selected entrance,
    save namespace, readiness, and cleanup outcome.
  - Distinguish a genuine crash from the game's known abnormal-looking but
    user-initiated exit path.
- [ ] Complete the focused Alpha.58 matrix and publish `0.2.0`.
  - Normal JMV gameplay map.
  - Zone17/zone18 or another map that previously refused script injection.
  - Dungeon map and `bz` briefing map.
  - `drawall`, traversal, disabled Save/Load/Main Menu, return/stop, and graceful
    exit.

### Confirmed runtime behavior and research risks

- [x] Confirm that the staged edited map remains mounted for the lifetime of a
  single Ctrl+F5 run: travelling into a connected installed zone and returning
  reloads the staged edited version with its changes intact.
- [ ] Audit scripts that depend on exact zone IDs, primary MOB names, or
  `Auto<zone>*.mob` naming/discovery.
  - Determine whether the temporary map identity changes any script lookup or
    supplemental-MOB behavior.
  - Do not rewrite embedded scripts speculatively. First inventory concrete
    zone/MOB-name references and prove which engine or addon lookup consumes
    each one.
  - Existing evidence is mixed but encouraging: location-first-entry scripts,
    including the tested zone11 behavior, execute in instantiated maps.
  - If redirection is required, prefer profile-scoped runtime lookup aliases
    over destructive script text replacement.

### Implemented in Alpha.58, awaiting runtime confirmation

- [x] Remove disposable-player script injection; stage the exact validated
  in-memory MOB. ([`1810546`](https://github.com/Ilufus/ei-mapper-endurance/commit/18105469f0b0f5cd46189824ca65dbdde3f3ce23))
- [x] Substitute the selected `map.txt` zone during the normal New Game
  bootstrap. ([`1810546`](https://github.com/Ilufus/ei-mapper-endurance/commit/18105469f0b0f5cd46189824ca65dbdde3f3ce23))
- [x] Prototype redirecting recognized runtime save filesystem operations to a
  retained AppData tree. Runtime testing rejected cross-run persistence for
  temporary map identities; this is superseded by disabling Save/Load.
  ([`1810546`](https://github.com/Ilufus/ei-mapper-endurance/commit/18105469f0b0f5cd46189824ca65dbdde3f3ce23))
- [x] Suppress the two known startup movies without renaming live files.
  ([`1810546`](https://github.com/Ilufus/ei-mapper-endurance/commit/18105469f0b0f5cd46189824ca65dbdde3f3ce23))
- [x] Show launch-preparation progress immediately and allow the safety warning
  to be acknowledged once. ([`1810546`](https://github.com/Ilufus/ei-mapper-endurance/commit/18105469f0b0f5cd46189824ca65dbdde3f3ce23))
- [x] Reset per-run launcher state so a failed staging attempt cannot reuse an
  earlier manifest. ([`1810546`](https://github.com/Ilufus/ei-mapper-endurance/commit/18105469f0b0f5cd46189824ca65dbdde3f3ce23))

## 0.2.1: JMV hot reload and test-session continuity

- [x] Define a versioned, hash-carrying hot-reload request schema that rejects
  payloads outside the Endurance-owned test-run directory. Runtime consumption
  remains disabled until game-thread teardown/reload is proven.
  ([`4e62424`](https://github.com/Ilufus/ei-mapper-endurance/commit/4e62424))

- [ ] Make repeated Ctrl+F5 hot-reload the Endurance-owned game process.
  - Validate and restage the exact current in-memory pair.
  - Request teardown/reload on the verified game thread without restarting JMV.
  - Never attach to or stop an independently launched game.
  - Never replace a staged file while the runtime loader still owns it.
- [ ] Preserve position on same-map reload.
  - Capture player position and facing before teardown.
  - Restore them only after ground, bounds, and collision validation.
  - Fall back to the selected map-table entrance when unsafe.
- [ ] Use normal entrance deployment when the editor switches to a different
  map set before Ctrl+F5.
- [ ] Keep the staged version mounted when travelling away and returning.
  - Returning to the edited map must not silently load the installed original.
  - Record every active staged map identity and hash.
- [ ] Keep Endurance's **Stop JMV Map Test** control available during hot reload
  and restore editor focus only after exit or confirmed termination. No in-game
  End Test integration is required; normal `Alt+F4` remains available.

## 0.2.x: multi-MOB and workspace workflow

- [ ] Replace the secondary-MOB workflow with Unreal-style multi-MOB editing.
  - Show every open MOB in a **Loaded MOBs** list/tab.
  - Make the active destination MOB explicit for paste/create operations.
  - Allow individual MOBs to be locked against selection and accidental edits.
  - Show which loaded MOB owns the selected object.
  - Save/validate all changed MOBs as a coordinated workspace action.
- [ ] Add deterministic multi-MOB JMV test loading.
  - Load the base MOB plus declared supplemental MOBs exactly once and in order.
  - Resolve the JMV `Auto<map>*.mob`/`AddMobEx` convention through the profile
    provider rather than universal assumptions.
- [ ] Add portable Endurance `.collection` files.
  - Store one optional MPR and one or more ordered MOB references; never embed
    or copy game data.
  - Add **Open Collection**, **Save Collection**, and **Save Collection As**.
  - Store them under `collections/` by default.
  - Prefer profile/map-root-relative references and report unresolved paths
    together before opening.
  - Current source implements the UTF-8 `key=value` v1 document, ordered MOB
    list, create/edit/open/save/save-as UI, grouped missing-reference errors,
    browser population, and explicit refresh. It is held for the next combined
    build while deterministic multi-MOB test loading remains unfinished.
    ([`4e62424`](https://github.com/Ilufus/ei-mapper-endurance/commit/4e62424))
  - Edit/reorder without loading is included in the editor dialog rather than
    deferred.
- [x] Add the hideable **Maps and Collections** browser with a `map.txt`-based
  Maps MVP and customizable Ctrl+Space shortcut. ([`a8e1ac7`](https://github.com/Ilufus/ei-mapper-endurance/commit/a8e1ac7), [`d86188f`](https://github.com/Ilufus/ei-mapper-endurance/commit/d86188f))

## 0.2.x: MOB editing workflow and recovery

- [ ] Add periodic autosave with versioned, recoverable backup states.
- [ ] Add a resizable bottom workflow-log panel with filtering, copy/export, and
  quick access to the full rotated log.
- [ ] Add phase-aware blocking progress for MOB/MPR save, including
  serialization, validation, backup, and atomic replacement.
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
- [x] Provide independent internal/external MOB script editing with document-
  owned workspaces, undoable imports, recovery, and configurable editor path.
- [x] Warn before changing/resetting/deleting object IDs referenced by the
  active MOB script. ([`a8e1ac7`](https://github.com/Ilufus/ei-mapper-endurance/commit/a8e1ac7))

## 0.2.x: resources, appearance, and performance

Isolated implementation status: profile-correct visuals are under active
runtime correction. `feature/humanoid-equipment` is clean at `7dcdfac`; the
cumulative equipment/idle-animation line reached `b38d7b4` with 13/13 visual
provider tests and 12/12 animation tests before the current corpus correction
pass. Nothing is merged. Known remaining classes include striped equipment
materials, non-humanoid empty-mask handling, weapon duplication, missing model
parts/textures, standing-idle selection, dragon wings, and large-map progress.

- [ ] Complete cancellable asynchronous RES indexing, MOB parsing, and CPU
  decoding while keeping OpenGL creation in bounded GUI-thread batches.
- [ ] Persist resource indexes by archive path, size, modification time, and
  header hash.
- [ ] Add a diagnostics panel for phase timings, shadowed assets, missing
  resources, and decode failures.
- [ ] Cover every MMP format found in the JMV corpus. Unsupported or malformed
  textures display a diagnostic placeholder with asset, archive, format, and
  failure reason.
- [ ] Re-run cold/warm performance acceptance on representative JMV maps and
  keep the post-index target at least 10x faster than original v0.3 where the
  current machine permits it.
- [ ] Resolve applicable humanoid appearance from active-profile database and
  template data, including humans, skeletons, orcs, undead, and other equipped
  humanoids.
- [ ] Render database-equipped armor and weapons.
  - Base scope: one one-handed or one ranged weapon.
  - JMV profile scope: armor and ordinary equipped visuals.
  - JMV stretch: 1H+shield, dual 1H, 2H, dual 2H with perk, and 2H+shield with
    the applicable perk.
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

- [ ] Add original-versus-current and candidate-save-versus-transaction entry
  points to the existing MOB diff.
- [ ] Add the read-only 3D perspective overlay.
  - Removed: translucent red baseline ghost.
  - Added: green candidate.
  - Modified: amber overlaid versions.
  - Moved: old/new markers and a movement vector.
  - Filter unchanged objects, logic paths, and opaque structural changes.
- [ ] Export deterministic JSON in addition to the readable text/details view.
- [ ] Keep ambiguous duplicate-NID matches unresolved rather than guessing.
- [x] Add deterministic file-to-file MOB semantic diff, including embedded
  script and document-section differences. ([`fd30be0`](https://github.com/Ilufus/ei-mapper-endurance/commit/fd30be0), [`0a4624f`](https://github.com/Ilufus/ei-mapper-endurance/commit/0a4624f))

## 0.2.x capability boundaries

- [x] Fingerprint Base and JMV runtime stacks and expose profile capabilities.
  ([`97b6991`](https://github.com/Ilufus/ei-mapper-endurance/commit/97b6991))
- [x] Define profile-scoped appearance, equipment, prototype, semantic
  validation, and map-test provider interfaces. ([`b467d87`](https://github.com/Ilufus/ei-mapper-endurance/commit/b467d87))
- [x] Keep JMV DLL-defined behavior out of the editor process; use a
  fingerprinted isolated runtime provider.
- [ ] Keep direct launch disabled for Base unless a separate Base provider is
  explicitly implemented and validated.

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
- [x] Add resizable outliner/property layouts, filtering, recent files,
  independent shortcuts, and persisted UI state.
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
