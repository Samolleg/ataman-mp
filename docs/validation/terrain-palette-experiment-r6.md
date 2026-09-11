# Modern palette repaint experiment — r6

Task: EI-TERRAIN-PALETTE-001, explicitly authorized on existing PR #6.

The loaded Zone1 atlas has the same names and slot structure as the built-in
preset but different pixels. Exact global source binding therefore rejected its
thumbnails. A separate first-paint failure reported missing uniform material
1712759006; its first catalog source is bz2g slot 195, not Zone1.

## Reproduction and implementation

The synthetic regression creates two complete two-page archives with identical
slot structure and different pixels. Before the change, the resolver chooses the
old exact-pixel archive despite the repainted archive being preferred. That
assertion was observed failing before production edits.

The modern palette now explicitly requests experimental name/structure matching.
The default resolver remains exact for all other callers. Loaded map tiles take
priority for local thumbnails; the active archive precedes configured archives
for global uniform and transition sources. Each binding uses a single complete
archive and retains page presence, slot bounds, decoding and file stability checks.
Returned identities carry the actual decoded pixel hash.

Cache keys include ordered candidate paths, sizes and modification timestamps.
Changed source selection cannot reuse previous positive/negative lookup keys.
Stale deferred results are rejected, and a prepared paint stage is checked again
before adoption. Rebuilds retain reusable caches rather than clearing all entries.

## Validation — 2026-09-12

- Eight focused QtTest cases passed (10 passes including setup/cleanup):
  coherent archive selection, installed-resource probe, modern hydration wiring,
  exact embedded binding, semantic profile binding, semantic field binding,
  embedded profile round-trip, and save source-race rejection.
- The synthetic case also rejects missing pages, damaged DXT payloads, wrong
  page counts and out-of-range uniform slots. It checks candidate-order and file
  change fingerprints; strict matching still selects the original exact archive.
- Read-only installed-resource probe: Zone1 thumbnail uses actual hash
  `a44f78f46933dfba29125ec6867c3541acb6be56d93083afa6a6ce9616b5f997`.
  Material 1712759006 loads bz2g slot 195 from the active HD archive, hash
  `10c214c7fcd677931efe2def80fafb5762f14527d7858514e38bd11bd8437156`,
  and resolves a uniform generated tile. Strict binding rejects that repaint.
- Release editor compiled with Qt 5.12.12/MinGW. The wrapper's runtime step
  references an unavailable Visual Studio edition; both runtime components were
  rebuilt from current source with the installed Build Tools through local build
  helpers, then copied into the release tree.
- Fresh creature/item/lever creation diagnostics passed, including undo/redo,
  save/undo/save/redo/save and corrupt-provenance rejection.
- JMV desktop preview diagnostic passed textured previews, map-texture eligibility,
  pointer placement, cancel/commit, history, save/reopen and item-to-lever conversion.
- Broad `--ui-widget-check` still exits with access violation `0xC0000005` under
  the minimal Qt platform, reproducing the pre-existing coverage gap.
- Separate unpacked `EI-Mapper-Endurance-0.5.0-alpha-terrain-palette-r6-test`
  staged and delivered; both finalizations passed with no stale files. Executable
  hashes match the tested release:
  `4e56defeaf8248469a5aff5f31a476dc030245752c909d1b8e5e14b855e2393b`.
  Earlier tester folders are preserved; the finalized package was not launched.

## Manual checks and limits

Open the affected map with the active HD resource profile. Check modern palette
thumbnails, paint the previously failing material and inspect mixed transitions.
Switch maps/profiles and check that source artwork follows the selected resources.
Then undo/redo and save/reopen a test map copy.

Matching structure cannot establish that each slot still represents the preset's
material. Semantic correspondence and interactive mixed-transition appearance are
unverified; the editor labels the palette experimental. Tests prove source access
and uniform generation, not visual correctness of every material. In-place file
changes that preserve both size and modification timestamp are outside cache
invalidation coverage; reload after externally replacing resources.

No installed game assets, profiles or maps were changed or included in the PR or
package. Original database-item acceptance remains recorded in the r5 checkpoint.
