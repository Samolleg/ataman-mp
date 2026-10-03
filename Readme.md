# EI Mapper Ataman

Соавтору и новому Codex: [вход в проект](docs/COAUTHOR_HANDOFF.md),
[шаблон задания](docs/CODEX_TASK_TEMPLATE.md). Начните с них перед настройкой
окружения; действующие правила остаются в AGENTS.md и проектных договорах.

Самостоятельная локальная линия от
`19168b91a10562525fb6c83b665275c2fd43cf4c` (2026-09-17).
Git-история и авторство сохранены. Приватный репозиторий исходников:
[Samolleg/ataman-mp](https://github.com/Samolleg/ataman-mp).
При переносе история очищена от игровых карт/графов и старых сборок.
Исходная локальная история сохранена отдельно; Git SHA очищенной линии отличаются.
О составе и проверках: [публикация исходников](docs/PUBLISHING_SOURCE.md).
Единственная рабочая ветка — `main`. По решению владельца 2026-10-03
«сделай коммит» означает коммит в main, «отправь на GitHub» — обновить main
на GitHub без дополнительного согласования интеграции.
Правила: [AGENTS.md](AGENTS.md),
[порядок разработки](CONTRIBUTING.md), [дорожная карта](ROADMAP.md).
Имя исполняемого файла остаётся `EIMapperEndurance.exe`; код не переименован.

EI Mapper: Endurance Edition is an independently versioned derivative of
[`konstvest/ei_maper`](https://github.com/konstvest/ei_maper), focused on safe,
fast Evil Islands and Jamais Vu map editing.

Current Ataman version: `103`. Each separate fix advances the integer version.
Selected architecture parts can be saved as catalog assemblies, placed and copied
as one rigid group, then disassembled. Definitions and editor membership live in
the profile model catalog beside database.res. See [assemblies](docs/OBJECT_ASSEMBLIES.md).
Rigid building modules can snap by authored connector frames stored in the profile
model catalog. The gallery includes a connector editor; moving/creating a module
supports a magnet, connector choice, Esc and atomic Undo/Redo. See
[modular building guide](docs/MODULAR_BUILDING_SNAP.md) for setup and limits.
Working dialogs remember their position and size. Workspace docks, including
Travel and Map Scenario, restore their floating geometry and tab arrangement
after initialization; unavailable monitors do not leave windows off screen.

The default dark theme uses readable hierarchy text, disabled controls, status messages
and script highlighting. `--dark-theme-check` checks real widgets and text contrast;
`--ui-widget-check` uses the same production palette and includes this check.

Technical objects have a model gallery with lazy previews and search by model ID
or editable Russian name. `model-catalog.json` next to the active profile database.res is user
data: `models` stores Russian names, root `modelTextures` is the portable texture
library, and `profiles` retains legacy overrides and local `scanCache`. Moving
the same mod to another computer no longer loses the bindings. Old JSON is read
without manual editing; conflicting legacy profile bindings are not merged. Unchanged MOBs reuse their saved metadata and pairs
after restarting; Russian names and texture bindings survive program updates.
Existing object properties use the same gallery and matched texture choices,
with the Russian name beside the model ID, search by either name, clearly grouped
model/other textures and one undo action for a gallery model/texture choice.
The old executable-side JSON is retained as a migration backup; existing profile
catalogs take precedence. Invalid or externally modified JSON is never overwritten.
Open **Create object → Technical objects → Choose model from gallery**. Select a
tile, edit its Russian name and press **Save name**. Texture choices begin with
the model's bindings; **Bind texture** adds an installed texture. Double-click a
tile or press **Choose model** to apply its technical ID and selected texture.
Russian names start empty, and do not rename model resources or MOB objects.
For a manual JSON edit, use `models.<model-id>.russianName` and the ordered
`modelTextures.<model-id>` array for portable bindings. Keep `schemaVersion: 1`;
legacy profile-specific overrides remain supported. Reopen the gallery to load edits.
A populated library is used immediately without automatic MOB scanning. Use
**Обновить связи из MOB…** in the gallery to refresh observations when maps change.
Gallery sections have icons and three island tabs: Gipat, Ingos, Suslanger.
**All models** includes unassigned resources. Choose **Section** and **Island**
then **Save section** to assign/move a model; **Unassigned** clears placement.
The JSON fields `models.<model-id>.category` and `.region` are optional, so older
catalogs stay readable. Categories: `houses`, `architecture`, `gates`, `utensils`,
`trees`, `bushes`, `levers_chests`, `decor`; regions: `gipat`, `ingos`, `suslanger`.
No subtitle is displayed when the Russian name is empty.
Open **Create object → Technical objects → Choose model from gallery**. Select a
tile, edit its Russian name and press **Save name**. Texture choices begin with
the model's bindings; **Bind texture** adds an installed texture. Double-click a
tile or press **Choose model** to apply its technical ID and selected texture.
Russian names start empty, and do not rename model resources or MOB objects.
For a manual JSON edit, use `models.<model-id>.russianName` and the ordered
`modelTextures.<model-id>` array for portable bindings. Keep `schemaVersion: 1`;
legacy profile-specific overrides remain supported. Reopen the gallery to load edits.
A populated library is used immediately without automatic MOB scanning. Use
**Обновить связи из MOB…** in the gallery to refresh observations when maps change.

Portable DejaVu: choose the package root containing `Engine`, `Base`, and `MODS`
in Game and mod setup, then the DejaVu profile. The editor loads the selected
mod resources before `Base/Res`. Test Current Map prepares an isolated temporary
Windows profile from portable-defaults.xml and portable-settings.xml; the portable
Starter need not be open. Test-process settings are discarded after exit; the
installed Windows profile and portable preferences are not changed. Game-runtime
acceptance of this portable layout is still pending.
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

This fork preserves upstream attribution and exports its history without game
fixtures or old builds to the private Samolleg/ataman-mp repository. The original
local history remains intact. Historical upstream GitHub links are attribution
and evidence, not authorization to synchronize with upstream projects.

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
