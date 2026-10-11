# Применение 138 — HD-пересборка 14 карт Dejavu — 2026-10-11

**Установка 137→138 и разрешённое применение к живым картам завершены.** Новые HD namespaces
добавлены в общий Dejavu HDPack без удаления/подмены прежних members, затем заменены
14 MPR. Все 41 карта Dejavu открыты установленной 138 и отдельно проверены точным
MainWindow/CView helper из её production objects. Base и прочие игровые файлы сохранены.
Игра и интерактивный редактор не запускались; художественная/игровая приёмка владельца остаётся открытой.

## Поручение, база и границы

Владелец через Claude 2026-10-11: «применяй». Явно разрешены постоянная установка138,
`C:/GOG Games/PZ/MODS/Dejavu/res/HDPack_Terrain.res` и MPR14 опубликованных карт
из отчёта138. RES-компаньоны не потребовались; ни один другой игровой файл не заменялся.
Base — легаси и исключён из записи. В этой задаче новые номера версии не назначались.
Ветка main, свежая программа из точного `git archive`
`57b998d3ee1f7b207cc866ca68879687eca97e49`.
AGENTS SHA256 `f4b7fdd5ea82fcdf43d002d23d64ec36be853650f054edf70376527cd6432ec3`
совпал при входе и финальной сверке; действующие договоры и четыре назначенных отчёта прочитаны.
Private-engine/modding skills недоступны и не понадобились; приватные исходники не исследовались.
Сеть, установка зависимостей, push, новые чаты и подагенты не использовались.
При продолжении в checkout обнаружены незакоммиченные изменения source/tests и два
скрипта tools/terrain_bake_prototype. Они сохранены вне работы и вне scoped-коммита;
программа и production helpers относятся к точному archive указанного SHA.
Во время проверок main продвинут чужим scoped research-коммитом
`73b9712da03ab95a16eddbf309017abdb2a632cb` (terrain bake stage C prototypes).
Этот коммит и прочие чужие изменения сохранены; они не подменяют базу программы138.
Позднее принят чужой scoped-коммит `eb344e092ad12206e82b3b3cc98aad59873016e0`
(mixed-edge polish, Ataman139). VERSION checkout теперь139; эта передача устанавливает
проверенную138 из заданной базы57b998d3. Постоянная установка при завершении применения
остаётся138, EXE SHA приведён ниже; чужая139 и её исходники сохранены в main.

Все UI/открытия скрыты на собственных WinSta0 desktop под `build-locks/ui-tests.lock`.
Clipboard sentinel использует in-memory backend: нормальные UI/CLI/actual consumer
запуски имеют `nativeAttempts=0`, 14 hooked imports. Системный буфер владельца не открывался и не блокировался.

## EI-FINALIZE-138

Снимок: [source.zip](../../build-tests-138-final/source.zip), SHA256
`4e8a5bc35b3dbe4bdd111a5b0351c1abdf2ec03ec08ec6336338e021539bc128`; 462 файлов исходников/скриптов сверены с archive.
Release wrapper и новые runtime components: exit0.
Shadow build `build-release-138-final`; сохранённый Qt5.12.12/MinGW7.3 и VS toolchain
использованы только для чтения, зависимости не устанавливались.
Fresh/staged/installed `--version`: `EI Mapper Endurance Edition 138`.
EXE всех трёх: `eafaa4899cf5a8987c7dda12dd60b43a28b4168a7f47770df4b85832903d8f31`.

| Обязательная проверка свежей программы | Результат |
|---|---|
| Полный UI widget/lifecycle | PASS; sentinel nativeAttempts=0 |
| Scenario | 49 PASS / 0 FAIL; sentinel0 |
| Dark-theme / gallery | 0 failures / PASS; sentinel0 |
| Terrain virtual atlas / resolve audit | 146/0/0 / 5/0/0 |
| Recipe bake | 17425/0/0;18241 edges,2240132 RGBA comparisons |
| World sculpt | 48/0/0, все optional installed cases выполнены |
| Production mixed-resolution / reader-drain | 4/0/0 / 23/0/0; sentinel0 |
| Random / mixed-edge | 80/0/0 / 63/0/0; sentinel0 |
| Production terrain_hd_republish | 4/0/0 |
| RES documents, включая бывший FAIL799 и Save As | 28/0/0 |
| Backup manager / production root probe | 12/0/0 / 12 assertions PASS |

