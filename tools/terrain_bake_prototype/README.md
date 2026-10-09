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
