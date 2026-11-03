# Ataman275 — EI-QUEST-MAP-275 (2026-11-03)

## Scope and source identity

Owner's full brief: integrated quest parchment generator and mandatory movable
stamp editor; PNG/DDS and protected game export, no game artwork in source or
packages. Main only; STOP BEFORE INSTALLATION, no push, computer use, desktop,
clipboard, visible windows, Base maps, owner ProgramData locks or live writes.

274 was cancelled first by owner decision2026-11-03. Revert commit
`93399144df1906d6dc8bafe89c36f31919319e4a` restores program/shaders/
render_environment/tests exactly to `4463874f`; VERSION in that HEAD is273.
274 remains occupied.275 starts above that revert. AGENTS SHA256:
`2a2f58ef34f63d6c5d67d165d0b809b046d0a519181119d22ea8fdd8781fe3b7`.

Read-only sparse clone: `work/quest-map-275-20261103/upstream`,
https://github.com/Kyr4l/ei-universal-mod,
SHA `810a9019a57d1b3081e39d50444f3339ba0fa56f`.
Adapted: quest_map.hpp/.cpp algorithms, classification/name mapping, TSV
metadata, texture_app.cpp stamp background-removal approach. GPL-3.0-or-later,
Copyright(C)2026 Kyr4l; SPDX/copyright retained, exact source in NOTICE.md.
No quest-icons PNG or quest_icons_generated.hpp arrays are transferred.
No private engine sources were used; corresponding private-source skills are
not available in this session.

## Game binding and real resource audit

`map.txt` in the active profile's `texts.res` binds zones via `#maps`:
sector dimensions, then `MinimapTexture ObjTexture`. For the DejaVu zone1
MPR/MOB the game-zone ID is `gz1g`, and the line is `zone1map zone1quest`.
The MPR stem, game-zone ID and objectives texture are distinct identities.
The quest texture is the second token, not a quest-text substitution.
Existing names require no catalog edit. A changed name can be staged in the
normal Map Catalog Editor after export, then saved explicitly there.

Read-only audit: `work/quest-map-275-20261103/resource-audit.json`,
DejaVu-map.txt and Shared-stock-map.txt (private evidence, not in Git).
DejaVu `res/textures.res`:16 zone-number quest textures,13 at512x512,
one at256x256 and two at1024x1024, DXT1. Shared original
`C:/GOG Games/PZ/Base/Res/textures.res`:29 quest/questm records,256x256,
5551/A1R5G5B5, one-bit alpha. Only Base resources were read, no Base maps.
The inspected stock and mod zone1 originals are256/5551 and512/DXT1.
The public editor documents the opaque black-background `questm` companion;
the exporter publishes both members coherently in one RES transaction.

JamaisVu135: no installed texture resource directory found in C:/GOG Games
or known PZ project locations; only configuration/old save-check directories.
Its exact current resource path was requested. Separate real JMV readback
remains unavailable unless that path is supplied.

## Product behavior

Tools -> Quest Map Editor, directly beside Export Minimap. Own Qt dialog,
no separate program. Owning snapshot of canonical current terrain (including
unsaved edits), loaded MOB family static objects, and catalog exit rectangles
(`#remove`, otherwise `#deploy`). Object rotations are preserved for lines.
Default256x256, selectable size/seed/edge roughness/icon scale, terrain/water/
forests, three frames; generated icons can remain movable sprites.

Palette add, widget-local drag, delete, integer nearest-neighbor scale,
Undo/Redo, bake and Undo bake. Compositing changes only existing opaque
parchment pixels. Available extracted category variants take priority;
missing categories and scroll geometry have procedural code fallbacks.
Original stock-resolution sources are searched across configured archives,
so a winning HD override does not make an available stock crop disappear.
Cache is under QStandardPaths::CacheLocation/quest-map/icons-v1 and keyed by
source bytes and coordinates; no program-folder icon cache. Dialogs retain
their icon pixels when another map/profile is opened.

5551 and mod DXT1 export; “Use original size and format” selects the actual
loaded texture profile. Destination and replacement are explicitly confirmed.
Native lease, verified backup, streamed RES transaction, atomic publication,
then native readback. Unknown members remain byte-identical. PNG/DDS use an
explicit file choice and atomic QSaveFile. DDS is standard uncompressed32-bit.

MCP deferred under the brief's explicit allowance: this independent graphical
document and RES export need a dedicated preview/apply/save authority adapter;
no bypass of dock permissions or partial remote save was introduced.

## Verified behavior

Production MainWindow/real dialog, isolated test executable linked to production
objects, private settings/AppData/cache/locks/backups and copied maps. Only
widget-local mouse events are delivered; no QCursor or desktop automation.
Windows enumeration verifies zero visible windows.

Final zone1 (`final-zone1`):85 PASS/0 FAIL/exit0,32.84s; zone7
(`final-zone7`):85 PASS/0 FAIL/exit0,35.69s. Both use production MainWindow
and the actual quest dialog, Russian catalog loaded.758/1489 model/exit markers,
209 original extracted icons,221 total palette entries with fallbacks.
Native SS_TEXT decoding discovers all3 zone1 MOBs and all5 zone7 MOBs; every
discovered member is copied and loaded. The earlier raw-byte fixture regex
was replaced because SS_TEXT is encrypted; its initial67 checks did not prove
complete family loading and are superseded by the final runs.

