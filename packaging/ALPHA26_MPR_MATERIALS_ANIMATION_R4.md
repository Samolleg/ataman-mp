# 0.3.0-alpha.26 MPR materials animation R4

## Changes

- Retains the R3 overlay animation, focus, and cross-MPR refresh fixes.
- The **Wave multiplier** tooltip now gives the effective geometric amplitude:
  `world Wind strength x 0.3 x Wave multiplier`.
- The tooltip explicitly states that zero world wind produces no geometric
  motion and that Wave multiplier does not control atlas-frame animation.

## Focused check

Open **MPR Settings > Materials Editor** and hover **Wave multiplier**. Confirm
the tooltip clearly distinguishes MOB world wind, geometric water displacement,
and independent atlas-frame animation.
