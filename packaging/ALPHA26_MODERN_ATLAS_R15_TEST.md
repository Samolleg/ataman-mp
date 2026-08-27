# Modern atlas r15 tester changelist

## What changed

- Expanded 1024/2048-slot MPRs no longer build every atlas-inspector icon during map load. The inspector materializes only visible rows, and DXT1 terrain pages use the fast validated decoder.
- Landscape Tools can shrink to a normal dock width. The material palette uses the remaining vertical space and keeps scrolling when its contents exceed the pane.
- Exact blend resolution now checks the active map first, then the installed stock JMV transition library, and generates a new exact tile only when neither source contains the required four-corner signature.
- Generated fallback transitions use deterministic material-aware irregular edge masks rather than equal-quarter/lattice cuts. Exact pixels, rotations, seams, and repeatable cache identity remain covered by tests.
- Atlas status now reports `original + added exact = active slots`; added tiles can be imported stock blends or newly generated exact combinations.

## Manual checks

1. Open the included Zone18 1024- and 2048-slot collections. Compare map-open time with r14, then open Inspect Atlas and scroll from the first through the last slots.
2. Narrow the Landscape Tools dock substantially. Confirm it no longer forces roughly half-screen width and the palette grows vertically to the controls below it.
3. On a disposable map copy, paint across several existing material boundaries. Confirm the stroke is never refused, the terrain updates on release, and the status count changes only when a new exact tile is added.
4. Paint a corner tuple known to exist in another stock JMV atlas but not the current map. Confirm the log reports stock authored transition reuse and that repeat painting reuses the same added slot.
5. Exercise genuinely missing grass, sand, rubble, snow, stone, and water pairings. Capture any generated edge that still looks structurally wrong, together with the paint diagnostic and logs.

## Known limitations

- Automated checks prove resolver order, exact pixel preservation, seam continuity, deterministic generation, and the expanded-page decode budget. Visual quality and end-to-end map-open latency still need tester confirmation in the editor.
- The 8192/16384 shipped-game runtime limit remains unresolved; those sizes must not be reported as passing unless the player is visibly confirmed on the staged map and crash telemetry remains clean.
