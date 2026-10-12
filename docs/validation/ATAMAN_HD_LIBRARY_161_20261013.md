# Ataman161 — local HD library

Source main `9e31b80d11be827b2383b0574606c19347fb2e17`. Request: store conversion/painting donors beside the mapper instead of rereading mutable game RES archives on each operation.

- `hd-library/sources`: atomic initial import of terrain BC1 pages at native128/256 tile sizes and atlas profiles; other game assets are excluded. Source archives remain read-only. Existing library is authoritative, without automatic invalidation after game publication.
- `hd-library/index`: full-content-bound matching index; local archive names/order and SHA preserve identity across directory moves. The index is saved before map-specific conversion so a rejected map does not discard successful indexing.
- `hd-library/variants`: existing compatible bank migrated with exact page hashes and local donor witnesses. Relative `@library/` references keep it movable. New generation stores compact map classes/animation ranges in `map-witnesses.json`; maps need not be copied wholesale or reread on subsequent generation.
- Map loading and output publication retain game resources as required by the existing map format. This change isolates HD donor preparation, not the entire game runtime.

## Verification

- Production Release161 built successfully from the scoped source snapshot; final incremental compile only changes the matching-index key. No production code differs from the recorded source commit.
- `localLibrarySurvivesGameChangesAndCancellation` + `progressCacheAndCancellation`: **4 PASS /0 FAIL**, including lifecycle. Cancellation leaves no published sources; import excludes unrelated assets; local conversion donors work after deleting the game RES; matcher cache and painting-bank mip blocks survive relocation; damaged local data is rejected. Existing full-SHA cache invalidation regression passed.
- Native Windows `--ui-widget-check`: exit0, renderer/widget checks passed. Clipboard isolation reports in-memory backend, nativeAttempts0 and unchanged clipboard.
- Real Dejavu/Base/JMV import:3 terrain-only archives,15296 HD donor tiles. Cold preparation196768ms; repeat with a deliberately unavailable source path succeeded in5045ms with `cacheHit=1`. Existing326 painting variants migrated; all63 source dependencies point to `@library/`, none to game archives. Original texture payloads remain exact; no regeneration of those variants was needed.
- Guarded160→161 installation completed:141 managed program files,6 replaced,1 added. All23997 preexisting owner files retained (digest `FAA603E6E756C192A006289984283AB8FCF1A245AE28555BC3C6BDA7C282BDF7`).13 library files moved into `artifacts/EI-Mapper-Ataman-2-first-test/hd-library` and verified by SHA after transfer. Source libraries remain private unlisted user data, outside the package manifest.
- EXE SHA256 `D41E15722EF9B3FE27B6389BBA8C8B698A69C6DC1E52423F700F55453DA3B2BD`. Clean package finalizer passed. Retained rollback160; receipts/logs in `build-tests-161-library`. Packaged report predates installation; this repository report records final evidence without changing finalized package bytes.
- This validates local donor preparation, matcher reuse and painting-bank loading. It does not claim a new gameplay acceptance or a new visual review of every map; terrain generation/rendering algorithms are unchanged.
- Cleanup completed: own161 staging, source ZIP and temporary build snapshot removed; previous owned rollback159 removed after verifying retained rollback160. No repeated game copies remain from this task. Installed library size1488225913 bytes (about1.39 GiB); it is the retained working data, not a disposable build artifact.
