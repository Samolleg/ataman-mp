# Landscape Preview R2 — Manual Test Cases

Tester: ____________________  Date: ____________________  Profile: Base / JMV

Use only disposable copies. Never open the live game corpus for a mutation test.

## Preparation

1. Copy a known-good MPR and optional matching MOB into a disposable folder.
2. Record the MPR SHA-256 and last-modified timestamp.
3. Start `EIMapperEndurance.exe` from this build folder and open the copied MPR.
4. Keep the original/backup outside the disposable folder.

### TC-01 — Menu state without MOB

Steps:

1. Open only an MPR.
2. Open the File menu.

Expected:

- Save Active MOB, Save Active MOB As, and Save All MOBs are disabled.
- MPR actions remain available.

Result: Pass / Fail  Notes: ________________________________________________

### TC-02 — Landscape modes and `B`

Steps:

1. Open **View > Landscape Tools (Preview)**.
2. Inspect the Landscape tool list.
3. Disable editing, focus the viewport, and press `B`.

Expected:

- Sculpt terrain, Paint land tiles, and Paint existing overlays are visible.
- `B` opens/shows Landscape Tools, selects Paint land tiles, and enables painting.
- Sculpting does not occur while tile painting is selected.

Result: Pass / Fail  Notes: ________________________________________________

### TC-03 — Stationary sculpt hold and undo grouping

Steps:

1. Select Sculpt terrain, Raise, radius `4`, strength `0.35`, step `180`.
2. Hold the left mouse button still on one terrain point for three seconds, then release.
3. Undo once, then redo once.

Expected:

- Terrain continues rising while held without mouse movement.
- History contains one Sculpt terrain entry for the full hold.
- One Undo restores the pre-stroke terrain; one Redo restores the complete hold.

Result: Pass / Fail  Notes: ________________________________________________

### TC-04 — Moving sculpt and sector seam

Steps:

1. Draw one continuous Raise stroke across a known sector boundary.
2. Repeat with Lower, Flatten, and Smooth.
3. Undo/redo each stroke.

Expected:

- No crack, discontinuity, or one-sided seam appears.
- Each mouse-down-to-release gesture is one undo entry.
- Normals/lighting update with the preview surface.

Result: Pass / Fail  Notes: ________________________________________________

### TC-05 — Tile grid

Steps:

1. Enable **View > Show Terrain Tile Grid**.
2. Orbit and zoom the camera.
3. Sculpt beneath the visible grid.
4. Disable the grid.

Expected:

- Cyan lines outline 2 x 2 terrain tiles without internal triangle lines.
- Lines follow terrain height without obvious detachment or severe flicker.
- Disabling the action removes the overlay.

Result: Pass / Fail  Notes: ________________________________________________

### TC-06 — Land-tile painting

Steps:

1. Select Paint land tiles and click **Choose atlas tile...**.
2. Choose a conspicuous atlas tile.
3. Left-drag over several cells; rotate with the mouse wheel.
4. Right-click an existing cell to pick it.
5. Undo and redo the stroke.

Expected:

- Only intended tile cells change.
- Rotation preview and painted rotation agree.
- Atlas category remains read-only.
- Undo/redo restores/reapplies the stroke.

Result: Pass / Fail  Notes: ________________________________________________

### TC-07 — Existing-overlay painting safety

Steps:

1. Use a copied MPR containing an existing water/overlay surface.
2. Select Paint existing overlays and choose an atlas tile.
3. Paint an active overlay cell.
4. Attempt to paint outside the active overlay.

Expected:

- Active overlay texture/rotation changes in preview.
- Existing liquid material is preserved.
- No new overlay/liquid cell is created outside the active surface.

Result: Pass / Fail  Notes: ________________________________________________

### TC-08 — Heightmap orientation and undo

Steps:

1. Import `heightmaps/orientation_corners_257.png` with black `8000`, white `48000`, Flip disabled.
2. Note the corner orientation; Undo.
3. Import again with Flip enabled; Undo and Redo.

Expected:

- Confirmation reports 257 x 257 samples and the explicit range/orientation.
- Flip exchanges top and bottom orientation.
- Each import is one undo entry and restores exactly.

Result: Pass / Fail  Notes: ________________________________________________

### TC-09 — Heightmap shape and seam continuity

Steps:

1. Import `radial_hill_257.png`, then inspect and Undo.
2. Import `seam_cross_257.png` and inspect sector boundaries with the tile grid enabled.

Expected:

- Radial hill is centered and smooth.
- Cross ridges remain connected across sector boundaries.
- Undo restores the original landscape.

Result: Pass / Fail  Notes: ________________________________________________

### TC-10 — Unchanged same-path MPR Save

Steps:

1. Reopen a fresh disposable MPR copy and make no edits.
2. Record its SHA-256 and last-modified timestamp.
3. Invoke Save MPR.
4. Recheck hash and timestamp.

Expected:

- Save reports success/no error.
- SHA-256 is unchanged.
- Last-modified timestamp is unchanged.

Result: Pass / Fail  Notes: ________________________________________________

### TC-11 — Unchanged MPR Save As

Steps:

1. With an unchanged MPR open, invoke Save MPR As to a new disposable filename.
2. Hash the source and destination.
3. Reopen/validate the destination.

Expected:

- Destination is created atomically.
- Source and destination hashes match exactly.
- Destination reopens successfully.

Result: Pass / Fail  Notes: ________________________________________________

### TC-12 — Changed-MPR save refusal

Steps:

1. Make one sculpt or tile preview edit.
2. Invoke Save MPR.
3. Invoke Save MPR As to a new filename.

Expected:

- Both operations clearly refuse changed-MPR output.
- Original copied MPR hash/timestamp remain unchanged.
- Save As destination is not created.

Result: Pass / Fail  Notes: ________________________________________________

## Failure report

Attach privately:

- test-case number and exact tool settings;
- Base/JMV profile and copied map name;
- `logs/workflow.log` and any generated dump;
- before/after hashes and whether the editor exited gracefully.

Do not publish proprietary MPR/MOB files or crash dumps.
