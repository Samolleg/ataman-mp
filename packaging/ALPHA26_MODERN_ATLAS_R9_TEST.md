# Modern atlas r9 tester notes

This build fixes the atlas-wide corruption seen after repeated Material stamps.
Authored JMV atlas slots are immutable during painting; generated exact blends
are appended from slot 512 onward. The renderer also preserves the original
page order when an expanded atlas grows to multiple rows.

Material identities with the same display name are no longer merged merely by
name across unrelated map atlases. Generated transitions use deterministic,
irregular interior masks while retaining exact shared-edge pixels. The counter
now states the authored, generated, and total active slot counts explicitly.

## Focused manual test

1. Open a fresh copy of Zone 2 and select **Paint land tiles > Material**.
2. Stamp several visibly different Used and Unused material cards, including
   Dark Grass and Stone, until generated slots are added beyond 512.
3. Confirm every completed stroke appears immediately and remains the selected
   material; no requested vertex may be dropped as a best-effort fallback.
4. Pan across untouched areas after every few stamps. Their original imagery
   must remain unchanged; the whole map must never turn into one repeated tile.
5. Confirm the counter reads, for example,
   `512 authored + 12 generated = 524 active slots`.
6. Save to an isolated test output, reopen it, and confirm the painted area and
   untouched terrain remain visually identical.

## Expected scope and limitations

- Missing 1/3, 2/2, 3/1, and four-family tuples resolve exactly; nearest-family
  substitution is not permitted.
- Installed JMV/HD imagery was used only for read-only research and is not
  redistributed in this package. No synthetic visual fixture is presented as
  a substitute for a real game-data atlas.
- Expanded atlases above eight pages are editor-validated here. Shipped-game
  runtime validation at 9 and 16 pages remains a separate controlled test.
- The new transition-mask aesthetic still needs human review on real Zone 2
  material pairings; report any family card containing the wrong visual theme.
