# Alpha 26 modern terrain integration

This build integrates the complete modern terrain painter into the current
main editor without dropping the newer database/resource tools, viewport
visual controls, sculpt-performance work, settings, or behavior validation.

## What to test

1. Open a representative map and confirm ordinary map loading and the database
   and resource tools still work normally.
2. In Landscape mode, paint with **Modern**, **Classic Auto**, and **Manual**.
   Confirm the first stroke is immediately visible and each stroke is one
   undo/redo step.
3. Add or generate an atlas slot and paint it immediately, without reopening
   or finalizing the map.
4. Use **Shift+R** on a mixed transition area. Confirm it resolves in one
   undoable step and preserves the active runtime layer/type.
5. Save a modern draft under a new name. Confirm the MPR and adjacent
   `-atlas.res` are created together, reopen cleanly, and remain editable.
6. Run **Test Current Map** after an atlas edit and confirm the owned runtime is
   restarted or refreshed without missing, transparent, or cyan terrain pages.

The package does not modify installed maps or resource archives automatically.
Private-corpus and live-game runtime checks remain manual, opt-in validation.
