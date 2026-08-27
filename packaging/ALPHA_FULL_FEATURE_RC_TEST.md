# EI Mapper Endurance 0.5.0-alpha — full-feature release candidate

This package is the first complete alpha release-candidate build of the current
Endurance main line. It includes the integrated Russian interface update and
the bilingual English/Russian user manual.

## Included scope

- Lossless MOB/MPR editing, undo/redo, backups, validation, and multi-MOB work.
- Object, unit, behavior-slot, patrol/look-point, trap, diplomacy, and script
  authoring tools.
- Modern, Classic Auto, and Manual terrain/overlay painting; sculpting;
  materials and flipbook animation; minimap tools; and navigation-graph tools.
- Integrated Database Editor and RES Resource Browser with cross-navigation.
- Profile-aware Base and Jamais Vu previews plus isolated Test Current Map.
- English and Russian UI plus an offline bilingual manual under `Manual/html`.

## Focused checks

1. Complete first-run setup, then switch **Settings → Language** between English
   and Russian and restart when prompted.
2. Open copied/mod-owned MOB and MPR files. Exercise object selection,
   properties, behavior mode, patrol/trap collections, undo, and validation.
3. Exercise terrain and overlay painting in Modern, Classic Auto, and Manual
   modes. Inspect the publication summary before saving/finalizing a Modern
   Draft pair.
4. Open the Database Editor and Resource Browser, use their cross-navigation,
   and confirm both windows preserve their dock/floating state.
5. Inspect/regenerate an AI graph, export a minimap, and run Test Current Map on
   a supported owned profile.
6. Open `Manual/html/en/index.html` or `Manual/html/ru/index.html` offline.

## Known boundaries

- **Lava + Resolve Blends:** direct Modern painting follows the selected Lava
  material, but **Shift+R** cannot yet preserve one visually consistent route
  owner across every rare connected Lava-on-Water transition without
  regressing ordinary Ground/Overlay repair. Inspect the complete connected
  transition after resolving and retain a pre-resolve undo/save point.
- The shipped 32-bit runtime is release-validated through **4,096 terrain-atlas
  slots / 64 pages**. Larger atlases are unsupported; a controlled 8,192-slot
  load failed under 32-bit address-space pressure.
- The integrated `--ui-widget-check` currently terminates with access violation
  `0xC0000005` after startup on the packaging host. The release build and
  focused manual/localization/package validation completed, but this headless
  harness result remains an unresolved validation-infrastructure issue.

Built from source commit `eec794b1aab6df8e187816174bdebc7987e49ce2` plus
the version-reset/package metadata commit included in this distribution.
Source: https://github.com/Ilufus/ei-mapper-endurance
