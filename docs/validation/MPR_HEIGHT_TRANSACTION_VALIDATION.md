# MPR height transaction validation

This report covers the internal landscape height candidate path. It does not enable changed MPR saving or terrain-editing UI.

## Normal convention

Read-only analysis decoded packed terrain normals and compared them with geometry from copied Base and JMV MPR fixtures. A unit-weight average of the normalized faces in the eight-neighbour vertex fan matched the observed convention essentially exactly on one Base surface map and one JMV map, and was the closest tested method on the other sampled maps. The implementation includes signed horizontal vertex offsets and converts raw height through the MP `maxZ` value.

Only a changed height and the normals in its 3x3 dependency neighbourhood are emitted. Other packed vertex fields and normals remain byte-identical. Shared seam aliases receive identical height and normal bytes. A pre-existing conflicting seam fails closed when it participates in an affected normal.

## Synthetic validation

The focused Qt suite reports 39 passed, 0 failed, and 0 skipped. Coverage includes:

- canonical shared-seam height and normal propagation;
- deterministic undo, redo, and regenerated transaction equality;
- exact byte locality outside the changed height and affected normal bytes;
- preservation of all overlay vertices, tiles, and material indices;
- fresh semantic reopen and untouched-member equality;
- rejection when a normal depends on conflicting seam geometry.

## Private recovery-copy corpus

An external, untracked harness performed one in-memory point-height edit on 8 copied Base MPRs and 32 copied JMV MPRs. For all 40 files it:

1. loaded the lossless archive document and canonical terrain lattice;
2. generated affected packed normals;
3. created provenance-checked same-length sector patches;
4. applied the patches only in memory;
5. freshly reopened the candidate;
6. confirmed the semantic diff contained exactly the transaction's edited sectors.

Result: 40 passed, 0 failed. The harness wrote no candidate map files and the private corpus remains outside Git.

## Remaining release gates

- Changed landscape saving remains disabled in the editor.
- No generated candidate has been installed into a game directory.
- In-game terrain traversal, collision, navigation, water interaction, unload/reload, and graceful-exit validation remain outstanding.
- Brush UI, live renderer updates, `maxZ` saturation warnings, MOB/navigation diagnostics, texture painting, and overlay creation remain separate increments.
