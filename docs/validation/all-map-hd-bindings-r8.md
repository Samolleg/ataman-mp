# All-map exact HD bindings — r8

User request: enable painting access for all maps, continuing r7's exact-hash
allowlist rather than restoring r6's generic structure matching.

The editor's native complete-atlas decoder measured all 38 built-in namespaces
from the installed HD archive, reading only. Four actual revisions were already
approved; 34 additional `hd` bindings were added. Prior bindings, material labels,
slot signatures, runtime types and all other preset fields are unchanged.
No renderer or loading code changes are required.

## Reproduction and coverage

`installedHdAtlasAllowlistProbe` enumerates the catalog and decodes each complete
atlas through the strict archive resolver. Before the data change, it reports
38 measured atlases and fails with 34 missing approvals. After the change:

- 38/38 actual HD atlas hashes have exact approved profiles.
- 88/88 global material families have at least one actual, approved uniform
  source after the existing runtime-type filtering.
- Authored transition sources cover all 38 namespaces.
- All 41 installed MPR files resolve their inner namespace and page count to an
  approved actual HD profile, including maps sharing an atlas.
- Nine focused QtTest cases pass (11 with setup/cleanup), including strict
  unknown-hash/slot rejection, archive coherence, embedded profile/semantic
  binding checks and save source-race rejection.

Run the optional read-only probe with `EI_TEST_HD_ATLAS_ARCHIVE` and optionally
`EI_TEST_HD_MAP_DIRECTORY`. `EI_TEST_HD_ATLAS_REPORT` writes only measured
namespace, slot count and hash metadata to an explicitly supplied report path.

Release editor and current runtime components build. The runtime build uses the
installed Build Tools through local helpers because the wrapper names an absent
Visual Studio edition. Fresh database object creation/history/save checks pass.
The broad UI-widget diagnostic still reproduces its known `0xC0000005` failure.

Separate unpacked `EI-Mapper-Endurance-0.5.0-alpha-all-map-hashes-r8-test` passes
both staging and delivery validation; tested/delivered executable SHA-256:
`809cb01e05ba02a68aeeb53d02a27757dd0304051100394a521d43aa3266c826`.
Previous tester folders are preserved. The finalized package was not launched.

## Limits and manual acceptance

The approval is for these exact installed revisions, not arbitrary future HD
repaints. Global runtime-type conflicts still exclude individual incompatible
variants; every current global family nevertheless has an approved compatible
source. No type policy or semantic slot mapping has been changed.

Check Modern Ground/Overlay thumbnails after background loading, paint several
materials on a test map copy, inspect transitions, switch maps and verify
undo/redo/save/reopen. Native decoding and source availability are verified;
interactive artwork, every transition and full-load timings for r8 are not.
Installed resources and maps were not modified, copied into the package, or
committed. Existing database placement behavior is retained.
