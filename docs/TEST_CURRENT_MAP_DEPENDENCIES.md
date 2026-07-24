# Test Current Map dependencies

This document describes the dependency contract implemented by the current
editor-owned Test Current Map transport. It deliberately separates files that
Endurance must recognize from components that the selected game or mod needs
for its own gameplay.

## Endurance compatibility gates

Both Base EI and JMV require:

- a loaded, validated MPR and primary MOB;
- a usable `map.txt` identity and selected entrance, or a valid Play from Here
  position;
- the packaged `runtime/EIMapperEnduranceRuntimeHost.exe`;
- the packaged `runtime/EIMapperEnduranceJmvBridge.plg`;
- writable Endurance test-run storage under `%LOCALAPPDATA%`;
- an exact approved `game.exe` fingerprint plus successful byte guards at every
  hook site used by the bridge.

The executable selected by the active profile is:

- Base EI: `<game root>/game.exe`;
- JMV: `<game root>/Starter/Engine/game.exe`.

For JMV, the selected Endurance profile and Starter's persisted `AddonPath`
must identify the same mod directory. Starter loads and caches part of its mod
configuration before the run-local Endurance bridge can reliably virtualize
every consumer. Endurance therefore checks the persisted selection before
staging and refuses a mismatched launch with both paths shown. Select the
intended mod in Starter before retrying; launching the game from Starter is not
required for this selection update.

The run-local bridge still virtualizes later `AddonPath` reads inside the owned
game process, but this is defence in depth rather than a replacement for
preflight. Endurance does not modify `starter.config` or the user's global
registry selection.

The runtime host creates that executable suspended, loads a run-local copy of
the bridge into the process it owns, calls `EnduranceInitialize` outside
`DllMain`, and resumes the game only after validation succeeds. Endurance does
not request elevation, use Starter's `DEV/Plugin` slot, or attach to an
independently started game.

Approved executable fingerprints are recorded in
[the Base runtime probe](base-runtime-probe.md) and the built-in resource
profiles. A matching hash is necessary but not sufficient: guarded code bytes
must also match at runtime.

The canonical built-in executable allowlist lives in
`source/runtime/runtime_variant_registry.h`. Both the editor's profile
evaluation and the injected bridge compile from that same registry; neither
side maintains a private duplicate hash list. The registry remains built-in
and exact-hash. User-managed experimental variants require a future guarded
analysis/approval workflow and cannot currently be enabled by editing settings.

## JMV gameplay components versus bridge dependencies

Starter `addon.dll`, `SpellAddonX.asi`, the JMV `addon.dll`, Lua files, and mod
configuration are **not Endurance bridge-delivery dependencies or manifest
hash gates**. Updating one of them does not by itself make Test Current Map
unavailable.

They can still be required by JMV to run JMV content correctly. Endurance starts
the selected Starter engine in its normal JMV environment; it does not replace
the mod's gameplay implementation. A broken or incompatible gameplay component
may therefore make the game fail after Endurance has successfully delivered
the bridge, but that is distinct from bridge compatibility.

## Conditional JMV autorun-MOB dependency

A normal selected-entrance launch does not require Endurance to rewrite an
autorun script. The following optional features do require a readable,
structurally valid installed `AutoRunSpellAddon123.mob`:

- Play from Here;
- Godmode;
- a Time of Day override selected in the viewport;
- explicit supplemental or collection MOB loading.

For these features, Endurance creates a lossless run-local copy of the autorun
MOB and adds only the required `SetCPFast`, `GodMode(0,3)`,
`RunWorldTime(hours)`, or `AddMob(...)` commands. The installed MOB is never
changed. The time command sets the editor's effective clock and resumes normal
time progression independently of the selected viewport Lighting Mode. It is
regenerated for each hot-reload generation. The commands naturally depend on
the active JMV runtime implementing the functions they call; no particular
`SpellAddonX.asi` hash is treated as a global compatibility requirement.

## Staged content and isolation

The current in-memory MPR/MOB data, supplemental MOBs, bridge, manifest, logs,
and generation-scoped saves are staged beneath the owned test-run directory.
Temporary runtime aliases are journalled and removed only when their recorded
hash still matches. Live campaign saves and source editor documents are not
used as the test session's writable save namespace.

Base and JMV share the owned-process delivery mechanism. Profile-specific game
data, map conventions, bootstrap behavior, and optional providers remain
separate.
