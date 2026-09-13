# EI-MOB-SCENARIO-VISUAL-001 acceptance

Baseline: af24d453ac3a5a86f1664a4b1af7cefaec1a9a08.

The existing path is MOB encrypted text -> decoded document text -> independent
script editor buffer. CScriptReferenceAnalyzer filters only requested literal
IDs for ID-repair review. It has no representation for commands, named spawns,
conditions, unresolved targets or destinations, and no scenario viewport consumer.
The first missing state is the analysis result, before rendering/navigation.

Synthetic original scenario (no installed data):

```
DeclareScript Gate(this : object)
Script Gate(
  if(IsAlive(GetObject(7))) then(
    AddUnitToServer("visitor", "fixture.guard", "Visitor", 12, 14, 2)
    SetCP(GetObjectByName("visitor"), 18, 20, 3)
    SwitchLeverState(GetObjectByID("8"), 1)
    MoveToPoint(this, GetX(GetLeader()), 9)
  )
)
```

Expected: source/function/line links, objects 7/8 highlighted, virtual visitor
spawn and destination at literal XYZ with verified fixture.guard prototype,
all calls retain the enclosing condition; `this` and computed X remain unresolved
with no invented location. Neither analyzing, inspecting nor navigating changes
document bytes, script buffer, dirty state or history. Comments/prose/numbers do
not produce references. AddObject is group membership, never entity creation.

Research used installed API documentation read-only and current public repository
parser/runtime fixtures. EI syntax uses `Script name(...)`, `WorldScript(...)`,
`if(...) then(...)`, and names containing `#`. AddUnitToServer has name/prototype/
display-name/XYZ arguments; SetCP/SetCPFast take object/XYZ; MoveToPoint takes
object/XY; MoveToObject takes two objects. SwitchLeverState/Ex, EnableLever and
ActivateTrap control an object, and do not prove player interaction or execution.
GetObjectByName refers to runtime identity; do not equate a display label or
invent a hashed numeric ID. Float GetObject precision requires conservative
handling, while string GetObjectByID preserves an exact uint32 identity.

Acceptance matrix:

- Lexer skips escaped strings, prose, // and /* */ comments; preserves UTF-16
  source offsets and CRLF/CR line locations; handles incomplete draft syntax.
- Real script blocks/conditions, nested calls, command arity and dynamic arguments.
- Spawn prototype validation, finite coordinates, XY without a fictional Z;
  dynamic/missing prototype, duplicate spawn names and ambiguous IDs stay explicit.
- Active primary/supplemental AddMob closure, conditional dependency caveat,
  unrelated loaded maps, missing dependency, duplicate IDs and zero/float IDs.
- Navigation uses the analyzed source revision; unapplied editor buffers take
  priority without applying them; close/discard/undo/map switch refresh safely.
- Overlay consists only of transient geometry; focus links belong to the selected
  participant. No marker becomes an editable/persisted CNode.
- Release, focused integration diagnostic, full UI diagnostic, package finalizer;
  interactive package execution remains the user's check.

Graphs, runtime execution/reachability and arbitrary symbolic evaluation are
deferred. Unsupported expressions remain visible as unresolved. Named EI skills
were absent from the catalog and searched local skill/plugin directories.
