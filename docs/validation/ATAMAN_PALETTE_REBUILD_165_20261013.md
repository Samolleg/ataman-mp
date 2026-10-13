# Ataman165 — local palette survives reopening maps

Owner evidence: installed164 showed dashes for the entire palette although hd-library sources existed. Installed workflow.log13.10 records88 material sources at14:19:35, then0 after loading zone1 again at14:19:57, with38 exact archive bindings retained. No library files were missing.

## Cause and change

Map unloading clears the authored atlas snapshot. During the next catalog construction, cachedGlobalUniformSource used that empty snapshot to choose a texture-size cache key, before the new authored snapshot was adopted. Ready library pixels therefore missed synchronous lookup. The deferred path then skipped the ready bindings without republishing their cached pixels/source ranks into the new generation, and marked materials unavailable.

165 uses the incoming loaded atlas dimensions for synchronous cache lookup and variant previews. Ready binding tasks republish already-decoded pixels and provenance into the new palette generation without opening/decoding archives again. Actual changed resource configuration retains its invalidation path. Game resources and the user's open map are not modified by this fix.

## Validation

- Native checks use an isolated copy of the current zone1/HDPack and the installed local library read-only, hidden Windows/OpenGL widgets, an exclusive UI lock and in-memory clipboard.
- The old164 warm-rebuild-only harness passed15/0: it did not close and reopen the map, so it missed this regression. The expanded harness checks paint availability/source counts as well as pictures for Ground, Underwater and Overlay through rebuild and reopen.
- First165 probe passed rebuild checks but stopped at the test's incorrect attempt to open a second map without closing the first. Only that owned hidden process was stopped. Harness now calls production unloadLand(false) before loading again. This incomplete probe is not PASS.
- Second probe encountered a Qt5Gui access violation because the direct harness reopened after unloadLand had called doneCurrent. The harness now restores the GL context before loadLandscape. This failed probe is retained as native-final; it is not PASS.
- Final native check (`build-tests-165-palette/reopen.stdout.log`):22 PASS /0 FAIL, exit0. All88 paint sources survive rebuild and close/reopen; all three layers retain thumbnail pixels, original variants and paint availability. Warm rebuild queues0 archive jobs. `reopen/palette-after.png` and `reopen/palette-reopened.png` were visually inspected: material textures are present, no empty main palette. Selection defaults on opening a map as before.
- Native widget lifecycle check PASS, exit0. Release builds PASS. In-memory clipboard unchanged,0 native attempts.
- Source main1d14347059dc85e2b045e7247f124c8269c0532c. EXE SHA256 FF6AD030D9B92AABEDD81F2F71EE31D129FF95AAF3869382813B932E5F7D7174. Clean package finalizer PASS. Installed165 into artifacts/EI-Mapper-Ataman-2-first-test after exact host process check:145 managed files,4 replacements,1 addition. All21760 user files retained with identical digest3F00BBF13B27342384D8A95F6184396A804DC820E48F3DE9DDE93B57ADDDB52D. Retained verified rollback164. Removed1754347120 bytes of owned private fixture/build/staging and previous rollback163. Receipts: build-tests-165-palette/install-receipt.json and cleanup-receipt.json. The installed report copy records pre-install validation; this repository copy also records delivery.

Interactive owner acceptance is distinct from the native screenshot inspection.
