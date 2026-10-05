# EI-POINTER-MODES-FIX-117 — 2026-10-06

Код main `4b727f0780e84e1e0a5ade186cf24309f65da5db` (источники проверенного EXE). База main `bc951867c44b54d5b2c567288b00bc0d316608af`; VERSION116→117. Продолжение подтверждённого аудита `ATAMAN_POINTER_MODES_AUDIT_116_20261005.md`. Владелец поручил исправления; игры/редактор интерактивно не запускались, игровые документы/ресурсы не изменялись.

## Изменения

- Кисть владеет нажатием/движением/отпусканием ЛКМ/ПКМ, включая промах и отсутствие доступного рельефа. При смене инструмента старый жест прерывается и не передаётся другому инструменту. ПКМ независимо сбрасывает held-подсказку тайла и завершает sampling.
- Select принимает только своё нажатие ЛКМ и сохраняет отдельный якорь рамки; потеря фокуса/Esc/смена операции отменяют его. TileBrush также требует собственного нажатия и не возобновляет отменённый штрих после потери фокуса.
- Кнопки Move/Rotate/Scale отключают все режимы ландшафта, включая Sculpt и OverlaySculpt. Внутренний Select для sculpt сохраняет кисть через явный preserve-флаг. Палитра не уничтожает живой TileBrush во время синхронного sampling callback.
- G/R проходят общий переход с проверкой hasEditableSelection. Выбор из неактивного/смешанного MOB не запускает пустой/частичный transform; русское сообщение в status bar объясняет необходимость сделать MOB активным. Автоматическое снятие блокировки/смена документа не выполняется. В исходном видео активен Autozone9camp.mob, выбранные оранжевые объекты принадлежат заблокированному zone9.mob.

## Автоматические проверки

Свежий shadow `build-release-117-pointer-modes` и финальный wrapper `build-tests-117-pointer-modes/build-final2.log`: PASS. Версия native/staged — EI Mapper Endurance Edition117. `--pointer-modes-check`:56 PASS/0 FAIL; реальные native CView/COperation/CMob и скрытый OpenGL viewport, синтетический MPR из репозиторного fixture. Проверены все5 кистей→Move, группа/Apply/Esc/Undo/Redo, locked G/R, промахы обоих sculpt, настоящий ground sculpt, положительный Select control, прерванные жесты/фокус, смена кистей и кисть→Move, настоящий текстурированный TilePaint+точные Undo/Redo, настоящее RMB sampling, MMB и Rotate/Scale→выход из sculpt.

Регресс: terrain-mob-height19, group-rotation26, model-connection42, assembly-catalog91, database-creation39 PASS; каждый native exit0. Общий `--ui-widget-check` exit1: те же10 baseline clipboard failures, `ui-regression-comparison.json` changed={} против116; новых нет. Это ограничение, общий UI не объявляется PASS.

Промежуточные отказы не скрыты: первоначальный synthetic paint fixture упал в CPreviewTile::updateTile из-за неинициализированного preview (fixture обходил штатный load); fixture дополнен тем же preview, который создаёт нормальная загрузка. Первый group-rotation прогон дал9 FAIL, поскольку fixture требовал старого обхода смешанной блокировки через R; теперь отдельно проверяет отказ mixed selection и вращение active selection, прямой backup по-прежнему проверяет исключение locked узла. Первый paint assertion получил2 history items: один был оставлен положительным Select control, второй был правильным штрихом; историю подготовки очищают перед независимой проверкой штриха. Два диагностических пересбора имели ошибку строкового литерала instrumentation; финальная сборка успешна. Эти результаты не являются PASS исходных промежуточных прогонов.

## Передача и сохранность

Fresh staging: `C:/GOG Games/maped/ei-mapper-ataman/build-staging-117-pointer-modes`,127 managed payloads+SHA256SUMS. Raw/staged EXE совпадают: `FD289CC14B0B64CA0DBB2BF9339668AC0DA2D30FA5C8271171F5D34E08EA781D`. Staged --version117 проверен до финализации; finalizer и независимый validate_distribution PASS. После finalizer пакет не запускался. Данные настройки/логов/кэша/карт/игровых архивов не включены. ZIP не запрошен для этой обычной тестовой сборки.

Чужой `translations/ei_mapper_ru.qm` восстановлен byte-identical SHA256 EDA31955A912758ACB5A7BDE043BBF2EB5F8A2606FA32DC52AFA4156CA74ED9F. Чужие ROADMAP/terrain atlas report и неотслеживаемые артефакты сохранены; в коммит входит только собственная часть ROADMAP.

Установка: ожидает закрытия EXE116 в постоянной папке `artifacts/EI-Mapper-Ataman-2-first-test`; проверенный пакет готов. Следующий шаг — закрытие, проверенный backup/managed-only update/все hashes и сохранность unlisted user files. Ручной runtime тест исходной карты ещё не проведён; родительские115/116 не закрыты.

## Ручная проверка

1. В «Загруженной карте» сделать zone9.mob активным, снова выбрать нужные объекты; голубой выбор должен переноситься кнопкой/G. Проверить ЛКМ, Esc, Ctrl+Z/Ctrl+Y. Для оранжевого/смешанного выбора G должен объяснить блокировку и ничего не менять.
2. С выбранной группой покрасить землю и изменить рельеф; выбор не меняется. Повторить промах кистью и переход инструментов при зажатой ЛКМ, затем обычную рамку с Shift.
3. Проверить ПКМ sampling, MMB камеру и возврат из кисти в Move/Rotate/Scale. Потеря фокуса и запоздавшее отпускание не должны начинать новый штрих.
