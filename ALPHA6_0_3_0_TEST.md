# EI Mapper: Endurance Edition 0.3.0-alpha.6

This build replaces alpha.5's unsafe live synthetic-zone mutation.

Please test:

1. Open `00-empty-control` without assigning a borrowed map identity.
2. Place **Play from Here**, then use **Test Current Map**.
3. Confirm the map loads with no inherited minimap/travel zones, the player
   appears at the selected position, and the game remains stable for at least
   30 seconds before exiting normally.
4. Start an ordinary mapped zone, then hot reload into the same mapless
   collection and repeat the stability/position check.

Expected runtime log markers include `MAP_TABLE_REDIRECT` and
`SYNTHETIC_MAP configured preloaded`. The installed `texts.res`, MPR, and MOB
files must remain unchanged.
