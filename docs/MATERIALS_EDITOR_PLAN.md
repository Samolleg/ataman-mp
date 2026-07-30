# MPR Materials Editor plan

## Purpose

Refactor the inherited **MPR Settings -> Materials** presentation into a clear
**Materials Editor**. The editor must expose the MPR material table without
conflating it with terrain atlas textures or semantic paint families.

This is a typed MPR-data editor. It does not author atlas imagery, generate
transition tiles, or infer that a texture is emissive because it resembles
lava.

## Data ownership and terminology

The UI must keep these independent concepts visually and verbally separate:

1. **Terrain material / atlas texture** selects the visible tile imagery used
   by land or overlay painting.
2. **Surface material** is a slot in the MPR material table referenced by
   overlay tiles. It controls the surface's typed material properties.

Several atlas textures, tile families, and overlay cells may reference the same
surface-material slot. Editing a shared slot therefore changes every current
user of that slot; it does not rewrite their atlas textures or tile-family
identity.

The typed surface-material controls are:

- material type;
- RGB color;
- opacity;
- **Illumination**, mapped directly to the material's self-illumination value;
- wave multiplier;
- warp speed.

The emitted RGB is the component-wise saturated product:

```text
emittedRgb = clamp(surfaceMaterial.rgb * surfaceMaterial.illumination, 0..1)
```

Emission belongs to the surface-material slot. It is not an intrinsic lava,
texture-family, atlas-slot, or semantic-family flag. A lava-looking texture
assigned a non-emissive surface material must not emit. A non-lava texture
assigned an emissive surface material must emit.

## Required presentation

The Materials Editor should provide two clearly separated areas:

- **Terrain texture / atlas usage**: read-only context showing which atlas
  texture or semantic family the selected overlay cells use. Atlas mutation
  remains in the dedicated atlas workflows.
- **Surface material**: slot selection and the typed controls listed above.

The surface-material list should show, at minimum, slot ID, type, color,
opacity, Illumination, wave multiplier, warp speed, and usage count. When a slot
is shared, the editor must say so before applying a change and identify the
scope in user terms. It must not imply that selecting or editing the slot edits
the atlas.

The label **Illumination** may remain user-facing, but its tooltip must explain
that it scales the selected surface material's RGB emission. No control should
describe emission as a lava flag.

Validated Base/JMV presets may fill these typed fields, but applying a preset
must be explicit and previewable. Presets must not silently replace unknown or
mod-specific values.

## Transaction and preservation contract

Surface-material changes must use the canonical `MprDocument` edit path and be
undoable. They must preserve:

- every untouched material slot byte-for-byte;
- existing overlay-to-material slot references unless the user explicitly
  reassigns them;
- unknown/reserved fields in edited records unless their meaning and mutation
  are independently validated;
- untouched SEC members, archive members, member names/order/metadata,
  namespaces, hash chains, alignment, trailer data, and raw bytes.

Changed saves remain subject to staged serialization, fresh reopen, semantic
diff, backup, source-race checks, runtime-compatible archive validation, and
atomic replacement. Editing a material must never invoke the legacy rebuilding
serializer or modify atlas imagery.

## Root-cause tests required before implementation

Capture these as failing tests against the current UI/data path before changing
production code. Each test needs explicit expected values and must trace the
first incorrect binding or ownership decision rather than only checking the
rendered symptom.

1. **UI field binding**
   - Select a known synthetic material slot.
   - Verify type, RGB, opacity, Illumination, wave multiplier, and warp speed
     display the exact stored values.
   - Change one field at a time and prove that only its typed property changes.

2. **Slot selection and sharing**
   - Create two overlay tile/texture families that reference one material slot.
   - Verify the UI reports the shared usage.
   - Edit that slot and prove both users still reference the same slot while no
     atlas identity or tile selection changes.
   - Reassign one user explicitly and prove only that reference changes.

3. **Lossless MPR serialization**
   - Prove no-op open/save byte identity.
   - Edit one material property and prove untouched material slots, SEC
     members, unknown fields, archive structure, and unrelated raw bytes remain
     equal.
   - Cover undo/redo, failed staged reopen, source-race refusal, and atomic-save
     rollback.

4. **Texture-family/material independence**
   - Bind a lava-looking atlas texture to a non-emissive material and verify the
     computed emitted RGB is zero.
   - Bind a non-lava overlay texture to an emissive material and verify emitted
     RGB equals the saturated RGB-times-Illumination product.
   - Change only the texture family and prove material properties do not
     change; change only the material slot and prove the atlas selection does
     not change.

5. **Profile and preset boundaries**
   - Validate representative Base and JMV material records independently.
   - Prove a preset is explicit, undoable, and affects only documented typed
     fields.
   - Preserve unsupported or ambiguous records read-only and diagnose them
     rather than inventing values.

## Integration boundary

Implementation begins only after merging the then-current committed local
`main`. Prefer current main for shared renderer, character, runtime, and
viewport behavior while retaining the canonical MPR transaction and lossless
save contracts.

This refactor is independent from the three terrain-painting modes:

1. direct manual tile placement;
2. smart painting constrained to existing atlas tiles;
3. modern painting that may generate blends and extend the atlas.

Those modes consume or extend atlas content. The Materials Editor edits MPR
surface-material slots. **Finalize Atlas** belongs to the atlas-writing flow and
must not implicitly rewrite surface materials.
