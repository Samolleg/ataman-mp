# 0.2.0-alpha.7 focused validation

This build follows the alpha.6 flicker fix. It does not modify installed maps,
the installed SpellAddon autorun MOB, resource archives, or game executables.

## Viewport and profile lifecycle

- Open a map through **Maps and Collections**. The camera should focus the first
  authored entrance automatically.
- Entrance labels must be upright. The yellow overlays must remain stable.
- Confirm the window title names the active Base/JMV editing profile.
- Open **Game and Mod Setup** and confirm that profile is preselected. Switching
  profiles with a map open must close the current documents before resources are
  refreshed.

## Authored entrances

Run JMV `zone1` with entrance 5. Alpha.7 still initializes the session through
`gz1g / 5`, then the run-local autorun bootstrap explicitly places player 0 at
the selected deploy point through `SetCPFast`. Confirm the player is inside the
yellow entrance-5 rectangle instead of the map corner.

## Play from Here

- Click **Play from Here** and move over visible terrain. A translucent
  hero-shaped outline should follow the terrain height without flicker.
- Place it. The button must become **Clear entrance**, the authored entrance
  selector must become disabled, **Go to Entrance** must focus the custom
  anchor, and **Test Current Map** must remain available.
- Launch JMV and confirm the player appears at the chosen marker.

## Godmode

Enable Godmode and launch JMV. Confirm the party receives `GodMode(0,3)`. The
run's `runtime.log` should contain `RUNTIME_BOOTSTRAP redirect`; `manifest.json`
records the injected `SetCPFast` and `GodMode` commands.

## Base EI status

The local alpha.6 report confirms levels 3 and 4 loaded their staged Base map
pairs. Alpha.7 keeps the exact-hash Base transport unchanged. Play from Here and
JMV Godmode remain disabled for Base because those commands use JMV's autorun
provider.
