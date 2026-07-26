# EI Mapper Endurance 0.3.0-alpha.22 test notes

## Entrance overlay visibility

- The viewport test toolbar now has a compact open/closed-eye button beside the
  entrance selector.
- Uncheck it to hide all map.txt entrance rectangles and labels; check it to
  show them again.
- The visibility choice is remembered between editor sessions.
- Hiding the overlays does not change the selected entrance, Test Current Map
  spawn behavior, or the Play from Here marker.

## Focused test

1. Open a mapped MPR and confirm its entrance overlays appear.
2. Toggle the eye button closed and open.
3. With overlays hidden, select another entrance and run **Test Current Map**;
   the selected entrance should still be used.
4. Restart Endurance and confirm the last overlay visibility state is restored.
