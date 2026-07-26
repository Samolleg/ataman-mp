# Alpha 23 tester changelist

This build fixes generated DXT1 minimaps that appeared transparent in the game.
The exported MMP now carries the legacy RGB565 descriptor required by the
runtime texture loader.

Minimap capture now respects hidden static objects. Use the refresh icon to
regenerate explicitly. Auto is off by default; enabling it queues regeneration
after map edits, including hide and unhide.

Export Minimap now shows generated and embedded minimaps side by side and adds
shading intensity, saturation, contrast, and brightness controls. Apply & Lock
keeps the chosen generated texture unchanged until Unlock is pressed in the
viewport minimap.

Private runtime checks are still required: visibility/opacity in both supported
game profiles, hidden scripted-gate exclusion, grading fidelity on outdoor and
underground maps, lock/unlock behavior, and embedded/generated hot reload.
