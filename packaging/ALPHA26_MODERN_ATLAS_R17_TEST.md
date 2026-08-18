# Modern atlas r17 tester changelist

This revision incorporates the 4096 paint-refusal and 2048 post-paint staging
reports, and records the first honest expanded-atlas runtime endpoint.

## Changed

- A fully occupied 4096-slot authored atlas retains extension capacity through
  the packed MPR limit; painting no longer inherits the authored slot count as
  its working-cache ceiling.
- Test Current Map packages every authored page plus generated extension pages,
  updates the staged MPR page/slot metadata, and freshly reopens the pair before
  launch.
- First palette population extracts only the requested DXT blocks for each
  uniform brush tile. It no longer decodes a complete 1024/2048-pixel page just
  to crop one thumbnail.
- The included real Zone18 painted-extension probe starts from 2048 authored
  slots, generates one exact missing blend, references slot 2048 in terrain,
  and stages 33 pages / 2112 physical slots.
- Runtime stress classification now records combined-atlas dimensions and
  recognizes allocation/address-space exhaustion even when the game reports
  only the generic native runtime dialog.

## Focused manual checks

1. Open the 4096-slot Zone18 collection and paint several normal materials over
   existing boundaries. No stroke may be refused because all original slots are
   allocated.
2. Open the 2048-slot Zone18 collection, add several blends, and use **Test
   Current Map**. The game must load the painted terrain with textures.
3. Compare first open of the 1024/2048/4096 collections. Palette creation should
   no longer pause while decoding dozens of complete global JMV atlas pages.

## Validated runtime boundary

- 1024, 2048, 2048 plus one painted extension page, and 4096 pass.
- 8192 crashes from the terrain-atlas allocation itself, including with
  `drawall` disabled. It is included only as a failure/stress artifact and is
  not claimed as supported. 16384 remains unvalidated beyond that first proven
  failure.
