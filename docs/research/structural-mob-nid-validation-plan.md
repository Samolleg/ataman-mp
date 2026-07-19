# Structural MOB NID validation plan

Status: implementation underway on the critical `0.2.x` line. Parser-backed
family/range/NID indexing, diagnostics, non-blocking preflights, deterministic
suggestions, and conservative single-workspace repair are implemented.

## Why this is critical

`Autozone7altar.mob` contains eight objects whose NIDs collide cross-file with
established scenery in `zone7.mob`. The AutoZone entries are seven units and one
lever, so these are incompatible runtime aliases rather than harmless repeated
decorations. Preserve the established Zone 7 IDs. A deterministic read-only
audit suggested `1000321` through `1000328` for the new AutoZone objects, but an
editor must not apply those values until every reference has been audited.

The collision is strongly correlated with the tester's Zone 7 unchecked-null
lookup crash and invisible enemy. The limited dump does not reveal the exact
NID, so a renumbered runtime control remains necessary before claiming exact
crash attribution.

## Existing architecture and insertion points

- `CMobDocument` already performs complete parser-backed section coverage and
  exposes `SMobObjectSnapshot` with section path/type, NID, OBJTYPE, name,
  prototype, position, and subtree hash. Detection must consume these snapshots.
- `CValidationReport` already carries code, severity, message, and location and
  exports text/JSON. Extend findings with structured NID/object evidence rather
  than replacing this report path.
- `CMob::buildValidatedCandidate()` produces the exact current in-memory bytes,
  reparses them, and is shared by save and Test Current Map staging. Family
  validation runs on these candidate bytes, not stale source files.
- `CMob::saveAs()` stages, freshly reparses, backs up, and atomically replaces a
  single MOB. Cross-document repair must be represented above this layer and
  must not partially save a reference cohort.
- `MainWindow::runMapTest()` already builds the main and supplemental candidates
  before runtime staging. It is the mandatory Test Current Map preflight hook.
- Direct NID edits use `CChangeProp`; add/paste/duplicate use undo commands and
  raw-provenance-aware object creation. These are change notifications, not
  alternate scanners.
- The existing script reference analyzer is conservative evidence for numeric
  script occurrences. It may warn/refuse repair but must not rewrite scripts by
  guessing.

## Components

### `CMobNidIndex`

Build immutable entries from parsed object snapshots:

- profile and family ID;
- source file and document revision/hash;
- section path, occurrence, subtree hash, and OBJTYPE;
- NID, name, prototype, MOB role (base/main/supplemental), and edit provenance;
- known typed references originating from the object/document.

Index open documents from their validated in-memory candidate. Index closed
family members from read-only parser results cached by absolute path, size,
modification time, and a header/content hash. Never use raw `NewObject` scanning
as the authoritative object boundary.

### Runtime-family resolver

Start from the selected `map.txt` primary or the collection's explicit MOB
members, then follow active transitive `AddMob(...)` calls. Comments are
ignored; literal strings, `StrCat`, and constant `+` expressions are resolved.
Conditional calls remain potential runtime edges. Dynamic expressions,
missing targets, and unreadable files produce incomplete-family evidence.
Collections are authoritative, so a custom primary does not silently inherit
the installed MOB with a similar filename.

### `CMobNidValidator`

Provide three manual scopes:

1. Current MOB.
2. User-selected MOB files.
3. Complete active zone family.

Group structurally parsed objects by NID. Report every participant with source
file, role, name, section path/type, OBJTYPE, prototype, and subtree hash.
Classify at least:

- incompatible object types or unit/logic-bearing aliases: error/critical;
- referenced aliases: error/critical;
- same-type static-decoration aliases with no known references: warning;
- unresolved family ownership or incomplete family inventory: incomplete
  preflight, not a clean result.

Profile providers may refine severity, but cannot hide raw evidence.

## Triggers and UX

### Non-blocking automatic validation

After loaded-MOB changes, undo, redo, and direct NID edits, debounce validation.
Show the complete family membership, ranges, collisions, provenance, and
suggestions in the MOB Family Validation dock. A newly introduced risk receives
one consolidated warning; direct ID entry shows every current owner/range. No
edit is blocked and nothing is renumbered automatically.

### Mandatory fresh preflight

Before Save, Save As, Save All, Test Current Map/hot reload, export, and a future
package/release action, rebuild candidates and run a fresh complete-family pass.

- Newly introduced incompatible-type or referenced collisions warn immediately.
- Existing collision, range, and incomplete-family findings are consolidated in
  one preflight report and the workflow log.
