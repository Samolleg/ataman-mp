# AI graph terrain policy and landscape interaction manual test

Use disposable copied Base/JMV maps only. Do not edit an installed game map or
private source corpus in place. AI graph regeneration remains a non-saving
diagnostic preview.

## Changelist

- apply terrain-gradient limits at the game's 0.5 m navigation-grid scale;
- keep the levitating layer more tolerant than ordinary movement while
  rejecting genuinely cliff-like steps;
- expand steep-slope exclusion around the medium and large profiles;
- automatically rebuild a regenerated preview after a 500 ms quiet period
  following sculpt/height history or object-history changes;
- keep **Use loaded graph** stable: selecting it disables automatic rebuilds
  until **Regenerate preview** is chosen again;
- start each editor process with Landscape Tools closed while remembering the
  sculpt tool, radius, strength, step, and flatten target;
- suspend sculpt/paint input and outlines during Play From Here placement or
  anchor movement, then resume the selected landscape tool cleanly;
- show a blocking four-stage progress dialog for changed MPR saves;
- remove the obsolete `(Preview)` suffix and changed-MPR-disabled text.

## Test cases

1. Start the editor after closing it with Landscape Tools open. Confirm the
   panel starts closed. Open it with **L** and confirm the prior sculpt controls
   were retained. Close and restart once more; the panel must again start
   closed.
2. Load the copied mountain test map and compare **Regenerate preview** with
   **Use loaded graph** around X 202.04, Y 61.91, Z 20.50. On an ordinary/human
   layer, the summit must not link down a cliff face to inaccessible ground.
3. With the regenerated preview selected, sculpt a ridge and release the
   stroke. After the debounce, regeneration must begin automatically without
   blocking the editor. Undo and redo must each trigger another automatic
   rebuild after the quiet period.
4. Move, rotate, or scale fence 568 and commit the transform. Confirm the
   regenerated preview updates automatically. Select **Use loaded graph**,
   make another edit, and confirm it stays on the authored graph until
   regeneration is explicitly selected again.
5. In Sculpt mode, choose **Play from Here**. The sculpt outline must disappear
   and clicks must not edit terrain. Complete or cancel placement; moving the
   pointer must restore the sculpt outline and sculpting without reopening the
   panel.
6. Repeat test 5 in automatic and manual land-paint modes. No tile preview or
   brush outline may remain detached during placement, and painting must resume
   with the same mode/material after completion or cancellation.
7. Select an existing Play From Here marker, move it with the Move tool, and
   confirm landscape input remains suspended until the move completes or is
   cancelled.
8. Save a changed disposable MPR. Confirm one application-modal **Saving MPR**
   progress dialog covers patch preparation, staged validation, backup/atomic
   commit, and reopen/adoption. Verify the resulting backup and reopen as in
   `MPR_DIRECT_SAVE_VALIDATION.md`.

## Automated evidence

- `ai_graph_document_tests`: 9 passed, including walk-versus-levitating cliff
  thresholds and existing fence/vertical-clearance coverage.
- `endurance_document_tests`: 57 passed, 0 failed, 3 fixture-dependent skips.
- copied private MPR save-coordinator check: passed; source and destination
  safety remained isolated from the installed map.
- linked-worktree build regression: passed.
- full Qt 5.12.12/MinGW release build: passed.
- `--ui-widget-check`: passed.

