# JMV supplemental MOB loading

This record describes the locally fingerprinted JamaisVu135 corpus. It is not
a universal rule for Base EI or unrelated mods.

## Evidence

- `map.txt` associates a zone with its primary MPR and MOB. It does not provide
  a complete ordered AutoZone list.
- Primary MOB scripts contain exact, conditional `AddMob("filename.mob")`
  calls. Examples occur in zone1, zone11, zone12, zone18, and many other JMV
  maps.
- Supplemental MOB scripts can call further supplemental MOBs. For example,
  inspected AutoZone scripts contain nested `AddMob` calls.
- A real direct-launch trace opened the staged primary MOB, then
  `AutoRunSpellAddon123.mob`, then `Autozone1gate.mob`, matching the main MOB's
  script reference.
- Starter's addon exports `AddMobEx`, but no call to it was found in the
  inspected JMV MOB scripts. The observed content uses `AddMob`.

## Endurance policy

The first collection MOB matching the `map.txt` primary name is staged as the
normal temporary map MOB. Every other loaded collection MOB is validated and
staged under its original filename in the run-owned supplemental directory.

An explicitly listed collection MOB is part of the requested test composition,
so the run-owned autorun bootstrap calls `AddMob("filename.mob")` for it in
collection order. The runtime bridge redirects that request only when the exact
basename exists in the owned supplemental directory. The original main and
supplemental scripts remain byte-for-byte unchanged. Consequently:

- collection-listed supplemental MOBs load explicitly instead of depending on
  campaign quest state;
- nested `AddMob` calls work through the same filename lookup;
- an unlisted supplemental filename falls back to the installed JMV map;
- Endurance does not scan and force-load every `Auto*.mob`;
- hot reload keeps each MPR/MOB generation immutable and retains it until the
  game exits, avoiding replacement of files held open by the engine;
- duplicate loading remains a runtime validation target when an explicitly
  listed file is also requested later by conditional map script logic.

## Runtime validation still required

Test one immediate main-script load, one conditional load, and one nested load.
For each, place a unique disposable marker in the supplemental MOB, confirm it
appears exactly once, edit it in Endurance, invoke Ctrl+F5, and confirm the new
version appears exactly once. The bridge log must contain a corresponding
`SUPPLEMENTAL_MOB redirect` entry and the run manifest must inventory every
source and staged hash.
