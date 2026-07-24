# EI Mapper Endurance 0.3.0-alpha.2

Focused collection and graphless-runtime regression build.

- Editing the currently loaded collection now offers to reload it. The normal
  dirty-document save prompt runs before the workspace is replaced.
- A collection with an empty `map=` identity remains explicitly unmapped after
  load/reload, so entrances and metadata from the previous map are cleared.
- All bundled particle test collections are now unmapped and require a Play
  From Here anchor.
- Release builds now rebuild the x86 Test Current Map bridge and runtime host;
  stale runtime binaries are no longer silently copied into packages.
- Graphless launch preflight rejects an obsolete bridge with a clear error
  instead of allowing game.exe to stall/crash during staged map loading.

Focused checks: collection document tests, JMV launcher tests, complete release
build (including fresh x86 runtime components), and UI widget check.
