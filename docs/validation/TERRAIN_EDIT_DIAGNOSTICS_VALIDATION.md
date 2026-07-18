# Terrain edit diagnostics validation

This increment adds pre-apply diagnostics around canonical terrain height
commands. It does not expose terrain mutation or MPR saving through the UI.

## Safety contract

- Diagnostics do not mutate the terrain model.
- Raw-height saturation fails closed during apply unless the caller records an
  explicit decision to accept clamping.
- Navigation and MOB findings are warnings, not claims about undocumented
  engine limits.
- Slope and object-height thresholds are disabled by default. A Base or JMV
  profile must select and label them explicitly.
- MOB clearance uses bilinear sampling of the canonical terrain lattice. It is
  an editor risk estimate, not the engine's collision or contact projection.
- Shared seam vertices are counted once canonically while every affected SEC
  sector is reported.

## Synthetic validation

`endurance_document_tests` covers:

- non-mutating pre-apply diagnostics;
- explicit versus disabled risk thresholds;
- maxZ saturation and fail-closed apply;
- stale-command provenance rejection;
- duplicate vertex rejection;
- seam vertex and affected-sector accounting;
- slope, navigation, and object-height findings.

The full synthetic suite passes 40 tests with no failures or skips.

## Private runtime validation

Private Base/JMV MPR validation is run only against copied inputs outside the
repository. The localized height transaction probe applies a non-saturating
point edit in memory, stages SEC replacements, reopens the result, and checks
untouched archive members. All 40 copied inputs passed (8 Base and 32 JMV). No
proprietary fixture or generated map is committed.

Manual testing is deferred until brush interaction and live renderer preview
are wired to this model. Changed landscape saving remains disabled.
