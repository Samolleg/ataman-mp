# Safe character authoring workflow

Use extracted copies only. Never point Blender, ei_figer, a converter, or an
automation output at a Base/JMV install, live `Res`, or live map directory.

## Capability boundary

The authoring workflow has two isolated Blender lanes. Do not share preferences,
installed add-ons, startup files, or candidate archives between them.

| Lane | Blender/add-on | Primary assets | Export status |
|---|---|---|---|
| Item | Blender 3.5.1 + konstvest/ei_figer 5.2 plus the audited repository patch | Standalone inventory weapons, shields, armor and item/world figures | Primary item writer; require fresh re-open and semantic diff |
| Humanoid/JMV | Blender 4.5.12 LTS (supported family 4.2-4.9) + alex-khod/ei_figer 6.10 at `a7eb929` | Humans, wearable assemblies, JMV hierarchy/UV rules; animations are secondary | Humanoid writer; experimental item writer only with the audited patch and fixtures |

For **every import from copied JMV game resources**, select the add-on's literal
`JABAIS_VOUX` option **before import**. This is the import setting, not merely an
export preference or a mode reserved for newly authored JMV assets. It changes part classification, UV
unpacking, UV bases, and JMV weapon/armor families. It is fixture provenance,
not a mode inferred from a mesh name. Base assets use `VANILLA` unless direct
evidence says otherwise.

| Asset | Inspect/import | Edit | Export status |
|---|---|---|---|
| FIG8 geometry/UV/morph extremes | Endurance report + lane-specific ei_figer | Blender, fixed topology | Both lanes; use expanded-corner semantic comparison because legal UV/component reindexing is not byte-stable |
| `.mod`/`.lnk` assembly tree | Endurance report + lane-specific ei_figer | Blender hierarchy/names | Validate one root/no missing parent/cycles and canonical attachments |
| ANM channels/morph frames | Endurance report + ei_figer | Blender actions/shapes | Secondary; not a geometry/item acceptance gate yet |
| ADB action metadata/masks | Endurance provider/parser | Do not author yet | No verified writer in this workflow |
| MMP/PNT3 atlas | Endurance decoder/viewer | External lossless RGBA editor | No verified general encoder in this workflow |
| RES container | Read-only report / ei_figer | Staged member replacement | ei_figer writes; clean repack needs verified eipacker round trip |

An ei_figer export is a candidate, not proof of compatibility. Stock 5.2 is not
a safe item writer: it discards one field of every FIG vertex-component triple,
forces the normal index to equal the vertex index, decodes component-major
normal blocks as sequential vectors, and overruns Blender loops when a shipped
FIG has an incomplete one/two-index tail. Apply
`patches/konstvest-ei_figer-5.2-character-pipeline.patch`; it preserves all
three indices, deswizzles normal blocks, retains imported split-normal tables,
and explicitly omits only indices that cannot form a triangle. Use the
independent report/modern reader for final comparison. Both RES append/replace paths leave
unreachable old data. For zero-morph ANM members, the v5.2 writer omits an
explicit stored zero morph-vertex-count dword; the copied
`cidle01_/lh2.shield00` control would shrink from 1,864 to 1,860 bytes. Its
Blender import sets `frame_end` but not `frame_start`, while export begins at
`frame_start`, so Blender's usual start at 1 can also drop sample 0. Set the
range explicitly, then require a no-op round trip. Do not distribute an export
until the exact asset class passes the comparisons below.

## Portable Blender quick path

