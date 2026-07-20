# Base EI runtime feasibility probe

Base direct launch is experimental and is enabled only for the exact executable
observed in the local Base installation (the private installation path is not
recorded):

- size: `3,825,725` bytes
- SHA-256: `B871D70D3876B2A5634F0920C92118BC49058B50C7D8CBE68376349F51C2E570`
- machine: PE32/i386 (`0x014c`)

The comparison runtime is the fingerprinted Starter engine with SHA-256
`BE0FC330035E03983CCE590038CC7F881F1F5D1BCF082717F31776DD4DEE94E2`.
Both files have the same section layout and differ at exactly five file bytes:

| File offset | Base | Starter | Classification |
|---:|---:|---:|---|
| `0x13e` | `0x0f` | `0x2f` | COFF characteristics; Starter adds `IMAGE_FILE_LARGE_ADDRESS_AWARE`. |
| `0x180..0x182` | `00 00 00` | `8f 09 3b` | Optional-header checksum (`0` versus `0x003b098f`). |
| `0x56bb5` | `eb` | `74` | Executable code: Base has unconditional `jmp 0x456c10`; Starter has `je 0x456c10`. |

The single code difference is outside every bridge entry point currently used.
The provider still verifies the complete executable hash and independently
checks the expected prologues at the map loader, transition controller, New
Game bootstrap, and player-default reset before installing any detour. The
loader detour also accepts redirection only from the expected transition caller.
An exact hash match alone is therefore necessary but not sufficient.

## Owned-process transport

Endurance starts the Base executable suspended through the packaged 32-bit
`EIMapperEnduranceRuntimeHost.exe`. The host creates a kill-on-close Job,
injects the run-local bridge DLL only into that newly created process, calls the
explicit `EnduranceInitialize` export outside `DllMain`, and resumes the game
only if initialization succeeds. No DLL or map is placed beside Base
`game.exe`; all provider and map payloads live under the test-run directory.

## Probe ladder

1. Observation only: verify owned injection, file-observation hooks, main menu,
   and normal exit. No map detour is installed.
2. Pass-through: install byte-guarded detours but keep a non-launch state so
   all original map arguments pass through. Start Base New Game manually.
3. No-op redirect: accepted only when the staged pair is byte-identical to the
   editor source files.
4. Editor-state redirect: stage the validated current editor pair.

Each level is reported separately. Until all four are tested successfully, the
result is an experimental hash-specific probe rather than validated Base direct
launch support.
