# Alpha.26 grouped multithreaded character preparation r4

## Changed

- Character appearance scheduling now groups every unresolved unit by its exact
  preparation identity before choosing work. Up to 16 nearest unique
  appearances are selected, and every matching instance across the map joins
  its group instead of sharing only with neighbors in one 16-unit slice.
- Unique appearance groups resolve concurrently on a private worker pool capped
  at four threads. Equivalent units receive one immutable result while retaining
  their own generation, revision, animation, transform, selection, and picking
  state.
- Prepared-unit OpenGL finalization and submission of the next appearance batch
  can now overlap. OpenGL uploads and mutable unit/render state remain on the
  viewport's owning thread.
- `appearanceCacheHits`, `sharedPreparations`, and `providerExecutions` are now
  mutually exclusive counters in the character preparation log.

## Manual checks

1. Configure the Jamais Vu profile and cold-load a zone with repeated character
   prototypes, then reload it in the same editor session.
2. Confirm repeated units retain correct bodies, complexion, armor, weapons,
   animation, transforms, and independent selection behavior.
3. In the message log, confirm grouped submissions report more than 16 instances
   when a selected appearance has many copies and `workers` reports up to 4.
4. At `Full character visual preparation complete`, record `total`,
   `appearanceCacheHits`, `sharedPreparations`, `providerExecutions`, and the
   assembly cache totals for comparison with r3.
5. Inspect a special character with a distinct embedded assembly and confirm it
   was not merged with an ordinary unit sharing only its prototype name.

## Known limitation

- CPU-safe appearance resolution is multithreaded, but OpenGL upload and mutable
  figure/unit attachment remain on the viewport thread. Further parallelism
  requires separating immutable CPU assembly construction from the current
  OpenGL/state-commit tail.
