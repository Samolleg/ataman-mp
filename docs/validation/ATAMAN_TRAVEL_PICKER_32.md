# Ataman 32 — compact travel picker

Base ab17292836d7cd9b3a093a06064a63f267af735f; branch codex/travel-picker-v32.
Screenshot reproduction: menu-style popup fills screen; arbitrary target ID can
point at missing deploy and is rejected only at save. Changed to bounded searchable
non-menu combos, explicit existing target entries and adjacent new-route action.
Old invalid selections remain visible; search text alone cannot alter link data.
Missing deploy entries disabled; edge records remain selectable without deploy.

22 focused tests PASS (build-travel26-tests/results-v32.txt). Initial extended
fixture had an obsolete expected row 0; corrected to selected row 1, rerun PASS.
Fresh Release build PASS (build-picker32.log); hidden native UI check exit 0,
travelEditorProductionRouteWorks=1 (build-release-32-picker/ui-widget-check.stdout.txt).
No game resource writes. Save rejection guard retained; message now actionable Russian.
Screenshot error proves target validation failed, not which target data is present
on disk versus unsaved editor state. Game/manual screen-scale acceptance remains open.
