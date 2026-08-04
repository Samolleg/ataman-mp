# Character pipeline evidence and regression matrix

This matrix separates established behavior from probes that still need engine
or broader corpus evidence. Input assets remain private/read-only; fixture reports
may record hashes and structural summaries but not redistribute game data.

## Evidence ledger

| Claim | Provider | Decisive evidence | Regression fixture/control | Status |
|---|---|---|---|---|
| Normal/imported resolution split | Base/JMV | provider code + appearance tests | conflicting normal/imported unit; `bz18h` 1000054752 | Verified renderer/provider rule; runtime control retained |
| Provider isolation/no profile-ID branching | Base/JMV | provider registry/context tests | unknown ID and Base↔JMV switch | Verified |
| Lower before upper; Wear Order descending | JMV | resolver + focused tests | plate/leggings `1,0` and `0,0` | Verified renderer; successful corpus controls documented |
| Exact armor replacement plus linked descendants | JMV | `35f2ce9` + tests + ANHUM links | `unhufe` TTI24; autozone4hadagans 1000017; bz14h 1000295968 | Verified JMV corpus/renderer |
| `.x2` equipment transform, stored `.x4` body UV | JMV | viewer commits + UV tests/probes | zone1 203/205/221 | Verified JMV controls |
| Exact PNT3 block grammar | JMV | fingerprinted `2dintmmx.dll` SHA-256 `7FBBF9452520CC1A8F11E4C955B6DC1988AA24A624298B130E291C4368423B7A`, routine `0x100018AF`; 1,068-member audit | `UnhumaPL_05.bl.0.x4`, `UnhumaPL_24.bl.0.x4` | Verified binary/corpus/renderer |
| Material suffix exact; incompatibility fails closed | JMV | resolver tests and corpus | bz8k 1000292348; bz16h 382 | Verified provider; visible runtime diagnostic control |
| Helmet hair follows DB selection, exact assembly replacement/removal/fallback; quick items invisible | Base/JMV | shared runtime decision + focused provider/validator tests | legacy `hr`, exact `hr.NN`, replacement and equipped-undead controls | Verified algorithm/renderer/provider |
| Base one weapon; JMV first pair/layouts | Base/JMV | provider tests | ordinary, 1H+shield, dual 1H, 2H | Verified renderer/provider |
| Standalone item, family, and hand geometry are independent | JMV | clean nested FIG replacements plus controlled game screenshots | `InitWeapons.res/initwedg8weapon.fig`; `unhuma.mod/rh3.dagger08`; negative `lh3.dagger08` control | Verified JMV male runtime control; other families/hands require their own control |
| TitanGrip requires StrongMan ownership | JMV | bounded active-profile Lua parser + provider/validation tests | zone17 47533; zone19 997569 | Verified JMV provider rule; missing/malformed/absent rows fail closed |
| Attachment channel inherited once | Base/JMV | hierarchy tests + viewer | zon3obr 233/234 shield across frames | Verified renderer; runtime control retained |
| ADB standing IDLE and provider masks | Base/JMV | animation parser/provider tests | unarmed/1H/ranged, JMV 2H | Verified renderer/provider |
| 15 Hz, closing sample, child*parent, root-relative translation | Base/JMV | unit animation tests + ei_figer behavior | synthetic nested ANM | Verified renderer/ei_figer triangulation |
| Synchronized bone + vertex morph | Base/JMV | animation tests | JMV `unmodg` wing membrane | Verified renderer; runtime control retained |
| Complexion uses placed geometry values | Base/JMV | unit/provider flow + visual comparison | same prototype, varied MOB constitution | Verified renderer rule; per-family engine comparison incomplete |
| JMV saved appearance tuple is not direct primary-stat normalization | JMV | read-only decoded `unhufe` save record + renderer FIG8 interpolation + controlled Blender/game silhouette | tested heroine: displayed `25/25/25`, stored `0.20/0.30/0.85` | Verified for this hero record; general character-creation mapping unresolved |

## Tool and copied-fixture provenance (2026-07-22)

- `C:\Users\ilufu\Downloads\ПЗ Жамевю\ei_figer_v5.2.zip`:
  SHA-256 `DC15EA7DCD2E3703DFDE021051DF077F52F99E83124053445B2FDFB74E3649B6`.
  Audited research clone commit: `384f7b13da423d84d7f3e78f78ef0bf1a14d3714`.
