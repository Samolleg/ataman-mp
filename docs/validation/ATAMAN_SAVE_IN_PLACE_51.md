# EI-MPR-SAVE-UX — Ataman51, 2026-09-22

Запрос «исправляй все эти проблемы»: новая карта уже зарегистрирована в мире,
обычное сохранение скрыто за черновиком, повторная кнопка публикации,
затем отказ same-path. Ветка codex/mpr-save-in-place-v51 от
28c750069b12f2e417000c96bb04148f05fcfb00. Игровые карты не редактировались.

## Исправление

Save/Save As публикуют working draft; явный экспорт черновика остаётся отдельным.
Меню имеет постоянные названия Save MPR / Save MPR As. Ctrl+S без активного MOB
вызывает saveLand. Снятие same-path guard сопровождается сохранением трёх файлов
через commitMapSaveBatch: игровой RES, существующий adjacent RES и MPR.
До замены проверяются архивы, MPR, профиль, semantic field и recovery;
все исходники проверяются по байтам, создаются backups и recovery journal.
В соседний RES добавляются те же финальные страницы/профиль, посторонние члены
сохраняются. При повторном открытии finalized profile предпочитает указанный
runtime archive только после проверки совпадения физического atlas hash.
Пути MPR и world-map-layouts.json не меняются. После публикации старое состояние
активного атласа удаляется из retained world session. Другие изменённые карты
сначала сохраняются существующим общим сохранением.

## Проверки

- Fresh Release51 + runtime build PASS. Первоначальная компиляция выявила
  ошибочно вставленный дублирующий блок в другом save path; удалён, повтор PASS.
- Native --ui-widget-check после финальной компиляции: exit0 PASS,
  включая GL-regression50 и существующие UI-checks.
- endurance_document_tests: modernSaveRoutesThroughPublicationExactlyOnce,
  finalizedProfileBindsPostDxtPhysicalPagesAndPadding,
  workingAtlasProfileBindsExtensionSignaturesForDraftReopen,
  modernDraftAdoptionStagesPairBeforeLiveMutation: 4 теста PASS (6 с init/cleanup).
- res_archive_document_tests: draftPublicationKeepsPathsAndRollsBackEveryStage,
  travelSaveBatchRollback: 2 теста PASS (4 с init/cleanup).
  Новый тест использует реальные synthetic RES, проверки всех трёх точек отказа,
  rollback, backup, прежние пути, unchanged world JSON и чужие RES members.
  MPR payload в этом тесте условный; корректность MPR/atlas проверяется отдельными
  document-тестами. Это не end-to-end запуск пользовательского gipat1.
- Первый запуск RES-tests в sandbox/offscreen завис до результатов, остановлен
  через сессию; native повтор с Qt plugins завершился за270ms. Не считается PASS.

Ручная проверка: открыть созданную карту, сохранить на месте, закрыть/открыть,
изменить воду, снова сохранить, проверить положение в мире и игру. Не выполнена
автоматически. Исходная приёмка crash50 также остаётся открытой.

Упаковка/установка: ожидаются.
