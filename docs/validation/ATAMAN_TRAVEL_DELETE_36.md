# Ataman 36 — EI-TRAVEL-DELETE-036

2026-09-19. Base e5477366 (version 35), branch codex/travel-delete-v36.
User reports a transition cannot be deleted. Attached MP4 is readable (48,500,620
bytes), but frames were not decoded: no available ffmpeg/PyAV/OpenCV decoder was
found. The exact video sequence is not claimed as observed.

## Confirmed code defect and reproduction

Version 35's table refuses deletion of an entry referenced by another record.
The UI set m_travelExit=-1 before attempting that operation, leaving a highlighted
row while further Delete requests silently return. A synthetic linked record
reproduces the blocked document operation; the production UI test now covers
confirmation cancellation, linked removal, and selection after each outcome.

## Change

CTravelDocument::deleteExit constructs a private candidate, disconnects incoming
links (including same-zone/self references) with none 0, removes the requested
entry and lets the table migrate later ordinal destinations. Other records and
geometry remain, including legacy assignment order and fifth angle parameters.
The candidate is committed only after all operations succeed.

The confirmation reports the incoming count and detailed source list and explains
that those records become unlinked. Cancel/failure leave selection intact; success
selects an adjacent entry (or none if empty). A single existing TravelUndo stores
the entire transaction. Ctrl+S retains the existing backup/conflict/atomic save path.
No installed game archive or map was changed by implementation/testing.

## Checks

- Fresh Release version 36 PASS (build-delete36.log).
- 24 focused RES/document/panel QtTests PASS (build-tests-36-res/results.txt).
  Added linked/self-linked deletion, duplicate source labels, case-insensitive
  references, remapping later destinations, unchanged legacy effective geometry,
  invalid index/no mutation, last-entry deletion, snapshot undo/redo, save/reopen,
  and preservation of the unrelated archive member.
- Native hidden --ui-widget-check PASS (build-release-36-delete/checks.json,
  ui-widget-check.stdout.txt, travelEditorProductionRouteWorks=1). Exercises the
  real confirmation Cancel/Yes, preserved selection, actual linked deletion,
  one-step Undo/Redo and subsequent saving. Run with required Windows clipboard
  access; no interactive packaged editor launch.
- Packaging/installation evidence follows after finalization.

Manual acceptance of the user's scene and parent EI-TRAVEL-EDITOR gameplay remain
open. No claim of watching the submitted video or runtime game acceptance.

## Handoff

Source 662cb046651046e036909e8fc129af278b668fb8.
Fresh build-staging-36-delete version and executable identity PASS; finalizer and
independent 101-file manifest verification PASS. Not run after finalization.
EXE SHA256 815e593846a98038048e0872820dd54fbed44ad857f15497dc6e8b29f94f329d.
Installed artifacts/EI-Mapper-Ataman-2-first-test version 36: 7 managed files
replaced, 0 removed, 9721 user files preserved and hash-verified.
Version-35 rollback retained as build-rollback-delete36. Positively identified
older version-34 rollback removed only after successful installation verification.
