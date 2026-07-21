# 0.2.0-alpha.4 focused validation

This build corrects failures found in `0.2.0-alpha.3` without changing installed
game, JMV, map, or SpellAddon files.

## Base EI probe

Run levels 1 through 4 separately. The packaged x86 bridge now exposes the
stable `EnduranceInitialize` protocol export that the owned-process host
requires. Level 1 observes startup only; level 2 requires manually starting a
normal Base New Game; levels 3 and 4 perform the staged redirect experiments.

Record whether the game starts, whether a New Game was started manually, the
selected probe level, and whether the staged map appeared. General Base launch
support is not claimed until the ladder succeeds.

## JMV Godmode

Enable Godmode and launch a normal JMV map test. The editor creates a losslessly
patched `AutoRunSpellAddon123.mob` under the run directory and the bridge
redirects only the owned game process to it. Confirm both the in-game effect and
the `GODMODE_BOOTSTRAP redirect` entry in `runtime.log`.

## Entrances

Open `gz10g` and compare the in-game player position with the translucent yellow
deploy rectangle for the selected entrance. Use **Go to Entrance** to center the
editor camera. Report whether the player is outside the rectangle, inside it but
away from its center, or obstructed by an object.

## Play from Here

Verify that one click on visible terrain places the requested/resolved marker
without flicker or changed surface normals. The actual runtime position adapter
remains deliberately gated in this build, so **Test Current Map** remains
disabled while the custom spawn toggle is active.

## Operation controls

With no selection, Move/Rotate/Scale and their shortcuts must be disabled. With
one or more selected MOB objects, the buttons and `G`/`R`/`T` actions must become
available and invoke the same tools. Select is available whenever an active MOB
is loaded. Tile Brush remains separate landscape-branch work.

