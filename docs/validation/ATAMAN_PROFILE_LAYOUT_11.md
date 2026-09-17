# EI-LAYOUT-011 — profile-local world coordinates (2026-09-17)

Branch `codex/profile-world-layout-v11`, base
`a0c4fa93205ce139b7384f6a2fe16df209f59605`. Version 11.

## Request and evidence

Owner requests world-map-layouts.json above Maps, alongside the profile addon.
MainWindow previously used the document's global AppData default regardless of
the active resource profile. Read-only inspection confirms the supplied JMV-root
file already exists, alongside addon.dll, and matches the legacy AppData file:
SHA-256 `33C7CC6C9A9A5FB481F5811414BB040F2C5FB13E000C27929DFDF2A35C9144AE`.
Both contain jamais-vu and deja-vu. Do not overwrite or strip this existing file.

## Implementation

Bind the layout document to evaluated modRoot (Base: gameRoot) when refreshing
the map context. Invalid profile/root disables persistence rather than writing
to AppData. A path/profile change clears the old cached state. Existing files
take priority and retain unrelated profile records. If the destination is
absent, import only the active profile from the old document and atomically
persist it with inferred additions; retain the legacy file for recovery.
Corrupt legacy/destination files block writes. A destination appearing after
initial missing-file load is refused. No MOB/MPR/RES or addon changes.

## Validation

`build-tests-11-layout/results.txt`: 15 PASS, 0 FAIL, Qt 5.12.12.
Synthetic temporary directories cover migration preserving manual coordinates,
JMV/DejaVu/Base paths, switching away/back, isolation of newly created files,
legacy byte preservation, precedence of an existing multi-profile file even
with corrupt legacy input, invalid root, corrupt destination/source and a
new destination appearing after initial load. Existing inference, reset,
serialization and coordinate-transform regressions also pass.

## Delivery

Build/install pending. Actual supplied JMV JSON and legacy source left unchanged.
Manual acceptance pending: open JMV, check known positions, move a neighboring
map and verify the JMV-root JSON changes; switch profiles and verify separation.
Do not claim a live-user map dragging test from synthetic tests alone.
