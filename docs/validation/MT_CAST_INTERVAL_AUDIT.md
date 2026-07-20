# `MT_CAST_INTERVAL` (`0xBBAB0005`) corpus audit

## Result

`0xBBAB0005` is conclusively a four-byte unsigned interval value, not the Byte boolean described by
the English and Russian Object Editor manuals.

The terminology is not contradictory: the manuals' "Reloading" label and EILib/Endurance's
`MT_CAST_INTERVAL` both describe the trap's reload/cast interval. The manuals are wrong only in
their Byte type and `0/1` explanation.

## Evidence

The read-only scan covered every `.mob` immediately under the installed Base and JMV map folders,
plus the three supplied `zone13 (6)` legacy samples:

- Base: 13 occurrences in 6 files.
- JMV: 31 occurrences in 7 files.
- Supplied legacy paths: 3 occurrences; all three files are byte-identical.
- Total: 47 occurrences in 16 paths.

For all 47 `0xBBAB0005` occurrences:

- declared section length: exactly 12 bytes;
- payload length: exactly 4 bytes;
- observed little-endian DWord values: `15, 25, 30, 43, 44, 50, 74, 75, 90, 100, 104, 105, 150`;
- observed Boolean values: none.

For all 47 adjacent `0xBBAC0005` occurrences:

- declared section length: exactly 9 bytes;
- payload length: exactly 1 byte;
- observed values: only `0` and `1`.

MobExplorer commit `ca7cd10e6fb71a013e109370f28e2bccdaef32b3` independently maps
`ID_MT_CAST_INTERVAL` to `SectionType.ST_DWORD` and `ID_LEVER_CAST_ONCE` to
`SectionType.ST_BYTE`. Endurance's installed EILib-derived schema and typed projection agree.

Representative hashed inputs:

| Profile | File | SHA-256 | Values |
|---|---|---|---|
| Base | `zone11.mob` | `C8BB948A464BEE27A58FEDA99DB5704653CB6ACBF4A7B2A8BBC3ED12F82FF3B9` | `15` |
| Base | `zone2.mob` | `B6AD605534DA2E0BD2418F2EBF3965D42CE0EAF00EFBB476A8A5A6ABB7D674B5` | `50, 150` |
| Base | `zone13.mob` | `654DE0CEB0617B645ABBE78283F1F7D276D79A567AE26282B97B36717967FAF7` | `30` |
| JMV | `zone2.mob` | `D44F6AC99B3E1B6B9F3DF587AF3DB7B4D7F6071EA92571061B49C28C7DCF131B` | `43, 44, 50, 74, 75, 104, 105` |
| JMV | `zone13.mob` | `78FE8AF66CFCD00CEA5B5418C2B1ABB7F8B17DFCC973CD5A8BD90EEA8AE3559A` | `90` |
| Legacy | `zone13 (6).mob` | `3FCBE733F01C2510CB62741E5622CC66DBCE76C37B4D2FAD3B766C7FDD4331AD` | `150` |

## Runtime-unit confirmation

The Base executable with SHA-256
`B871D70D3876B2A5634F0920C92118BC49058B50C7D8CBE68376349F51C2E570` confirms the unit and
behavior:

- the MOB loader reads `0xBBAB0005` through its DWord reader and stores it in the magic-trap source
  object;
- runtime initialization copies that DWord unchanged into the trap's interval field;
- after a cast, the engine copies the interval into a separate countdown field;
- the trap's logic-update method decrements that countdown by exactly one and casts again when it
  reaches zero;
- EI logic time is 15 updates per second.

`0xBBAB0005` is therefore an unsigned interval measured in 15 Hz game ticks: `15` is one second and
`150` is ten seconds. Endurance should preserve the raw DWord and may display seconds as
`ticks / 15`. Non-multiples such as `43`, `44`, `74`, and `104` are valid fractional-second
intervals and must not be rounded during editing or serialization.

The nearby `0xBBAC0005` value is loaded separately as a one-byte Boolean and controls CastOnce; it
does not encode the reload interval.
