# Save crash audit — Ataman 33, 2026-09-19

Read-only diagnostic request. No program or game resource writes.
Installed EXE hash matches v33: abab2dfe5ad9d7efd30f1cbf2badfec55a4e507c4fd32f122a0c60cc740d7557.
Source 2aaf382ef83aaafc228d26a5b5793571789986a8.

Evidence in artifacts/EI-Mapper-Ataman-2-first-test/logs:
- workflow.log: backup 17:34:50; neighbor render backgrounds released,
  texts.res indexed and sky resolved 17:34:53; then access violation.
- crash-20260919-173453-p21568-t14516.dmp: c0000005 at
  Qt5Gui.dll+0x3a2729, read 0x2a0. PE export mapping identifies
  QOpenGLTexture::bind(unsigned int, TextureUnitReset)+0x89.
- Windows Application events 1000/1001 confirm same binary/module/offset.
- Exception-context stack scan finds application return candidate EXE+0x2a1440;
  disassembly shows indirect call preceding it. This is a scan, not an unwound
  symbolic stack; exact CView caller and ownership fault are not established.

Save inspection, independent read-only RES bounds/member comparison:
Live C:/GOG Games/Evil Islands/Starter/MODS/JamaisVu135/res/texts.res
size 1322176, modified 17:34:50, SHA256
c968812eb750b26af88622676de43f3aa1775fc6bd5e1528e14bf4fcdb2f939b.
Backup root C:/Users/samol/AppData/Local/EI Mapper Endurance/backups/
f904d820be802932fbffb17cc92c567dba1b22fc0ab15dc2e76cb7a798f54562/
20260919-123450-324-pre-save-texts.res-f1eda3f0b06e085d.backup.
Backup size1322004, original hash f1eda3f0b06e085d767e1135a04b2523431462a4f820c24d2fc1a70bfd90c99a.
6512 members before/after; only map.txt payload changed. Added exit6 with none0,
remove and deploy before zone cz1g. No pending map-save-recovery journal found.
Thus this archive write completed; gameplay validity and untracked unsaved actions
cannot be established from this comparison alone. No automatic restore performed.

Next: reproduce save/scene refresh with neighbors, trace texture ownership and
current GL context. Do not blame Qt or GPU driver solely on fault module.
Temporary analysis helpers: build-release-33-draw/analyze-dump.py, inspect-save.py.
