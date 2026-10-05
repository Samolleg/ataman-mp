# EI-MAP-WEAPONS-112 — 2026-10-05

## Scope and provenance

Owner requested standalone weapon placement from mapweapons.res/catalog beside the active profile database, Russian texts.res names, database material selection and automatic texture. Armor is deferred. Source base main c145448f25aa9f793ac415c9c8e3675d9ff715fa; version112. No game resources, catalog, maps, database or settings were modified. No interactive application was launched. Private-engine skills unavailable; implementation uses ordinary repository code and read-only resource indices/FIG data.

Read-only inputs (Dejavu/res):
- mapweapons-catalog.json SHA256 7E7D348CA92E6742A7E0338CB46026184E3D0E29BE3E2079F7E960B90232109E.
- mapweapons.res SHA256 CC5A5ACE0F71D60FBDA90A384BD490092BCD0AEFD5E038C39C062D10C96A4E40.

## Implementation

Catalog schema2 resolves relative to the active database, never a hardcoded installation. Validate weapon name/class/model-index against active DB; record_index/hash do not bind positional records after unrelated DB edits. Require exact mod/bon members and reject unsafe archive paths. Only confirmed weapon records appear when the catalog is present; an invalid present catalog cannot silently restore legacy mappings. Other profiles without a catalog keep their former item path.

99 weapon identities use146 linked model variants out of203 archive models. Left/right variants are explicit; default right where available. Existing texts.res localization/search is retained. Material list comes from active DB, uses Russian material names, marks absent textures and chooses first available. Geometry and both texture identifiers persist as an ordinary world object. No unhuma body-part proxy, transient equipment resolution or forced map texture suffix.

Inspected extracted FIG UVs for dagger/sword/shield/two-handed club span the full atlas. Their direct original x2 textures render correctly without applying an extra UV transform. JMV requires its exact x2 resource; missing texture does not substitute a different material, variant or legacy map texture. Archive provenance is checked before preview creation.

## Automated evidence

Fresh shadow build build-release-112-map-weapons; final incremental repairs use the same112 definition. One intermediate compile failed on missing QJsonDocument include, corrected before validation. Final build and --version PASS.

Native hidden checks in build-tests-112-map-weapons:
- database-preview-check with EI_TEST_MAP_WEAPONS_ONLY=1: PASS. All146 linked models decode;618 exact weapon/material textures present,187 absent, no weapon lacks all materials.8 actual placements: ritual dagger, stone sword, two-handed bone club and small leather shield with two materials each. Russian list names, model variants, colored GL previews, pointer placement, Escape, positive IDs, exact model/texture save/reopen, one-step Undo/Redo PASS.
- Synthetic index/catalog fixtures: valid semantic link, stale model index refusal, unsupported schema, missing catalog legacy mode, path escape refusal, unavailable archive in profile, unbound model, invalid material and missing exact texture refusal PASS. Fixtures are authored synthetic bytes; no game files copied for editing.
- Full database-preview-check: PASS, including creatures, ordinary DXT, actual item save/reopen, conversion to lever retaining ID, texture after conversion, pointer movement and guarded candidate.
- EI_TEST_CREATE_CATALOG_ONLY: PASS, Russian search, category visibility/order/icons, lever texture, gallery batch/cancel (32ms for two objects).
- --ui-widget-check: FAIL with exactly the same10 clipboard flags as111; no new failure flags. This is not a full UI acceptance PASS.
- git diff --check: PASS for own changes.

Direct GL images inspected: weapon-uhwdg03r.png, weapon-uhwsd01l.png, weapon-uhwth03r.png. Materials display on geometry. QWidget grab has a black GL child because it does not capture QGLWidget; GL renderPixmap images above are the render evidence. No in-game runtime acceptance claimed.

Final EXE SHA256 BF34A3EB8501EBC83C10FFB1DADFA1150A6CA672DF549064F12E9B03A2638FF9.

## Handoff status

Source commit main9bffd973b8945823e377dd7093ac07d201596a46. Fresh build-staging-112-map-weapons: source/staged EXE hash, --version, finalizer and independent distribution validator PASS. Permanent artifacts/EI-Mapper-Ataman-2-first-test updated111→112:127 managed files and manifest checked,6 program replacements plus manifest backed up;16439 unlisted user files SHA256 byte-identical. Sole verified rollback artifacts/rollback-EI-Mapper-Ataman-v111-to-112-20261005 retained; previous owned110→111 rollback removed after checking both generations. Independent post-install manifest/hash pass confirmed all127 installed payloads.

Scratch stage/install wrappers initially logged a duplicated UTF-8 BOM on their first preference assignment; the remaining guarded steps and hash checks completed. Wrappers corrected; a separate Stop-on-error installed manifest/hash verification passed. No package validation error or resource mutation occurred. Foreign translation QM restored byte-exact to EDA31955A912758ACB5A7BDE043BBF2EB5F8A2606FA32DC52AFA4156CA74ED9F. Other unrelated dirty files/ROADMAP entries preserved. Manual owner check remains: Create → Items, Russian search, material and model choice, Place/Escape/Undo/Redo, save/reopen and test appearance in game. Missing187 material textures are resource gaps, shown explicitly; no resource repair performed. Armor remains the next separately requested stage.
