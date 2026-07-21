# Safe character authoring workflow

Use extracted copies only. Never point Blender, ei_figer, a converter, or an
automation output at a Base/JMV install, live `Res`, or live map directory.

## Capability boundary

| Asset | Inspect/import | Edit | Export status |
|---|---|---|---|
| FIG8 geometry/UV/morph extremes | Endurance report + ei_figer | Blender, fixed topology | ei_figer v5.2; require re-open and semantic diff |
| `.mod`/`.lnk` assembly tree | Endurance report + ei_figer | Blender hierarchy/names | ei_figer v5.2; validate one root/no missing parent/cycles |
| ANM channels/morph frames | Endurance report + ei_figer | Blender actions/shapes | ei_figer v5.2; validate counts, closing sample, hierarchy |
| ADB action metadata/masks | Endurance provider/parser | Do not author yet | No verified writer in this workflow |
| MMP/PNT3 atlas | Endurance decoder/viewer | External lossless RGBA editor | No verified general encoder in this workflow |
| RES container | Read-only report / ei_figer | Staged member replacement | ei_figer writes; clean repack needs verified eipacker round trip |

An ei_figer export is a candidate, not proof of compatibility. Its FIG writer
contains a TODO around vertex-component preservation, and its RES append/replace
path intentionally leaves unreachable old data. For zero-morph ANM members, the
v5.2 writer omits an explicit stored zero morph-vertex-count dword; the copied
`cidle01_/lh2.shield00` control would shrink from 1,864 to 1,860 bytes. Its
Blender import sets `frame_end` but not `frame_start`, while export begins at
`frame_start`, so Blender's usual start at 1 can also drop sample 0. Set the
range explicitly, then require a no-op round trip. Do not distribute an export
until the exact asset class passes the comparisons below.

## Portable Blender quick path

The audited investigation workspace is
`work/blender-uv-investigation`, containing Blender 3.6.23, ei_figer probes,
example scenes, UV overlays, and hash records. Launch reproducibly from the repo:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\launch_character_blender.ps1 `
  -Workspace ..\work\blender-uv-investigation `
  -EiFigerSource ..\research\ei_figer
```

Use `-PrepareAddon` only on a fresh/archived `portable-config`; it refuses to
overwrite an existing addon. Use `-BackgroundScript <path>` for deterministic
headless probes. The launcher refuses a workspace containing `game.exe` and
isolates Blender config/scripts/datafiles under that workspace.

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
