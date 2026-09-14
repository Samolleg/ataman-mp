# EI-NPC-IDLE-015 — preferred editor idle

User requests `cidle15` for NPCs on the map. This changes transient editor
animation, including existing actors; MOB/script data and game behavior stay
unchanged. The shared unit animation resolver also serves character previews.

Read-only installed Base/JMV ADB inspection confirmed a 115-frame IDLE cycle,
stage 2, allowedState 0. JMV's previous state-2-only selection discarded it;
Base's weapon mask selection also rejected the unarmed fixture. The new test
failed before implementation at Base selection. The previous deterministic
fallback remains for models without a valid named cycle.

The explicit editor preference is applied before the game's stance/weapon
choice. It accepts only nonempty IDLE cycles and retains Base's rejection of
extended JMV weapon masks. Duplicate named actions use probability then action
number for deterministic selection. This is an editor display preference, not
a claim that the engine would select this action for each equipment set.

Validation: 39 animation tests pass, including the new real-shape state-0
fixture, invalid-cycle fallback, old creature/two-hand/Titan Grip fallback,
provider isolation, animation parsing, pose interpolation and cache behavior.
Release built with the retained Qt 5.12.12/MinGW 7.3 wrapper.

The targeted database UI check now loads normal and JMV overlay action tables,
then verifies that the actual provider selects `cidle15` and the installed
model animation archive contains a parseable clip. Maps created by the check
are synthetic; installed assets are read-only.

Actual Desktop OpenGL checks passed for Jun Male Hero, Jun Female Deva M1 and
Base Human Gipath Brigand Boots F1 (provider choice and parsed model clip),
plus creation/save/reopen/history regression for those actors, AncientCurseM25
and ArmoredTurtle110. Both R2 package finalizers pass; all 92 delivered files
match staging. Executable SHA-256:
`7745e10678d128ae8cea52b483a3229a533c9fe7b09a6b0485ab5dd3c69dc49f`.
Previous packages are preserved and the delivered GUI was not launched.

Remaining manual acceptance: view the resulting motion and equipment pose on
the user's map. Models without this cycle intentionally retain prior fallback;
the inherited full UI-widget diagnostic remains unpassed. No other material
implementation uncertainty was identified.
