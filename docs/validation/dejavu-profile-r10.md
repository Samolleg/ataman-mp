# DejaVu profile — r10 validation

Task: `EI-PROFILE-DEJAVU-001`, 2026-09-12.

## Behavior

The built-in catalog now offers DejaVu alongside Jamais Vu and Base. It uses
its own mod Maps/res roots before Base resources, with the existing JMV visual
providers and fingerprint-guarded Starter runtime. Named built-in presets cannot
silently select a neighboring mod with matching signature files.

Map-test eligibility, launch requests and hot-reload control accept the DejaVu
profile ID. Existing engine fingerprints, request path guards and unknown-profile
rejection remain enforced. Installed mod data was inspected read-only.

## Evidence

- The new profile regression first failed because the preset was absent.
- Resource-profile suite: 10 passed, including actual installed DejaVu discovery
  (41 MPRs, 69 RES archives including Base) and bidirectional neighboring-mod
  isolation. The installed Starter runtime fingerprint was accepted.
- Runtime-provider suite: 6 passed, including DejaVu manifest identity and
  rejection of invalid paths and unknown profiles.
- Targeted launcher suite: 8 passed, 1 skipped. Coverage includes autorun
  capability, mixed-root rejection, generation-specific saves, resource-change
  restart decisions and hot-reload identity. Actual autorun payload injection was
  skipped because its optional maps fixture environment was not configured.
- DejaVu database preview: two creature previews passed against installed
  resources. The exact cheat-dagger map texture was absent, and placement was
  correctly rejected instead of borrowing a Jamais Vu asset.
- With authored temporary DXT1 map fixtures, the DejaVu diagnostic passed item
  preview, missing-material rejection, pointer placement/cancel, conversion to
  lever, positive-ID preservation, undo/redo and save/reopen checks. This verifies
  binding and transactions; it does not establish the missing asset's appearance.
- Release editor and both current runtime components built successfully. The
  release wrapper's runtime step required a local Build Tools path override
  because its default Visual Studio Community path was unavailable.
- The broad `--ui-widget-check` still exited with `0xC0000005`; this pre-existing
  diagnostic failure is not counted as a pass.
- Clean unpacked r10 tester package passed the package finalizer. It was not
  launched after finalization. No synthetic textures or installed mod data are
  included in the package or commit.

Editor SHA-256:
`6A94423855C5EAC513CB79BFBB9F16257B6862B5035F64C8134BD5A82F409386`.

Bridge SHA-256:
`D99853CC38236F7D099AC951402C9C7B79A9F86124F586519F20B359C3952C3B`.

## Manual acceptance and uncertainties

Select DejaVu in game/mod settings, open one of its maps and inspect database
previews. Then verify Test Current Map, Play from Here and hot reload in the
actual game. These runtime actions were not executed during automated checks;
compatibility of all mod scripts and content remains unverified.

The installed DejaVu needs `unhumadg_03.br.1.x2map.mmp` before the corresponding
cheat-dagger material can be placed. No asset was copied from Jamais Vu.

Prior database-placement coverage gaps and the converted-lever approach-distance
gameplay issue remain separate open work; this profile addition does not close
them. No other material uncertain implementation choices were identified.