- Save, Save As, Save All, Test Current Map, and hot reload always remain
  available. Runtime testing is intentionally retained as an important oracle
  for collision behavior the editor does not yet understand.
- Byte-identical no-op open/save remains possible, but never produces a false
  clean report.

The guided repair dialog is opt-in. It preserves the mapped primary by default,
offers deterministic IDs for loaded supplemental objects, and applies selected
safe rows as one undoable action. Rows with script or typed `ParentID` evidence
remain visible but require manual resolution.

## Deterministic suggestions

Never choose the established base-map participant as repair owner. Ownership
priority is explicit edit provenance (newly pasted/duplicated/created or direct
NID edit), then supplemental versus base role, then user selection. If ownership
is still ambiguous, suggest nothing until the user chooses.

Allocate from the selected profile-valid active range, excluding every NID in
the complete family and every reserved/reference-only ID known to the profile.
Visit objects in stable `(family role, normalized file path, section path)`
order and choose the lowest valid free IDs. The same inputs must always yield
the same suggestions; for the confirmed fixture the expected read-only result
is `1000321` through `1000328` in source order.

## Reference-aware repair transaction

First audit and register exact typed reference sections, including
Parent/ParentID, directory membership, logic links, and other confirmed ID
fields. Do not infer writable references merely from equal four-byte values.

Build a `CZoneFamilyNidRepairTransaction` containing provenance-checked leaf
edits per MOB plus a complete reference census:

- apply the NID and all confirmed typed-reference rewrites together in memory;
- group them as one undo/redo operation across the loaded workspace;
- leave established base IDs unchanged;
- reparse every candidate, rerun family validation, and require the intended
  old-to-new map before committing the UI transaction;
- mark all participating documents as one save cohort so one file cannot be
  saved while another still contains the old reference;
- stage and validate every cohort member before creating backups or replacing
  any destination.

Script numeric occurrences and possible references inside opaque records are
listed with file/path/line or byte-offset evidence. Unless a profile-specific
grammar proves an unambiguous typed rewrite, their presence blocks automatic
repair and requires manual resolution. Script comments, strings, and helper
arguments must not be rewritten by a general number replacement.

Cross-file coordinated replacement cannot be made truly atomic with independent
filesystem renames. Therefore initial repair delivery should remain in-memory
and require coordinated Save All with all candidates/backups prepared first;
on any replacement failure, stop, retain backups/staged files, and report the
recovery set. Single-file repair can ship earlier once its typed references are
complete.

## Responsiveness

- Run closed-file parsing and family indexing in cancellable workers.
- Snapshot open-document candidate bytes on the GUI thread, then parse immutable
  copies off-thread.
- Use generation IDs so stale results cannot overwrite newer edit findings.
- Cache only profile/family/path/hash-matching results.
- Critical preflight always waits for a fresh whole-family generation and uses
  the existing blocking progress pattern.

## Tests

- Eight supplied Zone 7 cross-file collisions with exact metadata/severity.
- Intra-file duplicates, including repeated static decorations and units.
- Zone 1 versus Zones 10-19 separation.
- Independent map-table roots for `zone3obr`, `zone12dun2`, and the numbered
  Zone 1/10-19 families, plus their actual transitive `AddMob` closures.
- Ambiguous/unregistered family refusal.
- Deterministic `1000321`-`1000328` allocation and occupied-range skipping.
- Typed Parent/ParentID, directory, and logic reference rewrites.
- Script and opaque-reference repair refusal with actionable evidence.
- One-command multi-object/multi-document undo and redo.
- Stale provenance, cancellation, and edit-during-background-scan handling.
- Failed candidate/stage/backup/replacement behavior without partial silent
  success.
- Byte-identical no-op round trips and unchanged-subtree equality.
- Save/Test/export trigger matrix and non-blocking continuation logging.
- Focused runtime control: renumbered copied fixture, `drawall`, reported Zone 7
  encounter, save, unload/reload, and graceful exit.

## Staged delivery

1. Family resolver, structural read-only index, manual reports, and supplied
   fixture tests.
2. Debounced editor integration and non-blocking critical-operation preflight;
   warnings only, no repair.
3. Deterministic ownership/allocation suggestions and Validation-panel UX.
4. Single-document typed-reference repair transaction with undo/redo.
5. Coordinated multi-document repair/save cohort after all reference schemas
   required by the corpus are audited.
6. Renumbered runtime control; promote profile severity rules only from recorded
   Base/JMV evidence.
