# Alpha.40 MOB-hardening tester candidate

Alpha.40 is the closed-group candidate that follows alpha.14. The MOB safety
implementation is complete for supported edits; this pass seeks concrete
regressions rather than additional serializer architecture.

1. Compare the supplied `zone1.mob` and `zone1test.mob` with **Tools > Compare
   MOB Files**. Confirm one `script` document row is present and selecting it
   shows readable added script lines.
2. Repeat internal and external script edits. Each accepted edit must be one
   undo action, and an identical external save must not dirty the MOB.
3. Exercise ordinary property/transform edits, copy/delete, and patrol/look
   creation on disposable copies. Undo/redo, Save As, reopen, and compare with
   the baseline.
4. Verify only intended objects/fields/script lines differ. Run focused JMV
   `drawall`, unload/reload, script, traversal, and graceful-exit checks.
5. Attach the reproduction steps and rotated/current workflow logs to every
   report. Do not include proprietary resources or public crash dumps.
