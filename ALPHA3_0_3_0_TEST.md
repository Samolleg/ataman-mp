# EI Mapper Endurance 0.3.0-alpha.3

Focused synthetic-map, collection workflow, landscape integration, and
particle crash-bisection test build.

- Unmapped loose MPR/MOB pairs and collections now receive a run-local
  synthetic map identity instead of borrowing `gz1g` metadata. The synthetic
  identity has no minimap or travel zones and uses the loaded MPR dimensions.
- Play From Here coordinates are passed through the synthetic map bootstrap
  for initial launch and hot reload.
- Creating or duplicating a collection offers to load it after saving. If the
  native Save As dialog is cancelled, the collection editor reopens with its
  entered values intact.
- The Maps and Collections panel displays the collection's authored name,
  falling back to its filename only when needed.
- The completed Landscape Tools branch is integrated, including direct safe
  MPR save, terrain editing, AI-graph preview/generation, and its dock/UI fixes.
- Test-run results retain a native crash classification even when a surviving
  game process is subsequently stopped by the user.
- Particle runtime diagnostics now contain the empty and row-0 controls plus
  ten single-particle controls for IDs `0x2000` through `0x2009`. Superseded
  half, quarter, and nonzero-row payloads are not distributed.

Automated checks: full Qt/MinGW release build with fresh x86 runtime bridge,
AI-graph tests, JMV launcher and hot-reload protocol tests, particle catalog
tests, particle bisection fixture tests, and clean-package validation.

Manual focus: confirm an unmapped particle collection starts without inherited
`gz1g` minimap/zones and that Play From Here matches the chosen world position.
