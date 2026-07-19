# Unchanged MPR save validation

Private runtime evidence collected 2026-07-20 from the user-supplied JMV files (read-only):

| Fixture | Size | SHA-256 | Observed timestamp |
|---|---:|---|---|
| active `zone1test.mpr` | 1,523,282 | `A1ED4AC4F39E4DA49BB29B3C80366DB747BE9F3A318726EAA2DB9D1C1B150AAE` | 2026-07-20 02:36:41 |
| active `zone1.mpr` | 1,523,282 | `A1ED4AC4F39E4DA49BB29B3C80366DB747BE9F3A318726EAA2DB9D1C1B150AAE` | 2026-07-20 02:36:29 |
| guaranteed backup `zone1.mpr` | 1,523,282 | `A1ED4AC4F39E4DA49BB29B3C80366DB747BE9F3A318726EAA2DB9D1C1B150AAE` | 2026-02-23 21:25:53 |

All three files are byte-identical. The previous preview build introduced no archive or terrain
changes, but its unchanged same-path save performed an atomic rewrite and refreshed the filesystem
timestamp. That violates the no-modification-without-an-effective-save rule.

The corrected contract is:

- changed MPR: save and save-as remain refused;
- unchanged MPR, same path, identical current bytes: return success without staging, backup,
  replacement, or timestamp mutation;
- unchanged MPR, same path, externally changed/unreadable current bytes: refuse overwrite;
- unchanged MPR, different path: retain staged validation, optional destination backup, and atomic
  byte-identical copy.

The synthetic test `unchangedExistingFileComparisonIsReadOnly` verifies that comparison neither
writes the file nor changes its timestamp. Private manual validation must still exercise both the
same-path no-op and different-path copy using disposable fixtures.
