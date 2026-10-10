# Terrain bake research prototype

Independent offline tool for `EI-TERRAIN-BAKE-RESEARCH-0`. It is not referenced
by the mapper project and contains no game assets. It reads configured sources;
it does not save MPR, modify RES or launch the editor/game. Generated output must
stay in the ignored `build-tests-terrain-bake-research-0` directory.

`export.cpp` links the existing MPR decoder, MMP decoder, exact semantic profiles
and in-memory corner-field recovery. Resource ordering uses the project's
`orderedResourceSources`, including the mod's `config.ini` overrides. Physical
atlas size is `textureCount * 64`, distinct from MP's terrain-class count. It
loads exact embedded profiles and authored overrides. A namespace/slot-layout
fallback is explicitly flagged as **HYPOTHESIS**, never as a pixel-bound profile.

Build this tool in that private directory using the retained Qt 5.12.12 MinGW
kit and `qmake ../tools/terrain_bake_prototype/export.pro`, then `mingw32-make`.
No new dependencies are required. Supply `config.json` with this structure:

```json
{
  "overrideRoot": "<existing portable terrain-atlas-profiles directory>",
  "profiles": [
    {"id": "<profile>", "mapRoots": ["<authorized Maps directory>"],
     "resourceRoots": ["<mod res directory>", "<base res directory>"]}
  ]
}
```

Run `terrain_bake_export config.json .` from the private output directory with
Qt/MinGW DLL directories on the process PATH. Run `research.py <output> stats`,
then `research.py <output> render` using the existing Python with Pillow/numpy.
The exporter enumerates direct MPR children of each configured root; the
research corpus had no nested Maps directories. Enumeration is per physical
map source, so same-named Base and mod maps remain separate.

Statistics count active overlay only, ordered four-corner tuples, separate
layer/runtime-class keys, and four homogeneous variants per used identity.
N=4/8/16 is a conditional bounded-recipe budget, not a measured pixel deduplication
result. Rotation-canonical counts are a more optimistic separate model.

The renderer selects an exact-bound crop with >=3 materials, no active overlay,
many transitions and uniform source tiles. It prioritizes lossless recovered
corners. Current terrain uses SEC slots/inverse grid rotations, the fixed Y
adapter and the mapper's 1/16 UV inset. This is an unlit texture plan, not a game
screenshot or a displaced-heightmesh render. After terrain uses shared bilinear
weights perturbed by continuous world value noise and a narrow blend. Pure
corners/cells stay pure. Sources repeat every cell; an additional macro image
exposes the quality/capacity tradeoff. Pixel hashes include preserved predecessor
runtime classes. The tool does not encode DXT1, mipmaps, gutters or game-ready
atlas pages, and it does not prove a finite N recipe scheme is seamless.

See the dated validation report for measurements, assumptions and runtime gates.

## Stage 1: measured stock edges, anti-tiling and finite seamless recipes

`stage1.py` reads the previous private snapshot and verifies the selected source
MPR hash. It copies stock tiles only into `build-tests-terrain-bake-stage-1/input/`.
It produces nine same-scale 4096px PNGs, a <=2048px contact sheet, edge close-up,
stock RGB/estimated ownership analysis, homogeneous anti-tiling study and JSON
slot budgets. No mapper code or project is changed. Run with existing Python:

```powershell
python -B tools/terrain_bake_prototype/stage1.py `
  build-tests-terrain-bake-research-0 build-tests-terrain-bake-stage-1