- Portable Blender 3.6.23 `blender.exe`:
  SHA-256 `FC104BCAE000C8EDAD75BBC7B55EE6137929F6FD1BE4501EF3E28C6E3BC8986C`.
- Copied `zone1-203-textures/unhuma.res`:
  SHA-256 `E8430155D0AA96558B75BEDA8B66F7360C4BEAD7685D9D855FFBCEDCA88800CE`.
  Its copied `unhuma.mod` and `unhuma.anm` members hash to
  `A36DEB816DC607D8BC2C5B0165EE209D1D84DB69753228325C0FFD5A90372DBC`
  and `60DEE65FD9C6860CCB6D87DEC00B1B248FF92A5320E4BC2A2C1F11B4D98D9E0E`.
- Copied `bd.armor05` and `unhumapl_05.bl.0.x4.mmp` hash to
  `EF1AD153DACAF206A9602E30FF2653FD43188D8D5369604811E28BB0D1EA08D9`
  and `344B29256BB070B71EB72D342AE8186F550A141B1247160EE353E7F809891B4E`.

These hashes identify research copies, not proof of their original archive or
runtime provenance. Preserve that distinction in every report.

## Family controls

| Family | Representative controls | What must be checked | Open question |
|---|---|---|---|
| Human male/female | zone1 203/205/221/224; bz8k 919536 | DB assembly, full atlas, armor overlap, bow/quiver | Broaden Base material/TTI corpus |
| Orc male/female | zone3obr; `unorma`/`unorfe` | shared equipment atlas, model-specific skin, weapons | Complete Base/JMV gender matrix |
| Skeleton | `unhusk`, zone3obr | x4 skin and shared equipment families | Confirm every shipped skeleton subtype |
| UndeadF16/F17 | zone17 | keep embedded wearable assembly, DB skin 29/items | Why RaceModels mask differs remains data semantics, not replacement rule |
| Goblin | `unmogo` spear/sling | one integral group, primary/secondary texture, compatible idle | General Monster Items/MOB/FIG selection routine |
| Lizardman | `unmoli` | trident attachment/contact and two-hand ready idle | General integral-weapon resolver |
| Ogre | `unmoog*`; zone17 763 | ordinary OBJ-secondary club vs corpus-backed `club02` atlas | General alias/group selection rule |
| Dragon | `unmodr`, `unmodg` | standing idle, wing hierarchy + morph synchronization | Complexion/root-height behavior |
| Bat | corpus inventory required | embedded assembly, intrinsic parts, animation/morph channels | No complete fingerprinted matrix yet |
| Harpy | corpus inventory required | same, plus flying root/height | No complete fingerprinted matrix yet |
| Banshee | corpus inventory required | embedded texture/assembly, floating root | No complete fingerprinted matrix yet |
| Beholder | corpus inventory required | embedded parts/animation | No complete fingerprinted matrix yet |
| Shadow | corpus inventory required | transparency/material and hierarchy | No complete fingerprinted matrix yet |
| Spider | corpus inventory required | multi-limb hierarchy and idle | No complete fingerprinted matrix yet |
| Octopus | corpus inventory required | tentacle hierarchy/morphs | No complete fingerprinted matrix yet |

“Corpus inventory required” is a deliberate fail-closed finding. Until exact
profile archives and model/action names are fingerprinted, the supported rule is
to preserve embedded creature data and diagnose missing/incompatible members.

## Rejected interpretations

- A normalized full-image UV assumption: rejected by stored FIG domains, viewer
  alignment, and `.x2`/`.x4` controls.
- Global same-suffix armor scans: rejected by authored `l_shell/r_shell` parent
  links and negative same-suffix controls.
- Loot array order controls armor: rejected by lower/upper and Wear Order tests.
- Every armor region has replacement geometry: rejected by atlas-only lower
  equipment and TTI0 controls.
- Equipment implies TitanGrip: rejected by JMV StrongMan Lua ownership logic.
- Database may repair missing imported items: rejected by Import semantics.
- Renderer behavior alone proves engine truth: rejected as methodology; keep
  renderer, corpus, ei_figer, binary routine, and controlled game comparisons
  distinct in reports.
- Inventory thumbnail and equipped weapon share one FIG: rejected by the
  `initwedg8weapon.fig` thumbnail-only edit and the separate held
  `unhuma.mod/rh3.dagger08` runtime edit.