The old `work/blender-uv-investigation` Blender 3.6.23 scenes (including the
humanoid examples with the normalized/full-image UV layout) predate the exact
PNT3 decoder, the `JABAIS_VOUX` import requirement, and current assembly rules.
They are historical evidence, not authoring fixtures, and must not be used as
UV-placement references. Prepare the two current lanes from downloaded official ZIPs:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\prepare_character_blender.ps1 `
  -Lane item -Workspace ..\work\character-blender-pipeline `
  -AddonSource ..\research\ei_figer `
  -DownloadCache ..\work\character-blender-pipeline\downloads

powershell -ExecutionPolicy Bypass -File scripts\prepare_character_blender.ps1 `
  -Lane humanoid -Workspace ..\work\character-blender-pipeline `
  -AddonSource ..\work\alex-khod-ei_figer-pipeline `
  -DownloadCache ..\work\character-blender-pipeline\downloads
```

Archive names, official URLs, and SHA-256 values are pinned in
`scripts/character_blender_lanes.json`. The setup writes a per-lane tool
provenance report and never overwrites an installed add-on unless
`-RefreshAddon` is explicit. Launch a lane reproducibly:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\launch_character_blender.ps1 `
  -Lane item -Workspace ..\work\character-blender-pipeline `
  -EiFigerSource ..\work\character-blender-pipeline\item-lane\portable-config\scripts\addons\ei_figer
```

Use `-PrepareAddon` only on a fresh/archived `portable-config`; it refuses to
overwrite an existing addon. Use `-BackgroundScript <path>` for deterministic
headless probes. The launcher refuses a workspace containing `game.exe` and
isolates Blender config/scripts/datafiles under that lane.

The modern add-on at upstream `a7eb929` needs
`patches/alex-khod-ei_figer-6.10-character-pipeline.patch` for deterministic
headless import, correct FIG normal decoding, incomplete-triangle tolerance,
and semantically correct standalone-item bounds. The patch makes
UI context optional in background mode, restores Euclidean bounding radius,
and applies the part-name bounds convention independently of model-level
header/UV classification and maps archive-prefixed standalone BON members back
to local Blender part names. It also assigns authored per-corner FIG normals as
Blender split normals. Its validated research commit is `4d03453`. It does not
claim general modern item-export support.

Current controls prove:

- Blender 3.5.1 + 5.2 imports and exports `initwedg8`; its no-op changes normals
  by at most `1.82e-5`, while geometry, UV mapping, hierarchy and bounds agree;
- patched Blender 4.5.12 + 6.10 imports the complete JMV `unhuma` tree in
  `JABAIS_VOUX` mode (6,232 base/morph mesh objects in the control);
- the compact `unhuma`/armor24 inspection scene imports copied JMV data with a
  part mask, preserves authored atlas UVs and split normals across a fresh
  re-open, and records `JMV` -> `JABAIS_VOUX` plus the source RES hash in the
  scene. It has no composed material/atlas yet and is therefore a geometry/UV
  inspection fixture, not a corrected textured demo. A full 6,232-object import
  with the new custom-normal assignment crashed Blender 4.5.12 late in import;
  that unresolved scalability/control failure must pass before the new normal
  path is declared safe for complete-family imports;
- patched 6.10 no-op exports of `initwedg8` and `initarhl21` preserve expanded
  per-corner position, normal and UV semantics and bounding volumes, while
  legally reindexing UV/component tables and appending replacement RES data.
- JMV shield `initwesd14weapon.fig` contains 2,944 indices: 2,943 make 981
  complete triangles and tail component index `877` is undrawable. The patched
  5.2 no-op drops only that tail; all drawable corner positions, normal values,
  vertex references, and bounds remain exact. Sixty-four UV components round by
  at most `2.98e-8`. Against that repaired no-op, the controlled edit changes
  exactly one corner U by `0.0009765625`, changes no drawable position, normal,
  topology, or bound, and survives a fresh Blender re-open with matching hashes.
- JMV armor `initarhl21` attachment control moves local part `armor` by `0.01`
  on Z. Export changes exactly the Z coordinate in all eight 96-byte BON morph
  positions; FIG mesh hashes remain unchanged, and a fresh patched 5.2 import
  restores `0.009999999776482582`. Stock 5.2 silently left the object at zero
  because it tried to apply archive name `initarhl21armor` to local object
  `armor`; both repository patches correct that standalone-name mapping.

