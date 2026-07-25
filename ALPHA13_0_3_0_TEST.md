# EI Mapper: Endurance Edition 0.3.0-alpha.13

Focused test build for custom-spawn capability gating and camera movement.

## Changes

- Play from Here is available only when the active JMV gameplay-map lifecycle
  can execute SpellAddon's autorun MOB.
- Play from Here is disabled for Base EI, JMV briefing/edge maps, a disabled
  `gameplay/autorunmobfile` setting, or missing required autorun components.
- No temporary runtime entrance mutation fallback is used.
- Godmode and editor Time of Day commands are not staged when the autorun path
  is unavailable.
- Keyboard camera movement is delta-time based and no longer changes speed with
  viewport frame rate.
- Selection/picking passes no longer advance camera movement a second time.

## Suggested checks

1. Open a normal JMV gameplay map: Play from Here should remain available.
2. Open a JMV briefing map: Play from Here should be disabled with an explanatory
   tooltip, while Test Current Map with a normal entrance remains available.
3. Switch to Base EI: Play from Here remains disabled.
4. Compare sustained WASD camera movement at low and high viewport frame rates;
   distance per second should remain consistent.
