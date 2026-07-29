# 0.3.0-alpha.26 - MPR canvas resize R2 test

## Changelist

- Replaces nine-point anchoring with explicit X/Y placement of the original
  map in terrain tiles. Partial-sector offsets are re-tiled and seam-validated.
- Fixes the generated-map preview orientation and adds wheel zoom plus left-drag
  panning. The preview image is not embedded into the output.
- Adds an optional lossy closed-edge taper with a selectable tile width;
  overlays inside that outer band are deactivated.
- Can create a separately named companion MOB beside the resized MPR, translate
  its objects and dependent unit/trap points, optionally remove objects outside
  the new bounds, and omit its stale embedded AI graph. Existing MOB files are
  never overwritten.
- Prevents a deferred Landscape Tools visibility callback from dereferencing a
  deleted viewport during shutdown. This addresses the access violation found
  in the R1 logs.

## Manual test

1. Open a copied MPR and use **MPR Settings > Canvas resize**. Change X/Y by a
   non-sector offset such as 3 and -2 tiles; zoom/pan the preview and confirm
   north/south matches the viewport/minimap.
2. Save As, reopen the result, and inspect terrain and overlays across every new
   sector seam. The source MPR must remain unchanged.
3. Repeat with **Close outer edges** at widths 1 and 2. Confirm the selected band
   slopes to a closed raw-zero edge without overlay fragments.
4. Load and activate a copied MOB, enable **Create a translated companion MOB**,
   and crop with a non-zero offset. Test once retaining outside objects and once
   removing them. Confirm a new same-basename `.mob` is created, the source MOB
   is untouched, units/traps keep their dependent points aligned, and an
   existing destination MOB is refused.
5. Close the editor after hiding/showing or docking Landscape Tools. If shutdown
   is slow or a dump appears, return the complete logs folder.

## Known limits

- Out-of-bounds removal uses each root object's origin, not its full mesh or
  collision bounds.
- Map catalog/minimap registration and exported AI graph staging remain separate
  operations; resize does not invent them.
