# 0.3.0-alpha.26 MPR materials animation R2

## Changes

- **Show material slots on overlay tiles** is now a real on/off toggle. Its
  checked state and label stay synchronized with the equivalent Landscape
  Tools option.
- Active MPR atlas-tile animation records now play on overlay tiles in the
  viewport. This restores the authored 32-frame water sequence used by
  `zone5_1` while preserving tile rotation and multi-page atlas addressing.
- The Materials Editor explanation now describes active viewport playback
  instead of incorrectly calling shipped Base/JMV records dormant.

## Focused checks

1. Open `zone5_1.mpr` and confirm its water texture cycles while viewport
   animation is enabled.
2. Disable viewport animation and confirm both overlay geometry and atlas-frame
   animation stop; re-enable it and confirm they resume.
3. Open **MPR Settings > Materials Editor**. Toggle **Show material slots on
   overlay tiles** on and off, then change the same option in Landscape Tools;
   confirm both controls remain synchronized.

## Known limitation

- Automated checks verify the authored 64..95 atlas sequence, shader linkage,
  lossless Zone5_1 loading, and both ANGLE and desktop OpenGL startup. Final
  visual cadence/parity still requires the focused in-editor check above.