Required suites: FAIL0/SKIP0. Исторические внешние32 SKIP test-tails не переносились
в PASS и не входят в этот свежий набор. Backup-root probe подтверждает прежний путь
обычного режима, частные временные roots checks/Qt test-mode, разные process generations
и explicit env override. Пользовательские backup roots до/после совпали по существованию,
составу, размерам и mtime_ns; это inventory-проверка, не новое полное SHA хеширование42.5GB.
Native save tests получают собственные temporary backup roots.
Evidence: [регресс](../../build-tests-138-final/regression-summary138.json),
[HD/799/backup tests и root probe](../../build-tests-138-final/extra-checks.json),
[backup inventory до](../../build-tests-138-final/user-backups-before.json) / [после](../../build-tests-138-final/user-backups-after.json).

### Миниатюры против установленной137

137 EXE до замены `cf69724878349614e4540cbabe15e7fe52df9adb26214affca1d13a01ab50343`.
Benchmark связан с её production objects и production138; одинаковая clipboard isolation.
Пять чередующихся пар,80 measurements,8/8 ready,unavailable0; cold calls8,warm/repeat0.
Медианы fill_ms; допуск ≤10% **или** ≤+15ms:

| Набор | Состояние | 137ms | 138ms | Δms | Δ% | Результат |
|---|---|---:|---:|---:|---:|---|
| characters-unmo | cold | 87.609 | 88.837 | +1.228 | +1.40 | PASS |
| characters-unmo | disk-warm | 30.462 | 28.469 | -1.993 | -6.54 | PASS |
| characters-unmo | repeat | 5.082 | 5.225 | +0.143 | +2.81 | PASS |
| characters-unmo | repeat-after-disk | 5.262 | 4.937 | -0.325 | -6.18 | PASS |
| equivalent-buildings-stbu | cold | 30.653 | 30.923 | +0.270 | +0.88 | PASS |
| equivalent-buildings-stbu | disk-warm | 10.125 | 9.954 | -0.171 | -1.69 | PASS |
| equivalent-buildings-stbu | repeat | 6.117 | 5.703 | -0.414 | -6.77 | PASS |
| equivalent-buildings-stbu | repeat-after-disk | 5.747 | 5.904 | +0.157 | +2.73 | PASS |

8/8 допусков пройдены; максимум+2.81%/+1.228ms. [Замер](../../build-tests-138-final/gallery-summary138.json), [raw](../../build-tests-138-final/gallery-raw138.json).

### Staging и постоянная установка

Свежая `build-staging-138-final`:127 файлов, добавлений0/удалений0 против staging137;
изменены EXE,оба runtime,VERSION,Readme и один текущий CHANGELOG с тестовыми заметками.
Перед finalizer сверены tested/staged EXE SHA и staged version. Finalizer и его
внутренний distribution validator PASS; после финализации staging не запускалась/не менялась.
Manifest SHA256 `2cf7298e513d6d1b0300f1d63e5fb359996c07bb49bac432efa3e67a3fa2349f`.
Постоянная установка: [EI-Mapper-Ataman-2-first-test](../../artifacts/EI-Mapper-Ataman-2-first-test).
До записи все127 managed SHA137 совпали с её manifest. Backup7 файлов (6 changed+manifest)
проверен; atomic adjacent copy/File.Replace с ненулевым displaced backup, manifest последним.
После записи127/127 SHA138 верны. **23357 пользовательских файлов,
включая настройки/карты/кэши, неизменны по SHA до/после установки и после live openings**.
Все9 ранее существовавших rollback generations сохранены; rollback этой138 также сохранён.
Finalizer на постоянной установке не запускался.
Backup: [rollback137](../../artifacts/rollback-137-to-138-final),
[точные файлы и SHA до/после](../../artifacts/rollback-137-to-138-final/rollback-files.json),
[package receipt](../../build-tests-138-final/package-receipt.json),
[install receipt](../../build-tests-138-final/install-receipt.json).

