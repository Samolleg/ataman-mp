# Environment resource evidence

Read-only inspection of Base `Res/textures.res` establishes the six shipped sky
members as `sky00.mmp` through `sky05.mmp`. Every face is a 256 by 256 DXT1 MMP
with seven mip levels.

Decoded content and edge comparison establish:

- `sky04`: cloud ceiling;
- `sky01`: dark ground/bottom face;
- side cycle: `sky00`, `sky02`, `sky03`, `sky05`.

The renderer cube array is ordered `+X, -X, +Y, -Y, +Z, -Z`, therefore the
resource binding is `sky00, sky03, sky02, sky05, sky04, sky01`. This replaces
the earlier inferred side order that swapped `sky02` and `sky03` and created
avoidable seams.

No game archive was modified. Diagnostic DDS/PNG copies used for inspection
live outside the repository under `Codex/work/environment-sky-probe`.
