# Endurance owned runtime host

This is a small 32-bit helper because the 64-bit editor cannot directly call
32-bit remote exports. It creates only the requested `game.exe`, suspended,
places it in a kill-on-close Job, and loads the run-local bridge outside loader
lock. Base EI is activated before process resume. JMV path isolation is prepared
before resume, while engine detours are activated only after the owned process's
executable patch phase has been observed and has become quiescent. This prevents
Endurance from racing Starter/JMV's own startup patches without pinning those
extensions as manifest components. The host never attaches to an existing game
process and never writes into the game installation.

Build with `build-x86.cmd` in an x86 Visual Studio environment. Generated files
under `bin/` are local build artifacts.
