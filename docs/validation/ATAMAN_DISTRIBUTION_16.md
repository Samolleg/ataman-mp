# Ataman 16 distribution — 2026-09-18

Task EI-DISTRIBUTION-016. Branch codex/distribution-v16.
Owner requested a distributable version; no publication or ZIP requested.
Code/binaries unchanged: source 2f747dd57469928bdc752050d4eeee5af2edac30.
Version remains 16 under the no-bump-for-repackaging rule.

Input build-staging-16-paths reverified against all 100 manifest hashes and
build-release-16-paths/release/EIMapperEndurance.exe. Copied to a newly created
build-staging-16-distribution. Replaced packaging-only Readme/CHANGELOG with
recipient instructions, no author profile details, and explicit validation gaps.
Retained LICENSE.txt, NOTICE.md, synthetic fixtures, Qt/MinGW and runtime files.
Finalizer ran only on this fresh owned staging; clean validator passed.

Delivered folder:
C:/GOG Games/maped/ei-mapper-ataman/artifacts/EI-Mapper-Ataman-16-distribution

Independent checks passed on staging and final copy: version 16, one current
changelog, safe unique manifest paths, 100 recomputed SHA256 hashes, complete
one-to-one file coverage excluding manifest, exact tested EXE equality.
Final clean distribution validator passed. Text/config scan found no author
absolute paths or saved resource/map roots. Total 101 files, 76,439,708 bytes.
EXE SHA256: 87AD51EDA304A1AB39BAEF2CE299F4A75E4067ECB6BE4B42CC68903B28421CF0.

No packaged GUI launch, no game resources/settings/saves/cache/logs included.
Permanent user installation was not modified. No new code/build/test rerun:
existing binary checks are documented in ATAMAN_WORLD_PATHS_16.md (19 document,
49 integration, 47 scenario PASS; UI timeout). Full manual scene acceptance and
another-PC compatibility remain unverified. Packaging completeness is verified,
not a declaration of stable-release acceptance.
