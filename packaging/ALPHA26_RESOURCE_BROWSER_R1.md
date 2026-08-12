# Resource Browser test build

## What changed

- Added a dockable Resource Browser under **View > Resource Browser**.
- Active Profile mode indexes archives in runtime lookup order and marks every
  member as winning or overridden with provider provenance and override chains.
- Open Archive mode browses an arbitrary external `.res` without changing the
  active profile.
- Added responsive search, type/status filters, lazy MMP and text previews,
  metadata/hex fallback, copy-name, export, and open-location actions.
- Added reviewed Save As transactions for replace, add, rename, and delete.
  Existing payload offsets stay unchanged where possible; candidates are
  staged, reopened, race-checked, backed up when replacing a destination, and
  atomically published.
- Protected `database.res` and `databaseJmv.res` from generic mutation and
  routed them to the Database Editor integration event.

## Manual checks

1. Open **View > Resource Browser** and confirm Active Profile lists configured
   archives without blocking the editor.
2. Filter a duplicated member name and confirm one provider is **Winning** and
   lower-priority providers are **Overridden**.
3. Preview an MMP and a TXT/Lua/REG member; confirm image metadata or detected
   encoding/line endings appear.
4. Switch to **Open Archive**, choose a copied or disposable `.res`, export a
   member, and compare it byte-for-byte with a trusted extraction.
5. On a copied archive, review one Replace/Add/Rename/Delete operation and save
   to a new `.res`; reopen that result in Open Archive mode.

## Known limitations

- FIG/MOD/LNK/animation and audio resources currently use metadata/hex preview;
  renderer/audio integration remains future work.
- Nested RES browsing, external-editor workspace/save detection, persisted
  index/cache cancellation, and automatic structured name-reference discovery
  are not included in this slice.
- Rename/delete warn that reference coverage is incomplete. Opaque or dynamic
  references must be reviewed manually.
- Mutation is intentionally Save As oriented. Never select a live installed
  archive as the destination without the separate installed-archive approval,
  backup, and validation workflow.
