# EI-TERRAIN-MIXED-RES-ATLAS-137 — 2026-10-10

## Поручение, база и границы

Поручение владельца через Claude: необходимая правка программы перед отдельной
заменой HDPack, VERSION 136 → 137. Вход: main,
`b6b3be3cd3b3710613ae848182015f14f1240931`. Действующие AGENTS.md,
PROJECT_CONTRACT.md, TEST_BUILD_HANDOFF.md и отчёты
[apply](ATAMAN_HDPACK_RESTORE_APPLY_20261010.md),
[prep](ATAMAN_HDPACK_RESTORE_PREP_20261010.md),
[compare](ATAMAN_HDPACK_COMPARE_20261010.md) прочитаны.
SHA256 AGENTS.md при входе:
`f4b7fdd5ea82fcdf43d002d23d64ec36be853650f054edf70376527cd6432ec3`.

Работа только в main; сеть, установка зависимостей, подагенты, интерактивный
редактор/игра, push и обновление постоянной установки не выполнялись.
Все UI-процессы запускались на невидимом Windows desktop под атомарным
`build-locks/ui-tests.lock`. Чужой занятый lock ожидался без удаления.
Обнаружен независимый пункт EI-SEMANTIC-REFRESH-COPY-20261010 в ROADMAP;
его рабочая запись сохранена и не включена в scoped-коммит137.
Его документальный коммит `acb54b86d6992db7c626a7e18ae3a447f38d962e`
сохранён в main перед137; относительно входной базы он добавляет только свой
отчёт, кодовая база benchmark/реализации от этого не меняется.
Приватные engine/modding skills в этой сессии отсутствуют: исследование ограничено
обычным кодом проекта и разрешёнными файлами данных, без приватного движка.

## Что исправлено

- Binding/adoption принимают разные размеры корректных квадратных страниц.
  Namespace, physical slots, approved profile и decoded SHA остаются проверкой
  **нативных** пикселей; масштабированный raster не становится идентичностью.
  Проверка источника архива (путь/размер/mtime) сохранена.
- Палитра хранит исходные QImage. Общий GL-атлас смешанных страниц строится
  по **максимальному** native tile size. Меньшие изображения масштабируются
  SmoothTransformation отдельно по каждому тайлу, без интерполяции соседнего
  тайла. Для одинаковых страниц сохранён прежний compressed GL путь.
  Preview, regrid-copy, incremental upload и rollback используют тот же размер.
  Есть проверка GL_MAX_TEXTURE_SIZE; неподдерживаемая GPU получает отказ,
  без скрытого уменьшения качества.
- Modern/генерация масок, mixed-edge импорт и Resolve/Finalize создают новые
  растры в максимальном размере. Малые физические padding-слоты существующей
  страницы не используются для нового крупного тайла: создаётся новая страница.
  При обычной публикации исходные authored MMP payloads сохраняются;
  нет пересжатия неизменённого хвоста только ради расширения MP-таблицы.
  Finalize создаёт отдельный namespace с единым максимальным размером.
- TestClipboardSession считает FAIL при неготовой изоляции/hooks либо
  `nativeAttempts != 0`. Sequence/форматы — информационная запись; ошибки их
  снимка и внешние изменения не превращают изолированный прогон в FAIL.
  Системный буфер runner'ы не открывают, не удерживают и не восстанавливают.
  Reader entry point также включает in-memory isolation до QApplication.
  В tracked PowerShell/Python runner'ах вызовов OpenClipboard не найдено.
  Старый локальный ignored `build-tests-136-final/stable_clipboard.py`,
  удерживавший буфер, заменён инертным контекстом; исторические отчёты не изменены.

## Сборка и воспроизводимость

Qt5.12.12/MinGW7.3 x64 из уже установленного toolchain; штатный release wrapper,
папка `build-release-137-mixed`, Qt executable
`build-release-137-mixed/release/EIMapperEndurance.exe`, номер137.
SHA256 финального EXE:
`a3070fb25d3c0ae40d37e5c5293cd0c376131feabb517ff08dcf5ed2934f6d9a`.
Оба x86 runtime components также собраны штатным wrapper.
Это инженерная сборка для ревью, **не финализированный комплект тестировщика**.
Staging/ZIP/manifest и постоянная установка оставлены сопровождающему по
прямому исключению владельца для137.

