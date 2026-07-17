# Alpha.30 ordinary MOB edit validation

Use disposable MOB copies. Topology-preserving property and transform edits save
normally through the raw-tree transaction path. There is no save preview and no
per-field whitelist. Structural operations that add, remove, or reorder sections
remain blocked until their raw-subtree transaction is complete.

## Normal edits

1. Move an existing wolf or other unit and save to a new filename.
2. In another copy, change several ordinary fields together (for example player,
   model/texture/database entry, rotation, and a chest Hands value) and save.
3. Undo all changes and confirm the dirty marker clears and Save As is byte-identical.

Expected workflow entries:

- one leaf edit per raw field changed by the user action;
- `MOB transaction save authorized class:RAW_LEAF_EDITS`;
- successful staged validation, backup, and atomic save.

Reopen the output in Endurance. Compare source and output with MobExplorer and
**Tools > Compare MOB Files**. Object count and NID set must remain identical;
only the selected objects and intended field values may differ.

## Current structural boundary

- Copy or delete an object whose operation changes the raw section tree. Until
  that operation is migrated, saving must be refused and no destination/backup
  should be created. Moving, renaming, and changing model/texture fields are not
  structural and should save normally.
- A genuine provenance mismatch or malformed candidate must still be refused.

Return the exact source/output hashes, clean-session workflow log, MobExplorer
comparison, and a focused in-game open/use/unload result. Continue using copied
maps; alpha.30 is still evidence collection, not approval for the only live copy.
