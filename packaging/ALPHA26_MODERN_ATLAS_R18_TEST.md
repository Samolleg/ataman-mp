# Modern atlas r18 tester changelist

This revision targets the remaining pause while opening expanded MPRs. It
retains r17's 4096-slot painting and painted-map staging fixes.

## Changed

- Game-wide material palette construction reads each encoded JMV MMP source
  page at most once per catalog rebuild. Material cards sharing that page then
  decode only their requested DXT tile.
- Missing source pages are negatively cached for the rebuild, so fallback
  candidates do not repeat the same failed archive lookup for many families.
- The load log now reports independent millisecond timings for renderer atlas
  and sectors, canonical MPR data, Inspect Atlas tile data, material catalog,
  and total editor opening time.

## Focused manual checks

1. Open the included 1024-, 2048-, and 4096-slot Zone18 collections once each.
   Compare the delay before the material palette becomes usable.
2. Paint several materials on the 4096-slot map. A fully allocated authored
   atlas must not refuse the stroke.
3. If opening remains slow, send the `MPR editor load phases` and
   `Game-wide material palette source pages` lines from `logs/workflow.log`.
   They identify the remaining owning phase without relying on wall-clock
   impressions.

## Runtime boundary

The shipped 32-bit game remains validated through 4096 slots and through the
included 2112-slot painted-extension case. The 8192 collection remains a known
allocation-pressure failure artifact, not a supported size.
