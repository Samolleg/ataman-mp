# Alpha 26 terrain paint modes R178: Save-route labels

This focused build makes the MPR Save commands name the operation that will
actually run. The underlying draft and publication behavior is unchanged from
R177.

## Changes

- A normal MPR without pending atlas changes shows **Save MPR** and
  **Save MPR As...**.
- A normal MPR with pending modern-atlas changes shows
  **Finalize and Save MPR** and **Finalize MPR As...**.
- An editable modern draft shows **Save MPR Draft** and
  **Finalize Draft MPR As...**.
- The File menu and the MPR context menu share the same actions and therefore
  stay synchronized.
- Labels are derived from the same save-route policy used by the command. They
  do not infer draft/publication state independently.

## Focused test

1. Open a normal, unchanged MPR. Confirm the commands read **Save MPR** and
   **Save MPR As...**.
2. Make a Modern paint change that adds or changes the working atlas. Confirm
   the commands immediately change to **Finalize and Save MPR** and
   **Finalize MPR As...**.
3. Save an editable modern draft, reopen that MPR with its adjacent
   `-atlas.res`, and confirm the commands read **Save MPR Draft** and
   **Finalize Draft MPR As...**.
4. Make an ordinary landscape edit in the draft and choose **Save MPR Draft**.
   Confirm it updates the draft pair without asking for a game RES.
5. Choose **Finalize Draft MPR As...** and confirm it enters the R177
   publication flow with separate final MPR and game-RES destinations.

No semantic assignments, atlas artwork, blend masks, or save destinations were
changed in this revision.
