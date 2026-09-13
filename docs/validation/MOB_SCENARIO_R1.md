# EI-MOB-SCENARIO-VISUAL-001 — first-stage validation

Baseline: `af24d453ac3a5a86f1664a4b1af7cefaec1a9a08`, refreshed from origin/main.
Branch: `contrib/Samolleg/mob-scenario-layer`. Local work only; no publication.

## Original problem and implementation

[Repeatable acceptance and first missing state](../research/mob-scenario-layer.md).
The legacy analyzer returned one numeric lookup for the three-operation original
probe and exited 1 before implementation. The new value-only analyzer represents
calls, enclosing script names and conditions, exact identities, dynamic targets,
MOB dependency closure, and verified virtual markers. It never receives a mutable
document, node or interpreter. The old ID-repair analyzer remains unchanged.

`CScenarioPanel` uses live object snapshots and the actual internal editor buffer;
unloaded family members come from the existing validator's read-only report.
Dependency closure is recomputed from analyzed buffers (including removal of an
AddMob). Missing or ambiguous sources remain incomplete. Numeric IDs are resolved
against the active family, not arbitrary neighboring loaded maps; collisions
stay ambiguous. GetObjectByName is bound only to a unique explicit script spawn,
without equating display labels or inventing hashed IDs. GetObject's float-ID
range is deliberately conservative; exact uint32 string lookups are supported.

The dock provides call/source/function/line/status, code preview, source and
participant navigation, conditions and unknown expressions. Virtual glyphs are
plain overlay geometry, never persisted/pickable CNodes. Only the focused
participant gets destination/relationship lines. XY-only positions are projected
only against a loaded surface, and are explicitly labeled as such.

## Checks

- Release wrapper: editor plus fresh x86 bridge/runtime host built with retained
  Qt 5.12.12 / MinGW 7.3 and VS BuildTools.
- Scenario model: **14 passed**, including the original scenario, comments/prose,
  CRLF/CR offsets, malformed drafts, escaped/dynamic literals, conditional loops,
  prototype/coordinate validity, family/external/duplicate scopes and nonmutation.
- Existing script-reference tests: **6 passed**. Family-validation tests:
  **32 passed**. Localization contract: passed. Viewport channels: **8 passed**;
  shadow-state contract: **4 passed**.
- Hidden native Windows integration/desktop GL with Russian catalog and corpus:
  **39 passed**. Covers actual Cancel/Discard/Save dialogs, document undo/redo
  while a separate draft exists, source revision guards, source lines, byte/dirty/
  history preservation, object-ID changes, map switching, replacement editors,
  repeated accepted close, layer disable, unload and destruction with an open
  script window. Native glyph pixels use the production shaders and adversarial
  stale instancing/animation state.
- ANGLE integration and pixel/state checks: **33 passed**, zero failures on the
  final executable. Native windows are kept hidden, and the packaged GUI is never run.
- Read-only JMV Zone1 family: 4 sources, 534 references, 362 conditioned records,
  1 creation call, 21 unresolved targets, 6 markers; complete inspected closure,
  analysis 9 ms. Base Zone1: 1 source, 155 references, 104 conditioned records,
  8 unresolved targets, no creation/coordinate markers, analysis 2 ms. Input
  hashes remain unchanged. These are bounded corpus checks, not whole-game proof.
- Staging and permanent package finalizer/distribution validator pass. All 92
  delivered files have identical hashes to finalized staging. One current changelist,
  Qt/compiler/runtime dependencies, notices and checksums; no settings, logs,
  dumps, saved maps, proprietary/private source or copied installed game assets.

Executable SHA-256:
`b6218ec8fd53469d35e50b8d050f896a13da215f907937cc8e69eb01904b5933`.

## Diagnosed regressions and limits

The scenario GL test originally detected `GL_INVALID_OPERATION` in ANGLE. A
bounded probe traced the first error to the shared overlay's core VAO reset on
an ES2 context, before drawing any glyph. The reset now uses a supported core
or OES entry point; scenario desktop/ANGLE state and pixel tests pass. This does
not establish that every other historical ANGLE rendering issue is fixed.

The full `--ui-widget-check` remains **not passed**: offscreen/minimal reproduce
`0xC0000005`; even a standalone Qt QMessageBox probe reproduces it. Native Windows
gets further but stalls after its Russian-layout output; the owned test process
was stopped. This broad test is neither fixed nor waived by the scenario tests.

Before handoff audit: no remaining material low-confidence implementation choice
is being treated as a proven runtime fact. Explicit evidence gaps remain:
representative interactive map acceptance, complete game-wide API/grammar
coverage, and the broad UI diagnostic. Arbitrary symbolic aliases/call arguments,
runtime hashes, execution/reachability, event graphs and runtime tracing are not
implemented. New dependencies outside the currently inspected source inventory
remain incomplete instead of being guessed. The named EI safety/reference skills
were unavailable; research used authorized read-only installed documentation and
resources, without private-source inspection or disclosure.

Delivered unpacked folder:
`C:/GOG Games/maped/EI-Mapper-Endurance-0.5.0-alpha-mob-scenario-r1-test`.
Next: user acceptance using [the tester steps](../../packaging/MOB_SCENARIO_R1.md).
The parent remains open for that acceptance; passing one parser test or build
does not close it.
