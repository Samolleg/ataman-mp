# Modern atlas r16 tester changelist

This revision targets the two failures observed in r15 on the expanded Zone18
collections.

## Changed

- A 4096-slot authored atlas now has working extension capacity through the
  packed MPR limit instead of refusing the first new exact transition because
  every authored slot is allocated.
- Test Current Map automatically snapshots the expanded working atlas after
  painting. It stages matching MPR metadata and a run-local RES containing all
  referenced pages.
- Authored JMV MMP pages are copied byte-for-byte into that run-local resource;
  only newly added extension pages are encoded.
- Reopening another expanded map in the same editor session reuses decoded
  game-wide material thumbnails instead of repeating the catalog decode pass.
- Automated atlas-limit runs now require authoritative controlled-player,
  page, and slot evidence. A new or changed native dump, crash status, timeout,
  or malformed memory report cannot be counted as a pass.

## Focused manual checks

1. Open the 4096-slot Zone18 collection, select a normal uniform material, and
   paint several boundaries that require new exact blends. No stroke should be
   refused merely because the original 4096 slots are allocated.
2. Open the 2048-slot Zone18 collection, paint until the active slot/page count
   increases, then use Test Current Map. The staged terrain must retain its
   textures and the game must reach the controlled player without crashing.
3. Switch between the 1024, 2048, and 4096 collections in one editor session.
   The first global palette population may still decode JMV source pages; later
   map opens should reuse those material thumbnails.

## Known boundary

The 16,384-slot packed MPR ceiling remains the highest representable physical
slot. Runtime support above the last visually confirmed size is still under
controlled stress validation and is not claimed by this build.