## Свежая пересборка и предварительная проверка

Текущий live HDPack и14 MPR совпали с expected SHA отчёта138; созданы новые14 входных
копий, прежние candidates не использовались как входы. Обе source archives проверены
до/после сборки. Установленная138 тем же `--terrain-hd-republish-check`/`mode:build`
создала новые MPR и отдельные atlas RES в `build-tests-138-final/rebuilt/` и
`build-tests-138-final/rebuilt-remaining/`.
Все14 новых MPR совпали побайтно с ранее проверенными candidates138, новые MMP/profile/
semantic payloads тоже совпали; recovery evidence отделён от pixel/profile identity.
Для каждой карты заново проверены все MPR members: namespace имен MP/SEC,
только textureSize/tileSize MP и semantic binding изменены; SEC payloads, прочие members,
классы, анимации/frames и данные воды неизменны.
Девять MPR получили штатный embedded semantic member из прежнего RES;
все13 существовавших полей сохраняют arrays/dimensions/random seed и данные вне binding.
41 исходная карта и14 staged candidate открыты fresh MainWindow/CView138 до live замены:
profile/palette/semantic, камера и land/overlay hashes подтверждены.
Проверочные MPR отделены в `candidate-maps/` от отдельных atlas RES, чтобы реальный
resolver использовал именно общий candidate HDPack. В actual live открытиях используются
прежние owner ResourceRoots/profile; файл настроек владельца сохранён по SHA.

Общий candidate HDPack создан production `CResArchiveDocument::buildWithPublishedMembers(...,false)`
из fresh138 objects; все1222 прежних members сохранили точные payloads,
encoded names и timestamps, опубликовано222 members, добавлено222.
Staged archive production parse и readback PASS. Никакие старые atlas namespaces не удалены.
[Batch fixture](../../build-tests-138-final/rebuild-fixture.json),
[14 receipts](../../build-tests-138-final/rebuilt-results.json),
[MPR proof](../../build-tests-138-final/candidate-mpr-proof.json),
[pack proof](../../build-tests-138-final/pack-build-receipt.json),
[candidate actual audits](../../build-tests-138-final/candidate-audits.json).

Первый общий запуск был внешне прерван обновлением приложения Codex около09:48,
во время zone3obr, после8 завершённых карт. Он не является полным PASS.
Сопровождающий удалил stale UI lock мёртвого PID32104. При продолжении заново сверены
AGENTS/HEAD,127 installed managed SHA,324 protected live hashes и14 копий входных MPR;
всё совпало.8 готовых результатов fingerprint-verified и переиспользованы без пересборки;
оставшиеся6 завершены отдельным запуском с exit0 и nativeAttempts=0. Объединённые
14 candidates после этого прошли полную проверку данных/публикации. Уже успешные
неизменные regression/benchmark/установка не повторялись.
[Resume guards](../../build-tests-138-final/resume-guards.json),
[interrupted incremental receipt](../../build-tests-138-final/rebuilt-results-interrupted.json),
[remaining6 receipt](../../build-tests-138-final/remaining-results.json).

## Живые замены, backup и откат

Backup всех14 исходных MPR и целого HDPack проверен до первой замены.
Перед **каждой** заменой: host process guard (нет game.exe/EIMapperEndurance.exe/компонентов
постоянной установки), expected live SHA, backup SHA, adjacent temp SHA;
atomic replacement с displaced оригиналом, немедленный SHA readback.
Сначала HDPack, затем14 MPR; manifest receipts сохранены. Общей атомарности15 файлов нет;
при отказе предусмотрен hash-owned rollback MPR, затем HDPack.
Откат не понадобился, команды подготовлены и проверены без выполнения live отката.

