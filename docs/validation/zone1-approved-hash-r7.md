# Zone1 exact atlas allowlist — r7

The user explicitly withdrew the r6 structure-only matching experiment after a
65.4-second Zone1 load and requested only approval of their specific pixel hash.

Renderer and archive-binding code and translations are restored byte-for-byte
to pre-experiment commit d1b977b0. This removes the per-transition archive
fingerprint scans responsible for the observed minute-long hydration planning.
The unfinished optimization test is removed. Database placement changes remain.

The only new production behavior relative to r5 is one additional `hd` binding
in the Zone1 preset (512 slots):
`a44f78f46933dfba29125ec6867c3541acb6be56d93083afa6a6ce9616b5f997`.
Existing hashes, slot semantics and all other presets are unchanged. The full
hash was measured from the installed atlas during the preceding read-only probe;
the prefix alone is not used. Unknown hashes and wrong slot counts stay rejected.

## Checks

- The focused allowlist assertion failed before the JSON change and passes after.
  It checks exact profile acceptance and availability to both global uniform
  materials and authored transitions, plus rejection of an unknown hash and
  a mismatched slot count.
- Eight focused cases pass (10 including setup/cleanup): approved HD sources,
  exact hydration wiring, coherent archive binding, exact embedded binding,
  semantic profile and field checks, embedded round-trip, and save source race.
- Structured comparison with r5 confirms that removing the one new binding
  reproduces the original preset catalog exactly. Renderer and translation
  comparison against r5 has no differences.
- Release editor and both current runtime components build. As with r6, local
  runtime helpers select the installed Build Tools because the release wrapper's
  hardcoded Visual Studio edition is unavailable.
- Fresh creature/item/lever creation, actual history/save sequences and JMV
  desktop preview/pointer placement checks pass on r7. Broad UI-widget check
  reproduces its known `0xC0000005` access violation.
- Separate unpacked `EI-Mapper-Endurance-0.5.0-alpha-zone1-hash-r7-test` passes
  staging/delivery finalization; tested and delivered executable SHA-256:
  `6269e11997ba495801b25c3dbb45f54d53cbb23b68b4ba395d3b70918043871a`.
  Previous folders remain intact and the finalized package was not launched.

## Remaining manual verification

Open Zone1 with the user's HD resource profile, check loading responsiveness,
palette thumbnails, painting and transitions on a test copy. Actual r7 map-load
timing and interactive artwork are not measured by these focused tests. Other
modified atlas revisions remain subject to exact hash approval; adding Zone1
does not approve bz2g or other namespaces. The known broad UI diagnostic access
violation remains outside this bounded change. Installed assets are not modified.
