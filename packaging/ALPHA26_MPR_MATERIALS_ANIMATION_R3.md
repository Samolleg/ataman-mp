# 0.3.0-alpha.26 MPR materials animation R3

## Changes

- Retains R2 overlay atlas-frame playback and the synchronized material-slot
  visibility toggle.
- Pressing Enter while editing a material value no longer activates or focuses
  the material-slot visibility toggle.
- Keeping MPR Settings open while switching maps now reloads the selected
  material's type and numeric fields, even when both maps select the same row.

## Focused checks

1. Edit a material number and press Enter. Confirm the edited value commits and
   **Show/Hide material slots** neither activates nor receives focus.
2. Keep **MPR Settings > Materials Editor** open and switch between two MPRs
   with different material 0 values. Confirm the type dropdown and every numeric
   field update immediately.
3. Open `zone5_1.mpr` with its MOB. Confirm its atlas-frame water animation plays
   but its geometry remains still: the map deliberately stores wind strength 0,
   so Wave multiplier has no geometric amplitude there.
