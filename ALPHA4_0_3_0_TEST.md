# EI Mapper Endurance 0.3.0-alpha.4

Focused synthetic-map bootstrap correction.

- Fixed an unmapped collection becoming unresponsive after the main menu
  loaded. The bridge no longer scans or locks every process heap while trying
  to locate the bootstrap map record.
- Synthetic maps now use the fingerprinted runtime's byte-guarded map-table
  lookup and validate the returned zone, MPR, MOB, dimensions, and entrance
  storage before applying the run-local identity.
- The installed `texts.res`, `map.txt`, maps, and campaign saves remain
  unchanged.
- All alpha.3 collection, landscape, particle-control, and native-crash result
  improvements remain included.

Automated checks: full Qt/MinGW release build with fresh x86 runtime bridge,
hot-reload protocol tests, JMV launcher tests, bridge import audit, headless UI
widget checks, and clean-package validation.

Manual focus: open `00-empty-control`, place Play From Here, and confirm the
game proceeds from Main Menu readiness into the synthetic map with no inherited
minimap or travel zones and with the player at the selected coordinates.
