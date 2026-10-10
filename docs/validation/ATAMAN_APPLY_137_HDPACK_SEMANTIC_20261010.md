# Применение 137, HDPack v2 и семантики — 2026-10-10

**Все три разрешённых шага завершены.** Постоянная установка обновлена 136→137,
Dejavu HDPack заменён на v2, свежая семантика Zone2/Zone3obr применена к живым картам.
Обязательные проверки замены, открытия, семантики и скорости успешны. World 45/1 —
явно предусмотренный прежний FAIL, не PASS; нового регресса не обнаружено.

## Поручение, база и границы

Владелец через Claude 2026-10-10: «да, играл [zone10/bz7g в JamaisVu135 со
смешанными страницами], всё нормально — делай по порядку». Это разрешение на
установку 137 и последовательную замену только трёх игровых файлов ниже.
Уточнение сопровождающего: STOP относится к провалам проверок и отклонениям при
live replacement; ошибки собственной подготовки исправлять и повторять.

main; исходный код VERSION 137 — `b586e970b0d02655f31e346f22ba9785c2c1d8d4`.
Промежуточный документальный коммит `cbb38e3dba772183608a80c1324c14ff135371f2`
зафиксировал преждевременную остановку по неверному пути toolchain. Настоящий отчёт
заменяет её текущим результатом; программа собрана из назначенного b586, а не из
изменённых отчётных документов. VERSION не повышалась. SHA итогового scoped-коммита
документов приводится в сообщении передачи и локальном Git.

AGENTS.md проверен при входе и продолжении: SHA256
`f4b7fdd5ea82fcdf43d002d23d64ec36be853650f054edf70376527cd6432ec3`.
Прочитаны CONTRIBUTING, TEST_BUILD_HANDOFF, PROJECT_CONTRACT и три назначенных
отчёта. Недоступные private-engine/modding skills не заменялись догадками:
использованы обычные исходники маппера и явно разрешённые данные.

Все GUI-тесты и открытия выполнялись скрыто на собственных desktop WinSta0 под
`build-locks/ui-tests.lock`; sentinel нормальных запусков `nativeAttempts=0`,
`hookedImports=14`. Системный буфер владельца не открывался и не блокировался.
Отрицательные clipboard-probes перехватывались до вызова OS; их ожидаемые попытки
не подменяют нулевой sentinel нормальных запусков. Сети, установки зависимостей,
новых чатов/подагентов, push и интерактивных запусков игры/редактора не было.

Пути карт уточнены по установленному `endurance-options.json`: gameRoot
`C:/GOG Games/PZ`, mapRoots Dejavu/Maps и Base/Maps, resourceRoots Dejavu/res и
Base/Res. Разрешённые карты — именно `MODS/Dejavu/Maps/zone2.mpr` и
`MODS/Dejavu/Maps/zone3obr.mpr`. Base — легаси оригинальной игры, без изменений.

## Шаг 1 — EI-FINALIZE-137

Свежий `git archive` полного b586 распакован в `build-tests-137-final/source/`;
ZIP SHA256 `0498eabadc96a260d23dc55d05db5861ad9f664069734dcb91c3620f58de0f9b`. Снимок и 459 файлов кода/скриптов
сверены; исходники не менялись. Свежая shadow Release:
`build-release-137-final/`. Использован уже установленный Qt 5.12.12/MinGW73:

```powershell
& "C:/GOG Games/maped/ei-mapper-ataman/build-tests-137-final/source/scripts/build_release.cmd" `
  -QtBin "C:/GOG Games/maped/ei-mapper-endurance/.tooling/Qt/5.12.12/mingw73_64/bin" `
  -MinGwBin "C:/GOG Games/maped/ei-mapper-endurance/.tooling/Qt/Tools/mingw730_64/bin" `
  -BuildDirectory "C:/GOG Games/maped/ei-mapper-ataman/build-release-137-final" -Jobs 6
