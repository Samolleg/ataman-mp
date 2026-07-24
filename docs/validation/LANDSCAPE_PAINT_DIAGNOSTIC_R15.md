# Landscape semantic-paint diagnostic R15

R14 is rejected for visual orientation. Its automated tests proved archive
locality, deterministic catalog lookup, and internal signature consistency;
they did not independently prove that a catalog signature matches the
renderer-visible corners of a packed atlas tile.

R15 does not claim an orientation fix. It expands the existing JSON diagnostic
so one failing stroke can be inspected without enabling changed-MPR saving.
Each changed cell records:

- global and sector-local grid coordinates;
- its offset from the brush center, using neutral X/Y names;
- before and after packed values, atlas slot, and packed rotation;
- the exact four-corner signature requested by the solver;
- the catalog signature decoded from the selected packed value; and
- the same atlas slot's catalog signature at packed rotation zero.

Corner keys are `x0y0`, `x1y0`, `x1y1`, and `x0y1`. The export does not
assert that Y0 means north or south. Resolving that convention requires
comparison with the visible result and the renderer's actual UV transform.

The diagnostic contains no MPR payload or atlas pixels. Changed-MPR Save and
Save As remain disabled.

The next orientation test must use an independent rendered-corner oracle. A
test that generates requested signatures and validates them through the same
catalog mapping is circular and cannot establish visual correctness.
