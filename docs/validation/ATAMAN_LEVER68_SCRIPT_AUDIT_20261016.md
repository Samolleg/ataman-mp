# Zone1 / lever 68: read-only diagnosis, 2026-10-16

Status: OPEN. Owner confirms interaction occurs on a new save, but dagger remains and inventory item is absent. No cause or fix has yet been established.

## Confirmed evidence

- Current map script matches the pre-feature 00:57 backup byte-for-byte after decoding: SHA256 231590910e7d333944f3b179773167480068e03f38fef6977d41511a3ba25cc9.
- Entire serialized object 68 matches that backup: SHA256 e5962e2fa08bc42159ea3abe68a31d9d00d1ad3f29ddc50005f8ded9dda8a8d6. Model uhwdg03r; script registration names cheat dagger.bronze. Script-access flag false was already present.
- DB approach changed from two offsets (2,0),(-2,0) to one active offset (0.4,-0.8). Scale, time, material, sound and description unchanged. Last database save changed only levers.ldb; item database members unchanged.
- Actual recent runtime is original game.exe with addon.dll/plugin.dll, not the Rebuild executable. Installed addon SHA256 34209B33786F970FBDA5387FC7E2E93677E438B70C3C2447AB5997F48A138B6B. Available reference code is not proven to match this binary.
- Read-only decoding of savea (20:40:24) and saveq (19:15:43) succeeded, including envelope checks. Both contain the registered-loot item marker for object 68, with values 2 and 1 respectively. Neither decoded mission has registered-loot pending/player markers. This confirms persisted registration; it does not establish when these saves were taken relative to the failed interaction or prove which runtime branch failed.
- Existing logs do not contain activation/grant tracing sufficient to establish the failed branch.

## Remaining diagnosis

Need runtime evidence for the transition from successful lever activation into registered-loot callback and item grant, or a controlled comparison of old/new approach coordinates with otherwise identical inputs. A visible interaction animation alone does not establish a successful ApplyScience return. Do not modify script-access flag, restore live database, or claim a mapper/runtime cause based solely on these static comparisons.

Private working evidence stays in ignored build-lever68-audit-20261016 (audit.json and decoded save payloads). No game resources, saves, installed program or runtime DLLs were changed. No release/version increment for this audit.
## Follow-up: editor save and horizontal item approaches

Owner confirmed that the successful editor save also assigned Tools\loot.wav to new levers. Reconstructed the pre-editor levers.ldb using unchanged original 50 records, the current eight appended records with field6 restored to a single NUL, canonical new-record headers, and preserved suffix. SHA256 matches the recorded mapper output exactly: 1e21b59b568c3a2ef6c18b7807ddd45da5d3e82ef5fd99d160b6a1149d315238. All other RES member payload hashes are unchanged. Thus the table was not repaired structurally by the editor: only eight sound fields differ. This does not prove that blank sounds caused the ID68 symptom; its own record was unchanged. Outer RES metadata was not reconstructed by this comparison. Receipt: build-lever68-audit-20261016/sound-comparison.json.

Owner decision: research the game portion only; do not patch/install runtime yet.

Runtime reference confirms authored offsets use the object's full oriented X/Y basis. A vertical/tilted weapon therefore tilts the approach plane as well. Mapper reproduces this in source/core/lever_approach.h; the inverse projected transform also becomes singular near a vertical basis. This is a semantic limitation, not merely incorrect marker drawing.

Proposed consistent behavior for the requested item categories:
- Keep DB offsets relative to the existing object origin, not a rotated visual bounding-box center. Use fixed world X/Y axes, ignoring all model rotation, including yaw. Preserve scale and actor clearance semantics.
- For nonzero d=(dx,dy), compute r=length(d)*scale+actorClearance and P=(origin.x+r*dx/length(d), origin.y+r*dy/length(d), origin.z). Zero offset remains the origin. Terrain/navigation should handle the actual walking surface; constant origin.z means the mathematical plane stays horizontal, not that it follows terrain elevation.
- Classify weapon/armor/quick/quest item models using validated item-model bindings, not model-name prefixes and not the fact that the object is any lever. Both uhwdg03r and uhwbw02 have item bindings in the active catalog. Mapper already has mapweapons-catalog.json and mapitems-catalog.json readers; game needs an agreed identical model-set contract and missing/stale binding behavior.
- Implement at the game use-point generation boundary (existing addon hook or Rebuild equivalent), preserving ordinary doors/chests/levers and all activation/tool/script checks. Then use the same policy for mapper forward/inverse placement and indicate whether the runtime supports it.
- Existing authored points need explicit migration/preview: reinterpreting local rotated coordinates as fixed XY changes their world position. Never silently rewrite every model's shared DB row.
- Acceptance: ID741 upright/lying/tilted and rotated in yaw, ID68, one/two points, four requested categories, normal lever/door regression, save/reload and actual RegisterLoot grant. Match mapper marker to real path destination. Current installed addon provenance must be verified before selecting patch sites; reference source is not proof of binary identity.

No program, game resource, runtime or save edits in this investigation. A mapper-only cosmetic flattening would misrepresent gameplay and has not been applied.