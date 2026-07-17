# Alpha.38 MOB Parameters close validation

Alpha.38 retains the alpha.37 external-script hotfix and corrects the MOB
Parameters title-bar close lifecycle.

1. Open MOB Parameters and make no change. Click the title-bar X once. Confirm
   the window closes immediately and can be reopened.
2. Make a parameter change and click X once. Choose **No**. Confirm the local
   parameter change is reverted and the window closes immediately.
3. Repeat and choose **Yes**. Confirm the change becomes one main undo-history
   entry and the window closes immediately.
4. Repeat and choose **Cancel**. Confirm the window remains open with its local
   changes intact.
5. Repeat all four cases with the window's Close button. Behavior must be
   identical to the title-bar X.
6. Confirm manually undoing all local parameter edits back to the clean state
   allows either close control to close immediately without a question.

Also repeat the alpha.37 focused external-script test.
