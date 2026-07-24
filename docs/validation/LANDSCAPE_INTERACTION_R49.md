# Landscape interaction and graph-source validation R49

## Scope

- Landscape Tools starts closed on first setup as well as later sessions.
- The viewport button, View menu item, and `L` shortcut are one dock toggle.
- `Esc` resolves Play From Here and Landscape state in a fixed order.
- Loaded AI graph selection is a visible ON/OFF override and enables graph
  display automatically.
- Only authored raw NIDs are authoritative for MOB Family Validation. Legacy
  projected objects without NID fields are not treated as duplicate objects.

## Automated evidence

- `mob_family_validation_tests`: passed, including two objects with NID 568 in
  one synthetic MOB and the `nid.duplicate_within_mob` warning.
- Qt 5.12.12 / MinGW 7.3 release build: passed through the documented Unicode
  worktree wrapper.
- `EIMapperEndurance.exe --ui-widget-check`: passed.
- The supplied MOB was accessed read-only; its SHA-256 remained
  `E95A4AC4E9B0E22B6B38FBD8B750476F3B804EF731B40CD7CEBA7467C3067609`.

## Manual test cases

1. Start with fresh Endurance settings and complete profile setup. Confirm
   Landscape Tools is closed. Restart and confirm it remains closed.
2. Open an MPR. Toggle Landscape Tools from the viewport button, View menu,
   `L`, title-bar close button, and `Esc`. All controls must mirror visibility.
3. Select a Play From Here anchor in normal view and press `Esc`; only the
   anchor selection clears. Repeat in Landscape Tools. With no anchor selected,
   `Esc` closes Landscape Tools. During anchor placement/move, `Esc` cancels or
   restores the anchor before affecting Landscape Tools.
4. Confirm the bottom control hint says `L-Landscape Tools` and contains no old
   `B-Landscape tile painting` text.
5. Open `zone1testhut.mob`. Confirm no duplicate-ID modal appears and MOB Family
   Validation reports no NID overlap for the file. Its nine legacy projected
   mapId aliases are not authored raw NIDs and must not appear as duplicates.
6. Regenerate the AI graph, then enable Loaded graph override. Confirm Show
   Graph activates automatically, the button reads `ON`, and the loaded graph
   is displayed. Toggle it `OFF`; regeneration should begin and retain visible
   graph output when complete. Confirm the source diagnostic wraps without
   clipping.

No test step saves or replaces the supplied MPR/MOB files.
