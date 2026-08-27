# Modern atlas r13 tester notes

This revision fixes the expanded-atlas editor failures reported from r12:

- Every 1,024–16,384-slot Zone18 probe now includes an exact-bound portable
  semantic profile. Repeated physical slots inherit the reviewed semantics of
  their exact Zone18 source-cycle slot, so baseline recovery covers every slot
  and brushes are not refused merely because the expanded namespace has no
  built-in preset.
- Repeated byte-identical MMP pages share decoded RGBA storage and repeated
  physical pages share tile thumbnails. The game-visible physical page/slot
  layout is unchanged; only redundant editor memory and decode work are
  removed.
- The game-wide material palette decodes each source MMP page once per catalog
  rebuild instead of once per material family.
- **Inspect atlas** now has an **All physical atlas slots** page selector. Each
  page exposes its 64 physical slots with global slot, local slot, assignment
  state, preview, and four corner identities, including all expanded aliases.

## Focused manual test

1. Select the `jamais-vu` profile and refresh Maps and Collections.
2. Open `Zone18 expanded atlas - 1024 slots`. The material status must report
   full `1024/1024` coverage, not `749/1024`.
3. Paint several continuous strokes with different ground brushes, including
   the area around cells 110,62–111,66. No `terrain.virtual_paint_unknown` or
   `terrain.virtual_stroke_field` refusal is expected.
4. Open **Inspect atlas**, switch across pages 0–15, and confirm every page
   shows exactly its physical slots and selectable previews.
5. Repeat editor loading and painting at 4,096 slots. Compare load time and
   memory against r12; repeated pages should no longer allocate independent
   decoded images and icons.
6. Use **Test Current Map** on the 2,048-slot collection and confirm the staged
   Zone18 workload reaches gameplay with terrain textures.

## Known scope

The 1,024–16,384 fixtures are exact repeated stock Zone18 atlas capacity probes,
not artistic blend showcases. Normal map-specific authoring finalization still
uses its current validated ceiling; the probes test the packed runtime range.
