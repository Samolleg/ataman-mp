# EI-FINALIZE-137 / HDPack / semantic apply — 2026-10-10

**STOP на шаге 1 до компиляции. Передача137 и шаги2–3 не выполнены.**

## Поручение и база

Владелец через Claude2026-10-10: «да, играл [zone10/bz7g в JamaisVu135 со
смешанными страницами], всё нормально — делай по порядку». Это свидетельство
игровой приёмки указанного комплекта владельцем, не новая игровая проверка здесь.
Разрешены установка136→137 и последовательные guarded replacements только
Dejavu HDPack_Terrain.res, zone2.mpr, zone3obr.mpr. Base — legacy, без изменений.
Порядок и полный объём сохраняются; при провале любой проверки шага — откат
этого шага, проверка отката, STOP. Старые rollback сохранять.

Вход: main, `b586e970b0d02655f31e346f22ba9785c2c1d8d4`, VERSION137,
рабочее дерево чистое. AGENTS.md SHA256
`f4b7fdd5ea82fcdf43d002d23d64ec36be853650f054edf70376527cd6432ec3`
совпадает с отчётом137. Прочитаны CONTRIBUTING.md, TEST_BUILD_HANDOFF.md,
PROJECT_CONTRACT.md и три назначенных отчёта. Приватные modding/engine skills
недоступны; приватные исходники не исследовались. Сеть, зависимости,
подагенты, новые чаты, push и интерактивные открытия не использовались.
Системный буфер не открывался и не блокировался.

## Шаг1: точный сбой и остановка

Создан `build-tests-137-final/source.zip` командой `git archive` указанного
полного SHA; распакован в `build-tests-137-final/source/`.
Затем вызван штатный `source/scripts/build_release.cmd` с целевым каталогом
`C:/GOG Games/maped/ei-mapper-ataman/build-release-137-final`, Jobs6.

**Ошибка агента:** параметры QtBin/MinGwBin указывали на несуществующий
`ei-mapper-ataman/.tooling/Qt/...`, хотя предыдущие receipts указывают
существующий комплект в `ei-mapper-endurance/.tooling/Qt/...`.
Resolve-Path завершился ошибкой:

```text
Cannot find path 'C:\GOG Games\maped\ei-mapper-ataman\.tooling\Qt\5.12.12\mingw73_64\bin'
```

Внешняя PowerShell-команда: exit1, NativeCommandError; компиляция не началась,
`build-release-137-final` не создан. Перенаправленный `build.log` пуст:
сообщение осталось в stderr внешней команды, а не в полном build receipt.
Факт сбоя дополнительно сохранён в ignored `failure-receipt.json`.
Правильные qmake.exe и mingw32-make.exe проверены на наличие read-only,
но повторная сборка **не запускалась**: применён явный STOP владельца.

| Обязательная стадия | Фактический результат этого запуска |
|---|---|
| Свежая Release137 | FAIL до компиляции |
| --version137 свежего EXE | Не выполнено |
| UI169/scenario/dark/gallery, sentinel nativeAttempts=0 | Не выполнено |
| Terrain/mixed-resolution/random/mixed-edge/reader-drain | Не выполнено |
| World45/1 | Исторический известный FAIL; здесь не запускался |
| Миниатюры против установленной136, ≤10% или ≤+15ms | Не измерены |
| Новый staging, состав против136, finalizer, manifest | Не выполнено |
| Установка136→137, rollback136, сохранность user/cache | Замены0; установка остаётся136 |
| Live HDPack v2, все41 Dejavu открытия и render256 | Не выполнено |
| Свежие semantic copies с v2, reopen/resave/live apply | Не выполнено |

Успехи предыдущего отчёта137 не объявлены свежими PASS этой передачи.
UI/opening-процессы не запускались, ui-tests.lock не захватывался.

## Фактические пути и сохранность

Прочитан существующий `artifacts/EI-Mapper-Ataman-2-first-test/endurance-options.json`:
gameRoot=`C:/GOG Games/PZ`, mapRoots включают `MODS/DejaVu/Maps` и `Base/Maps`,
resourceRoots — `MODS/DejaVu/res` и `Base/Res`. Таким образом разрешённые карты
находятся в Dejavu/Maps, а не Base. Настройки не переписывались.