HDPack до: `b3b123dde10513cf6dcc40d60ff4a9e21177017a51989b991c2445eb3f0868cf`, 1270745195 bytes (1211.88MiB).
HDPack после: `f40c71c16370e792faf59ec18f80eea1bf5c7f4ac6cde8f7741c48e7d15ea29a`, 1769076570 bytes (1687.12MiB).
Прирост: 498331375 bytes (475.25MiB).
Backup папка: [live-backup-20261011](../../build-tests-138-final/live-backup-20261011).
Файлы `имя-карты.mpr` и `HDPack_Terrain.res` внутри неё — точные original copies;
`displaced/` хранит дополнительный original каждой atomic replace.

Готовая команда отката (при закрытых игре/редакторе):

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "C:/GOG Games/maped/ei-mapper-ataman/build-tests-138-final/live-backup-20261011/rollback.ps1"
```

Скрипт сначала проверяет весь набор SHA/backup/process guards, затем восстанавливает
**14 MPR, после них HDPack**; при любом последующем чужом SHA останавливается.
Создаёт owner-token UI lock и adjacent temp, сохраняет displaced файлы, проверяет readback.
PowerShell syntax parse PASS. Вызовы с `-ValidateOnly` до первой замены и после применения PASS:
проверены реальные backup/live hashes, ни один live файл не записан.
[rollback script](../../build-tests-138-final/live-backup-20261011/rollback.ps1),
[rollback hashes](../../build-tests-138-final/live-backup-20261011/rollback-files.json),
[replacement receipt](../../build-tests-138-final/live-replacement-receipt.json),
[rollback preflight](../../build-tests-138-final/rollback-before-validate.out),
[rollback validation](../../build-tests-138-final/rollback-validate.out).

### SHA MPR до/после

| Карта | SHA256 до (backup) | SHA256 после | bytes до→после |
|---|---|---|---:|
| basegipat | `d6c3445f22d5fd50bc2a53498cdf616e0c06d07cd1cffc0c5ee00dd3186bb1ff` | `ebe9be34b478072d0bba5f8cd0dd83cce35d93ab8a2e42ca9bfc6194049d55f6` | 209071→212405 |
| zone1 | `07cf12287dc8a911abad943e819ace2f9cacca47532b307f975680fdfce0b7cc` | `96866411d850366446ba7d982ca5e8ffb42c1267363397d41cf56ea95af80fc0` | 1545463→1551703 |
| zone11 | `0ce97df95e1d5d9f0a422976a5049b81c8a1bd6d157f823d03bdfaf16b82a415` | `7a7db0104429cbc88d4b6cb082a8660cd601e578ed9adc71728b9c53960b5376` | 2184060→2209202 |
| zone12dun2 | `f4eec4ceb91fb29d90f11f6c1de57cd2b70ebba105909bf00efd7aa0ed5c0498` | `e48badcd37348c5c573f0f68a62487507b79c96384f1af177dc74fc2ec940505` | 305612→308322 |
| zone13 | `1cd29ff01b50e2a5ac3c6d8686dc828c6780e0d4c94786139b958692f8c5ff18` | `8b97c235b5b41c4367615da8f84f4e75e9f807b21ed990fe1bf7fa053d2bc7c5` | 1985940→2004330 |
| zone14 | `aa0848c0a47f0521330b7f703290596522c8d4b1b8b654c7e9201ae52e25d5b6` | `218f1ec9f107d8e89795d1a455407fc9f256a8890a3fc01de4cf9dba096b3c42` | 545292→552914 |
| zone18 | `c25f0f97318cecb27565ce44b28f258099939916a7cf73704ff91a4abda2ee0e` | `1e54df7aee0c3622b369d2fa869fb849e9d594275bdb1ce4c91df64d2fea9130` | 4251868→4279506 |
| zone19 | `6b7fe9de0c442e0b2c01057280bd2b767be578c0dbe5509131b74ce5077814e4` | `fef8e55fc945384b209d5a1edd03da5bc35e3ab3a41b8788f8b8f4ae16447c68` | 3047218→3047474 |
| zone3obr | `f567b860896d6084c2669046383d90dcbc59ae079b119e3dd21ff01d33e83caf` | `89f16b89a5a86a999f095c665ada6f0d8b2075190a6cd8fc212f465a91aabaaa` | 1509130→1516810 |
| zone4 | `4af7620ef1960e029b57835fe4f05e9d72dceca01e908867e05bbc97b3f275a3` | `e4d1a93fca121638983acc61fc275bb590e7eca652175e0c6c6d0b6abbaf15d9` | 1895371→1915073 |
| zone6 | `0676f3f559f4623b344b2773bdbae97dce02ec508965e6ba8cfba256613e3535` | `4e82423c43624f90eaffd249339611fbe61bc5811d200631e04ebc6a46fe09d5` | 1056171→1069025 |
| zone7 | `8e4a1dbbe6a373055e3f6c58eecd86d59aeea3faeb90366f19d11fd8ed9b36d1` | `21806ec7f5789744f812bb4aeda56e95b14d54c1b8ee6190134e013a082b9162` | 2671771→2692113 |
| zone9 | `d4b6914f190622a8d43bb53cc3df310b2f0ce33594ad5d952136708f584144a2` | `3b5aef762673532628bdab76b114a97d4f7188b699da0bd8056d2de64d435e6a` | 1733977→1738489 |
| zonemainmenunew | `696c740368c6d90ccd2f210eb3f39bd3b325416b5eab397d30d2400ea5bfd023` | `a7b32b2f93b9ca1e102a579ec290eae870e8355909e9d08d8272e8e99525fefa` | 246693→228229 |

### Карты и разрешение

| Карта | slots/pages | HD stock | gen256 | SD detail128 | native128 / carrier256 | Atlas MiB до→после | Palette land/overlay | Semantic |
|---|---:|---:|---:|---:|---:|---:|---:|---|
| basegipat | 448/7 | 445 | 0 | 3 | 0 / 3 | 4.67→18.67 | 81/7 | exact |
| zone1 | 960/15 | 672 | 76 | 212 | 0 / 212 | 10.00→40.00 | 81/7 | exact |
| zone11 | 512/8 | 455 | 14 | 43 | 0 / 43 | 5.33→21.33 | 81/7 | exact |
| zone12dun2 | 256/4 | 181 | 15 | 60 | 0 / 60 | 2.67→10.67 | 81/7 | exact |
| zone13 | 704/11 | 490 | 41 | 173 | 0 / 173 | 7.33→29.33 | 81/7 | exact |
| zone14 | 512/8 | 462 | 12 | 38 | 0 / 38 | 5.33→21.33 | 81/7 | exact |
| zone18 | 576/9 | 12 | 9 | 555 | 448 / 107 | 6.00→10.00 | 81/7 | exact |
| zone19 | 960/15 | 45 | 100 | 815 | 128 / 687 | 10.00→36.00 | 81/7 | exact |
| zone3obr | 1472/23 | 793 | 368 | 311 | 0 / 311 | 15.34→61.34 | 81/7 | exact |
| zone4 | 1024/16 | 618 | 206 | 200 | 0 / 200 | 10.67→42.67 | 81/7 | exact |
| zone6 | 1216/19 | 632 | 334 | 250 | 0 / 250 | 12.67→50.67 | 81/7 | exact |
| zone7 | 960/15 | 628 | 179 | 153 | 0 / 153 | 10.00→40.00 | 81/7 | exact |
| zone9 | 1600/25 | 700 | 474 | 426 | 0 / 426 | 16.67→66.67 | 81/7 | exact |
| zonemainmenunew | 384/6 | 238 | 79 | 67 | 0 / 67 | 4.00→16.00 | 81/7 | не было, сохранено |

Итого11584 slots/181 pages:6371 stock HD,1907 regenerated256,3306 SD detail128
(576 native128,2730 carrier256). Profile и палитра активны14/14;
13 сохранённых semantic fields exact/активны, у zonemainmenunew field не было и не добавлялось.
Actual installed renderer открывал256 px атлас каждой из14 карт; Zone18/Zone19
сохраняют смешанные native256/128 страницы. Это проверка редактора, не game/GPU residency PASS.

После замены **41/41** карта Dejavu открыта EXE из постоянной установки138, без save;
загрузка/deferred editor phases завершены, sentinel0, MPR unchanged.
Затем41/41 точный production-object MainWindow/CView audit:
palette/profile/semantic fields, SEC/classes/animations/water data, land/overlay hashes,
camera и physical slots не деградировали. Для остальных27 MPR не менялись вообще.
[Все41 installed openings](../../build-tests-138-final/live-installed-openings.json),
[41 точный audit](../../build-tests-138-final/live-audits.json),
[41 baseline audit](../../build-tests-138-final/before-all-audits.json),
[apply receipt](../../build-tests-138-final/live-apply-receipt.json).
Всего309 прочих контролируемых игровых файлов (включая Base,
Dejavu MOB/RES-companions и JamaisVu source HDPack) неизменны по SHA.
Полный список: [protected baseline](../../build-tests-138-final/live-before.json).

## Что увидит владелец и ограничения

При открытии14 перечисленных Dejavu карт установленной138 будут активны их перенесённые
profile/палитры и сохранённая семантика; восстановленные stock и reproducible transitions
используют256 px. Карты Base остались легаси. Команда
«Ландшафт → Дополнительно → Пересобрать атлас карты в HD…» включена в138.
В общий пакет добавлены новые namespaces; старые namespaces иstock не удалены.
Скорость миниатюр укладывается в согласованный порог.

SD carrier256 увеличивает размер ячейки, **сохраняет детализацию128**, не восстанавливает
неподтверждённый HD. Неоднозначные/непродуцируемые источники остались SD. Детали regenerated
переходов могут отличаться при сохранении крупной формы/ownership; подтверждённые картинки
5 карт из [отчёта138](ATAMAN_TERRAIN_HD_REPUBLISH_138_20261010.md) применимы к тем же
проверенным MMP payloads. Дополнительный исторический чёрный Qt framebuffer capture
не используется как визуальный PASS. Игровая и художественная приёмка здесь не выполнялись.
Zone9:25 pages/1600 slots,66.667MiB BC1 или533.333MiB RGBA+mips;
общий HDPack теперь1687.12MiB. Требуется проверка в игре с текущим compressed
renderer; проверки редактора/архива не доказывают game memory budget,GPU residency или
корректность старого RGBA/assembly пути. На отдельной копии владелец может проверить
покраску/Undo/Redo/save/reopen, затем визуальные переходы/воду и gameplay.

## Ошибки подготовки и точность результата

Собственные ошибки не объявлены PASS: unquoted qmake path в pack/root helpers;
строгий parser `--version` не учитывал sentinel строку; ранний extra runner встретил
занятый собственным регрессом UI lock. Они исправлены; ранее успешные неизменные suites
не повторялись. При первом installation attempt активный runner использовал старый
вызов atomic helper после изменения его mandatory arguments: helper отказал **до**
первой File.Replace. Старый VERSION137/manifest/целевой CHANGELOG SHA подтвердились,
rollback handler не имел изменённых bytes для восстановления. Точные backups сохранены
и повторно проверены, затем установка полностью выполнена исправленным runner.
Никакие функциональные FAIL/SKIP/таймауты не подменены PASS.
После завершения6 карт собственный MPR verifier ошибочно требовал равное число members;
он остановил публикацию до live изменений. Причина установлена в штатном коде138:
RES semantic переносится в embedded MPR при prepareTerrainHdPublication.
Проверка исправлена на точный разрешённый набор members и неизменность semantic arrays/seed;
все14 результатов проверены без повторной пересборки. Затем занятой UI lock другого
живого benchmark PID34700 остановил pack helper/preflight до запуска; затем lock занимали
mixed-edge polish PID32436/35168. Чужие процессы/lock не изменялись,
работа продолжена после его освобождения. Первый candidate-zone1 открывался с соседним
отдельным RES: exact profile/semantics PASS, но expected общий archive gate отказал.
Проверочные MPR изолированы от соседних RES; проверка общего архива выполнена заново,
уже успешные неизменные baseline openings и basegipat combined opening сохранены.
Эти отказы подготовки не объявлены PASS.
