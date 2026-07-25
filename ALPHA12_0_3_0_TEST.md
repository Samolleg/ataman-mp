# EI Mapper: Endurance Edition 0.3.0-alpha.12

Alpha.11 is withdrawn and must not be used for Test Current Map.

## Fixed

- Fixed the access violation after `MainMenuReady` in both normal and
  synthetic Test Current Map launches.
- Kept the shared temporary RES staging mechanism without replacing installed
  game archives.
- Added a real owned-process release test. Packaging now requires both normal
  Zone 1 and the generated empty-control map to reach `GameplayReady`.

Please retest initial Test Current Map, synthetic empty-control loading, and
hot reload. Send the editor workflow log and the matching test-runtime log if a
run does not reach gameplay.
