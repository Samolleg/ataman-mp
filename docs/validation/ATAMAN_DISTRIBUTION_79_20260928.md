# Ataman79 distribution — 2026-09-28

Task: EI-WATER-DISTRIBUTION-79. Owner requested a distribution after confirming
that the zone18 water issue was resolved. This is packaging of the accepted79,
not a new implementation/version.

Source: 4c7f58c479869c9168f6d100158ed102dc436541.
Branch: codex/water-animation-baseline-v79.
Delivery: C:/GOG Games/maped/ei-mapper-ataman/artifacts/EI-Mapper-Ataman-79-distribution

The existing finalized source package passed validation before copying to a new
owned build-staging-79-distribution. Its EXE matches build-release-79-water-final
and the accepted permanent installation:
F9B68DABCFFD38CA0DF99AC96DD29F1A80FE509C4E12355CEF26144BF5EED8C2.
The new stage --version reported EI Mapper Endurance Edition79, exit0.
Finalization succeeded. The delivery copy independently passed the distribution
validator: complete DLL/runtime/license set, exactly one current changelist,
all manifest hashes, no settings, logs, game data or compiler debris.
Neither finalized stage nor delivery was executed after finalization.

128 files total (127 managed plus SHA256SUMS.txt),77.9 MiB. All127 managed files
and the manifest in artifacts/EI-Mapper-Ataman-2-first-test already match, so
no replacement or user-file changes were needed. No ZIP requested/created.

Prior same-binary checks:131 atlas tests PASS with no failures/skips; full native
UI check PASS. Owner subsequently confirmed the original water issue resolved.
These functional checks were not redundantly rerun for byte-identical packaging.
Other open defects are not closed by this distribution.

Evidence: build-tests-79-water/distribution-version.out,
distribution-finalize.log, distribution-validate.log; source validation report
ATAMAN_WATER_BASELINE_79_20260928.md.
