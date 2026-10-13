# Ataman165 — local palette survives reopening maps

Owner evidence: installed164 showed dashes for the entire palette although hd-library sources existed. Installed workflow.log13.10 records88 material sources at14:19:35, then0 after loading zone1 again at14:19:57, with38 exact archive bindings retained. No library files were missing.

## Cause and change

Map unloading clears the authored atlas snapshot. During the next catalog construction, cachedGlobalUniformSource used that empty snapshot to choose a texture-size cache key, before the new authored snapshot was adopted. Ready library pixels therefore missed synchronous lookup. The deferred path then skipped the ready bindings without republishing their cached pixels/source ranks into the new generation, and marked materials unavailable.

165 uses the incoming loaded atlas dimensions for synchronous cache lookup and variant previews. Ready binding tasks republish already-decoded pixels and provenance into the new palette generation without opening/decoding archives again. Actual changed resource configuration retains its invalidation path. Game resources and the user's open map are not modified by this fix.

## Validation

- Native checks use an isolated copy of the current zone1/HDPack and the installed local library read-only, hidden Windows/OpenGL widgets, an exclusive UI lock and in-memory clipboard.
- The old164 warm-rebuild-only harness passed15/0: it did not close and reopen the map, so it missed this regression. The expanded harness checks paint availability/source counts as well as pictures for Ground, Underwater and Overlay through rebuild and reopen.
- First165 probe passed rebuild checks but stopped at the test's incorrect attempt to open a second map without closing the first. Only that owned hidden process was stopped. Harness now calls production unloadLand(false) before loading again. This incomplete probe is not PASS.
- Final native reopen and package delivery: pending.

Interactive owner acceptance is distinct from the native screenshot inspection.
