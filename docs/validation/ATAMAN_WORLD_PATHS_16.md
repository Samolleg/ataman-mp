# Ataman 16 — world resource precedence

Task EI-WORLD-PATHS-016, dependent on EI-WORLD-ADD-015.
Branch codex/world-path-priority-v16, base cce34cb4391bbc623781f6509440e6c724e5da60.

The v15 resolver accumulated same-name MPR/MOB across all profile roots and
required global uniqueness. Mod/base copies therefore produced empty paths;
refreshEditorWorld skipped these records, removing both list availability and
neighbor terrain/objects. Actual zone1/zone9 files exist in both roots.

Resolution now honors ordered root precedence, with direct children preferred
over recursive backup copies within a root. Equal-priority ambiguous recursive
matches remain unresolved. Explicit JSON paths are unchanged. No map.txt use.

Evidence: build-tests-16-world/before.txt reproduces empty MPR path before fix.
After fix, after.txt: 19 PASS, including ordered/reversed roots, same-root backup,
fallback, MPR/MOB, preserved coordinates and unchanged JSON bytes. Read-only
installed JamaisVu JSON resolves 24 records; zonemichel is genuinely absent in
both configured roots. First corpus assertion that every record exists failed on
zonemichel; the corrected check compares against independent filesystem lookup.

Release build passed (build-paths16.log, then build-paths16-final.log after the
integration harness was extended); executable reports 16. Hidden Windows OpenGL
integration: 49 PASS, including all 24 resolved MPRs parsed without validation
errors, active zone9 + five neighbors constructed from real JSON, MOB presence,
CPU model/texture preparation and GPU rendering. JSON stays read-only.
Logs: build-release-16-paths/neighbor-perf.stdout.txt.

General checks: version/database/resource navigation PASS, scenario 47 PASS.
The first scenario invocation inherited the OpenGL integration environment while
using offscreen QPA and failed its GL-context check. A clean invocation then
failed the synthetic backup check and crashed under sandbox; the identical
executable passed all 47 checks with access to isolated Qt qttest data. These
failed attempts are not PASS. UI-widget check timed out after 120 seconds, as
in prior versions; only the owned test process was stopped.

Package and installation pending. Full interactive visual acceptance remains
open; automated checks do not prove the complete user window's final pixels.
