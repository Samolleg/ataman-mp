# EI Mapper Endurance manual coverage inventory

This inventory is the authoring regression for the English and Russian user
manuals. It records the implemented surface verified at repository commit
`596327e509f4cee5a4072a5a3a5cda7b425d09bd`. Every row must resolve to the same
anchor in both manuals. `Current`, `Limitation`, and `Advanced` identify normal
end-user capability, a boundary that must be stated, and an expert diagnostic
or ownership operation respectively.

The validation script fails when an anchor is missing, the English and Russian
anchor orders differ, or a row is left without a classification or evidence.

| ID | Class | Implemented surface or required boundary | Primary evidence | Manual anchor |
|---|---|---|---|---|
| SAFE-01 | Current | Preservation-first authoring; open/inspect does not repair; unsupported mutations fail closed | `docs/PROJECT_CONTRACT.md`; `Readme.md` | `scope-and-safety` |
| MODEL-01 | Current | MPR terrain, MOB objects, map registration, resources, database, and AI graph are separate documents/owners | `docs/PROJECT_CONTRACT.md`; `source/app/main_window.cpp` | `mental-model` |
| SETUP-01 | Current | First-run game/mod setup, Base and Jamais Vu profiles, provider priority, profile validity | `source/ui/resource_setup_dialog.cpp`; `source/resources/resource_profile.cpp`; `docs/PROFILE_PROVIDERS.md` | `install-and-profile` |
| UI-01 | Current | Main viewport, Loaded Map, Object Outliner, Object Properties, validation, log, resources, landscape docks | `source/app/main_window.ui`; `source/app/main_window.cpp` | `interface-tour` |
| MAP-01 | Current | Open MPR/MOB independently, load collections, active editable MOB versus visible locked MOBs | `source/app/main_window.cpp`; `Readme.md` | `maps-and-collections` |
| MAP-02 | Current | Map Catalog relation and explicit registration boundary | `source/app/main_window.cpp`; `source/ui/map_catalog_editor.cpp` | `maps-and-collections` |
| NEW-01 | Current | New MPR from proven template, dimensions, heightmap, atlas/material inheritance, optional overlay | `source/app/main_window.cpp` (`on_actionNew_MPR_triggered`) | `create-mpr` |
| NEW-02 | Limitation | New MPR does not create a MOB or register the map | `source/app/main_window.cpp` | `create-mpr` |
| NEW-03 | Current | New empty primary/supplemental MOB, ID range, selected metadata, optional diplomacy | `source/app/main_window.cpp` (`on_actionNew_MOB_triggered`) | `create-mob` |
| NEW-04 | Limitation | New MOB never clones objects, VSS, editor directories, or an embedded AI graph | `source/app/main_window.cpp` | `create-mob` |
| SAVE-01 | Current | Active MOB, all MOBs, MPR, Save As, staged reopen, semantic validation, atomic publication | `source/app/main_window.cpp`; `docs/PROJECT_CONTRACT.md` | `save-backup-recovery` |
| SAVE-02 | Current | Open, pre-save, periodic autosave generations; save refusal when the safety backup fails | `source/app/backup_manager.*`; `Readme.md` | `save-backup-recovery` |
| SAVE-03 | Current | Unclean-session recovery notice and manual recovery from the backup folder | `source/app/main_window.cpp`; `source/app/backup_manager.*` | `save-backup-recovery` |
| VIEW-01 | Current | WASD/arrows, Q/E, Shift speed, wheel, middle-drag orbit, reset and focus | `source/render/view.cpp`; `source/ui/settings_dialog.cpp` | `viewport-camera` |
| SELECT-01 | Current | Click/rubber-band selection, Shift toggle, active MOB edit boundary | `source/render/view.cpp` | `selection-transform` |
| XFORM-01 | Current | G/R/T transforms, X/Y/Z constraint, Shift+axis plane, numeric entry, accept/cancel | `source/render/view.cpp` | `selection-transform` |
| EDIT-01 | Current | Undo/redo/history, clipboard, duplicate placement, hide/unhide/delete | `source/render/view.cpp`; `source/app/main_window.cpp` | `history-clipboard` |
| EDIT-02 | Limitation | Save and graph-ownership commits establish a new baseline and are not Edit > Undo operations | `source/app/main_window.cpp`; `source/documents/mob_document.cpp` | `history-clipboard` |
| UI-02 | Current | Outliner filters, selection synchronization, properties, multi-selection and active-document context | `source/render/view.cpp`; `source/ui/object_outliner.*` | `outliner-properties` |
| OBJ-01 | Current | Create/copy/paste/duplicate/delete static and dynamic MOB records while preserving unknown fields | `source/render/view.cpp`; `docs/OBJECT_PROPERTY_TERMINOLOGY.md` | `object-authoring` |
| OBJ-02 | Current | Common identity, class, model, script, position/rotation/scale, parent and flags | `docs/OBJECT_PROPERTY_TERMINOLOGY.md`; `source/ui/object_property_model.cpp` | `object-authoring` |
| UNIT-01 | Current | Unit database prototype versus MOB overrides, equipment/model parts, character preview | `docs/OBJECT_PROPERTY_TERMINOLOGY.md`; `docs/HUMANOID_VISUAL_RUNTIME_VALIDATION.md` | `units-equipment` |
| UNIT-02 | Limitation | Empty model-parts list means all parts and can crash for incompatible assemblies | `docs/OBJECT_PROPERTY_TERMINOLOGY.md` | `units-equipment` |
| BEHAV-01 | Current | Object/Behavior mode; all five unit logic slots; default and alarm slot meanings | `source/render/view.cpp`; `docs/OBJECT_PROPERTY_TERMINOLOGY.md` | `behavior-logic` |
| BEHAV-02 | Current | Stay, Guard area, Patrol, Sentry, Player/script controlled, Alarm guard and aggression | `docs/OBJECT_PROPERTY_TERMINOLOGY.md` | `behavior-logic` |
| BEHAV-03 | Current | Per-MOB diplomacy editor and trap target preview; directional Ally/Neutral/War cells | `source/ui/diplomacy_editor.*`; `docs/releases/0.2.1-alpha.28-trap-target-review.md` | `behavior-logic` |
| PATH-01 | Current | Patrol points, look points, wait ticks, direction/flags and logic-slot ownership | `docs/OBJECT_PROPERTY_TERMINOLOGY.md`; `source/render/view.cpp` | `patrol-look-traps` |
| TRAP-01 | Current | Lever, magic trap, floor trap, torch, guard point fields and safe editing boundaries | `docs/OBJECT_PROPERTY_TERMINOLOGY.md`; EI modding `mob-object-fields.md` | `patrol-look-traps` |
| FX-01 | Current | Lights, particles and sounds; helper visualization versus rendered/effect channels | `docs/OBJECT_PROPERTY_TERMINOLOGY.md`; `source/render/view.cpp` | `lights-particles-sounds` |
| VIS-01 | Current | Visuals channel groups, lighting modes, entrances, helpers, fog, shadows, skybox, freeze animation, time | `source/render/view.cpp`; `source/ui/settings_dialog.cpp` | `visual-channels` |
| VIS-02 | Limitation | Some Full environment channels require the desktop renderer; caves suppress the skybox | `source/render/view.cpp` | `visual-channels` |
| TERR-01 | Current | MPR/MP/SEC ownership, fixed vertex/tile relationships, raw height and maxZ | `docs/MPR_SETTINGS_AND_HEIGHT_RANGE.md`; `source/documents/mpr_document.cpp` | `terrain-model` |
| SCULPT-01 | Current | Raise/lower, smooth, flatten and noise brushes; sampling, radius, strength and one-stroke history | `source/render/view.cpp`; `source/ui/landscape_tools.cpp` | `sculpt-terrain` |
| PAINT-01 | Current | Modern, Classic Auto and Manual terrain painting with Z/X/C, sampling and rotation | `source/ui/landscape_tools.cpp`; `docs/MODERN_TERRAIN_EDITOR_MVP.md` | `terrain-paint-modes` |
| PAINT-02 | Limitation | Classic Auto does not extend the atlas; Manual writes exact physical slot/rotation | `source/ui/landscape_tools.cpp`; `docs/TERRAIN_ATLAS_REVIEW.md` | `terrain-paint-modes` |
| OVER-01 | Current | Existing-overlay paint, add/remove surface coverage and overlay-height sculpt | `source/ui/landscape_tools.cpp`; `docs/validation/OVERLAY_AUTHORING_R2.md` | `overlay-authoring` |
| OVER-02 | Limitation | Add requires an existing compatible overlay atlas family and surface material; land and overlay ownership stay separate | `source/ui/landscape_tools.cpp`; `translations/ei_mapper_ru.ts` | `overlay-authoring` |
| MAT-01 | Current | MPR surface-material slots, shared usage, type/color/opacity/illumination/wave/stored warp | `source/render/view.cpp`; `docs/MATERIALS_EDITOR_PLAN.md` | `mpr-materials` |
| MAT-02 | Current | Contiguous surface flipbooks, animation range validation and repack/remap boundary | `source/render/view.cpp`; `source/documents/mpr_document.cpp` | `mpr-materials` |
| MAT-03 | Limitation | Surface material is not terrain texture; stored warp speed is not a free runtime speed control | `docs/MATERIALS_EDITOR_PLAN.md`; `source/render/view.cpp` | `mpr-materials` |
| ATLAS-01 | Advanced | Profile semantic Material atlas metadata, page bindings, slot/corner assignment and local overrides | `source/ui/material_atlas_editor.*`; `docs/TERRAIN_ATLAS_REVIEW.md` | `material-atlas` |
| ATLAS-02 | Limitation | Material atlas editor owns metadata, not texture-page imagery or MPR material records | `source/ui/material_atlas_editor.*`; `docs/research/semantic-atlas-profile-contract.md` | `material-atlas` |
| PUB-01 | Current | Plain MPR save versus Modern Draft pair versus Finalize and Save As | `source/app/main_window.cpp`; `docs/validation/MODERN_TERRAIN_MAIN_INTEGRATION.md` | `draft-finalize-resolve` |
| PUB-02 | Current | Resolve Missing Blends scans ground and overlays, preserves disputed corners, creates one undo record | `source/ui/landscape_tools.cpp`; `ROADMAP.md` | `draft-finalize-resolve` |
| PUB-03 | Limitation | 0.5 RC Lava + Resolve Blends / Shift+R route-owner limitation | `ROADMAP.md` | `draft-finalize-resolve` |
| LIMIT-01 | Limitation | Runtime-validated atlas support through 4,096 slots / 64 pages; >4,096 unsupported; 8,192 address-space crash | `ROADMAP.md`; `docs/validation/EXPANDED_TERRAIN_ATLAS_RUNTIME.md` | `atlas-boundaries` |
| LIMIT-02 | Advanced | Packed format can encode more slots than the supported runtime boundary | `ROADMAP.md`; `docs/validation/EXPANDED_TERRAIN_ATLAS_RUNTIME.md` | `atlas-boundaries` |
| VALID-01 | Current | MOB family and map validation panel, complete report, double-click/context navigation | `source/ui/validation_panel.*`; `docs/releases/0.2.1-alpha.7-validation-ux.md` | `validation-repair` |
| VALID-02 | Advanced | Explicit NID collision review/repair is limited, selected and undoable; never automatic | `docs/releases/0.2.1-alpha.7-validation-ux.md`; `source/ui/nid_collision_repair_dialog.*` | `validation-repair` |
| GRAPH-01 | Current | Loaded embedded/external graph inspection, eight profile layers and N overlay | `source/render/view.cpp`; `docs/validation/AI_GRAPH_PROFILE_POLICY.md` | `ai-graph` |
| GRAPH-02 | Advanced | Regenerated diagnostic preview, loaded/generated diff, auto rebuild after committed relevant edits | `source/render/view.cpp`; `docs/validation/AI_GRAPH_TERRAIN_POLICY_R47.md` | `ai-graph` |
| GRAPH-03 | Current | Selected graph is staged by Test/Hot Reload | `source/render/view.cpp`; `source/app/main_window.cpp` | `ai-graph` |
| GRAPH-04 | Advanced | Export sibling `.grp`, bake into primary MOB, or switch ownership after validation | `source/app/main_window.cpp` (`persistAiGraph`) | `ai-graph` |
| GRAPH-05 | Limitation | Embedded primary-MOB graph has runtime priority; bake/switch saves immediately and is not undoable | `source/app/main_window.cpp` | `ai-graph` |
| MINI-01 | Current | Embedded/generated viewport minimap, collapse/size/lock/regenerate, camera footprint and navigation | `source/app/main_window.cpp`; `docs/releases/0.3.0-alpha.26-minimap-polish.md` | `minimap` |
| MINI-02 | Current | Modeless export review, grading, linked comparison, RES export and Test staging | `docs/releases/0.3.0-alpha.19-minimap-generation.md` through `0.3.0-alpha.26-minimap-polish.md` | `minimap` |
| MINI-03 | Limitation | MPR-only captures omit MOB geometry; briefing entries suppress minimap operations | `docs/releases/0.3.0-alpha.26-minimap-polish.md`; `source/app/main_window.cpp` | `minimap` |
| TEST-01 | Current | Validated Test Current Map, entrance selection, Play from Here, Godmode and time override gates | `docs/TEST_CURRENT_MAP_DEPENDENCIES.md`; `source/app/main_window.cpp` | `test-current` |
| TEST-02 | Current | Ctrl+F5 hot reload and owned-process restart when atlas identity/layout changes; Shift+Esc stop | `source/app/main_window.cpp`; `ROADMAP.md` | `test-current` |
| TEST-03 | Limitation | Run-local staging only; exact profile/dependency/hash gates; editor controls only its own test process | `docs/TEST_CURRENT_MAP_DEPENDENCIES.md` | `test-current` |
| DB-01 | Current | Profile-aware editor for all 26 database tables and schema-described fields | `docs/DATABASE_EDITOR_VERTICAL_SLICE.md`; `docs/releases/0.3.0-alpha.26-full-database-editor.md` | `database-editor` |
| DB-02 | Current | Typed/list/structured/hex editing, clone/delete, undo/redo, references, validation, review | `source/ui/database_editor_dialog.cpp`; `docs/DATABASE_EDITOR_VERTICAL_SLICE.md` | `database-editor` |
| DB-03 | Current | XLSX interchange, Save As/Apply with immutable-source minimal patch and reopen validation | `docs/releases/0.3.0-alpha.26-full-database-editor.md`; `source/documents/database_document.*` | `database-editor` |
| DB-04 | Current | Item and spell constructors share the database transaction | `source/ui/database_editor_dialog.cpp`; `source/ui/database_constructor_dialog.*` | `database-editor` |
| DB-05 | Limitation | Descriptor-unknown Hex meaning is not claimed; structured values use line-based editor and compact grid previews | `docs/DATABASE_EDITOR_VERTICAL_SLICE.md`; `docs/releases/0.3.0-alpha.26-full-database-editor.md` | `database-editor` |
| RES-01 | Current | Active-profile and external archive browse, search/filter, winning/overridden provenance and previews | `source/ui/resource_browser.cpp`; `docs/releases/0.5.0-beta-database-resource-foundation.md` | `resource-browser` |
| RES-02 | Current | Export, add, replace, rename, delete through reviewed RES Save As transaction | `source/ui/resource_browser.cpp` | `resource-browser` |
| RES-03 | Limitation | Base archives are read-only; `database.res` routes to Database Editor; rename/delete reference coverage is incomplete | `source/ui/resource_browser.cpp`; `docs/DATABASE_EDITOR_VERTICAL_SLICE.md` | `resource-browser` |
| PREF-01 | Current | General, render, keybindings and advanced legacy-resource settings | `source/ui/settings_dialog.cpp` | `preferences-language` |
| PREF-02 | Current | English/Russian UI selection and restart requirement; logs/diagnostics remain English | `docs/RUSSIAN_LOCALIZATION.md`; `translations/ei_mapper_ru.ts` | `preferences-language` |
| KEY-01 | Current | Current default file, camera, edit, transform, landscape, graph and test shortcuts | `source/app/main_window.cpp`; `source/render/view.cpp`; `source/ui/settings_dialog.cpp` | `shortcuts` |
| TROUBLE-01 | Current | Logs, workflow evidence, backup/test-run folders and bug-report privacy | `docs/BUG_REPORTING.md`; `source/app/main_window.cpp` | `troubleshooting` |
| PRACTICE-01 | Current | Copy-first mutation, profile separation, small changes, validate/reopen/test, provenance-safe reporting | EI modding `SKILL.md`; `docs/PROJECT_CONTRACT.md`; `docs/BUG_REPORTING.md` | `safe-authoring` |
| BOUND-01 | Limitation | Implemented versus known limitation versus advanced/internal diagnostic labeling | `ROADMAP.md`; current UI/source inventory | `limitations-diagnostics` |
| TERM-01 | Current | User-facing Russian terms match the finished TS catalog; serialized tokens and raw names are not translated | `translations/ei_mapper_ru.ts`; `docs/RUSSIAN_LOCALIZATION.md` | `terminology` |

## Deliberately excluded roadmap items

Unchecked roadmap wishes are not treated as available. In particular, the
manual does not promise an association editor, automatic script-reference
rewrites, arbitrary third-party profile plugins, or private-source parity.
Where a present UI exposes a diagnostic or ownership operation, it is labeled
as such and tied to the current source implementation above.
