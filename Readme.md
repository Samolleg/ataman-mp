# EI Mapper Ataman — локальный форк

Самостоятельная локальная линия от
`19168b91a10562525fb6c83b665275c2fd43cf4c` (2026-09-17).
Git-история сохранена, remotes отсутствуют. Разработка, ветки и коммиты локальные;
GitHub и PR не используются. Правила: [AGENTS.md](AGENTS.md),
[порядок разработки](CONTRIBUTING.md), [дорожная карта](ROADMAP.md).
Название приложения и бинарников пока остаётся Endurance: код не переименован.

EI Mapper: Endurance Edition is an independently versioned derivative of
[`konstvest/ei_maper`](https://github.com/konstvest/ei_maper), focused on safe,
fast Evil Islands and Jamais Vu map editing.

Current Ataman version: `43`. Each separate fix advances the integer version.
See [Ataman changes](CHANGELOG.md). This numbering does not imply stable-release
acceptance; inherited Endurance validation gaps remain open.

Project status and upcoming work are tracked in the living
[Ataman roadmap](ROADMAP.md).

New contributors using Codex should start with the Russian-first
[safe contribution guide](CONTRIBUTING.md). It keeps each task on a separate
branch and reserves `main` integration for the maintainer.

Endurance creates hash-addressed recovery snapshots when MOB/MPR documents are opened and
timestamped pre-save revisions before replacing existing files. Backups and their JSON metadata
are stored under `%LOCALAPPDATA%\\EI Mapper Endurance\\backups`; use **Tools → Open Backup
Folder** to inspect them. A save is refused if its pre-save backup cannot be completed.

This local fork preserves the baseline's Git ancestry and upstream attribution.
No origin/upstream remote is configured. Historical GitHub links are attribution
and evidence only, not a synchronization or publication workflow.

## Alpha scope

- Lossless MOB/MPR transactions preserve unchanged bytes, patch verified source
  leaves for supported edits, create pre-save backups, and refuse unproven
  structural rewrites.
- Object, unit, behavior, patrol/look-point, trap, diplomacy, script, collection,
  and multi-MOB authoring share undo/redo and family-aware validation.
- Landscape tools cover terrain and overlay sculpting, tile painting, canvas
  resizing, material/animation editing, minimap generation, and navigation-graph
  inspection and regeneration.
- Profile-driven Base and Jamais Vu resource resolution powers character,
  equipment, environment, lighting, particle, water, shadow, and sky previews.
- Integrated Database and Resource Browser tools provide typed database editing,
  safe RES transactions, previews, cross-navigation, validation, and rollback.
- Test Current Map stages isolated Base/JMV runs with guarded runtime components,
  redirected saves, selected entrances, Play from Here, hot reload, and cleanup.
- Headless validation, round-trip, profile, widget, and corpus checks support
  repeatable verification without launching an interactive editing session.
- First-run setup, persistent multi-monitor docks, scalable UI, filtered logs,
  backups, autosaves, and crash diagnostics support long modding sessions.

The alpha remains preservation-first: unsupported or unproven mutations are
blocked instead of being serialized approximately. The roadmap identifies the
remaining release gates and distinguishes integrated work from isolated feature
branches still under review.

See [BUILDING_ENDURANCE.md](docs/BUILDING_ENDURANCE.md) for build and test commands,
and [NOTICE.md](NOTICE.md) for attribution and licensing information.
Character construction, provider differences, and safe Blender/ei_figer
authoring are documented in [CHARACTER_PIPELINE.md](docs/CHARACTER_PIPELINE.md),
[CHARACTER_AUTHORING.md](docs/CHARACTER_AUTHORING.md), and the
[character evidence matrix](docs/CHARACTER_EVIDENCE_MATRIX.md).
Particle reconstruction coverage, preview-only effects, the complete remaining
family list, and the torch corpus audit are maintained in
[PARTICLE_RENDERING_STATUS.md](docs/PARTICLE_RENDERING_STATUS.md).

## Safety

Do not test against the installed editor directory or live mod files. Work from
copies, record hashes before and after each operation, and keep the original
EI Mapper v0.3 installation and `options.json` untouched.

## License

GPL-3.0, preserving upstream authorship and history. Distributions of binaries
must include the corresponding source and notices.
