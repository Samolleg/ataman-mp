# Ataman163 — local palette and retained thumbnails

Source main `7dff284df690bf04e5d77e67695e4e6ff7ebcd1c`. EXE SHA256 `D2FE23A8A5E41B8EEE1B846A4E6C5E975912913E41284A38EFCEF2C69264FC01`.

The owner reported palette cards flashing and the original-variant strip returning to placeholders after HD rebuilding. Global palette hydration and paint/transition donor resolution still used configured game archives. Catalog rebuild also rebuilt authored preview vectors without cached images; every hydration update destroyed and recreated all palette buttons.

## Result

- Global palette bindings, confirmation witnesses, uniform donors and transition prewarming use existing `hd-library/sources` only. If the library is absent, its initial import runs in a worker; no silent fallback to mutable game RES. Current-map rendering/loading/publication retains the map's actual resources, as required by its saved atlas.
- Ready original previews use the retained exact-source cache during catalog rebuilding. Successful immutable bindings, missing-source results and decoded transitions survive map publication; resource setup refresh still invalidates them. Warm hydration skips resolved work.
- Preview changes update existing buttons; grid structure changes still rebuild the appropriate layout. The selected variant strip is retained when its content is unchanged, and its horizontal position is restored when content changes.
- Unavailable exact sources remain explicitly unavailable. This fix does not substitute invented artwork for missing reviewed variants or change texture-generation algorithms.

## Evidence

- Release build passed. Snapshot `697aa968` plus the recorded changed files from subsequent scoped commits matches the final source above. Final incremental logs show actual recompilation/linking after copied-source timestamps were refreshed.
- Native hidden Windows/OpenGL zone2 fixture: **15 PASS /0 FAIL**, exit0. All global palette bindings come from the installed local library; ready ground, underwater and overlay thumbnails and original variants remain pixel-identical after the production catalog rebuild. Native button identities and outer scroll position survive. Warm rebuild queues zero archive-hydration jobs;38 exact bindings and88 material sources remain available.
- Initial hydration35257ms; the repeated catalog rebuild reuses its completed results. No claim of instant first-start decoding is made.
- Visual inspection: settled real panel captures before/after are identical, SHA256 `425065088F4DB61129F56E534CDB392C8CC8393F046043287CEC294759B7F71E`. The earlier14/0 probe captured a frame before the test resize's deferred layout finished; the amended probe waits for layout settlement and explicitly checks scroll retention. No extra production scrolling workaround was introduced.
- `--ui-widget-check`: PASS, exit0; palette/layer/selection and other widget regressions pass. Both native checks used in-memory clipboard isolation with0 native attempts and unchanged clipboard.
- The fixture invokes the same production catalog rebuild used by HD adoption. It does not repeat the unchanged multi-minute texture reconstruction and paired-save checks already completed for162. Interactive owner acceptance remains distinct from hidden native UI capture.
- Logs, final identity and the two panel captures are retained under `build-tests-163-palette`. Package/installation receipt follows in the repository report.

## Delivery

Clean package finalizer passed. Installed163 in `artifacts/EI-Mapper-Ataman-2-first-test`:143 managed files,4 replaced,1 added; all24038 unlisted user files including hd-library retained byte-for-byte (digest `CE1C1F14558B134FD2E730FA0CD40CFFFFFE2A36DA57272E0DECED2DE0B3E823`). Installed program hashes match the finalized manifest. Packaged evidence predates installation; this repository report adds its receipt without altering finalized package bytes.

Deleted1879978257 bytes of this task's temporary source/build/staging, private map/archive copy and identified older rollback161. Retained verified rollback162 and compact evidence. Game files were read only. Owner manual check: reopen the mapper, select a material/variant strip, rebuild an atlas, and observe that already loaded thumbnails stay visible. Hidden native visual checks above are completed; owner interaction is not claimed.
