# JMV launch bridge

This x86 `.plg` is a profile-specific, temporary runtime provider for the
validated JMV stack. It is not loaded into EI Mapper itself. During a test run
the editor copies the bridge into the active mod root, where Starter's supported
plugin loader calls `EntryBeforePatch`/`EntryAfterPatch`. The editor verifies its
SHA-256, passes the source and disposable map paths through the child environment,
and removes the bridge only if its hash still matches.

The bridge accepts only the private `endurance_launch ed????????` marker emitted
by JMV's existing `2utoexec.cfg` transition hook. It verifies the supported
`game.exe` SHA-256 and hook prologue, redirects only the selected source map's
`.mpr`/`.mob` file opens to hash-owned temporary copies, then invokes the engine's
existing server handler with the real map identity on the game thread. Any
mismatch leaves the runtime untouched and is written to the run-specific log.

Build with `build-x86.cmd` from an x86 Visual Studio 2022 tool environment. The
generated binary and PDB under `bin/` are local build artifacts.
