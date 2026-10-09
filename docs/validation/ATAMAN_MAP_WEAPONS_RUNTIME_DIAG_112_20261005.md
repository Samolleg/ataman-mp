# Weapon runtime discrepancies after112 — read-only audit2026-10-05

Base main f042d122bbd7ca50fdce6a8317f2f1018bf6d431, installed112. Owner screenshots demonstrate missing texture and a placement discrepancy. The earlier editor-only validation does not establish runtime compatibility. No code, installation, resource or map changes in this audit.

zone1.mob SHA256 d0bd5a312991d7c7d6f026e2c373e593228f2a581d8982b77b67cebf1d9a09b3. Record NID68 at offset613649, type60 (lever), model uhwdg03r. Position(67.4635925293,139.2627410889,0.8400000334). Rotation WXYZ(0.9142541289,1.7881395e-7,-5.3644186e-7,-0.4051412046), squared norm1.0000000079; approximately Z−47.8deg. Constitution(1,1,5). Source parser reads and writes WXYZ consistently; this does not prove runtime use.

## Texture evidence

Both slots store unhumadg_03.br.1.x2. Its member in UnhumaDG.res is512×512 PNT3, SHA25647599d3eeba78b7edccd52e8cfb0351dd68522a56361464a2c2885c7b6dfb9c8. Config registers UnhumaDG.res under redress.res. Mapper searches configured archive sources together and decodes PNT3 itself. A direct map object's game binding cannot be inferred from successful preview. Whether the first rejection is resource routing, binding or surface creation remains untraced. Existing project r5 investigation records the distinction between equipment composition and ordinary surfaces; no private engine sources were accessed in this audit (required private skills unavailable).

The existing unhumadg_03.br.1.x2map.mmp is512×512 DXT1/10 levels, SHA256b4e0a62c49ee5405b363d0d031a3160caee9280a51478eb2b56f59dc3a27e668. In-memory decode at payload offset76 using repository PNT3 run semantics and Pillow BC1 shows differing current atlas layout: source34951 opaque pixels, same-coordinate map opacity overlap17.27%, RGB mean absolute difference98.39. Thus suffix substitution alone is not established for the new UVs. No decoded asset was written or converted on disk.

## Placement evidence and hypothesis

Read-only nested RES/FIG8/BON decode of uhwdg03r:
- At constitution(1,1,5), FIG vertex bounds before BON: min(0.11570715,−0.27058202,0.00901370), max(0.25555742,0.44415647,0.07033993).
- Interpolated root BON approximately(−0.18563228,−0.08678723,−0.00901370). It cancels the XY bounding-box center and sets the bottom Z to0.
- CFigure::calculateConstitution adds this root offset to vertices. CObjectBase applies the stored quaternion, then draw position. Lever height uses terrain height plus stored MOB Z.
- If the game omits this root compensation, it produces an approximately0.205m XY displacement; that is a concrete hypothesis matching the direction of the reported type of error, not measured runtime proof. Different cameras also preclude exact pixel comparison.

Next step: trace/experimentally isolate game root BON treatment for this lever and texture acquisition/composition. Do not compensate by editing user map coordinates before establishing the divergent transform. Keep parent112 runtime acceptance open.

## Authorized diagnostic installation2026-10-05

Owner explicitly approved installing the synthetic root BON probe. Installed only Dejavu/res/ataman-rootbon-probe.res (SHA25631f229c002108da08a7e5237695041f9c25e5d38e7bdf9e1d5024e431615e518) and one figures.res entry in config.ini. Config original bytes were verified unchanged after removing the inserted line. Config SHA256 before86f96ec40a2857e4d5b743140160c2a56a5d29883193f44ff8daad9aa201af19, after7964f7dceb0c11c06385a988e29b5465008512aca35a219389a7c9efd7b4bb46. Verified backup: build-tests-rootbon-runtime-112/rollback-20261005-101039/config.ini. Machine-readable evidence: build-tests-rootbon-runtime-112/installation.json. Game and mapper were not running; neither was launched. Original resources, maps and application binaries remain unchanged.

Compare atbon_zero, atbon_root and atbon_baked sequentially at identical position, zero rotation and unit constitution, first as a lever. All eight forms are identical. Root matching zero supports omission of root BON for this tested path; matching baked rejects that hypothesis. Native archive structure checks passed, but runtime loading/rendering and the original dagger discrepancy remain unverified. This installation is a diagnostic step, not a fix or parent-task completion.

## Owner-authorized placement of three levers

Owner requested automatic placement rather than manual object creation. Added ID900001/900002/900003 to zone1.mob at X67.4635925, Y134.7627411/136.2627411/137.7627411, Z1. Models zero/root/baked respectively; identity quaternion and unit constitution. The three separated rows share the same authored X: compare middle-row X alignment with either outer row. Lever interactions disabled, no script/quest/parent links, no door/graph recalculation; default0 texture instead of equipment PNT3. Original dagger unchanged.

Scanned151 MOBs for authored IDs; all three IDs free, within existing zone1 range0..999999. Preserved all1699 original records and all original bytes except two container lengths. Reversing insertion plus lengths reproduces source byte-exactly, including appended AI container. An initial local assertion caught incorrect use of total file size for root container length, before installation; corrected and final validation passed. Installed SHA256 c295b5c4383ddd7a207c9b33606952b08c65edf6dbc49cf9f1403978327e9c27. Backup build-tests-rootbon-runtime-112/map-rollback-20261005-101944/zone1.mob; machine-readable evidence build-tests-rootbon-runtime-112/placement.json. Installation rechecked all map hashes and absence of running game/mapper. No application source/version changes. Runtime test remains pending; load freshly from map, not an old saved world state.