- Editing all eight complexion extremes propagates to every character or hand:
  rejected. FIG8 extremes cover interpolation within one FIG member only;
  family (`unhuma`/`unhufe`) and hand (`rh*`/`lh*`) remain separate assets.
- Mapping displayed `25/25/25` stats directly to synthetic FIG8 coordinates:
  rejected by the exact saved heroine appearance tuple and the resulting
  short/thick-leg visual control. Primary stats and stored appearance must be
  treated as separate inputs until their provider-owned creation mapping is
  verified.

## Equipped dagger hand-resolution control (2026-07-22)

- Original copied/live-before-test `unhuma.res`: SHA-256
  `E8430155D0AA96558B75BEDA8B66F7360C4BEAD7685D9D855FFBCEDCA88800CE`.
- Serrated candidate: SHA-256
  `44C3B90030D5DE6BEB86B0CEE6ECF9510FE729A8C825D86056B532D426CD9465`.
- Only outer `unhuma.mod` and nested `unhuma.mod/rh3.dagger08` changed among
  10,389 recursive members. The 5,360-byte FIG retained 36 vertices, 68
  triangles, UVs, and renderer-visible corner normals. Three X coordinates per
  extreme changed across all eight extremes (24 stored vertex scalars; maximum
  absolute delta `0.009000003337860107`).
- In the controlled male dual-wield inventory view, the right/main-hand bone
  dagger displayed the coarse serration and the left/off-hand weapon did not.
  This confirms the hand-specific member selection. No female runtime claim was
  made; a staged `unhufe` experiment was stopped before installation because it
  was unnecessary to resolve this control.

## `unhufe` hand-size and shoulder controls (2026-07-22)

- The neutral test heroine shown as `25/25/25` stores the exact appearance
  tuple `0.20, 0.30, 0.85`. Renderer corner order is base, Y, X, XY, followed
  by the four scaled/Z counterparts; its ordered weights are
  `[0.084, 0.036, 0.021, 0.009, 0.476, 0.204, 0.119, 0.051]`.
  Earlier synthetic-stat modeler scenes are negative controls because they
  made the legs too short and thick. The corrected v5 handoff uses the stored
  tuple, a provider-resolved idle presentation, and the split unlayered-head /
  composed-body atlas rule.

- The accepted JMV visual control keeps the first 20% of each base-hand member
  (`unhufe.mod/lh3` and `rh3`) at its authored wrist scale, blends smoothly,
  and reaches 115% transverse palm/fist scale at 55% of hand length. The same
  fixed-topology rule covers all eight FIG8 extremes and both hands.
- Candidate SHA-256:
  `2ED0D220404AC046CF722FF1C9A7B7300B2E1563DF72F09070FDA3BCE76C4D19`.
  Among 10,506 recursive archive members, only the containing `unhufe.mod` and
  base `lh2/rh2/lh3/rh3` differ from vanilla; the forearms use the separately
  validated natural profile. Fresh reopen and semantic checks passed for 32
  meshes. Manual JMV comparison judged the resulting palm/fist size suitable.
- This is an accepted authoring target for the investigated `unhufe` asset, not
  evidence that 115% is a general female, family, or complexion rule.
- Rejected shoulder hypothesis: lowering the outer/top `bd` torso silhouette
  made the unchanged proximal `lh1/rh1` upper-arm caps stand out more strongly
  in game. V6 SHA-256
  `F6ACD1F29D16D43B522CA30E654B4A2F49BEE56A2375B617C372B7BF4B24F27D`
  is retained only as a negative control. Future shoulder work must begin from
  the accepted hand build, restore `bd`, and isolate the proximal upper-arm cap.

## Round-trip plan

1. Generate a SHA-256/member/structure baseline with
   `scripts/character_asset_report.py`.
2. No-op import/export one FIG+LNK model and one ANM action into a fresh RES.
3. Re-open with ei_figer and report both archives. Require semantic equality;
   document unavoidable container-table/timestamp differences.
4. Move one vertex without topology change, one UV within the existing domain,
   one attachment transform, and one animation quaternion in separate fixtures.
5. For each fixture, require only the intended semantic delta, preserved unknown
   fields/member hashes, correct Final Character Texture/UV display, and matching
   visible/picking pose.
6. Run family controls with `drawall`, several idle frames, orbit/distance,
   save/load, unload/reload, and graceful exit in disposable Base and JMV copies.
7. Archive successful and rejected reports separately. Never promote a failed
   round trip by manually normalizing unexplained bytes.
