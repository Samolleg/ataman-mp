# EI-MOB-SCENARIO-VISUAL-001-B — Russian default and right-click navigation

Baseline: `fc583c8c12ee9470a6b839ef731226e8b9bf8135`, branch
`contrib/Samolleg/mob-scenario-layer`. User requested Russian by default and
immediate object navigation on scenario-row right click, then explicitly chose
automatic loading and selection for unopened family MOBs.

Original failing checks: 4 failures for startup/settings language and right-click
selection; separate cross-MOB camera failure; separate unopened-MOB load failure.
The first incorrect decisions were English fallback values, no right-click
handler, camera selection search restricted to active MOB, and exclusion of raw
family targets from navigation. Each failure was captured before its fix.

Implemented behavior:

- Missing/unreadable/malformed language settings use Russian; saved `en` is
  preserved. Settings defaults and startup translation agree.
- Right mouse press picks the clicked row and navigates to its first available
  participant/marker. Multiple resolved participants remain selectable below.
  Unresolved rows do not reuse a prior target.
- Unopened family targets use normal MOB loading. Source hash is checked before
  loading, and object ID, raw section path and source hash must match afterwards.
  Reentrant navigation is guarded; repeated clicks reuse the loaded document.
  Camera targets the actual node's position, including another open MOB.
- Navigation preserves the active MOB, file bytes, dirty flags and undo history.
  Normal open-time recovery snapshots remain enabled.

Final validation:

- Release wrapper including fresh runtime components passed.
- Hidden native Windows Desktop OpenGL: **55 passed, 0 failed**, including Russian
  startup/catalog, right-click events, unrelated/unknown target guards, real
  synthetic MOB file loading, exact provenance selection, repeated click, stale
  file rejection, byte/history preservation and overlay pixels. Russian panel
  capture visually checked.
- Hidden native Windows ANGLE with read-only JMV zone6 corpus: **58 passed,
  0 failed**. Corpus aggregate: 8 sources, 668 references, 325 conditioned records,
  1 spawn, 31 unresolved targets, 28 markers, complete inspected family (9 ms).
  Input hash unchanged. This is bounded corpus coverage, not a replay of the
  user's complete interactive session.
- Localization contract passed: 3090 messages, 2933 unique source strings.
- Native synthetic open needs a recovery snapshot outside the worktree. Tests
  use Qt test locations and remove their own snapshot. The first sandboxed run
  exposed a denied backup-directory write; the final native tests ran with the
  approved tool escalation and passed without modal warnings.
- Full `--ui-widget-check` remains the documented R1 gap and was not rerun for
  this change. No packaged editor was launched by the agent.

Executable SHA-256:
`c2d507abc45b7699b633c911802f18ca496908957d5b43d0cd736dd0b28dd064`.

Audit: no pending material implementation choice; unopened-MOB loading was
explicitly approved. Remaining evidence gaps: acceptance on the user's actual
map/session and the broad UI diagnostic.

Delivered: `C:/GOG Games/maped/EI-Mapper-Endurance-0.5.0-alpha-mob-scenario-r3-test`.
Staging and destination finalizers passed, all 92 files hash-identical. Previous
tester folders preserved. Next: user verifies the screenshot case with R3; keep
the parent open for interactive acceptance. No push, PR or publication.
