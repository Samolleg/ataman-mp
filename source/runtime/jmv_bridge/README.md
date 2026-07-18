# JMV single-player map-loader entry probe

This x86 `.plg` is a temporary, telemetry-only provider for the fingerprinted
JMV runtime stack. It is not loaded into EI Mapper itself. During a probe run
the editor copies it into JMV's configured `DEV/Plugin=plugin.dll` slot, where
Starter calls `EntryBeforePatch` and `EntryAfterPatch`. The slot must be absent;
Endurance never replaces an installed mod plugin. The provider and other
temporary files are removed after exit only when their SHA-256 values still
match the run manifest.

The provider verifies the exact approved `game.exe` hash and patches the
already loaded modules' imports of `CreateFileA/W` and
`GetFileAttributesA/W`. It records only `.mpr`, `.mob`, and `.mq` operations,
including thread ID and a bounded module-relative call stack, in the run's
`bridge-result.log`. It also installs a byte-guarded entry probe at the normal
single-player loader identified by the alpha.51 evidence. That probe records
the loader's caller, owning controller, and three arguments without changing
them. Module bases are recorded so addresses can be reconciled offline.

The probe is deliberately passive:

- it invokes no console or internal game command;
- it performs no map redirection;
- it does not modify Starter, JMV configuration, maps, saves, or movie files;
- it does not attempt to infer readiness from a fixed delay.

The user should reach the main menu, start New Game normally, optionally travel
to another zone, and exit normally. Those transitions provide the evidence
needed to locate the real single-player map-loading path. The previously tested
internal `server` handler is intentionally absent because runtime evidence
showed it enters an incomplete multiplayer/server transition instead.

Starter treats a non-zero callback return as an assertion condition, so both
exports always return zero. Initialization failures are reported in the probe
log and otherwise leave the runtime untouched.

Build with `build-x86.cmd` from an x86 Visual Studio 2022 environment. Generated
binary/PDB files under `bin/` are local build artifacts.
