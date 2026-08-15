# Integrated Database Editor and Resource Browser

The Database Editor is a profile-aware authoring tool backed by the active
provider snapshot. Base and Jamais Vu retain separate provider identity, source
archive selection, hashes, and documents. The Resource Browser shares that
provider model but does not interpret the protected relational `database.res`
as an ordinary mutable archive.

| Area | Current coverage |
|---|---|
| Tables | All 26 physical database tables and every schema-described field |
| Browse | Lazy typed grid, table counts, sorting/filtering, column visibility/order/width/freeze persistence, literal `#` column frozen by default, and soft selected-row highlight |
| Edit | Typed scalar, list, bit-field, fixed-string, nested-record, and descriptor-unknown hexadecimal values; clone/delete; item/spell constructor |
| History | Per-document undo/redo and pending-change review manifest |
| Validation | Cross-table identity/reference checks, stable-name collisions, established monster-to-race links, and publication preflight |
| Interchange | All-table XLSX export/import using EIDB-compatible sheet and field identities |
| Preservation | Immutable source archive plus exact physical-record provenance and untouched member/field bytes |
| Publication | Reviewed Save As and Apply Database transactions that stage, reopen, validate, back up, and atomically publish |
| Resource integration | Protected `database.res` routes to this editor; established database resource fields can open the exact winning member in the docked Resource Browser |

Database-to-resource navigation publishes the destination immediately and
lets preview/decode work finish asynchronously. It activates the existing dock
tab and raises the correct host window without changing dock ownership or
burying the source Database Editor behind another top-level window.

Wide-table interaction is deliberately bounded. Frozen columns are painted as
a lightweight overlay rather than a second synchronized table, and selection,
context-menu, row highlighting, and horizontal scrolling avoid work
proportional to the full table. Focused offscreen checks on the real JMV Race
Models table reduced the measured r7-to-r8 paths as follows:

| Interaction | r7 | r8 |
|---|---:|---:|
| Actual click handling | — | 1 ms |
| Selection path | 134 ms | 82 ms |
| Context-menu path | 83 ms | 39 ms |
| Worst cold row | 81 ms | 36 ms |
| Four full horizontal traversals | 284 ms | 135 ms |

These figures are automated offscreen Qt measurements, not promises for every
machine. The corresponding r8 tester build was also confirmed interactively by
the user.

Descriptor-unknown fields remain visible as hexadecimal payloads and serialize
without touching unrelated bytes; their game meaning is not claimed. Resource
rename/delete operations likewise retain explicit reference-coverage warnings
for opaque or dynamically constructed references.