The patched 5.2 importer stores the original normal table and per-corner normal
indices in `ei_fig_normal_table_json` and `ei_fig_normal_index`. Keep those for
UV/material/attachment edits. A same-topology geometry edit therefore preserves
the authored source normals deliberately; delete or replace that provenance only
as part of an explicit normal-authoring operation, then validate lighting in a
disposable game profile. Automatic normal regeneration is not yet an acceptance
gate.

Use `character_model_blender_fixture.py` for isolated import/export controls,
`character_blend_scene_audit.py` for a read-only fresh-reopen scene audit, and
`character_fig_semantic_diff.py` for expanded-corner FIG comparison.

## Manual edit procedure

1. Copy the exact source archive/member into an immutable `inputs/` tree. Record
   SHA-256, source profile, archive/member name, extraction tool/version, and
   database/MOB references.
2. Run `scripts/character_asset_report.py INPUT --output STAGE/baseline.json`.
   Preserve the baseline report beside the staged work.
3. Import the copied model and, if required, its animation in portable Blender.
   Preserve exact object/component names, one authored parent tree, texture
   selector, UV coordinates, triangle winding, and vertex ordering.
4. Geometry edits may move vertices. Do not add/delete/reorder vertices across
   morph extremes or ANM morph channels. Do not normalize atlas UVs.
5. For equipment, author a numbered member under the canonical `*00` attachment
   node. Do not bake the attachment rotation into both node and geometry.
6. For animation, keep 15 Hz logical timing, a closing loop sample, EI
   child-before-parent rotation composition, root-relative translation, and
   morph frame/vertex counts synchronized with the FIG part.
7. Export into a new `candidate/` archive. Never append to the immutable input.
8. Re-open the candidate in a fresh Blender process and run the report again.
   Compare tree, names, header counts, UV bounds/values, indices, selectors,
   channel counts, frame endpoints, and every untouched member hash.
9. Inspect composed texture and per-part UVs in Endurance. Test visual and picking
   poses. Then use a disposable profile/game package for `drawall`, idle frames,
   distance/orbit, save/load, unload/reload, and graceful exit.
10. Package only staged deltas, reports, attribution, and rollback instructions.

## Automated procedure and report contract

The report script accepts files/directories, recursively hashes them, inventories
RES members, and structurally parses known envelopes. It never extracts or
rewrites and requires explicit JSON output outside all inputs:

```powershell
python scripts\character_asset_report.py EXTRACTED_COPY `
  --output STAGE\character-assets.json
```

For a deterministic build, create `inputs/`, `candidate/`, `reports/`, and
`rollback/` siblings. The build manifest must include tool hashes, command line,
provider/profile, ordered inputs, output SHA-256, semantic comparison result,
known diagnostics, and manual controls still outstanding. Re-run from immutable
inputs and require byte-identical candidate/report output when timestamps are not
part of the target format.

## Validation and rollback

Minimum automated gates:

- parser reaches the exact expected end or explains known trailing bytes;
- one assembly root, all parents present, no cycles;
- unchanged member hashes identical;
- FIG topology/counts/selectors and UV domain intentionally changed or equal;
- ANM counts valid, morph vertices match the target FIG, loop endpoints reviewed;
- candidate re-opens in a fresh ei_figer/Blender session;
- provider incompatibilities remain visible diagnostics, never aliases.

Known zero-morph ANM byte differences or a missing first sample fail the no-op
gate. Patch and fixture-test the writer before proceeding; do not waive them as
container noise.

Installation is a separate, user-authorized step. Before it, close the game,
hash the exact destination, and copy it to `rollback/` outside the replacement
tree. Install only the intended staged files. Rollback restores those saved files
and verifies their original SHA-256. Never call an extracted research copy an
original backup.
