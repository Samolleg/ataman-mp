# EI Mapper Endurance 0.1.0-alpha.11 validation

Validation date: 2026-07-16

## Automated tests

- Endurance document and staged-save tests: 8 passed, 0 failed.
- Backup-manager tests: 6 passed, 0 failed.
- Resource-name tests: 8 passed, 0 failed.
- Duplicate-ID report tests: 4 passed, 0 failed.
- Complete Windows x64 Qt 5.12.12/MinGW release build succeeded.
- `--version` reports `EI Mapper Endurance Edition 0.1.0-alpha.11`.

The staged-save suite covers successful temporary staging/reopen without
creating the destination, malformed MOB root rejection, malformed MPR table
rejection, duplicate-ID warnings, and byte-identical unchanged serialization.

## Read-only JMV corpus checks

The alpha.11 binary returned exit code 0, loaded successfully, and reported a
byte-identical unchanged round trip for each input:

| Input | Size | SHA-256 |
|---|---:|---|
| `zone1.mob` | 1,586,436 | `DB5F3AB08E8D6EA920096887B17BE498A1E81A315AB50C85754922150C6A4D34` |
| `zone1.mpr` | 1,523,282 | `A1ED4AC4F39E4DA49BB29B3C80366DB747BE9F3A318726EAA2DB9D1C1B150AAE` |
| `zone10.mpr` | 544,623 | `56F0A601330D88912EFCC17EF58A2EDB5168640D58E965CC040FC30E9076B925` |
| `zone12dun2.mob` | 265,220 | `5D8BD4FF067CD321A9561B74E6BC73F8D794809901D3A9B650E8ED0833EB8F8C` |
| `zone18.mob` | 4,498,380 | `0B3E1115FEA2C892B597D969AC8AB60965F88D9083398AF09C0E84581F279A90` |

These checks confirmed both observed valid MOB top-level layouts: a root section
ending at EOF and a root followed by an AI graph section ending at EOF.

## Changed-save structural audit

The previously captured `Autozone4hadagans.mob` save was accepted by the new
alpha.11 validator and remained byte-identical during its headless round trip.
Its exact pre-save and post-save hashes are:

- Before: `B345B2DA177B8F3F9C673DE26F0A384943431EC14AF0C87F281C8625104B5428`
- After: `EA3DC55447E39E5EB4B68251E66AAA2DB3D8681EB922F36ABA199AE6B47D943D`

Fresh MobExplorer 1.1.0.0 parsing found 118 objects in both files. Exactly two
objects changed: chest NID `1000023` Hands `0` to `100`, and chest NID `1000025`
Hands `85` to `120`. All other 116 objects and top-level sections were
byte-identical. A complete raw diff found exactly the corresponding two changed
bytes. MobExplorer SHA-256:
`253CDF9AAEE74640A219283A210C2A026A644161CE06F4DF25A297E9CAD4ECDD`.

## Remaining safety boundary

Alpha.11 is approved for collaborative testing on disposable copies. It is not
yet approved for editing the only copy of a production map. Fresh staged
validation checks exact staged bytes and known MOB/MPR container structure; it
does not prove lossless preservation of every unknown nested record across the
entire private corpus.