Three distinct frames, procedural-only frames with no texture sources, live
seed preview/determinism, goblin4/9 and bridge classification, add/drag/delete/
scale/Undo/Redo/bake/Undo bake, clipped opaque-only stamping. Runtime icon021
matches an independent crop of stock zone3quest at the TSV coordinates.
5551/native reload, exact binary alpha, opaque questm, DDS32 independent
header/pixel readback, original mod512/DXT1 dimensions/format/alpha; existing
RES replacement refusal, exact original backup hash, coherent pair in a new
RES, every unrelated existing member hash retained. MPR/MOB stay unchanged.

Additional production Map Catalog Editor run:6740 PASS/0 FAIL. Calls the new
stageObjectivesTextureForZone, saves through its real asynchronous save path
to a physical private texts.res copy, reopens it, checks the exact expected
map.txt bytes (one objectives token), the member set and every other member's
hash. Source texts.res remains unchanged. Zero visible native windows in all
three UI runs. Final protected-byte and metadata drift arrays are empty for
both full map runs; no source/installed program was written.

The first runner reported metadata drift for installed endurance-options.json:
SHA256 and length stayed identical, only mtime changed. An owner-launched
installed editor was present (PID10720, creation2026-11-03 10:00:21); no process
was stopped or controlled. All protected contents were rehashed unchanged.
Metadata changes remain separately recorded, not presented as unchanged mtime.

Qt DDS image plugin is absent from the retained kit. Independent DDS header/
pixel readback passed; native game compatibility is checked through MMP,
not by pretending the unavailable DDS plugin passed.

## Preservation, build and handoff

Revert evidence: `work/quest-map-275-20261103/revert274/`. All other dirty
owner/275 files retained exact SHA256 during rollback. view.cpp retains exactly
the13 owner-added lines; a reverse/forward roundtrip matches the complete
pre-action Git-normalized blob `473df40b61271b441fe699df5bd73ea5bf272b03`.
git apply normalized its physical line endings; whole-file pre-action SHA is
therefore not claimed identical. After resume the post-revert SHA remains
`752f7c861ad14f83622f05ef6d63b7cb09c5726114cff7de46a10d5b13d98830`.
The remaining owner dirty source/test/QM/.mcp.json hashes match initial receipts.
Owner ROADMAP sections remain outside the scoped index; only275's own entry
is added to committed HEAD's roadmap. Owner QM is never committed or rebuilt
in the working checkout; lrelease operates on archive sources only.

Final Release from git archive tree
`187414e17768a81ec99b625be4df9f33241f19be`:
`work/quest-map-275-20261103/source-final` and
`work/quest-map-275-20261103/build-release-275-final`.
The corrected TS produces3414 finished translations in the archive; source
checkout QM remains untouched. Later changes affect only verification/docs;
every production input matches this archive tree. Runtime bridge and host
rebuilt from source. Retained Qt5.12.12/MinGW7.3 kit read, not modified.
The wrapper's fallback VS discovery printed missing vswhere.exe warnings,
then found the supported compiler and built both runtime components.

Staging: `build-staging-275-quest-map-20261103`, compared with installed273.
Raw/staged EXE SHA256:
`5307420546de316607683f251368f9c8f633af43e41a6e5f515d67e03cfd92b9`.
Both hidden --version checks report EI Mapper Endurance Edition275, exit0.
Finalizer/composition receipt is in `work/quest-map-275-20261103/`.
No new icon PNG, generated game pixel array or loose game resource is included
in the scoped tree or staging; metadata is embedded via QRC. Screenshots stay
private in work, outside Git/staging. Composition:191 accepted managed paths retained,192 current files,0 losses.
The only new package path is
this validation report. Existing accepted managed paths are retained.

Snapshots (offscreen QWidget renders, not desktop screenshots):

- `work/quest-map-275-20261103/final-zone1/dialog-frame-0.png`
- `work/quest-map-275-20261103/final-zone1/dialog-frame-1.png`
- `work/quest-map-275-20261103/final-zone1/dialog-frame-2.png`
- `work/quest-map-275-20261103/final-zone1/comparison.png`
- `work/quest-map-275-20261103/final-zone7/comparison.png`
- zone7's three dialog/frame PNGs and both procedural-only frame sets are
  alongside these. Comparison top row: generated frames; bottom: configured
  original quest texture. Zone7 is the existing DejaVu zone, not a Base MPR.

Cleanup: automatic approval review rejected removal of85 verified private
test paths with `blocked by policy`. No bypass was attempted. Exact absolute
paths and preserved evidence are listed in
`work/quest-map-275-20261103/cleanup-plan.json`; copies/caches remain there.

STOP BEFORE INSTALLATION; no push. Owner visual/gameplay acceptance and the
unavailable separate JMV-resource check are not claimed. After authorized
installation, open a map family, use Tools -> Quest Map Editor, edit a stamp,
Undo/Redo/bake, compare with the original and export to an owned test mod copy.
