# Alpha 25 tester changelist

- Removed Auto: unlocked now regenerates automatically; locked freezes capture.
- Viewport and Export Minimap lock controls are synchronized.
- Apply & Lock selects Generated mode; the same button becomes Unlock.
- Preview zoom/position survives grading and regeneration.
- Added Reset settings.
- Export Minimap remembers its position and remains open across map changes.
- Non-square previews crop texture padding consistently.
- The viewport minimap shows the current camera footprint. Click it to move the
  camera in XY without changing its height or angle.
- Minimap and AI graph overlays use the same reliable OpenGL-viewport repaint
  configuration.
