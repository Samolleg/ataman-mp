# Ataman 77: explicit world profile import (2026-09-27)

Task: EI-WORLD-PROFILE-IMPORT-77. Base: 4afd00c738248b475cf177273b257592624df1a2.
Branch: codex/world-profile-import-v77.

The portable PZ mod-local JSON has 14 zones in three allods under jamais-vu;
current settings select deja-vu. loadEditorWorld filters profileId, returning no
zones for DejaVu. The directory is correct; the profile association is missing.

Added an explicit World maps command to copy a chosen stored profile to the
active profile. No automatic migration on inspection. Copy the complete JSON
profile object, changing only profileId; retain the source, origin precision,
relative paths and unknown properties. Validate before writing. Lock, compare
loaded bytes, preserve a verified .before-profile-import.bak, then QSaveFile.
Existing destination profiles, external modifications and conflicting backups
are refused without overwriting them. The command refreshes the editor world.

Validation:
- Synthetic baseline: DejaVu loads zero zones, inspecting leaves bytes intact;
  the new import test fails while the import implementation is a no-op.
- Implemented document suite: 22 PASS, 0 FAIL, 0 SKIP. Includes actual PZ JSON
  read-only resolution with EI_WORLD_LAYOUT_CHECK / EI_WORLD_ROOTS_CHECK.
- Synthetic import, reopen, independent coordinate edits, unknown properties,
  exact backup, missing source, wrong destination, duplicate destination,
  external change and conflicting backup checked. No live game JSON modified.
- Release wrapper PASS (including both x86 runtime components). Staged --version
  reports77; --ui-widget-check PASS, exit0. Source EXE/staged SHA256 identical:
  C35FF5BB4713889447DCFB3A68AFFB29906B5440CD7A20097803CBF5C8E90968.
- Preliminary raw-build launch stalled, including a native --version timeout;
  not counted as PASS. Complete staged runtime passed the checks above.
- Finalizer and independent distribution validator PASS. Staging:
  build-staging-77-world-profile. No execution after finalization.

Manual acceptance: World maps -> transfer coordinates -> jamais-vu -> confirm;
reopen world list, inspect placement, move a map, reopen and verify its origin.
This manual operation was not performed on the owner's game. Earlier gameplay,
terrain/patrol and launch acceptance tasks remain open.
Handoff: installed77 into artifacts/EI-Mapper-Ataman-2-first-test after checking
no running processes in that folder. Replaced5 / verified127 managed files;
settings hash unchanged. Verified rollback at
artifacts/rollback-EI-Mapper-Ataman-v76-to-77-20260928. Earlier rollback folders
were retained under the earlier cleanup restriction. Owned stalled test
processes were terminated by exact path and test arguments. No user GUI launched.