```

Фактический wrapper запущен через subprocess имеющегося Python aqt-venv с полным
stdout/stderr receipt: exit 0. Внешний PowerShell ранее трактовал обычное qmake
предупреждение private headers как NativeCommandError; это исправлено. Первому
тестовому GUI-бинарнику не хватало Qt deployment; он не начал QtTest, собственный
PID остановлен, runtime/plugins развёрнуты, тест повторён. Эти попытки подготовки
сохранены в receipts и не объявляются PASS. Store python.exe shim не использован.

Fresh, staged до finalizer и установленный `--version` подтвердили
`EI Mapper Endurance Edition 137`. EXE всех трёх:
`cf69724878349614e4540cbabe15e7fe52df9adb26214affca1d13a01ab50343`.

| Свежая проверка | Результат |
|---|---|
| Полный UI widget/lifecycle | PASS, exit 0, nativeAttempts 0 |
| Scenario | 50 PASS / 0 FAIL |
| Dark theme | 0 failures |
| Gallery | PASS, functional/cache/stale-results/drain |
| Verified clipboard | 12 PASS / 0 FAIL / 0 SKIP |
| Terrain virtual atlas | 146 / 0 / 0 |
| Terrain resolve audit | 5 / 0 / 0 |
| Recipe bake | 17 425 / 0 / 0; 18 241 edges, 2 240 132 RGBA comparisons |
| Production-linked mixed resolution | 4 / 0 / 0 |
| Production-linked reader drain | 23 / 0 / 0; Release O2 с symbols, без stripping |
| Random paint | 80 / 0 / 0; transition changes 0/17 316 |
| Mixed edge | 63 / 0 / 0 |
| World sculpt | **45 PASS / 1 прежний FAIL / 0 SKIP**, тот же BoundaryFill Zone11/12 |

World сверён с 136: точная FAIL-строка прежняя, gap-after=0.000585550. Это
согласованное исключение world 45/1, не новая успешная проверка этого дефекта.
Полные результаты: `build-tests-137-final/regression-summary.json`, `.out` и
`qtest-final.txt` соответствующих suites. SKIP/таймауты/сбои не учитываются как PASS.

### Скорость миниатюр против установленной 136

До установки зафиксирован EXE 136
`53e595bac618a7a782a43cdc483398dbab0a49ef68ee5c36d791c3d024871602`.
Benchmark связан с production objects обеих версий и одинаковой clipboard isolation.
Пять чередующихся пар, восемь миниатюр, четыре состояния, два набора: 80 измерений.
Все ready=8/unavailable=0; cold calls=8, warm calls=0. Медианы в миллисекундах:

| Набор | Состояние | 136 ms | 137 ms | Δ ms | Δ % | Допуск ≤10% или ≤+15 ms |
|---|---|---:|---:|---:|---:|---|
| characters-unmo | cold | 90.235 | 86.025 | -4.210 | -4.67% | PASS |
| characters-unmo | disk-warm | 29.334 | 29.950 | +0.616 | +2.10% | PASS |
| characters-unmo | repeat | 5.098 | 5.063 | -0.035 | -0.69% | PASS |
| characters-unmo | repeat-after-disk | 5.080 | 5.369 | +0.289 | +5.69% | PASS |
| equivalent-buildings-stbu | cold | 29.041 | 29.687 | +0.646 | +2.22% | PASS |
| equivalent-buildings-stbu | disk-warm | 9.421 | 9.814 | +0.393 | +4.17% | PASS |
| equivalent-buildings-stbu | repeat | 5.753 | 5.534 | -0.219 | -3.81% | PASS |
| equivalent-buildings-stbu | repeat-after-disk | 5.723 | 5.880 | +0.157 | +2.74% | PASS |

8/8 допусков пройдены, максимальное замедление +5.69%, максимальная положительная
разница +0.646 ms. Receipts: `gallery-raw.json`, `gallery-summary.json`.

### Staging, manifest и установка

Новая staging `build-staging-137-final/`, состав сравнен со staging 136:
127 файлов, добавлений 0, удалений 0, изменённых программных файлов 6.
Один CHANGELOG текущей 137; `TRAVEL_EDITOR.md` неизменен. До finalizer проверены
version и SHA tested/staged. Finalizer с внутренним distribution validator — PASS;
очищен собственный workflow.log. После финализации staging не запускалась и не менялась.
Manifest SHA256 `22afaf063b83061d1dd8f2fbba18eca4baa664bae95c94920e74f19eefec6c4e`.

Постоянная установка:
`C:/GOG Games/maped/ei-mapper-ataman/artifacts/EI-Mapper-Ataman-2-first-test`.
Перед заменами все 127 старых managed-файлов совпали с manifest 136. Проверены
7 backup-файлов (6 changed + manifest); каждый заменён через adjacent temporary
copy и `File.Replace` с ненулевым displaced backup, с process/hash/backup guards
и readback. Manifest заменён последним. Установка 137: 127/127 SHA верны;
23 051 пользовательский файл, включая кэши, неизменен до/после установки и после
всех live openings. Все 8 прежних rollback-каталогов сохранены. Finalizer на
постоянной установке не выполнялся.

Backup root `artifacts/rollback-136-to-137-final/`; относительные пути ниже
соответствуют и установке, и резервной копии. SHA256 до/после каждой замены:

| Файл установки | До, 136 | После, 137 |
|---|---|---|
| `CHANGELOG.md` | `8d2f587b18f2547ce48fb3811649b2419fbb7dd2362eda00577cba9117060e6c` | `d04d95767b0d2a74a038e33aa9a7200eea958acadda72a1b5121b59acd61c5be` |
| `EIMapperEndurance.exe` | `53e595bac618a7a782a43cdc483398dbab0a49ef68ee5c36d791c3d024871602` | `cf69724878349614e4540cbabe15e7fe52df9adb26214affca1d13a01ab50343` |
| `Readme.md` | `e19fce361ac5a860eff3d1387b676d8b90a996bf43647912c5193e8b45ca081b` | `706e62167d4d70230bc5f3caa4cd06a43862ef02271fe3831846db8118491f76` |
| `runtime/EIMapperEnduranceJmvBridge.plg` | `3fdf45621e63035463a34e378f3a85ef2d50f44ea6b68fb5fa8c22241fbd38c2` | `5016acb13a1614dca2aeb94c320b254b166c8677428372dbbc81a383a9a707da` |
| `runtime/EIMapperEnduranceRuntimeHost.exe` | `51225a9a1b39b5ddc5d74004ca69d8e0c51671ce8034b5528731f71a6b555e8d` | `8fa405930386d19855789ab22d58c579d55f3ac6085ee2a3c9dcaf99efcf25cc` |
| `VERSION` | `cf43506c8d7737ae88293e557b9f1590712a7cceff570a7f7749a72ab926cf83` | `1e62542d131745fc4c3ebf51080463d7c1f9d34e4e209784ab34cecea35d42c6` |
| `SHA256SUMS.txt` | `074f49a2342aa2cb4e3576007bd2c96bb109030f301ea232a55fe5759355f9ee` | `22afaf063b83061d1dd8f2fbba18eca4baa664bae95c94920e74f19eefec6c4e` |

Receipts: `package-receipt.json`, `install-backup-receipt.json`,
`install-receipt.json`, `install-protected-before.json`, `final-readback.json`.

## Шаг 2 — live HDPack v2

Кандидат `build-tests-hdpack-restore-v2/HDPack_Terrain.res` и существующий original
backup перечитаны до замены; точные SHA совпали с назначенным отчётом. Перед заменой
host Win32_Process не содержал game.exe/EIMapperEndurance.exe и процессов установки.
Старый live SHA совпал с expected. Adjacent temporary copy проверена,
атомарный `File.Replace` оставил дополнительный displaced original
`build-tests-137-final/hdpack-displaced-original.res`; SHA обоих оригиналов верен.
Live повторно прочитан и совпал с v2. Размер RES: 775 817 323→1 270 745 195 bytes.

Точный кандидат сохраняет ранее доказанные свойства из отчёта подготовки v2:
1222 члена, перенос 12 profiles/10 semantic bindings, 1200 остальных payloads как v1;
236 восстановленных MMP с сохранными mip≥1 (2596 levels). Эти codec receipts
повторно использованы по полному SHA кандидата, не объявлены новым запуском.

**41/41 живых карт Dejavu открыты установленным EXE 137 до и после замены**, без
сохранения карт, autosave выключен, own cwd/options/cache/temp, exit 0,
nativeAttempts 0, MPR SHA до/после каждого открытия одинаков. Дополнительно выполнены
41/41 точных before и 41/41 after audits на fresh production objects 137 с
read-only instrumentation обычного CView. Helpers имеют отдельные QSettings/backup;
они не заменяют доказательства открытия фактическим установленным EXE.

Все exact profiles активны; palette=81 land/7 overlay. Physical counts и canonical
profile JSON без изменяемого atlasPixelSha совпадают с baseline 41/41. Все 15 ранее
действительных сохранённых semantic documents восстановлены точно (15/15);
ожидаемые прежние отказы Zone2/Zone3obr оставались до шага 3. Render не уменьшился
ни на одной карте: 25 карт 256 px, 16 карт 128 px.
bz10k/Zone12/Zone8 получили 256 px, palette active и exact semantics; Zone6/9/19
и прочие сохранили палитру/профиль. Published namespaces остаются 128 до отдельной
будущей пересборки, не подменяются stock namespaces.

| Карта | Фактический namespace | Render до→после, px | Physical tiles | Семантика |
|---|---|---:|---:|---|
| basegipat | basegipat_213430e77bcf | 128 → 128 | 448 | exact |
| bz10k | bz10k | 128 → 256 | 448 | exact |
| bz11k | bz11k | 128 → 256 | 512 | сохранённого поля нет, как baseline |
| bz13h | bz13h | 128 → 256 | 512 | сохранённого поля нет, как baseline |
| bz14h | bz14h | 128 → 256 | 256 | сохранённого поля нет, как baseline |
| bz15h | bz15h | 128 → 256 | 256 | сохранённого поля нет, как baseline |
| bz16h | bz16h | 128 → 256 | 512 | сохранённого поля нет, как baseline |
| bz18h | bz18h | 128 → 256 | 256 | сохранённого поля нет, как baseline |
| bz19g | zone10 | 128 → 256 | 512 | сохранённого поля нет, как baseline |
| bz2g | bz2g | 128 → 256 | 512 | сохранённого поля нет, как baseline |
| bz3g | bz3g | 128 → 256 | 512 | сохранённого поля нет, как baseline |
| bz4g | bz4g | 128 → 256 | 512 | сохранённого поля нет, как baseline |
| bz5g | bz5g | 128 → 256 | 512 | сохранённого поля нет, как baseline |
| bz6g | bz6g | 128 → 256 | 512 | сохранённого поля нет, как baseline |
| bz7g | Bz7g | 128 → 256 | 256 | сохранённого поля нет, как baseline |
| bz8k | bz8k | 128 → 256 | 512 | сохранённого поля нет, как baseline |
| bz9k | bz9k | 128 → 256 | 512 | сохранённого поля нет, как baseline |
| zone1 | zone1_17763baa57df | 128 → 128 | 960 | exact |
| zone10 | zone10 | 128 → 256 | 512 | сохранённого поля нет, как baseline |
| zone11 | zone11_0132fbfdd188 | 128 → 128 | 512 | exact |
| zone12 | Zone12 | 128 → 256 | 448 | exact |
| zone12dun2 | zone14_975e928d3190 | 128 → 128 | 256 | exact |
| zone13 | zone13_63226eeedc0c | 128 → 128 | 704 | exact |
| zone14 | zone14_60f652260b04 | 128 → 128 | 512 | exact |
| zone15 | zone15 | 128 → 256 | 512 | сохранённого поля нет, как baseline |
| zone16 | zone16 | 128 → 256 | 256 | сохранённого поля нет, как baseline |
| zone17 | zone17 | 128 → 128 | 256 | сохранённого поля нет, как baseline |
| zone18 | zone18_55491c494740 | 128 → 128 | 576 | exact |
| zone18dun1 | zone18 | 128 → 128 | 512 | сохранённого поля нет, как baseline |
| zone19 | zone19_bb84f59c5962 | 128 → 128 | 960 | exact |
| zone2 | zone2 | 128 → 256 | 1024 | прежний отказ → embedded v2 exact (шаг 3) |
| zone20 | Zone20 | 128 → 256 | 192 | сохранённого поля нет, как baseline |
| zone3obr | zone3obr_0bcba8177f9a | 128 → 128 | 1472 | прежний отказ → embedded v2 exact (шаг 3) |
| zone4 | zone4_fc5a0ba98c52 | 128 → 128 | 1024 | exact |
| zone5_1 | Zone5_1 | 128 → 256 | 192 | сохранённого поля нет, как baseline |
| zone6 | zone6_fe645a375f96 | 128 → 128 | 1216 | exact |
| zone6_2 | zone6_2 | 128 → 256 | 512 | сохранённого поля нет, как baseline |
| zone7 | zone7_44a43bfdda10 | 128 → 128 | 960 | exact |
| zone8 | zone8 | 128 → 256 | 896 | exact |
| zone9 | zone9_f24337452d99 | 128 → 128 | 1600 | exact |
| zonemainmenunew | zonemainmenunew_91d1b040b2b8 | 128 → 128 | 384 | сохранённого поля нет, как baseline |

До шага 3 остальные 82/82 контролируемых входа неизменны. Receipts:
`hdpack-apply-receipt.json`, `baseline-installed-openings.json`,
`v2-installed-openings.json`, `baseline-openings.json`, `v2-openings.json`;
отдельные `.log/.data.json/.out` сохраняют каждый запуск.

## Шаг 3 — свежая semantic-only запись и live apply

После полного успеха шага 2 созданы новые копии обеих карт, MOB, live HDPack v2
и Base textures в `build-tests-137-final/semantic/`. Хеши источников и копий
проверены. Свежий helper связан с production objects 137 и использует существующие
`buildTerrainSemanticFieldMember`, `commitValidatedMprBytes`,
`adoptCommittedLandscapeBaseline`: семантика восстановлена по физическим тайлам,
записан только `ei_mapper_terrain_semantics.json` в MPR; SEC patches и публикации
RES отсутствуют. Программа 137 не исправлялась и физические карты не пересобирались.

| Карта | Land active cells | Overlay active cells | Старых членов MPR сохранено | MPR bytes | Semantic payload bytes |
|---|---:|---:|---:|---:|---:|
| zone2 | 7680 | 224 | 31 | 351908 → 357994 | 4868 |
| zone3obr | 28160 | 5130 | 111 | 1490500 → 1509130 | 12680 |

Refresh/reopen/resave 6/6 PASS. Все 41 194 активные ячейки и 164 776 их углов
точны; unknown/corner mismatch/conflicting vertices/ties/vote-rule mismatch=0.
7857+28497 land vertices и столько же overlay vertices после reopen совпадают
с записанными полями. Неиспользуемые overlay vertices без голосов оставлены unset
(7595/22774), это не неоднозначность активных ячеек.

Все 142/142 прежних члена сохранены с именами/offset/size/timestamp/payload;
всё тело исходного MPR с offset 16, включая старые tables/trailer, побайтно осталось
префиксом. Из прежних header bytes меняются только offsets 4,8,9,12; добавлен
новый semantic member/table. Высоты, физические тайлы/вращения, overlay, вода,
объекты и оба MOB неизменны. Первое и повторное сохранение совпадают SHA и bytes.
Schema v2 привязана к topology/atlas/profile, без устаревающего полного MPR SHA.

**zone2:** topology `68137e08dc22b02fafec1efb712e49e0adf2407f0381ef0817d3ddfcb2713c5c`; atlas `1b59da26b824810105d0018f11a3e30c5e8715577cb03bc80f2141b8a3e103c6`; canonical profile `2088fa29acad879664b36cf247b74355b750827630617503599f1b6c51aaf727`.

**zone3obr:** topology `59aaf7b731ce979aca40c768a8331f3d160c7b04606208feabcb36828a47322a`; atlas `c582a9db3854e293fc7d01f1b2c8e4fbf9e375504119217bcefd1e41b28e2964`; canonical profile `a2e0a0c2bad891d65ca753848c457bda6dd58c4d794e9cf871ba7531ba97267c`.

Zone2 использует новый atlas/profile v2. У Zone3obr опубликованный namespace
`zone3obr_0bcba8177f9a` и его atlas/profile не менялись при восстановлении stock
страниц v2; новая процедура это заново подтвердила. Поэтому кандидат Zone3obr
побайтно совпал с прежним проверенным semantic-only кандидатом; применён именно
заново полученный из fresh copy под live v2 результат.

До live записи обе original-карты скопированы и SHA проверены в
`build-tests-137-final/semantic-live-backup-20261010/`. Перед каждой заменой:
process guard, live SHA=expected original из отчётов, backup SHA=original,
live HDPack SHA=v2, candidate SHA; adjacent temp/atomic File.Replace/readback,
дополнительный displaced original в backup/displaced/. Затем **установленная 137**
открыла каждую живую карту без сохранения: палитра активна, semantic restored,
отказов 0, nativeAttempts 0, MPR SHA неизменен. Fresh precise audit и отдельный
physical-cell reopen подтвердили embedded v2/binding/все углы 2/2.

После шага 3 из корпуса 83 входов изменены ровно 3 разрешённых; остальные 80/80
неизменны: 77 прочих MPR (включая все 38 Base), оба MOB и Base textures.
HDPack остался v2. Final readback также проверил original backups, 127 managed
и все 23 051 пользовательских файла установки. Lock освобождён, owned процессы
завершены. Провалов проверок замены/live validation не было; откат не понадобился.

## Точные игровые SHA256 и резервные копии

Все живые пути ниже относительно `C:/GOG Games/PZ/MODS/Dejavu/`;
пути резервных копий относительно корня Ataman. Значения «до» сверены перед
соответствующей заменой, «после» — readback и финальное чтение.

| Живой файл | До | После | Проверенная резервная копия |
|---|---|---|---|
| `res/HDPack_Terrain.res` | `2d20abdd3a753cd00671c84798a2bda030b7f192b71de58e0e09860f5bfe488c` | `b3b123dde10513cf6dcc40d60ff4a9e21177017a51989b991c2445eb3f0868cf` | `build-tests-hdpack-restore-v2/backup/HDPack_Terrain.res` |
| `Maps/zone2.mpr` | `6c42b54bba0e94578d9d2c50b31947381014edc49785335a444c2edf1228c0e3` | `86392b9408830981c9334ae43de8f4b3243846fba2cb31f97c7385a376bfe802` | `build-tests-137-final/semantic-live-backup-20261010/zone2.mpr` |
| `Maps/zone3obr.mpr` | `7bb28272e6c114afb2ef0bcc148585c85a4ff5a5d19446c94ce556e5b0c79ede` | `f567b860896d6084c2669046383d90dcbc59ae079b119e3dd21ff01d33e83caf` | `build-tests-137-final/semantic-live-backup-20261010/zone3obr.mpr` |

Receipts: `semantic-copy-results.json`, обе `semantic/*/byte-comparison.json`,
`semantic-live-results.json`, `semantic-apply-receipt.json`,
`final-readback.json`. Все игровые данные/копии/журналы/сборки остаются ignored,
в scoped-коммит входят только документы.

## Команды отката

Для полного возврата порядок: **карты → HDPack → программа 136**. Это сохраняет
согласованность встроенной семантики и атласа. Выполнять при закрытых игре/маппере;
скрипты проверяют process/hash/backup guards, захватывают ui-tests.lock и
останавливаются при неизвестном live SHA. Чужие последующие изменения не перезаписывают.

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "C:/GOG Games/maped/ei-mapper-ataman/build-tests-137-final/semantic-live-backup-20261010/rollback.ps1"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "C:/GOG Games/maped/ei-mapper-ataman/build-tests-hdpack-restore-v2/rollback.ps1"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "C:/GOG Games/maped/ei-mapper-ataman/artifacts/rollback-136-to-137-final/rollback.ps1"
```

Все rollback backup SHA перечитаны и верны; rollback scripts проверены по коду.
HDPack rollback `-ValidateOnly` на текущем v2 завершился exit 0 без замены.
Фактические live rollback-команды здесь не исполнялись: это не заявленная проверка
цикла rollback/reapply. Для HDPack прежний fixture-тест rollback/readback описан
в назначенном отчёте v2. Original backups и дополнительные displaced originals
сохраняются вместе со старыми rollback-каталогами.

## Что увидит владелец

В прежнем постоянном пути запускается 137 с сохранёнными настройками и кэшами.
Восстановленные stock зоны используют 256 px и более полные HD-варианты, включая
bz10k/Zone12/Zone8/Zone2; смешанные страницы поддерживаются маппером. На Zone2 и
Zone3obr палитра активна, прежнего отказа семантики больше нет. Published Zone6/9/
Zone3obr/19 сохраняют свои 128 px: отдельная HD-пересборка этих namespaces вне
этого поручения. Base и физические данные карт остаются прежними.

Владелец ранее подтвердил игру zone10/bz7g JamaisVu135; новая интерактивная игровая
приёмка установленного сочетания 137/v2/обеих карт не выполнялась. Автоматические
скрытые открытия и codec/semantic проверки не объявляются gameplay acceptance.
Известный world BoundaryFill 45/1 остаётся. Три пункта ROADMAP закрыты в пределах
разрешённого применения; будущая published HD-пересборка и этот старый дефект
не включены в их успешный результат.