Локальные receipts, runner'ы, fixture copies и полные логи:
`build-tests-137-mixed/`. Они ignored и не входят в Git/дистрибутив.
Синтетические новые tests/*.cpp/.pro входят в scoped-коммит.
Финальный receipt сборки/версии/хешей:
`final-release-wrapper.json`, `final-version.out`, `final-integrity.json`.

Основные команды (в существующем toolchain PATH):

```powershell
scripts/build_release.cmd -QtBin <Qt-bin> -MinGwBin <MinGW-bin> -BuildDirectory build-release-137-mixed -Jobs 6
qmake tests/terrain_mixed_resolution_tests.pro MIXED_APP_BUILD=<absolute-build-release-137-mixed>
qmake tests/res_reader_drain_tests.pro RESDRAIN_APP_BUILD=<absolute-build-release-137-mixed>
mingw32-make -f Makefile.Release -j6
# GUI checks — только скрыто, под ui-tests.lock:
tests/clipboard_isolation_regression.ps1 -AppBuild build-release-137-mixed -TestBuild build-tests-137-mixed
```

## Исходный дефект, синтетика и регресс

| Проверка | Итог |
|---|---:|
| Новый production-linked mixed-resolution suite | 4 PASS / 0 FAIL / 0 SKIP |
| Terrain virtual atlas | 146 / 0 / 0 |
| Terrain resolve audit | 5 / 0 / 0 |
| Terrain recipe bake | 17 425 / 0 / 0 |
| Random paint (копия реальной карты + синтетика) | 80 / 0 / 0 |
| Mixed-edge (сохранение/публикация/reopen на копии) | 63 / 0 / 0 |
| Полный ui-widget | 169 PASS, 0 FAIL |
| Scenario | 50 / 0 |
| Dark theme | 70 / 0 |
| Gallery | PASS |
| Clipboard isolation QtTest | 12 / 0 / 0 |
| Reader-drain, диагностическая сборка драйвера | 23 / 0 / 0 |

Синтетика намеренно проверяет оба порядка: сначала128, затем256 и наоборот.
Approved profile + реальный archive binder + CTileForm adoption принимают native
SHA; scaled SHA, другой namespace/count, неквадратный тайл и изменённый архив
отвергаются. Profile и native images не подменяются raster-версией.
GL regrid и incremental прочитаны через framebuffer: native128 синий/жёлтый
тайл действительно занимает256 px; исходные128 QImage остаются128.
ComposeGrid двух страниц =4096×2048. Modern-generated/mixed-edge new tiles =256;
Finalize new MMP page =2048 (tile256); опубликованный MP также содержит
textureSize2048/tileSize256 после fresh reopen. Отдельная проба до исправления
MP metadata дала1 FAIL: actual128 вместо expected256
(`terrain_virtual_atlas_tests/mp-before-fix.txt`); финальный suite146/0.
Draft publication с logical count70 и малым128 хвостом переносит новый тайл
в слот128 новой256 страницы; первые два compressed payload **побайтно равны**
исходным. Отдельно проверен physical import без preserveAuthoredPages:
он также пропускает малый padding и добавляет256 тайл в новую страницу.
Recipe bake: 18 241 edge comparisons и 2 240 132 RGBA comparisons.

Receipts: `terrain_mixed_resolution_tests/qtest-final.txt`,
`terrain_virtual_atlas_tests/qtest-final.txt`,
`terrain_resolve_audit_tests/qtest.txt`, `terrain_recipe_bake_tests.out`,
`random.out`, `mixed.out`, `ui-widget.stdout.log`, `scenario.stdout.log`,
`dark-theme.stdout.log`, `gallery.stdout.log`,
`clipboard/release/clipboard-results.txt`, `reader-debug-results.txt`.

## Скрытые открытия исходного пака и кандидата v2

MPR скопированы из разрешённого Dejavu/Maps в собственную приватную папку;
resource archive читался без изменений. Helpers линкованы с production objects
137 (и136 для сравнения), вызывают штатный open/palette/semantic pipeline,
с отдельными settings/backup paths. Это проверка скрытого UI, не запуск игры.
Проверены8 карт ×2пака =16 ожидаемых результатов; все16 совпали с ожиданием.
На bz10k/zone12/zone8 сравнивались **все** land/overlay semantic vertices с
embedded semantic document, а не только наличие семантики.

| Карта | Physical tiles | Палитра/profile на v2 | Exact semantic vertices land + overlay | GL tile original → v2 |
|---|---:|---|---|---:|
| bz10k | 448 | active, approved, native exact | 4 225 + 4 225 | 128 → 256 |
| zone12 | 448 | active, approved, native exact | 41 377 + 41 377 | 128 → 256 |
| zone8 | 896 | active, approved, native exact | 31 329 + 31 329 | 128 → 256 |
| zone2 | 1 024 | active, approved, native exact | **FAIL ожидаемый, прежний mprSha** | 128 → 256 |
| zone3obr | 1 472 | active, published profile exact | **FAIL ожидаемый, прежняя semantic binding** | 128 → 128 |
| zone6/zone9/zone19 | published namespaces | active, exact | exact, на обоих паках | 128 → 128 |

Zone2 stored semantic mprSha:
`c96290af87dbf0386151862abe41552ede39541cc62c420c3b8809cf24c4c2a9`;
actual MPR:
`6c42b54bba0e94578d9d2c50b31947381014edc49785335a444c2edf1228c0e3`.
Данные и семантика Zone2/Zone3obr не исправлялись в этой задаче.
Для original повторяются точные vertices остальных карт и те же два отказа.
`openings.json`, `open-137-*-*-0-audit.json` и соответствующие `.log` содержат
native hash, источник профиля, active family counts и готовность GL.
В zone8 v2: `prevalidatedTilesAdopted:true`, `prevalidatedBindingReused:true`,
`atlasHashMs:0`; прежнего terrain.atlas_profile_hash_mismatch нет.

Mixed HD-рендер требует больше памяти: zone8 v2 GL8192×8192,
peak working set1 215 709 184байт (≈1159MiB), ready5760ms.
Это ожидаемая цена256 raster; условие≤10% применяется к несмешанным картам.

## Открытие и память относительно установленной136

Установленный136 executable и исходный executable136 для baseline имеют один
SHA256 `53e595bac618a7a782a43cdc483398dbab0a49ef68ee5c36d791c3d024871602`.
Benchmark helpers136/137 используют matching production objects и одинаковую
процедуру открытия до готовой палитры/семантики/GL. Original pack, те же copied
MPR, нативные uniform128 pages. По5 пар на карту, порядок136/137 чередуется;
30 открытий. Медианы readyMs и peak working set (ProcessMemoryInfo, sampling20ms).

| Карта | Ready136 →137, ms | Δ time | Peak136 →137, bytes | Δ memory |
|---|---:|---:|---:|---:|
| zone8 | 5877 →5936 | +1.00% | 694976512 →642527232 | −7.55% |
| zone6 | 6437 →6390 | −0.73% | 670208000 →670298112 | +0.01% |
| zone19 | 6082 →6028 | −0.89% | 746065920 →788541440 | +5.69% |

Все шесть условий≤10% — PASS. Это локальный замер с работающим компьютером
владельца и обычными OS caches, не обещание абсолютной скорости на любой GPU.
Receipts: `open-performance-raw.json`, `open-performance-summary.json`.

## Миниатюры136 →137

Реальные RES,8 видимых миниатюр каждого раздела;5 пар с чередованием версии,
отдельные собственные app/data/cache directories. Cold process и второй
process с disk cache, плюс повтор в том же process:80 измерений.
В baseline оставлены gallery/resource/render production objects136;
только тестовый clipboard isolation слой одинаковый137 для обеих версий,
чтобы активность владельца не влияла на критерий. Каждый набор готов полностью,
нет unavailable; provider calls cold8, disk-warm0.

| Набор | Состояние |136, ms |137, ms | Δ ms / % |
|---|---|---:|---:|---:|
| characters-unmo | cold |96.019 |91.608 |−4.411 / −4.59% |
| characters-unmo | disk-warm |31.767 |30.487 |−1.280 / −4.03% |
| characters-unmo | repeat |5.067 |5.635 |+0.568 / +11.21% |
| characters-unmo | repeat-after-disk |5.370 |5.449 |+0.079 / +1.47% |
| equivalent-buildings-stbu | cold |31.374 |32.821 |+1.447 / +4.61% |
| equivalent-buildings-stbu | disk-warm |10.446 |10.015 |−0.431 / −4.13% |
| equivalent-buildings-stbu | repeat |5.904 |5.941 |+0.037 / +0.63% |
| equivalent-buildings-stbu | repeat-after-disk |6.211 |5.886 |−0.325 / −5.23% |

Критерий≤10% **или**≤+15ms пройден во всех8 строках. Строка+11.21% проходит
только по абсолютному допуску+0.568ms, это не скрыто как≤10%.
Receipts: `gallery-summary.json`, `gallery-raw.json` и individual outputs.

## Буфер: отрицательные и внешняя информационная пробы

Отрицательные subprocess probes `--probe-native-open`, `--probe-native-ole`,
`--probe-system-qpa` завершаются2 с FAIL sentinel/nativeAttempts>0.
OpenClipboard probe перехватывается IAT hook **до** реального Windows API:
системный буфер не открывается даже отрицательной пробой.
Обычные UI/scenario/dark/gallery/random/mixed/reader процессы имеют PASS и
`nativeAttempts=0`, `hookedImports=14`.

`--probe-external-metadata` детерминированно заменяет только process-local
наблюдение GetClipboardSequenceNumber после установки hooks: sequence меняется,
sentinel даёт PASS/nativeAttempts0/unchanged0 и INFO. Это имитация внешнего
изменения наблюдаемой метаинформации, **не реальная запись в буфер владельца**.
Содержимое буфера не читалось/не менялось. Реальное внешнее изменение именно
внутри одного законченного прогона не инициировалось и не подтверждено;
между сериями наблюдалась смена34522→34529 и4→6 formats. Правило PASS проверено
на детерминированной probe, живую активность владельца runner не блокирует.

## Сбои и границы доказательства

- Первый aggregate clipboard runner прошёл clipboard/UI/scenario/dark/gallery,
  затем reader оборвался exit5; его executable исчез из собственной папки.
  Это **FAIL первого runner**, не PASS. Подтверждённой причины удаления нет;
  антивирус/внешнее удаление — гипотеза, Windows Defender detection events
  не подтвердили её. Настройки защиты и системы не менялись.
  Диагностический reader driver пересобран release-оптимизированным с debug
  symbols без stripping, с теми же тестами/production objects:23/0/0 за12.070s,
  nativeAttempts0. SHA executable до/после одинаковый
  `2c5c62ad519fa3530e14d4f23f4fa3fe3f8e1511bd1d747dd569ee997a320272`.
  Полные успешные UI не повторялись вместо расследования reader.
- Initial test infrastructure errors (отсутствующий Qt platform plugin,
  helper argv/path collision, missing owned output directory) исправлены только
  в собственных build trees; соответствующие проверки повторены успешно.
  Compile/run эксперименты с ошибкой не учитываются как PASS.
- После общих UI/benchmark прогонов исправлен ещё display-only regrid upload,
  добавлена отдельная framebuffer probe, запрет малого padding для physical
  import и исправлен MP tileSize при Finalize первого малого тайла.
  Финальная synthetic/terrain проверка повторена. Общие UI, галерейный
  benchmark и uniform opening paths этими локальными ветками не изменяются;
  их успешные receipts повторно использованы по договору достаточности проверок.
  Они не объявляются повторным запуском на финальном EXE.
- Ни скрытый mapper UI, ни codec round-trip не являются gameplay acceptance.
  Интерактивная приёмка владельца/ревью сопровождающего остаются открыты.

## Игра, MMP и смешанные страницы

Проектный codec читает width/height/format **каждого MMP отдельно**; физические
слоты всегда8×8. Normalized tile UV не кодирует пиксельный размер. В формате MMP
нет запрета на разные страницы одного namespace. Но MP содержит один общий
textureSize/tileSize (offset20/28). Неизвестный потребитель игры может использовать
его как предположение о равномерности — **гипотеза риска**, не установленный баг.

Текущий read-only аудит RES headers (`game-format-audit.json`):

| Источник | Numbered MMP groups/pages | Mixed namespace |
|---|---:|---|
| Base/Res/textures.res |50 /572 | не найден |
| JamaisVu135/HDPack_Terrain.res |38 /259 | не найден;239 tiles256,20 tiles128 |
| JamaisVu135/textures.res |30 /155 | **zone10**: page000 tile128,001–007 tile256; **bz7g**:000 tile128,001–003 tile256 |
| v2 candidate |91 /1091 |11 stock namespaces со смесью;236 HD pages +855 SD |

В Base regex numbered groups включает не только terrain, поэтому количество
572 не означает572 terrain pages. Наличие zone10/bz7g в JamaisVu135 подтверждает
данные формата, но приоритет HDPack может перекрыть эти members в игре: это
**не доказательство**, что игра уже отображала именно смешанный комплект.
Приватный engine source и runtime gameplay не проверялись, сеть запрещена.

Безопасный вариант при обнаруженном game-risk: отдельная явная публикация в
новом namespace с равномерными256 pages (штатный Finalize теперь делает это),
либо отдельная data-only копия с апскейлом только поздних128 страниц до256 и
сохранением physical slots. При таком апскейле native decoded hashes изменятся;
approved profile/semantic bindings нужно явно перенести и перепроверить.
Ничего из этого к живым данным в137 не применялось. Последующая замена v2
остаётся отдельным поручением; игровые дефекты Zone2/Zone3obr не закрыты.

## Сохранность и следующий шаг

До/после проверяются live HDPack, candidate v2 и installed136; copied MPR
сравниваются с исходными (`final-integrity.json`). Ожидаемые неизменные SHA256:

- Live HDPack: `2d20abdd3a753cd00671c84798a2bda030b7f192b71de58e0e09860f5bfe488c`.
- Candidate v2: `b3b123dde10513cf6dcc40d60ff4a9e21177017a51989b991c2445eb3f0868cf`.
- Installed136: `53e595bac618a7a782a43cdc483398dbab0a49ef68ee5c36d791c3d024871602`.

Код137 и проверочные результаты переданы на ревью scoped-коммитом main;
full SHA коммита находится в итоговом сообщении и локальном commit receipt.
Следующий шаг сопровождающего: ревью, договорная подготовка/установка137;
отдельное решение об игровой приёмке mixed pages и guarded применении пака.

## Передача 137

EI-FINALIZE-137 завершён 2026-10-10. Свежая Release из git archive
`b586e970b0d02655f31e346f22ba9785c2c1d8d4`, VERSION 137; `--version=137`.
EXE SHA256 `cf69724878349614e4540cbabe15e7fe52df9adb26214affca1d13a01ab50343`.
Полный свежий UI/scenario/dark/gallery/terrain/mixed-resolution/random/mixed-edge/
reader-drain успешен под ui-tests.lock, nativeAttempts=0; world 45/1 — тот же
известный FAIL, не PASS. Миниатюры против установленной 136: 80 измерений,
8/8 допусков, максимальное замедление +5.69% / +0.646 ms (разные строки).

Новый `build-staging-137-final/`: состав 127/127 против staging 136, добавлений/
удалений 0, изменённых файлов 6; finalizer/внутренний validator PASS.
Manifest SHA256 `22afaf063b83061d1dd8f2fbba18eca4baa664bae95c94920e74f19eefec6c4e`.
Постоянная `artifacts/EI-Mapper-Ataman-2-first-test` обновлена 136→137,
127 managed SHA верны; 23 051 пользовательский файл/кэши сохранены.
Backup7/7: `artifacts/rollback-136-to-137-final/`; старые rollback сохранены.
Finalizer на установке не запускался.

После успешной передачи по явному разрешению владельца применён HDPack v2:
41/41 живых Dejavu карт открыты именно установленной 137, exact profiles/
палитры не ухудшились, stock bz10k/Zone12/Zone8 render 256 px. Затем заново
восстановлены fresh copies Zone2/Zone3obr под live v2: 41 194 активные ячейки,
0 неоднозначностей, 142/142 прежних MPR members сохранены, resave byte-stable;
guarded live apply обеих карт и installed reopen 2/2 PASS. Остальные 80 входов
корпуса, включая Base/MOB/textures, неизменны. Published namespaces остаются128.

Первоначальная остановка по неверному пути toolchain была ошибкой подготовки;
после уточнения сопровождающего она исправлена. Неуспешные попытки не учтены
как PASS. Новая интерактивная gameplay acceptance не проводилась.
Полные хеши до/после, замеры, receipts, backup paths и команды отката:
[отчёт применения](ATAMAN_APPLY_137_HDPACK_SEMANTIC_20261010.md).
