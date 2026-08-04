# Alpha.26 shared character visual preparation r3

## Changed

- Equivalent character instances in one preparation batch now resolve their
  database-backed appearance once and share that immutable result. Each unit
  still keeps its own animation, transform, selection, and picking state.
- Existing content-keyed sharing remains responsible for the composed body
  atlas, GPU texture upload, decoded source layers, and immutable geometry.
- Character preparation logs now report `sharedPreparations` separately from
  persistent/in-memory `cacheHits`, making repeated-character behavior visible
  during normal map loading.

## Manual checks

1. Configure the Jamais Vu profile and open a zone containing many instances of
   the same character prototype.
2. Confirm all instances retain the correct body, armor, weapons, animation,
   transforms, and independent selection behavior.
3. Open the message log and find `Full character visual preparation complete`.
   Confirm `sharedPreparations` is non-zero when equivalent units occurred in
   the same 16-unit preparation batch. Later batches may instead report
   `appearanceCacheHits` because the prepared result is already memory-cached.
4. Change or inspect a special character with a distinct embedded assembly and
   confirm it is not replaced by another prototype's assembly.

## Known limitation

- Coalescing is deliberately exact and batch-local. JMV database resolution can
  legitimately use embedded MOB assembly fields for special characters, so the
  editor does not merge requests merely because their database prototype names
  match when their authored assembly inputs differ.
