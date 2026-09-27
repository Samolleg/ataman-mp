# Ataman 75 — портативная установка PZ

Код версии 75: `bb55e62ba9e4a2f9f0996e93b052c68e97944574`. Ветка `codex/portable-pz-profile-v75`, база `b8991f554dc00b30586a202f7f1a9fcfc3f4d2cf`. Запрос: настроить маппер для автономного комплекта `C:/GOG Games/PZ`. Изменяются маппер и его настройки. Приватные исходники игры не использовались; игровые ресурсы читаются, но не исправляются.

Исходный отказ воспроизведён в build-tests-75-profile/baseline.txt: нет game.exe в корне. Встроенные JMV/DejaVu теперь распознают Engine/game.exe, Base/Res и MODS; Base-only не присваивает себе модифицированный движок. Приоритет mod/res перед Base/Res сохранён. Launcher и bridge передают отдельные корни данных и стартера процессу игры, не меняя реестр. Проверка активного Starter AddonPath и путей реестра остаётся обязательной: для Test Current Map нужно открыть портативный стартер с выбранным модом. Проверку машинного кода не ослабляли.

- Профили: 11 PASS, 0 FAIL, 0 SKIP. Проверены старые Base/JMV, портативные DejaVu/JMV, отсутствие нужного мода и движка, недоверенный EXE и настоящий PZ. PZ: 78 RES, 41 MPR в каталоге мода; deja-vu, движок SHA-256 BE0FC330035E03983CCE590038CC7F881F1F5D1BCF082717F31776DD4DEE94E2 соответствует реестру совместимости.
- Launcher: native 56 PASS, 0 FAIL, 3 SKIP (внешние autorun, zone6 graph и игровой smoke). Sandbox: 45 PASS, 11 FAIL создания временных каталогов Qt, 3 SKIP; не засчитан. Дополнена проверка отказа смешанных корней для портативного Base.
- Release75 и обе x86 runtime-компоненты собраны. EXE SHA-256 92ACC794F04C5220F117E04250DEF97B44F83A6FA233D4FEE4BE3FDECA16BE65; staged --version: Edition75, --profile-load PZ: exit0, valid=true, runtimeStackValidated=true.
- Native --ui-widget-check: exit0, PASS. Offscreen UI и загрузка не завершились, остановлены как таймауты, не засчитаны.
- Native --workspace-load-smoke PZ/MODS/Dejavu/Maps/zone1.mpr и zone1.mob: exit0, два документа, 1693 объекта, штатное закрытие; удаление view 319 мс. Это автоматическая загрузка, не ручная приёмка внешнего вида. Missing texture: vulkan — причина не установлена, ресурс не менялся.
- Результаты и журнал: build-tests-75-profile и build-tests-75-launcher. Предсуществующий изменённый translations/ei_mapper_ru.qm восстановлен после wrapper-сборки и совпал по SHA-256.

Чистый build-staging-75-portable создан заново из проверенного v74 с заменой EXE, runtime, VERSION, Readme и текущего CHANGELOG. Финализатор и независимый валидатор PASS; после финализации комплект не запускался.

Постоянная установка `C:/GOG Games/maped/ei-mapper-ataman/artifacts/EI-Mapper-Ataman-2-first-test` обновлена до75 после проверки отсутствия процессов из неё: 127 управляемых файлов проверены до и после, заменены6 и манифест. Резервная копия проверена по SHA-256: `artifacts/rollback-EI-Mapper-Ataman-v74-to-75-20260927`. Настройки переключены на PZ/deja-vu: изменены только gameRoot, lastVisitedFolder, resourceProfile, resourceRoots, mapRoots; остальные поля JSON проверены на сохранность. Игровые карты и архивы не изменялись. Реальный запуск игры из маппера остаётся открытым; прежние задачи не закрываются.
