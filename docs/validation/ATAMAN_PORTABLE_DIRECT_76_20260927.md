# Ataman 76: portable test launch without Starter

Date: 2026-09-27. Task: EI-PORTABLE-DIRECT-76.
Branch: codex/portable-direct-launch-v76.
Base: f0ce0df9d84313f0168a4d294c7110eb7a1d2540 (installed75).

## Cause and implementation

The owner reproduced the rejection with selected PZ/MODS/DejaVu and the previous
JamaisVu135 AddonPath, and confirmed that EIStarter was closed. Version75 still
required the global Starter registry selection to match. Removing this gate alone
would leave engine settings dependent on an unrelated installation.

The portable path now parses portable-defaults.xml and overlays optional
portable-settings.xml. Supported numeric, binary, string and multistring values
are serialized into a bounded run-local snapshot. Duplicate/invalid/out-of-scope
values fail closed. Selected paths are recomputed, mod confinement is checked,
saved autoexec is cleared, and a missing display resolution gets an800x600 default.
The normal installed profile retains its existing Starter validation.

The x86 supervisor creates a fresh volatile, uniquely named registry branch under
HKCU/Software/EIMapperAtaman/TestRuns. Before game entry, the bridge uses Windows
RegOverridePredefKey to give only the owned game process this isolated HKCU.
Starter and legacy Nival namespaces are populated. Original user registry values
are not replaced or restored; writes from the test process stay in its branch.
The supervisor deletes it after process exit or startup failure. It now pumps
WM_CLOSE during normal wait so Stop Test can release the profile. A forcibly
killed supervisor cannot run destructors; its volatile branch can remain until
Windows shutdown, but cannot overwrite the installed game profile.

The launcher refuses mismatched old host/bridge components. Neither PZ settings
nor config.reg, maps, saves or resources were edited during these checks. Runtime
preferences changed inside a test game are intentionally not exported to PZ.

## Evidence

- `build-tests-76-portable/results.txt`: 11PASS,0FAIL,0SKIP; includes installed
  PZ profile read-only parsing and saved override/type/malformed-profile checks.
- `portable_registry_tests.exe`: x86 native isolated read/write, real user root
  absence of probe value, reset and owned branch deletion PASS.
- Production host with a valid profile and deliberately nonexistent executable:
  exit66; registry branch inventory unchanged after cleanup PASS.
- `build-tests-76-launcher/results.txt`: 57PASS,0FAIL,3SKIP. New synthetic
  portable staging test reaches invalid-MOB validation after writing its private
  profile, without any Starter/config.reg; never executes its fake game file.
  Optional external autorun corpus, zone6 graph and real gameplay smoke skipped.
- Release wrapper: exit0; matching x86 bridge and host built. Final host also
  rebuilt after adding WM_CLOSE handling; corrected missing user32 link and
  repeated native host startup-failure/cleanup check successfully.
- Native `--ui-widget-check`: exit0, PASS. Packaged `--version`:76.
- Fresh stage EXE equals release EXE by SHA256. Existing unrelated Russian QM
  restored byte-for-byte after the build wrapper's generated translation step.

Initial test harness errors were not counted as passes: one malformed QtTest
output argument; the initial native test used a predefined pseudo-handle rather
than RegOpenCurrentUser to inspect the host root. Corrected tests passed above.

## Open acceptance

Real gameplay, controlled player spawn and Stop Test through the complete game
are not verified here. Keep EI-PORTABLE-PZ-75 open for that runtime acceptance.
Manual check: leave Starter closed, launch the installed mapper, open PZ map,
run Test Current Map, verify player/map/resources and stop the test. Earlier
terrain/patrol/water/data compatibility tasks are not closed by this work.

The isolated test HKCU does not inherit other user registry preferences. Its
scope is the owned test game, not normal game launches. Administrative/system
HKLM settings and drivers remain outside this override.
