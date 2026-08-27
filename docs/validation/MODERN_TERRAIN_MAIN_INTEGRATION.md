# Modern terrain integration into main

This records the conflict decisions for merging `feature/modern-atlas-mvp`
into the current Endurance `main` line.

## Decisions by subsystem

- **Semantic authority:** kept the feature branch's complete R162 catalog,
  generator, decisions, and semantic/topology tests byte-for-byte. The checked
  preset SHA-256 is
  `1E0609597348F6405BC2AF04870773C036DB779A1D74FD3D1AE957D2ED4E9DF3`.
- **Terrain documents and renderer:** kept expanded working-atlas addressing,
  physical/provenance identity, runtime tile type, rotation, duplicate-aware
  reuse, and immediate GPU refresh. Main's structured MPR material and animated
  tile records, persistent preview buffers, and material attributes remain.
- **Paint transactions:** kept the feature branch's Modern, Classic Auto, and
  Manual behavior; first-stroke transaction ownership; Shift+R transaction;
  and paired MPR/RES draft/finalize flow. Main's general operation cancellation
  infrastructure remains outside the terrain-specific atomic stroke path.
- **Save and validation:** combined main's broader staged MPR/MOB/canvas
  validators with the feature branch's validated MPR-byte commit and terrain
  atlas identity checks.
- **Runtime test launcher:** retained main's generic owned-process restart,
  recovery hardening, resource-path registration, minimap staging, and runtime
  cache behavior. Added the feature branch's terrain archive rebase, expanded
  atlas preload/observation, and atlas-change restart decision.
- **Resources and packaging:** retained main's generalized resource-manager
  caching and package cleanup while adding terrain atlas resources, masks, and
  distribution requirements.
- **Application/UI:** retained main's database/resource browser, viewport
  visual channels, sculpt performance work, settings, and behavior validation;
  added the complete modern terrain UI and its headless widget contract.
- **Tests:** preserved both branches' independent tests. Mechanical conflict
  overlap in two test files was repaired as a union: duplicate declarations
  were removed, independent test bodies were restored, and the launcher test
  project now links its virtual-atlas dependency.

## Validation

- Semantic catalog: 155 passed.
- Semantic topology: 4 passed.
- Endurance document tests: 166 passed, 7 expected fixture-gated skips.
- Virtual atlas: 115 passed.
- Terrain resolve audit: 4 passed.
- Landscape widgets: 8 passed.
- JMV map-test launcher: 51 passed, 3 expected fixture/runtime skips.
- RES archive tests: 16 passed.
- Resource-name tests: 31 passed.
- Retained Qt 5.12.12 MinGW 7.3 x64 release wrapper: passed.
- Explicit headless `--ui-widget-check`: passed.
