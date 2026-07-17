# EI Mapper: Endurance Edition

EI Mapper: Endurance Edition is an independently versioned derivative of
[`konstvest/ei_maper`](https://github.com/konstvest/ei_maper), focused on safe,
fast Evil Islands and Jamais Vu map editing.

Current development version: `0.1.0-alpha.48`.

Project status and upcoming work are tracked in the living
[Endurance roadmap](ROADMAP.md).

Endurance creates hash-addressed recovery snapshots when MOB/MPR documents are opened and
timestamped pre-save revisions before replacing existing files. Backups and their JSON metadata
are stored under `%LOCALAPPDATA%\\EI Mapper Endurance\\backups`; use **Tools → Open Backup
Folder** to inspect them. A save is refused if its pre-save backup cannot be completed.

This private development line preserves the complete upstream history. The
public project remains configured as the read-only `upstream` remote, with
commit `1cebe83d` tagged as `upstream-1cebe83d`.

## Alpha scope

- MOB loading no longer repairs duplicate IDs automatically; duplicates are
  reported without dirtying the document.
- Unchanged MOB and MPR Save As operations preserve the original bytes.
- Changed MOB output patches provenance-checked existing raw leaves directly.
  Ordinary property and transform fields can be mixed in one save; structural
  same-MOB copy/paste, object deletion, and known patrol/look-point edits use
  exact raw subtrees. New objects require a selected same-type source template.
- MPR writes retain extra archive members and the original internal map name.
- Resource lookup uses case-normalized RES table indexes and lazy payload reads.
- Terrain and tile-panel atlases use the same indexed, case-insensitive source
  priority, require the complete atlas set declared by the MPR, and report provenance.
- Tile Parameters uses the embedded `.MP` namespace when it differs from the
  outer `.mpr` filename, incorporating the map-name concept from upstream PR #19.
- Model-qualified `_xN` unit skins resolve to their physical archive names, with
  a logged viewer-only nearest-family fallback when a referenced variation is absent.
- MPR and MOB files can be opened independently, including MOB-only rendering.
- The outliner is resizable and its layout is persisted.
- Embedded scripts have a separate internal editor and a document-owned external
  workspace. External changes are detected while the MOB remains loaded and
  explicitly imported as normal undo-history entries.
- Headless `--validate` and `--roundtrip-check` commands are available.
- First-run setup accepts the game installation folder and automatically
  configures built-in or external mod resource profiles.
- Cached CPU frustum culling avoids submitting off-screen MOB objects in normal
  object view. It can be disabled under **Settings > Render** for A/B testing;
  selected objects remain conservative. Logic helpers are rendered separately,
  allowing their owning off-screen object geometry to remain culled.

The alpha is not yet a production replacement for the original editor. Existing
MOB fields can be edited and saved through the provenance-checked raw transaction
path without a preview or field-specific approval. Edits that change the MOB
section tree itself (such as some create/delete operations) remain under active
hardening and are blocked rather than falling back to the rebuilding serializer.

See [BUILDING_ENDURANCE.md](docs/BUILDING_ENDURANCE.md) for build and test commands,
and [NOTICE.md](NOTICE.md) for attribution and licensing information.

## Safety

Do not test against the installed editor directory or live mod files. Work from
copies, record hashes before and after each operation, and keep the original
EI Mapper v0.3 installation and `options.json` untouched.

## License

GPL-3.0, preserving upstream authorship and history. Distributions of binaries
must include the corresponding source and notices.
