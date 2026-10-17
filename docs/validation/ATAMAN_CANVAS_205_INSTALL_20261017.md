# Ataman 205 — installation receipt — 2026-10-17

**DONE / INSTALLED205** по поручению владельца. После сообщения «Редактор закрыт» повторён только guarded install; неизменные функциональные проверки и staging не повторялись. AGENTS.md SHA256 не изменился: `0C4F9C7EA51A36D54CDE243728A4B6CA577645BFB61C7A1DEEF5149B05521B36`.

- Source branch: `main`, полный SHA `98e779995f95bf8c96840ff2e6f3bbdd8fafd898`.
- Постоянная установка: `artifacts/EI-Mapper-Ataman-2-first-test`; VERSION205.
- Tested/raw/staged/installed EXE SHA256: `BA016CA67879B05C19B498A7127A316804E38D80EAF64508E2581A609B96EAC8`.
- Установленный manifest SHA256: `10E22CED9809CE8ABA10C3E69C100646A1CDACCEBF2B2798B5ECD97E3B1E8018`.
- 130 управляемых файлов проверены по manifest; заменено/удалено старых записей 7, новых 1, удалено 1. Перед заменой проверены отсутствие редактора, старые/новые SHA и отсутствие коллизий с пользовательскими файлами. Точная копия заменённых/удалённых файлов и старого manifest проверена до установки. Файлы заменялись атомарно, manifest последним.
- Все 17362 пользовательских файлов вне manifest сохранены побайтно, включая настройки, карты и sidecars. Совокупный digest до/после: `17362:300F3996E815FEC3E80F160D846976F3FBB8D00826348305E59C3DD291FFD018`. Размер живой gipat1 автоматически не менялся.
- Резерв204: `artifacts/rollback204-to205-canvas-20261017`. Реконструкция всех файлов прежнего manifest204 из этого резерва и неизменённых installed-файлов PASS. Только после этого удалён положительно опознанный старый резерв203→204; его все файлы перед удалением совпали с manifest203. Остальные исторические rollback-каталоги не затронуты.

Функциональное подтверждение: [155 native PASS / exit0](ATAMAN_CANVAS_205_20261017.md), настоящий hidden MainWindow/Qt/OpenGL; Apply/Cancel, точные MMP/SEC/семантика и unsaved paint, три MOB/все5logic/traps/grouping, race/rollback, Save As, отдельные recalc/Bake и стойкая отметка. Raw/staged --version205, finalizer/complete manifest PASS, 470 входов сверены с source commit. Пакет после финализации не запускался; редактор вместо владельца не открывался.

Receipts: `build-canvas-205-20261017/{build-receipt,final-probe-receipt,staging-receipt,install-receipt,rollback-retirement}.json`, `final.out`, `finalizer.log`, `install-run.log`. Данные и сборки не включены в Git. Ручная визуальная и игровая приёмка отдельно; GitHub не обновлялся.
