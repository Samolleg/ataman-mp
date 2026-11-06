# Rebuild64 display backend for Ataman286

The host embeds the pinned renderer and public typed adapters. It does not
define an editor art profile. The owner cancelled GL visual comparison on
2026-11-06; historical R1/R2 policies are superseded by report section R3.

The recipe `build_backend.ps1` takes SourceRepositoryPath (EIRebuild64),
RendererRepositoryPath (material source), and a new owned BuildRoot under work/.
Use an ASCII/8.3 path here. The source.lock.json pins both archives and patches.
No private game code, game resources, SDK or binaries are committed.

Active patches001–008 provide CPU snapshots, function-level linking, no-NGX
build substitution, readiness inspection, in-memory map loading and explicit
host retirement. New008 joins an embedded neighbour stream before DLL unload;
retail keeps its original stop/detach behavior. None changes shading. Historical
R2 patches008–010 and stock_sky.inc were
removed in R3. The pinned game's E1Terrain/surface path also resolves one mip;
R2's mip patch was not proven an editor-only loss and is not kept.

ABI8 is flat C, copied across one renderer worker thread. QLibrary remains
lazy: ordinary OpenGL startup does not load the DLL. The native presentation
receives only accepted renderer-owned output. PNG capture is its own readback.
Frame readiness observes resource/contact/Terrain2 publication; it does not
rewrite options or require optional specialized shader residency.

Before create, the worker reads Config64/rddraw.ini and settings.ini from the
configured game root and writes isolated copies under its cache. The renderer
may save only those copies. Missing config fails explicitly, not to an editor
quality preset. Only backend/DLSS/Streamline/Reflex are disabled for this
D3D11/no-SDK build. Cache identity is process-local bookkeeping.
Every effective registered renderer option is logged with EFFECTIVE_CONFIG.
All other options, including fog/mist, clouds, Terrain2, PBR and modern liquids,
are loaded by the renderer's normal config reader. No post-grade is applied.

GameInputs reads unchanged copies of Config/Lights[Cave]<region>.ini. An
empty private Maps root supplies the work-directory context also required by
the normal GI reader; it contains no maps and shadows no resource.
The world tuple has the editor's shared camera, selected hour/region and an independent preview
segment clock (.055 seconds/segment), which does not depend on hidden GL paints. Stock sky claim invokes the renderer's normal procedural sky.
The host supplies raw map LIGHT facts; native shading owns attenuation.
Source MPR pages precede swamp classification, and materials retain their
resource identities and map properties.

Limits are explicit: this editor has no running game CWeather/light manager,
scripted lights, live units, terrain wind normalization/phase or fog TL state.
The public adapter exposes their completed runtime facts, not all MOB-to-runtime
constructors. Synthetic GL TORCH colors/radius and LIGHT+2Z were removed;
the missing TORCH constructor contract needs the author. The host cannot claim
complete game-input equality or a matching game frame. See the R3 input table.

OpenGL stays the default. Object input in Rebuild64 uses the same CView handlers;
the host composes a CView GL depth/RGBA service layer. Pending transforms have
a service preview; confirmation restarts the embedded renderer on the same
device and publishes a full immutable document. No working incremental
main-source pose update is claimed. See the286 report for latency and open
DPR/ID/depth criteria. Terrain brushes require OpenGL. Panels and one-shot crash retry
remain. A cache retry latch keeps GL after an acknowledged crash without
rewriting ui/renderer; explicit menu selection clears it. Actual composition
of native panels over a visible D3D window has not been observed in automation;
readback/QWidget::render and input checks do not prove that owner check.

Optional -PrebuiltShaderRoot reuses validated .bin/.res tables only when all
shader producers match. Runtime source is fully rebuilt with every active lock
patch. R3 removes CPU-only patches, changes no shader producer and reuses638
prebuilt programs. It never splices a replacement object into an old library.

Neighbours285: C ABI carries the selected profile's world-map-layouts.json,
the real active terrain resource name and the existing human visibility toggle.
The host publishes the active terrain placement and immutable memory source;
the renderer's normal static-terrain flush starts neighbour_client/neighbour_maps
and adopts neighbour_piece values. No second neighbour renderer or GL placement
conversion is used. Selection remains the pinned renderer's deja-vu layout
selector: same allod/component, intersecting/touching rectangles, layout order.
map.txt is used by the mapper catalog; the native neighbour selector reads the
saved catalog paths/dimensions in world-map-layouts.json, as its public adapter
does. It does not read the mapper's current world-workspace placements.

All graphics options, including neighbour_maps, load_reveal_hold,
neighbour_fog_range/skip, terrain_blend_neighbours, shadow_neighbour_casters and
water_surf_no_sea_maps, stay in the normal file-backed registry. Human visibility
gates neighbour_maps with its source-config value; it never enables a source-
disabled stream. No game loading picture is supplied: native load_reveal_hold
therefore does not hold the main frame; pieces arrive in native frame order.

Map replacement/empty document and pausing the view stop/join the stream and
retire its renderer session on the worker. Embedded-only008 forbids detaching
a worker that could execute unloaded DLL code. The startup marker remains armed
while neighbours are pending, including after a usable main frame; a human
off->on toggle re-arms it. Stream errors are logged as NEIGHBOUR_FAILURE while
the active memory terrain remains usable. C++ decode failures are contained by
the native stream. The marker is recovery, not AV isolation: a native access
violation can still kill this in-process backend; process isolation remains a
separate prerequisite before making this the default.

Unsaved active edits keep the prior memory snapshot path. Neighbours always read
saved MPR/MOB files; another open document's unsaved edits are not visible there.
The renderer owns welding, decoration admission and fog. Units/torches/traps on
neighbours are excluded by its native family policy. Source/render/shader view
parameters are not adjusted against OpenGL; game-frame acceptance is the owner.
