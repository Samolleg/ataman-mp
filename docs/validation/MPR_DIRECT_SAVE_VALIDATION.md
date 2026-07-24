# Direct MPR Save Validation

Status: automated implementation validation passed on 2026-07-24; copied-map
manual validation is required before distribution.

## Supported save contract

A changed, currently open MPR may replace its own source only when Endurance
can express every change as provenance-checked, same-length canonical MP/SEC
member edits. The save coordinator then:

1. verifies that the source still equals the document baseline;
2. builds and freshly reopens a staged candidate;
3. rejects any semantic change outside the requested archive members;
4. rechecks source and destination for races;
5. creates and hash-verifies a pre-save backup;
6. atomically replaces the destination;
7. rereads the committed bytes and freshly reopens them;
8. adopts the committed document and terrain model as the new clean baseline.

The legacy rebuilding serializer remains unable to save any changed MPR.
Unknown members, member names, order, metadata, offsets, namespaces, untouched
payloads, and trailing bytes remain preservation-only.

An explicit Save displays the candidate hash, changed members, semantic diff,
backup behavior, and navigation warning. Exit/unload uses the Save decision the
user already made and does not show a duplicate transaction confirmation.

## Automated evidence

`endurance_document_tests` passes 57 tests with 0 failures and 3 expected
private-fixture skips. Direct-save coverage proves:

- same-path backup and atomic source replacement;
- committed bytes and SHA-256 equal the staged candidate;
- fresh document and terrain reopen;
- the rebound terrain is clean;
- undo after Save is dirty and redo back to the saved state is clean;
- source-race refusal;
- backup failure and simulated atomic failure leave the destination unchanged;
- same-path simulated atomic failure leaves the source byte-identical;
- untouched-member equality, namespace preservation, seam locality, overlays,
  and undo/redo remain covered by the existing synthetic suite.

The full release build also passes from the linked Unicode worktree through the
documented Qt 5.12.12/MinGW wrapper. The editor was not launched by automation.

## Required copied-map manual validation

Never perform this validation on the live game installation. Copy a known MPR
to a disposable test directory and record its SHA-256 before opening it.

1. Open the copied MPR, make one small sculpt change, and choose **Save**.
   Confirm there is one transaction prompt, the save succeeds, the title loses
   its dirty marker, and a backup is reported in the workflow log.
2. Reopen the saved copy and confirm the sculpted geometry is present.
3. Undo immediately after a second Save; confirm the map becomes dirty and the
   pre-save terrain returns. Redo; confirm the saved terrain returns and the map
   becomes clean.
4. Repeat with land painting and overlay painting. Reopen after each Save and
   confirm only the intended sectors changed.
5. With both MPR and MOB loaded, choose a new map or exit, answer **Save** for
   the MPR, and confirm there is no second MPR save prompt.
6. Hash the saved MPR, its backup, and untouched comparison inputs. Export a
   paint diagnostic and retain the workflow log privately.
7. Run **Test Current Map** only on the disposable saved copy and verify terrain,
   overlay, character placement, and navigation behavior.

Private Base/JMV corpus automation remains separate through
`ENDURANCE_MPR_SAVE_FIXTURES`; fixtures and generated archives must not enter
the repository or distribution package.
