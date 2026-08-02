# Native AI graph preservation corpus (2026-08-02)

Internal EI Mapper Endurance reference corpus for AI-graph parity testing.

- Contains 47 `.grp` files captured by forcing whole-map graph generation in
  the game after the primary map MOB had fully loaded.
- Includes primary maps and retained alias/control captures from the parity
  audit. Autozones are intentionally not represented as independent editor
  generation targets.
- The source maps and installed game/mod were read only. This directory stores
  graph outputs only; it contains no executable, addon, save, dump, or private
  implementation source.
- `MANIFEST.json` records the relative name, byte size, and SHA-256 digest of
  every graph. `MANIFEST.sha256` authenticates that manifest.
- Zone 9 is the authoritative control for the 2026-08-02 floating-point
  quantization regression.
- Connected-component identifiers are a separate derived plane from topology,
  weights, and packed positions and must be assessed independently.

These game-derived fixtures are preserved in the private project repository for
internal testing. Do not include them in tester packages or redistribute them as
ordinary editor assets. If the repository is ever made public, remove this corpus
from public history first or replace it with independently created synthetic
fixtures.
