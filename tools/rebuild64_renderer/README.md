# Rebuild64 display backend for Ataman283, R3

The host embeds the pinned renderer and public typed adapters. It does not
define an editor art profile. The owner cancelled GL visual comparison on
2026-11-06; historical R1/R2 policies are superseded by report section R3.

The recipe `build_backend.ps1` takes SourceRepositoryPath (EIRebuild64),
RendererRepositoryPath (material source), and a new owned BuildRoot under work/.
Use an ASCII/8.3 path here. The source.lock.json pins both archives and patches.
No private game code, game resources, SDK or binaries are committed.

Active patches001–007 provide CPU snapshots, function-level linking, no-NGX
build substitution, readiness inspection, in-memory map loading and explicit
host retirement. None changes shading. Patches008–010 and stock_sky.inc were
removed in R3. The pinned game's E1Terrain/surface path also resolves one mip;
R2's mip patch was not proven an editor-only loss and is not kept.

ABI5 is flat C, copied across one renderer worker thread. QLibrary remains
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

OpenGL stays the default and editing backend. Panels and one-shot crash retry
remain. A cache retry latch keeps GL after an acknowledged crash without
rewriting ui/renderer; explicit menu selection clears it. Actual composition
of native panels over a visible D3D window has not been observed in automation;
readback/QWidget::render and input checks do not prove that owner check.

Optional -PrebuiltShaderRoot reuses validated .bin/.res tables only when all
shader producers match. Runtime source is fully rebuilt with every active lock
patch. R3 removes CPU-only patches, changes no shader producer and reuses638
prebuilt programs. It never splices a replacement object into an old library.
