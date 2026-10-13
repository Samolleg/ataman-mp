# HD zone1: exact refusal diagnosis, 2026-10-13

Read-only investigation requested by the owner: establish the cause of `HD recovery does not bind to the current slot layout`. No program, map or RES repair is performed.

The current zone1.mpr sector namespace is `zone1_4615cc40315f`. Its bound profile in HDPack_Terrain.res is `ei_atlas_profile_zone1_4615cc40315f_c7debc4a8c1d7689.json`, provenance `portable-working-modern-atlas-v1`, atlasSlotCount1024 /textureCount16.

The same namespace's HD receipt `ei_atlas_hd_recovery_zone1_4615cc40315f.json` records physicalSlots896, outputSlots960, pages15. Exact member/index comparison finds two layout mismatches: outputSlots960 vs1024 and pages15 vs16. Recorded widths and byte lengths for the original15 pages still match. The guard returns at the first slot-count comparison, before publication.

The additional page is active artwork, not solely padding: current map sectors use Ground slots960–988 (for example slot965380 cells,966392,967381,968375). The working profile declares material signatures for these added slots.

Code chain:
- `CView::saveChangedLandscapeDocument` stages active tiles through `CTerrainVirtualAtlas::buildWorkingPreviewPackage`, creates a working profile, and replaces page/profile/semantic members in the same namespace using `buildWithPublishedMembers(...,true,...)` (source/render/view.cpp10490–10680).
- That replacement list contains no updated HD receipt. Old unrelated members are retained, including the existing `ei_atlas_hd_recovery_<namespace>.json`.
- `CView::republishTerrainAtlasHd` later reads that old receipt and passes it to `rebuildTerrainAtlasHd` (source/render/view_hd_republish.cpp100–103).
- `rebuildTerrainAtlasHd` requires the old outputSlots to equal the current physical page count*64 and rejects the mismatch (source/render/terrain_hd_republish.cpp919–920).

Established cause: saving an extended working atlas updates the current map's page/profile layout but leaves the prior HD receipt for the former layout. Repeat HD conversion treats that stale receipt as current. This evidence does not implicate another map's atlas and does not establish that existing textures were moved; expansion alone explains the first failing guard.

Evidence/script: build-diagnose-hd-zone1/receipt-comparison.json and inspect.ps1. The next implementation task is to reconcile/invalidate stale HD receipts when saving the extended layout while preserving validated original provenance and newly painted artwork. Blanket removal of the safety guard is not an established fix.
