# Landscape sculpt usability R21

## Changelist

- Landscape sculpt shortcuts now remain active after using the Tool dropdown.
  The physical `Z/X/C/V` positions work under Latin and Cyrillic keyboard
  layouts and select Raise, Lower, Smooth, and Flatten respectively.
- Smooth is listed before Flatten. The saved R20 tool choice migrates without
  being reinterpreted.
- Flatten now exposes and uses the selected Smooth, Linear, or Constant
  falloff.
- `Alt+Shift+Wheel` adjusts Flatten target while Flatten is active. For Raise
  and Lower it adjusts the step in 100-raw-unit increments.
- MPR Save As now follows normal document behavior: the validated destination
  is freshly reopened, hash-verified, and becomes the active MPR. The source is
  not silently overwritten.
- The landscape roadmap now records the closed baseline and the remaining
  semantic-atlas, structural resize/new-MPR, AI-graph persistence, recovery,
  and typed overlay-material work separately.

The R19 overlay-stroke ownership fix and R20 raw-height sampling remain
unchanged.

## Focused manual checks

1. Open Landscape Tools and choose Sculpt terrain. Click the Tool dropdown,
   select an item, then press `Z`, `X`, `C`, and `V` without first clicking the
   viewport. Confirm Raise, Lower, Smooth, and Flatten are selected.
2. Switch Windows to a Russian keyboard layout and repeat step 1. Also confirm
   `M` and `O` still toggle main terrain and overlays while focus is in the
   Landscape Tools dock.
3. Select Flatten and compare Smooth, Linear, and Constant falloff. Confirm
   each remains visible and changes the brush edge. Use
   `Alt+Shift+Wheel` and confirm Flatten target changes by 100.
4. Select Raise or Lower. Confirm `Alt+Shift+Wheel` changes Raise/lower step by
   100 without changing camera speed, radius, or strength.
5. On a disposable MPR copy, make an edit and use Save As to a second filename.
   Confirm the Loaded Map panel and title switch to the second file, subsequent
   Save writes there, the original stays unchanged, and the second file reopens.

## Known limitations

- Height sampling requires a visible hit on the active sculpt layer. In overlay
  sculpt mode, right-clicking an area without an overlay deliberately does not
  sample the terrain beneath it.
- Semantic automatic painting outside the reviewed Zone 1 atlas remains in the
  separate semantic-atlas workstream. Manual tile painting remains available.
- Structural MPR crop/expand and regenerated AI-graph persistence/staging are
  roadmap items, not part of R21.
- A previously supplied session recorded a shutdown-only access violation after
  view deletion and clean-session marking. It has not reproduced in the
  headless R21 checks and is not claimed fixed here.