```

Stock ownership is estimated by per-pixel RGB projection against every available
uniform pair; the lowest residual wins. Measured contour/width, transverse mean
profile and actual ownership residual texture modulate a continuous world score
field. Original authoring alpha cannot be uniquely recovered from baked RGB.
This is a score-distance proxy, not a metric signed-distance implementation.

The finite grammar uses ordered corner IDs, predecessor runtime class and N
interior labels. Edges have one fixed midpoint/texture phase; perturbation,
source mixtures and color modulation vanish at edges. Shared endpoints give
identical ownership and RGB there. Recipe rasters include their exact edge
samples. This guarantees C0 edge equality in this prototype; it does not guarantee
tangent continuity, DXT/mip equality, or arbitrary-world-curve preservation.
Several crossing/tangent states would multiply keys and require an edge-state
solver. Current full-corpus figures are conditional grammar budgets, not a bake
or visual acceptance of every map. The selected crop alone is pixel-deduplicated.

Zero-slot rendering changes only homogeneous cells to existing uniform slots of
the same material and runtime class with coordinate-deterministic quarter turns.
Isotropy and matching texture borders remain owner/material acceptance gates.
No MPR is saved for this visual experiment. Free world anti-tiling mixes three
phased/rotated uniform sources with low-frequency noise and adds macro color;
it deliberately exposes its unique-slot cost. No hex/stochastic algorithm is
claimed by this experiment.

`memory_pack.cpp` is a separate headless QCoreApplication. Build `memory_pack.pro`
using the same existing Qt/MinGW kit in `build-tests-terrain-bake-stage-1/compiler`.
Run it with `build-tests-terrain-bake-stage-1/memory-config.json`, produced by the
renderer. It validates current MPR and source RES page/pixel hashes, creates four
private 2048/4096 x 128/256 capacity fixtures via the ordinary codec/transactions,
then reopens and checks class/rotation/overlay/full SEC conservation and all-page
reference coverage. It neither installs nor starts anything. 256px is resampled
stock artwork for memory testing, not new detail. Run `prepare_kit.py <root>` for
baseline/witnesses/owner instructions, then `verify_stage1.py <root>` for independent
archive/metadata/PNG/raster-seam readback. Both restrict writes to the same root.

`sample_memory.ps1` is an owner-run, read-only process monitor writing only below
the private stage-1 root. It checks a 32-bit target and samples private/working
set and VA regions. Syntax/C# compilation is preparation evidence; actual game
monitoring and game acceptance are separate. See the generated memory-kit README.

Commit source files and the dated stage-1 validation report only; no private
PNG, maps, RES, manifests, logs or binary outputs.

## Stage 1b: full stock ownership strips in N4 recipes

`stage1b.py` is a separate offline experiment for `EI-TERRAIN-BAKE-STAGE-1B`.
Run with the existing Python/numpy/Pillow, from the checkout root:

```powershell
python -B tools/terrain_bake_prototype/stage1b.py `
  build-tests-terrain-bake-research-0 build-tests-terrain-bake-stage-1 `
  build-tests-terrain-bake-stage-1b
```

Only the last, ignored directory is written. No mapper/game/clipboard is opened;
the UI test lock is therefore unnecessary. The tool verifies the live source
MPR fingerprint and the complete decoded stock atlas fingerprint, then reads
existing snapshots. It neither builds nor installs anything.

The brush is the full unblurred two-dimensional RGB ownership estimate, rather
than a mean alpha profile plus a weak residual. Each material pair has its own
512px bank of deterministic 64px stock fragments. Straight 2/2, corner 1/3 and
alternating diagonal 2/2 topologies are fitted against uniform stock textures.
An 8px-smoothed guide derived from a 6px-blurred estimate removes the coarse
source curve; nearest-pixel samples transport the remaining width, tongues and
flecks. A 12px pure-side score prior confines uncertain RGB classification to
the +/-32px band. The original artist's alpha/dithering cannot be recovered from
opaque baked RGB. Source fit errors and pre-prior contamination are reported.

Three N4 variants use binary ownership, a 1.45x widened strip with coarse stock
contour displacement, and a mixed 1.15x strip with five ownership levels.
The curve uses smooth scores and a first-order gradient-normalized distance,
not an exact Euclidean SDF. Each shared edge has canonical gradient/brush phase;
interior variation fades at the edge. Corner/class/label keys, label assignment
and texture/color variation stay identical to stage 1 N4. No new edge labels
are introduced. All saved RGB seams and independently evaluated ownership/RGB
traces are checked. This establishes C0, not C1, compressed mip or game fidelity.

`preview.png` is <=2048px wide and compares current stock, stage 1 B in N4 form,
and the three new N4 variants at equal scale. `edge-closeup.png` uses the exact
earlier 5x5 window; three additional windows cover different material pairs.
Full 4096px renders, source/brush analysis and JSON evidence are private outputs.
Top-two ownership at triple junctions, distorted arc phase near edges, periodic
fragment banks, uncertain coarse artwork fits and clipped wide fringes remain
limitations; owner visual acceptance is separate from seam/budget checks.

Commit only `stage1b.py`, this README section and
`docs/validation/ATAMAN_TERRAIN_BAKE_STAGE_1B_20261010.md`. Do not commit any
generated game pixels, input snapshots or build-test artifacts.
