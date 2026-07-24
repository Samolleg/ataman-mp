# Map Catalog Editor

`EIMapTableEditor` is the isolated editor for the game's `map.txt` member in a
Base or mod-profile `texts.res`. It is intentionally separate from terrain and
MOB mutation: changing a minimap reference or entrance must never rebuild an
MPR, MOB, or unrelated RES member.

## Supported structured fields

- zone ID, allod/island ID, and zone type;
- primary MPR and MOB resource namespaces;
- the complete `#maps` block: sector width, sector height, minimap texture, and
  objectives texture;
- entrances: ID, destination zone/exit, deploy and remove rectangles, pass
  time, view angle, and deploy angle;
- explicit new-zone registration using only values entered by the author.

Unknown tags, comments, blank lines, casing, line endings, and authored raw
text remain in place when an unrelated structured field changes. The Raw
Review tab exposes the exact decoded logical zone block, including separator
and description comments authored immediately before its `#zone` line, but
does not pretend to understand profile-specific fields. New `game`
registrations require explicit MPR and MOB resource names. Registrations warn
that weather, sky, camera, figure, cage, LOD, music, and other profile metadata
are not invented.

The editor can export the exact current `map.txt` bytes for established
hand-editing workflows. Import accepts a hand-edited `.txt` only after an
explicit risk acknowledgement and basic structural parse. It cannot prove
profile-specific semantics, resource existence, travel-graph correctness, or
script compatibility. Import changes only the in-memory catalog; the RES save
contract below still applies before any archive is written.

## Save contract

The inherited `CResFile` writer is not used. It sorts names, regenerates
timestamps, and rebuilds archive structure, which cannot satisfy Endurance's
preservation rules.

The editor instead performs an append-only member replacement:

1. verify that the source archive and selected `map.txt` payload still match
   the versions originally opened;
2. retain the original archive bytes and append the changed payload at a
   16-byte boundary;
3. append a minimally patched copy of the original member table and the exact
   original filename buffer;
4. freshly reopen the staged file and verify member names, order, hash links,
   timestamps, filename metadata, and every untouched payload byte;
5. recheck source and destination for races;
6. hash and create a pre-save backup when replacing a destination;
7. atomically publish with `QSaveFile`.

Failure before the final commit leaves the destination untouched. A no-op save
is byte-identical. The append-only layout favors preservation and auditability
over compactness; archive compaction is deliberately outside this workflow.

## Standalone build

Build from an ASCII-only junction when the repository path contains Cyrillic
characters, following `docs/BUILDING_ENDURANCE.md`, then run qmake against:

```powershell
qmake ..\tools\map_table_editor.pro CONFIG+=release
mingw32-make -j4
```

The resulting `EIMapTableEditor.exe` accepts an optional copied `texts.res`
path. `--ui-widget-check` constructs the dialog and exits without showing a
window. `--validate-archive <copied-texts.res>` performs the same read-only RES
and `map.txt` open validation without showing the editor.
`--validate-derived-res <baseline.res> <candidate.res>` additionally verifies
that a saved candidate preserves member names, order, metadata, and every
untouched payload relative to its baseline. During development and automated
validation, never point write actions at the live game installation or
proprietary corpus; use synthetic archives or hash-verified copies.

## Tests

- `tests/map_table_document_tests.pro` covers CP1251 parsing, no-op identity,
  minimap and entrance edits, unknown-tag preservation, and explicit new-zone
  registration.
- `tests/res_archive_document_tests.pro` covers no-op archive identity,
  untouched-member equality and metadata, stale-hash refusal, malformed input,
  staged reopen, backup validation, source races, and failed atomic commits.

Private runtime validation remains separate: open a copied Base and JMV
`texts.res`, make one minimap-only edit and one entrance-only edit, reopen the
saved copy in the editor, then confirm the copied profile resolves the same
unchanged maps and the changed registration in game.
