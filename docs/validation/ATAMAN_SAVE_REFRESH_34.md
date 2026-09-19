# Ataman 34 — own travel saves and visual resource lifetime

Base 74090043; branch codex/travel-save-refresh-v34.
Original audit ATAMAN_SAVE_CRASH_33.md retained. Source fix scope is own map.txt saves.
Relinked exact v33 .o with link map without stripping: .text SHA256 identical to
installed/tested v33 (4ea411be9f228721734171e62f74c21389067317f40ce9545bdf1a35a7e435e1).
Return address EXE+0x2a1440 is RenderQueueContext::flush, texture bind call.
Qt fault is QOpenGLTexture::bind+0x89. Complete stack unwind not available.

Source chain: resource browser watches texts.res; travel save did not bracket
its own publication. indexedArchiveChanged can emit activeResourcesPublished,
which refreshes viewport resources. clearTransientVisualCache destroys layered
textures still referenced by resolved unit visuals. Scene queue binds stale pointer.
Own map.txt saves now suspend that watcher before commit and restore/reindex it
afterwards without visual-resource publication. Failed commit also restores watch.
Actual graphical-resource publication retains default behavior; its broader active
scene lifetime problem is separate follow-up, not claimed fixed here.

Regression: production travel UI fixture now enables active archive browser before
save; verifies successful save/reopen, Undo/Redo, zero visual-publication signals,
event processing, and restored file watcher. Live game files not modified.

Fresh Release and native hidden UI PASS. Initial extended test timed out because
its temporary archive was deleted while still watched after the test returned;
fixed fixture cleanup by clearing active test sources before temp removal. Rerun
exit 0, travelEditorProductionRouteWorks=1, watcher restored before fixture cleanup.
No claim that the initial timeout passed. Original crash sequence is prevented at
the publication boundary; full external-resource texture lifetime remains separate.
