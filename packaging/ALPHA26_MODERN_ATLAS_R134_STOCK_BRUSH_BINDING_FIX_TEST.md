# Alpha 26 modern-atlas R134 stock brush binding fix test

This revision fixes material palette brushes that could show a transition tile
instead of a genuine uniform bz3g/bz4g source after opening or switching maps.

## Changes

- Global material brushes and authored transition sources now load only from
  configured stock resource archives, never from the open map's private or
  finalized atlas whose member names can refer to different physical slots.
- Decoded brush tiles are cached by the actual MMP page payload as well as its
  member name and slot. A same-named page from another map can no longer reuse
  stale artwork.
- Palette thumbnails prefer a structurally proven uniform from the current
  atlas before a foreign display thumbnail. Mixed legacy thumbnail metadata is
  no longer allowed to turn a local bz3g/bz4g brush icon into a blend.
- The bundled R140 semantic catalog also corrects the authoritative bz4g/Zone4
  thumbnails to proven local uniforms: slot 353 for **Ground / Gray** and slot
  204 for **Ground / Gravel greenish**.
- Terrain semantics, masks, finalized maps, and resource archives are unchanged.

## Focused manual test

1. Open a Zone4/bz4g map and inspect the modern Ground material palette,
   especially **Ground / Gray** and **Ground / Gravel greenish**. Every brush
   icon must be a uniform texture, not a multi-material blend.
2. Paint several strokes with those materials and confirm uniform interiors use
   matching material artwork while transition cells remain confined to edges.
3. Open a Zone9/bz3g map, inspect and paint several materials, then switch back
   to Zone4. No icon or painted uniform may inherit artwork from the previously
   open map.
4. Repeat after opening one finalized/private-atlas MPR before the stock map.
   Stock brush icons and painting must remain unchanged.

## Known limitation

- Rotation-equivalent physical deduplication remains disabled by decision.
