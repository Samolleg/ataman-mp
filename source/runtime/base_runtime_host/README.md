# Base EI owned runtime host

This is a small 32-bit helper because the 64-bit editor cannot directly call
32-bit remote exports. It creates only the requested Base `game.exe`, suspended,
places it in a kill-on-close Job, loads the run-local bridge, calls
`EnduranceInitialize` outside loader lock, and resumes only after the bridge
accepts the exact fingerprint and byte guards. It never attaches to an existing
game process and never writes into the game installation.

Build with `build-x86.cmd` in an x86 Visual Studio environment. Generated files
under `bin/` are local build artifacts.
