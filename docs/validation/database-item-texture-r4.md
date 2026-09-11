# EI-ADD-FROM-DB-001-R9: item texture identifier, r4

The user placed `cheat dagger.bronze` in zone1 with r3 and reported a white item
in the game despite its textured editor preview. Read-only inspection of the
saved ordinary object found `unhumadg_03.br.1.x2.mmp` in both texture fields.
The exact archive member with one `.mmp` exists; the double-extension member does
not. The ordinary game texture lookup appends `.mmp` to the map identifier.
The editor normalizes the input before resource lookup and therefore masked the
bad serialized identifier. This is a creation serialization defect, not evidence
that the user's texture is absent.

## Reproduction and fix

A focused test first failed: an item entry with surrounding whitespace and an
uppercase terminal `.MMP` was saved unchanged instead of as the required texture
identifier. New ordinary item creation now uses the established texture-name
normalization before writing both texture fields. Material tokens and `.x2`
remain intact. Empty normalized texture names are rejected.

Existing imported maps and installed files are not rewritten. For the reported
object, changing both texture fields to `unhumadg_03.br.1.x2` is the direct repair.
No installed map or game asset was copied or modified; this report includes no
private source, source excerpts or private provenance.

## Validation and limits

- Red test reproduced before the fix; 13 focused QtTest results pass afterward,
  with zero failures/skips, including catalog and constructor regressions.
- The installed-resource picker diagnostic selects `cheat dagger`/`bronze` and
  requires `unhumadg_03.br.1.x2` exactly. It checks the exact indexed resource name,
  independent of permissive preview aliases; a terminal `.mmp` fails.
- Primary/secondary identifiers survive save/reopen; placement, pointer movement,
  cancel, commit, undo/redo and conversion to a lever pass.
- Base desktop and JMV desktop/ANGLE preview/placement checks retain textured output. Synthetic fresh
  creation and save/undo/save/redo/save pass for creatures, items and levers.
- Release editor and unchanged runtime components build. The known release-wrapper
  Visual Studio Community path issue uses the same local Build Tools fallback.
- The existing broad UI-widget-check access violation is unchanged; no new broad
  UI pass is claimed.
- r4 passes the distribution finalizer in staging and its permanent tester folder.
  Both executables match the tested release SHA-256:
  `39854c58bdd87bf1b6a1ba351667e8051ba454355d57b6c1b1c5914f036146a0`.
  Previous tester folders/settings were preserved; the packaged GUI was not run.

Subsequent user acceptance failed for both the corrected old and newly placed
dagger. See [continuing investigation](database-item-texture-r5-investigation.md).
The extension correction does not resolve the gameplay texture failure.

Confidence: the invalid persisted name and exact resource mismatch are confirmed.
No corrected in-game frame was captured; the user must reload the corrected map
and verify appearance and interaction. Editor screenshots alone are not gameplay
acceptance. Parent scope remains ordinary item placement, with the user supplying
lever conversion and script behavior.
