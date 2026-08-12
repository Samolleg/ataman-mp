# Database editor first vertical slice

The integrated Database Editor is an authoring preview backed by the active
profile provider. Base and Jamais Vu retain separate provider identity, source
archive selection, hashes, and documents.

| Area | First-slice coverage |
|---|---|
| Tables | Monster prototypes, race models, consolidated items |
| Browse | Table counts, sortable typed grid, all-visible-field filtering, optional read-only unknown/raw columns |
| Edit | Text, integer, decimal, and list fields; clone; delete |
| History | Per-document undo/redo and pending-change review manifest |
| Validation | Stable-name collisions and established monster-to-race links |
| Preservation | Immutable raw member baseline plus row-level unknown field bytes, including unknown members |
| Impact | External MOB/texts.res/tables.lua references explicitly reported opaque |
| Publication | Disabled pending proven EIDBEditor no-op, converter round trip, semantic comparison, backup, and atomic publication |

The narrow integration boundary is `ei::DatabaseDocument::fromSnapshot()`.
A future generic Resource Browser can provide the active immutable provider
snapshot and open the editor without interpreting `database.res` as a generic
mutable RES archive.

Unsupported tables and fields remain preserved in the raw baseline; they are
not silently rewritten or advertised as editable.
