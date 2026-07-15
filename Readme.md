# EI Mapper: Endurance Edition

EI Mapper: Endurance Edition is an independently versioned derivative of
[`konstvest/ei_maper`](https://github.com/konstvest/ei_maper), focused on safe,
fast Evil Islands and Jamais Vu map editing.

Current development version: `0.1.0-alpha.1`.

This private development line preserves the complete upstream history. The
public project remains configured as the read-only `upstream` remote, with
commit `1cebe83d` tagged as `upstream-1cebe83d`.

## Alpha scope

- MOB loading no longer repairs duplicate IDs automatically; duplicates are
  reported without dirtying the document.
- Unchanged MOB and MPR Save As operations preserve the original bytes.
- Changed saves are staged atomically and MOB backups are retained separately.
- MPR writes retain extra archive members and the original internal map name.
- Resource lookup uses case-normalized RES table indexes and lazy payload reads.
- MPR and MOB files can be opened independently, including MOB-only rendering.
- The outliner is resizable and its layout is persisted.
- Embedded scripts can be opened through the OS editor and explicitly imported.
- Headless `--validate` and `--roundtrip-check` commands are available.

The alpha is not yet a production replacement for the original editor. In
particular, changed MOB serialization still needs corpus-wide losslessness and
game validation before it is considered safe for live JMV maps.

See [BUILDING_ENDURANCE.md](BUILDING_ENDURANCE.md) for build and test commands,
and [NOTICE.md](NOTICE.md) for attribution and licensing information.

## Safety

Do not test against the installed editor directory or live mod files. Work from
copies, record hashes before and after each operation, and keep the original
EI Mapper v0.3 installation and `options.json` untouched.

## License

GPL-3.0, preserving upstream authorship and history. Distributions of binaries
must include the corresponding source and notices.
