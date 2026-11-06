# Rebuild64 display backend for Ataman 283

Only the Ataman host and flat C ABI are built in this repository. Renderer and
typed adapters are extracted from the commits in
`third_party/eirebuild64-renderer/source.lock.json`; they are never vendored here.
The local recipe reads Git objects without changing either source checkout.

```powershell
./tools/rebuild64_renderer/build_backend.ps1 `
  -SourceRepositoryPath <EIRebuild64-clone> `
  -RendererRepositoryPath <owned-renderer-clone> `
  -BuildRoot <new-directory-under-this-checkout-work>
```

Use an ASCII/8.3 path on this Windows machine. The optional `-ShaderStore` points
to an owned cache under `work/`; the upstream shader producer keys bytecode by
the preprocessed program. A fresh table is completed before the DLL is linked.
The resulting `bin/` contains the DLL, material data and original licenses and
notices. Stage them together with the MSVC 14 x64 runtime. No NGX, DLSS,
Streamline or NVIDIA SDK library is used. The replacement backend explicitly
reports DLSS unavailable and creates no SDK worker or writable NGX directory.

The caller loads `ataman_renderer_get_api` with QLibrary and ABI version 4.
All calls for a handle, including destruction, belong to one worker thread.
Inputs are borrowed only for each call and copied before returning. Captures
read the last committed renderer-owned texture. The renderer's internal
swapchain is invisible; the native widget receives a GPU copy only when the
host readiness observer accepts the current generation. It checks actual
material rows, atlas copies and height bake completion rather than RGB values.
There is no upstream general presentable-generation API in the pinned revision;
the observer is a temporary, explicitly pinned host contract, not such an API.

Reviewable patches provide typed terrain publication, function-level linking,
the no-NGX build selection, a read-only readiness observer, immutable memory
inputs to the loader, and configuration retirement between sessions. Generic
retained object parts receive their actual texture resource as material key.
No game classes, simulation, hooks, private engine headers or file save path
are part of this host.

OpenGL remains the default and tool owner. Settings / Map view switches without
restarting. The initial OpenGL path creates no renderer worker and does not
load the DLL. After first use, switching back pauses the warm renderer session;
its caches remain available for a subsequent switch. Documents are still owned
by CView and survive backend failure. Environment variable
`ATAMAN_REBUILD64_TEST_FAILURE` accepts `missing`, `load`, `no-d3d11`, `init`,
or `device-lost` for the isolated failure checks. Captures and validation must
follow the explicit task's offscreen and writable-path boundaries.

Renderer credit: Ilya Rudnev (Ilufus), evil-islands-owned-renderer; embedded
adapters from EIRebuild64. Original notices and third-party licenses travel with
the DLL. Author permission was settled by the owner on 2026-11-06.

Stage 3 host policy (not a change to the renderer's game defaults):

- ABI4 carries the authored sky matrix/tint and existing viewport sky/fog/
  terrain/water/static visibility. The host resolves nask0sky.fig and Sky00
  from the same read-only mounts and uses the shipped fixed-function shader.
  The clear colour also follows the environment below the open sky cylinder.
- sky_clouds=0 selects that stock sky; fog_preset=0 removes modern mist/wisps
  and the added atmospheric fog. draw_distance=1000 is the supported maximum,
  beyond the current GL fog end620; the previous default300 faded top views.
- render_scale=100/upscaler=0 (no SDK), R1 smaa=false matched the unmultisampled GL
  capture in R1. R2 enables standard SMAA after noise/aliasing comparison;
  Laplacian variance is not proof of visual sharpness. anisotropy=4 means16
  samples, material_quality=3 matches the saved
  game quality values. No completed-frame colour/sharpness filter is added.
- Patch008 only extends the embedded config range to the already implemented
  negative cave-light sentinel. cave_brightness=-1 preserves the caller's cave
  light table. Game defaults/ranges are unchanged outside EID3D11_EMBEDDED.
- Existing opaque CViewportWidget panels become native children of the active
  surface; logical geometry and Qt input remain owned by the same widgets.
  Lazy panels are discovered too. Scenario and autosave UI have independent
  parents. GL query/minimap/export paths remain available while hidden.
- startup-<pid>-<uuid>.json in the renderer cache is armed before loading the
  DLL and each document, cleared at an accepted frame and on normal teardown.
  PID plus process creation time distinguish a dead load from another live
  mapper. A stale marker suppresses automatic Rebuild64 selection, shows a
  message and preserves ui/renderer; explicit selection clears stale markers.
  This prevents startup crash loops; it cannot contain an in-process AV.

The isolated stage3 harness uses no owner profile, visible HWND, desktop capture
or ProgramData lock store. Diagnostic DLL fault/config exports exist only in
work, never in the production package. See the stage3 validation report for
remaining quality limits and the proposed process/tool migration stages.

R2 host corrections (283 remains uninstalled):

- ABI4 copies up to64 live scene point lights from the same LIGHT/TORCH inputs,
  including visibility, frustum, preview positions, color normalization, radius
  and shadow eligibility. The retained scene's ordinary world-light publication
  supplies the renderer; no game object lookup or shader special case is added.
- The interrupted startup is acknowledged once. A cache retry latch keeps GL
  on later starts without repeating the message or rewriting ui/renderer. An
  explicit menu selection of either backend clears both stale markers and the
  latch; a new interrupted load is a new crash, with one new message.
- Populate TerrainCpuAsset::pages before swamp-frame classification, as the
  source loader does. A GPU atlas alone does not provide the classifier's art.
- Document feature bits no longer override terrain_blend=0. The editor keeps
  authored atlas transitions; water_simple=true uses authored liquid tile art
  and opacity, avoiding modern SSR/refraction/foam. material_terrain=false avoids
  terrain PBR highlights from the stock cave's fixed diffuse light direction.
  These are native renderer options; no finished-frame colour/sharpness grade.
- build_backend.ps1 -PrebuiltShaderRoot may reuse validated .bin/.res files
  while rebuilding all runtime vendor objects from the pinned archive and every
  lock patch. Caller evidence must establish unchanged shader producers and
  record the reused hashes. R2 uses this path, with no single-object lib splice.
- Panel tests use renderer readback + QWidget::render and native parent/input
  checks. Nobody has observed their actual composition over a visible D3D
  window in these tests; that remains owner verification.

R2 patches009/010 close two further ingress defects: a Terrain2 table entry
was advertised even with terrain_blend=0, and completed GPU atlas surfaces were
forced to one mip and copied only at level0. Patch009 gates the CPU table index;
patch010 publishes E1SurfaceMipCount and copies every declared authored mip.
Existing one-level producers keep their old behaviour. No shader source changes.
The readiness observer ignores a disabled Terrain2 bake and still waits for
contact-atlas copies. The complete general shader is valid while optional
specialised optimisations build; their residency must not freeze a frame.
R2 resource/SRV descriptor inspection confirms12 levels, not just the host's
initial texture descriptor. Laplacian growth in R1 included this aliasing.
