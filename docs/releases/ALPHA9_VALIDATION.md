# 0.2.0-alpha.9 combined validation

Alpha.9 combines the patrol correction held after alpha.8 with the next map-test,
profile, and collection-file slice.

## Entrance and test-runtime checks

1. Open zone1 and select entrances 1 through 5. Confirm each green overlay and
   runtime spawn correspond to the same numbered `map.txt` entry. Entrance 5
   must no longer fall back to the map corner.
2. Place a custom entrance near objects. The click must not select an object.
   Click the hero marker, press G, and move it. Delete must clear it.
3. While a custom marker exists, the authored entrance selector must be disabled
   and Test Current Map must use the marker position.
4. Enable Godmode and launch JMV. Confirm the party receives Godmode after the
   normal player initialization. The installed autorun MOB remains untouched.
5. Open Game and Mod Setup under both Base and JMV. The configured profile must
   remain selected instead of jumping to the first available preset.

## Collection checks

1. Load one MPR and its main/Auto MOB set, then use File > Collections > Save
   Collection As. Confirm the file appears under `collections/` after Refresh.
2. Open the saved collection and confirm its MPR, ordered MOB list, and map.txt
   mapping are restored.
3. Edit/reorder the collection without loading it. Move the `.collection` file
   together with maps that use relative references and confirm it remains
   readable.
4. A missing MPR or MOB must produce one grouped error and must not partially
   unload/open the workspace.

Collection-backed multi-MOB test launch is not complete in alpha.9. Ctrl+F5
still requires exactly one active MOB.

## Patrol regression

For Radius unit NID 1038 in the supplied `zone1test5.mob`, pressing P inserts a
new first patrol point, changes Radius to Path, and reveals the retained route.
Undo removes only the new point and restores Radius; redo repeats the operation
as one compound action.

## Reporting

Attach `logs/workflow.log`, the relevant archived workflow log, the test-run
directory from the AppData quick-access command, and the exact map pair. Do not
send campaign saves or game resources through public channels.
