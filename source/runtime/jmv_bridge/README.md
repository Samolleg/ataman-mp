# JMV launch bridge

This x86 ASI is a profile-specific, temporary runtime provider for the validated
JMV stack. It is not loaded into EI Mapper itself. During a test run the editor
copies the bridge beside `game.exe`, verifies its SHA-256, passes the disposable
map name through the child environment, and removes the bridge only if its hash
still matches.

The bridge accepts only the private `endurance_launch ed????????` marker emitted
by JMV's existing `2utoexec.cfg` transition hook. It verifies the supported
`game.exe` SHA-256 and hook prologue before installing anything, then invokes the
engine's existing server command handler on the game thread. Any mismatch leaves
the runtime untouched and is written to the run-specific bridge result log.

Build with `build-x86.cmd` from an x86 Visual Studio 2022 tool environment. The
generated binary and PDB under `bin/` are local build artifacts.
