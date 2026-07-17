# JMV launch bridge

This x86 `.plg` is a profile-specific, temporary runtime provider for the
validated JMV stack. It is not loaded into EI Mapper itself. During a test run
the editor copies the bridge into JMV's configured `DEV/Plugin=plugin.dll` slot,
where Starter's plugin loader calls `EntryBeforePatch`/`EntryAfterPatch`. The
slot must be absent before the run; Endurance never replaces an installed mod
plugin. The editor verifies its SHA-256, passes the source and disposable map
paths through the child environment, and removes the bridge only if its hash
still matches.

The installed Starter loader calls each export as a one-argument `cdecl`
function and treats zero as success. Both callbacks therefore return zero;
initialization failures are reported through the run-specific log.

After `EntryAfterPatch`, the bridge registers a one-shot Windows timer on the
same game thread. Once the message loop is active, the callback verifies the
supported `game.exe` SHA-256 and server-handler prologue, redirects only the
selected source map's `.mpr`/`.mob` file opens to hash-owned temporary copies,
then invokes the engine's existing handler with the real map identity. It does
not modify `2utoexec.cfg`. Any mismatch leaves the runtime untouched and is
written to the run-specific log.

Build with `build-x86.cmd` from an x86 Visual Studio 2022 tool environment. The
generated binary and PDB under `bin/` are local build artifacts.