Таблица: ожидаемый исходный SHA из назначенных отчётов и текущий readback SHA
после неуспешной команды. Отдельного измерения live SHA до команды сборки
не было; замен и записи live данных в этом запуске не было.

| Файл | Ожидаемый до / фактический после, SHA256 | Замены |
|---|---|---:|
| `C:/GOG Games/PZ/MODS/Dejavu/res/HDPack_Terrain.res` | `2d20abdd3a753cd00671c84798a2bda030b7f192b71de58e0e09860f5bfe488c` / тот же | 0 |
| `C:/GOG Games/PZ/MODS/Dejavu/Maps/zone2.mpr` | `6c42b54bba0e94578d9d2c50b31947381014edc49785335a444c2edf1228c0e3` / тот же | 0 |
| `C:/GOG Games/PZ/MODS/Dejavu/Maps/zone3obr.mpr` | `7bb28272e6c114afb2ef0bcc148585c85a4ff5a5d19446c94ce556e5b0c79ede` / тот же | 0 |
| Установленный `EIMapperEndurance.exe`, VERSION136 | `53e595bac618a7a782a43cdc483398dbab0a49ef68ee5c36d791c3d024871602` / тот же | 0 |

Backup HDPack:
`build-tests-hdpack-restore-v2/backup/HDPack_Terrain.res`, вновь измеренный SHA
`2d20abdd3a753cd00671c84798a2bda030b7f192b71de58e0e09860f5bfe488c`.
Кандидат `build-tests-hdpack-restore-v2/HDPack_Terrain.res`, вновь измеренный SHA
`b3b123dde10513cf6dcc40d60ff4a9e21177017a51989b991c2445eb3f0868cf`.
Оба совпали с отчётом. Новые rollback136 и резервные копии карт не создавались:
до этапа замены не дошли. Существующие rollback-каталоги не удалялись.
Host Win32_Process inventory: **0 game.exe / 0 EIMapperEndurance.exe**
на момент контрольного readback. Это снимок состояния, не guard будущей замены.

Откат текущего шага не требуется: заменённых файлов0. Команда прежнего HDPack
rollback подготовлена ранее; здесь не выполнялась и не проверялась заново:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "C:/GOG Games/maped/ei-mapper-ataman/build-tests-hdpack-restore-v2/rollback.ps1"
```

При нынешнем original live SHA она должна дать NOOP согласно предыдущему отчёту.
Команды отката карт/установки137 в этом запуске не созданы: кандидаты с новым
атласом и соответствующие backup ещё отсутствуют. Старые semantic copies с
original HDPack применять под v2 нельзя.

## Продолжение полного поручения

Причина остановки — неверный параметр команды, не отсутствие установленного
toolchain и не дефект исходников137. Правильная команда для следующего запуска:

```powershell
& "C:/GOG Games/maped/ei-mapper-ataman/build-tests-137-final/source/scripts/build_release.cmd" `
  -QtBin "C:/GOG Games/maped/ei-mapper-endurance/.tooling/Qt/5.12.12/mingw73_64/bin" `
  -MinGwBin "C:/GOG Games/maped/ei-mapper-endurance/.tooling/Qt/Tools/mingw730_64/bin" `
  -BuildDirectory "C:/GOG Games/maped/ei-mapper-ataman/build-release-137-final" -Jobs 6
```

Далее — весь шаг1 с проверкой version, свежим регрессом под lock, замером
против136, staging/finalizer/manifest и guarded установкой с rollback136.
После его успеха — шаг2 v2 с проверками живых карт, затем шаг3: свежие копии
Zone2/Zone3obr под live v2, semantic-only save/reopen/resave, сохранность142
прежних payloads и MOB, guarded применение обеих карт. Перед каждой live
заменой заново проверять процессы, точный source SHA и backup, атомарность
и readback; любой провал — откат текущего шага и STOP.

**Что увидит владелец сейчас:** прежнюю установленную136, прежний HDPack и
прежние Zone2/Zone3obr. Исправленная программа137, HD-восстановление и новая
семантика ещё не переданы. Все три цели остаются открыты.
