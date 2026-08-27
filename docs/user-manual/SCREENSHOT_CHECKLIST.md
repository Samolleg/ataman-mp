# Current-UI screenshot checklist

No repository screenshot showed the current Endurance UI at the documented
baseline. The embedded images in the legacy upstream EI Mapper manuals show an
older interface and several obsolete bindings, so they were used only as
organization/style references and were not copied into this manual.

The two included SVGs are conceptual workflow diagrams, not fabricated UI.
Pictures may remain English in both manuals. When an authorized current build
and disposable copied maps are available, capture the screens below. Never use
a live installation/corpus map as the mutation target merely to make a manual
image.

## Capture rules

- Use the exact documented release and a disposable/synthetic or explicitly
  approved copied map.
- Capture the application window only; exclude desktop, taskbar, account name,
  private paths, logs, run IDs, hashes, dump names, and unrelated applications.
- Use the English UI so one safe image can serve both manuals.
- Before committing, crop to the relevant window/panel, inspect at 100%, strip
  metadata, and verify no source/map/resource payload is being redistributed.
- Do not launch the editor or game solely for this documentation task without
  authorization. Do not capture proprietary manual pages.
- Save web-friendly PNG at native scale under `assets/screenshots/`; add useful
  alt text and record the exact source build/map class below.

## Priority captures

| ID | Screen and state | Why it helps | Sensitive-data checks |
|---|---|---|---|
| UI-01 | Main workspace with Loaded Map, Object Outliner, Object Properties, validation and collapsed Visuals | Establishes the layout and active-MOB boundary | Neutral synthetic object names; no paths/log |
| TERR-01 | Landscape Tools on Paint land tiles showing Modern, Classic Auto and Manual controls | Shows the three different atlas-ownership modes | Synthetic/template map; no proprietary texture detail if redistribution is not approved |
| TERR-02 | Modern Draft versus Finalize save choice, with Resolve Missing Blends visible | Makes the paired-publication boundary concrete | Destination path cropped; atlas counts may remain |
| OVER-01 | Existing/Add-Remove/Sculpt overlay tools and MPR surface-material selection | Separates overlay coverage, texture and material ownership | Use synthetic colored tiles if available |
| MAT-01 | MPR Materials table plus one animation/flipbook record | Shows shared slot and contiguous-frame controls | No private resource namespace/path |
| OBJ-01 | Behavior mode for a synthetic unit with Logic 0–4, patrol and look points | Clarifies logic-slot ownership | Generic IDs/names only |
| TRAP-01 | Magic trap properties and read-only Will attack table | Connects diplomacy to derived targets | Generic group labels |
| VALID-01 | MOB Family Validation with one synthetic duplicate and selected object navigation | Shows findings versus repair | Synthetic paths only; crop provenance columns if necessary |
| GRAPH-01 | AI navigation graph widget with loaded/generated selector and comparison | Shows diagnostic versus persisted source | Generic copied/synthetic map; no filesystem source label |
| MINI-01 | Viewport minimap and modeless Export Minimap comparison/grade controls | Shows navigation, lock, generated/embedded ownership | Use a redistribution-safe map/image |
| TEST-01 | Test Current Map preflight with entrance and Play from Here controls | Shows dependency/owned-run boundary | Crop installation/staging paths and hashes |
| DB-01 | Database Editor on a synthetic table plus Pending Database Changes | Shows typed grid, references and transaction review | No proprietary row payload; use synthetic fixture |
| RES-01 | Resource Browser with Winning/Overridden chain and a safe synthetic image preview | Shows provider precedence and reviewed actions | Synthetic RES names and paths only |
| PREF-01 | General language/autosave settings and Keybindings page | Documents restart and customization | Crop configured paths/external editor |

## Recording table

Fill this only after an image is authorized and added.

| ID | File | Endurance version/commit | Source fixture/map class | Redaction/metadata review | Manual sections |
|---|---|---|---|---|---|
| — | — | — | — | — | — |
