# Ataman 100 — EI-CREATE-CATALOG-100

Base: main f823e90bbfcbbc21edf425c73b94571632e22203. Local implementation; no publication. User resources remain read-only.

## Result

- Assemblies and model gallery are technical-category actions only. Order: object type, model gallery, assemblies. Both actions use 24px icons and compact fixed-height rows.
- Creatures/items retain original IDs and append names from the active resource roots' texts.res. Search includes both identifiers and localized names. Item key normalization handles spaces/underscores.
- DejaVu UNIT BoarTutor is «Молодая кабаниха»; UNIT BoarTutor01 is «Молодой кабан». Actual profile text takes priority over the example in the request.
- Lever rows start with model ID. The installed texts archives have no matching lever titles for the investigated models. Database descriptions remain an explicit fallback; tooltips identify the source. No titles invented.
- Lever texture selection uses existing catalog bindings before the previous unambiguous model-prefix fallback. stst58 → cavelevel00 is verified with actual resources and a rendered textured preview. Existing catalog browsing no longer rewrites the ready catalog.
- Missing item texture unhumath_03.bn.0.x2map.mmp is a separate converted map-resource issue. It is not silently replaced with an unrelated skin and remains outside this UI fix.

## Verification

- Release build and incremental rebuild: PASS; logs under build-tests-100-create-catalog.
- --version: 100. --database-creation-check: PASS, including localized names, stable IDs and fallback provenance.
- --database-preview-check with EI_TEST_CREATE_CATALOG_ONLY=1 and actual DejaVu/Base roots: PASS (category visibility, localized ID/name search, stst58 binding, technical order/icon sizes). First test attempt failed because it inspected QTableWidgetItem rather than the QComboBox property editor; corrected test passes. Images: database-preview-check-native/{creatures,items,lever,technical,lever-texture}.png. Form captures use untranslated test strings; normal application localization is unchanged. GL texture image independently inspected.
- --ui-widget-check: exit 1; the same ten clipboard flags fail as version99: compatiblePastePreview, compatiblePasteCommit, fieldPastePreview, fieldPasteCommit, fieldCopyAvailable, nonTextPropertyPaste, numericComponentPaste, selectedComponentCopy, componentPlatePaste, componentContextPaste. No new zero flags. Relevant category/toolbar/preview/layout checks pass. This is not an overall UI PASS.
- Active database.res, texts.res, model-catalog.json and base texts.res hashes unchanged against resources-before.json. Foreign QM restored byte-for-byte (EDA31955A912758ACB5A7BDE043BBF2EB5F8A2606FA32DC52AFA4156CA74ED9F).
- Fresh staging: raw EXE SHA256 C7C0CC6B31DDDED6762E66909FA3FB42F7EC724082C94DE320C7431AE8B4B27F; staged version, finalizer and independent distribution validator PASS. Staging was produced from the pending source changes on the base above.

## Delivery / remaining acceptance

Validated staging: build-staging-100-create-catalog. Permanent installation update waits for normal shutdown of the user's running editor99. Do not finalize the permanent installation. Script build-tests-100-create-catalog/install.ps1 verifies managed hashes, creates rollback99→100 and preserves all unlisted files.

Manual acceptance remains open: actual Russian dialog on the user's DPI/profile, creation/placement of creature/item/lever, technical gallery/assembly navigation. Headless render and widget checks do not establish owner acceptance; prior task99's manual acceptance remains open.
