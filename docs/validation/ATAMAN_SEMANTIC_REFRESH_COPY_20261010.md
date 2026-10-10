# EI-SEMANTIC-REFRESH-COPY-20261010 — семантика восстановлена на копиях

Дата: 2026-10-10. **COPY VALIDATION PASS. К живым картам ничего не применено.**
Поручение владельца через Claude: «да, сделай семантику явно на копии с проверкой».
Ветка main; база входа и снимка программы136:
`b6b3be3cd3b3710613ae848182015f14f1240931`.

Проверены все **79 MPR** из `C:/GOG Games/PZ/MODS/Dejavu/Maps` и
`C:/GOG Games/PZ/Base/Maps`: **41 Dejavu + 38 Base**, **5351 SEC**, **1369856 клеток**.
Обнаружены **две** карты с отклонённой сохранённой семантикой: **Zone2 и Zone3obr**.
Обе копии имеют новую привязанную семантику v2 в MPR, активную палитру,
точное соответствие всех используемых физических тайлов и побайтно стабильное
повторное сохранение. В живом применении изменились бы только два MPR.

## Границы и происхождение проверки

- Проверены актуальные AGENTS.md, CONTRIBUTING.md, PROJECT_CONTRACT.md,
  [HDPACK_RESTORE_APPLY](ATAMAN_HDPACK_RESTORE_APPLY_20261010.md) и
  [семантика122](ATAMAN_TERRAIN_HEIGHT_RES_SAVE_FIX_122_20261007.md).
  SHA256 AGENTS.md: `f4b7fdd5ea82fcdf43d002d23d64ec36be853650f054edf70376527cd6432ec3`.
- Исходники программы, VERSION, установленная136 и чужие изменения параллельной
  EI-TERRAIN-MIXED-RES-ATLAS-137 не изменялись этой задачей. Сеть, установка
  зависимостей, новые чаты, подагенты, интерактивный редактор и игра не использовались.
  Системный буфер не открывался и не блокировался; clipboard/UI-widget suites не запускались.
- Обвязка находится только в собственных `build-release-semantic-refresh/` и
  `build-tests-semantic-refresh/`. `git archive` снял source/assets с указанного HEAD;
  source совпадает с `d832153bf0711f4ca00fea8979e081835b41c63f`.
  Программа собрана перелинковкой **271 неизменённого объекта136** из ранее
  зафиксированных private object copies; добавлены только диагностический bootstrap
  и перенаправление backup/QSettings в свои папки. Это не полная перекомпиляция
  программы и не замена установленного EXE.
- Финальный EXE обвязки `semantic_refresh136.exe`, `--version`: **136**;
  SHA256 `157bc24922b670d4f91457e2bde6b66e10c00243ca540c0aee3a2285d0bbcdac`.
  [Provenance](../../build-tests-semantic-refresh/provenance.json),
  [object identities](../../build-tests-semantic-refresh/object-identity.json).
- Полное открытие карты выполнялось оригинальным main/CView136 на отдельном
  невидимом WinSta0 desktop, с desktop OpenGL, изолированными settings/temp/backup,
  выключенным autosave и только под `build-locks/ui-tests.lock`.
  Занятый lock137 ожидался; чужой lock/процессы не удалялись и не завершались.
- Навыки evil-islands-modding / evil-islands-private-engine-reference недоступны;
  приватные движковые исходники не использовались. Исследован обычный код маппера
  и явно разрешённые игровые данные.

## Корпус и причины

[Полная квитанция корпуса](../../build-tests-semantic-refresh/corpus-result.json),
[список входов](../../build-tests-semantic-refresh/corpus-input.json).

| Категория | Dejavu | Base | Всего |
|---|---:|---:|---:|
| Карты, успешно прочитанные штатным CMprDocument | 41 | 38 | 79 |
| Есть сохранённый semantic-документ в MPR или активном RES | 17 | 0 | 17 |
| Привязка и штатное восстановление сохранённых массивов проходят | 15 | 0 | 15 |
| Сохранённый документ отклонён | 2 | 0 | 2 |
| Сохранённого semantic-документа нет | 24 | 38 | 62 |

Отсутствие документа у 62 обычных карт — отдельная категория, не отказ старого
документа и не PASS восстановления сохранённых вершин. На них миграция не выполнялась.
У всех 17 найденных документов проверены namespace, размеры, map/topology hash,
atlas/profile identity и загрузка land/overlay через
`CTerrainEditModel::restoreVirtualMaterialFieldState`. Для v2 профиль канонизирован
штатным `fromBytes/toBytes`; для v1 сохранена строгая привязка к полному MPR SHA.
Привязка профиля к реальным декодированным изображениям проверена native resolver136.

Неотклонённые документы: basegipat, bz10k, zone1, zone11, zone12, zone12dun2,
zone13, zone14, zone18, zone19, zone4, zone6, zone7, zone8, zone9.
Их карты и документы не переписывались. Устаревший локальный override Zone7
не применяется поверх принятого exact embedded-профиля и не изменялся.

| Карта Dejavu | Runtime namespace | Доказанная причина |
|---|---|---|
| Zone2 | `zone2` | v1 `mprSha256` не равен текущему полному MPR SHA; atlas/profile SHA совпадают, документ декодируется, validateBinding отказывает |
| Zone3obr | `zone3obr_0bcba8177f9a` | Та же причина: неверный полный MPR SHA v1; atlas/profile SHA совпадают, документ декодируется, validateBinding отказывает |

| Карта | SHA сохранённого старого MPR в v1 | SHA фактического исходного MPR |
|---|---|---|
| Zone2 | `c96290af87dbf0386151862abe41552ede39541cc62c420c3b8809cf24c4c2a9` | `6c42b54bba0e94578d9d2c50b31947381014edc49785335a444c2edf1228c0e3` |
| Zone3obr | `af37febcb286787135c6166f851858df2cea7959d52965151c02f9b4b105ca52` | `7bb28272e6c114afb2ef0bcc148585c85a4ff5a5d19446c94ce556e5b0c79ede` |

Старые члены:
`ei_terrain_semantics_zone2_4ec9cef40f80630c.json` и
`ei_terrain_semantics_zone3obr_0bcba8177f9a_1267aa3ca14792df.json`.
При начальном полном открытии обеих копий land/overlay semantic arrays имеют
длину **0/0**: это действительный отказ восстановления, не только сравнение хешей.
Старые MPR, соответствующие записанным SHA, отсутствуют в этой задаче; какие
именно прежние правки сделали их SHA устаревшими, не установлено.

## Штатное восстановление и запись

Каждая карта имеет собственную папку:
[Zone2](../../build-tests-semantic-refresh/zone2/),
[Zone3obr](../../build-tests-semantic-refresh/zone3obr/).
В ней находятся неизменённые `before/<map>.mpr` и MOB, рабочий `<map>.mpr`,
полные копии нужных `res/HDPack_Terrain.res` и `base-res/textures.res`,
снимки первого/второго save, backup и квитанции. Соседние MOB не загружались
и не сохранялись; их исходные и скопированные байты проверены отдельно.
Прочие ресурсные зависимости читаются из ранее созданного private common-res.

Восстановление вызывает оригинальный
`CTerrainEditModel::initializeVirtualMaterialField` с
`RecoverDeterministically` и действующим каталогом рабочего атласа CView.
Это тот же алгоритм, который используется при отсутствии полного поля:
сигнатура каждого packed-тайла учитывает его поворот и координаты сетки;
материал вершины определяется голосами соседних углов. При равенстве выбирается
меньший ID. **В этих двух картах ни конфликтов голосов, ни равенств нет.**
Solver/пересчёт физического атласа/покраска/Undo/Redo не вызывались.

Важное ограничение136 установлено до подготовки кандидатов: прямой вызов
`CView::saveChangedLandscapeDocument(..., true, false)` отказывает при **0 SEC patches**
(`terrainChangedSaveTransactionIsUsable` не принимает пустую физическую транзакцию).
Обычное сохранение неизменённой карты также не добавляет новое поле автоматически.
Это не ошибка привязки новой семантики и не основание делать фиктивную правку высоты.
[Квитанция отказа](../../build-tests-semantic-refresh/zone2/plain-zero-patch-failure.json),
[диалог](../../build-tests-semantic-refresh/zone2/refresh.json.dialogs.jsonl).

Кандидаты записаны **неизменёнными production-функциями обычного MPR save136**:

1. `CView::buildTerrainSemanticFieldMember` готовит semantic v2 и вызывает
   `STerrainSemanticFieldDocument::embedInMpr` на исходных MPR bytes.
2. `commitValidatedMprBytes` проверяет источник/candidate, делает проверенный
   backup и атомарно сохраняет целый MPR. `backupRootOverride` указывает только
   на свою копию. Phase **complete** для всех четырёх коммитов копий.
3. `CView::adoptCommittedLandscapeBaseline` повторно читает файл и восстанавливает
   семантику. Затем отдельный новый процесс штатно открывает этот MPR.

Это явная semantic-only транзакция через обвязку существующего ядра сохранения,
а не проверка пользовательской команды Ctrl+S с нулём физических правок.
Код UI/save136 не исправлялся. Экспорт Modern draft, способный включить RES,
для обхода ограничения не использовался.

## Результаты на копиях

| Проверка | Zone2 | Zone3obr |
|---|---:|---:|
| Сетка поля | 81×97 | 177×161 |
| Сохранены и точно восстановлены land / overlay vertices | 7857 / 7857 | 28497 / 28497 |
| Земля: все активные клетки декодированы и 4 угла совпали | **7680/7680** | **28160/28160** |
| Overlay: все активные клетки декодированы и 4 угла совпали | **224/224** | **5130/5130** |
| Неизвестные сигнатуры / несовпадающие клетки | **0 / 0** | **0 / 0** |
| Конфликтующие вершины / равенства голосов / неверный выбор | **0 / 0 / 0** | **0 / 0 / 0** |
| Неустановленные land vertices | **0** | **0** |
| Overlay vertices без активного покрытия, штатный unset | 7595 | 22774 |
| Активная палитра land / overlay choices | **81 / 7** | **81 / 7** |
| Physical atlas slots | 1024 | 1472 |
| Новый semantic: native validateBinding + restore | **PASS** | **PASS** |
| Отдельный fresh reopen: массивы равны embedded-члену | **PASS** | **PASS** |
| Resave без правок, точный SHA/bytes первого save | **PASS** | **PASS** |

Проверено **41194/41194** активных клеток обоих слоёв, **164776 углов**.
Неоднозначные клетки: **0**, список пуст. Никакое разрешение неоднозначности
или изменение физической клетки не понадобилось. Sparse overlay вне покрытия
остаётся `INT_MIN` и не является отсутствующим материалом активной клетки.

Сравнение со старым отклонённым документом: land массивы совпадают полностью.
В overlay восстановление убрало только прежние значения у неиспользуемых вершин:
Zone2 **1** вершина `(0,0)`; Zone3obr **13** — `(43..53,0)`, `(176,33)`, `(176,34)`.
Теперь они unset, поскольку нет активных физических overlay-тайлов, голосующих
за них. На активных overlay-клетках расхождений нет. Полный список с old/new ID
в [byte comparison](../../build-tests-semantic-refresh/byte-comparison.json).

V2 формата122 **не хранит полный mprSha256**: поле отсутствует штатно, а не
подменено фиктивным SHA. Привязка проверяет namespace/dimensions,
`terrainTilesSha256` (MP/SEC/material topology), atlas и canonical profile.

| Карта | Неизменный topology SHA | Atlas SHA | Canonical profile SHA |
|---|---|---|---|
| Zone2 | `68137e08dc22b02fafec1efb712e49e0adf2407f0381ef0817d3ddfcb2713c5c` | `3b8deab3a188943074f239a45f2dd38da855a83bcea55b0aba0ae6703be2a332` | `549b62c038f66d09b722e61ef599197f97b5591fd506996848d50d8ce840160f` |
| Zone3obr | `59aaf7b731ce979aca40c768a8331f3d160c7b04606208feabcb36828a47322a` | `c582a9db3854e293fc7d01f1b2c8e4fbf9e375504119217bcefd1e41b28e2964` | `a2e0a0c2bad891d65ca753848c457bda6dd58c4d794e9cf871ba7531ba97267c` |

[Zone2 refresh](../../build-tests-semantic-refresh/zone2/refresh.json),
[reopen](../../build-tests-semantic-refresh/zone2/reopen.json),
[resave](../../build-tests-semantic-refresh/zone2/resave.json);
[Zone3obr refresh](../../build-tests-semantic-refresh/zone3obr/refresh.json),
[reopen](../../build-tests-semantic-refresh/zone3obr/reopen.json),
[resave](../../build-tests-semantic-refresh/zone3obr/resave.json).
Все **6/6** стадий: exit0, без таймаутов и SKIP. **4/4** backup хеши проверены.

## Какие байты меняются

| Показатель | Zone2 | Zone3obr |
|---|---:|---:|
| Исходный MPR, bytes | 351908 | 1490500 |
| Первый и второй save, bytes | 357994 | 1509130 |
| Добавлено bytes | **6086** | **18630** |
| Прежние члены совпадают payload/name/offset/size/timestamp | **31/31** | **111/111** |
| Добавленный член | `ei_mapper_terrain_semantics.json` | `ei_mapper_terrain_semantics.json` |
| Его JSON payload bytes | 4868 | 12680 |
| Его offset | 351920 | 1490512 |
| Старый → новый active table offset | 350768 → 356800 | 1484624 → 1503200 |
| Active table bytes | 682 → 704 | 2442 → 2464 |
| Names buffer bytes | 458 → 490 | 3434 → 3466 |
| Изменённых bytes в общем старом префиксе | **4** | **4** |

У обеих карт отличаются только байты заголовка с абсолютными offsets
**4, 8, 9, 12** (member count, table offset, names length) и добавленный хвост.
Все исходные bytes начиная с offset16 до конца прежнего файла остались на месте
побайтно, включая старую таблицу и names buffer, теперь сохранённые внутри
append-only тела. В хвост добавлены выравнивание, semantic JSON, новая таблица
с native hash chains и names buffer с ещё одним именем. Старые имена/кодировка,
offsets, timestamp, gaps, unknown/trailer bytes сохранены.

**142/142** прежних MPR payloads, включая **140 SEC + 2 MP**, побайтно прежние.
Следовательно, SEC-ссылки, packed tiles/rotation, геометрия/высоты/нормали,
overlay, вода, материалы и анимационные MP records не изменены; перерисованных
клеток **0**. Внешние **2/2 MOB** тоже побайтно прежние. AI/объекты не пересчитывались.

Полные хеши **2/2 HDPack copies** и **2/2 Base textures copies** неизменны.
Старые semantic v1 в RES, все MMP/profile payloads и HDPack не переписывались.
Публикация ресурсов не входила в выполненный save.

| MPR-кандидат | SHA256 первого = второго save |
|---|---|
| `build-tests-semantic-refresh/zone2/zone2.mpr` | `2b3eba8ec14e9959cf5f166ee587abc5de6492926d7eeb1794867e9e3c5255fd` |
| `build-tests-semantic-refresh/zone3obr/zone3obr.mpr` | `f567b860896d6084c2669046383d90dcbc59ae079b119e3dd21ff01d33e83caf` |

[Independent byte comparison](../../build-tests-semantic-refresh/byte-comparison.json).
[Final guard](../../build-tests-semantic-refresh/final-guard.json): **83/83** живых
исходных файлов неизменны (**79 MPR + HDPack + Base textures + 2 MOB**);
установленный EXE136 также имеет прежний SHA.

## План применения — не выполнялся

Изменятся только:

- `C:/GOG Games/PZ/MODS/Dejavu/Maps/zone2.mpr`;
- `C:/GOG Games/PZ/MODS/Dejavu/Maps/zone3obr.mpr`.

RES/HDPack, MMP/profile payloads, MOB, Base Maps, установку и код не заменять.
Legacy semantic в RES оставить: embedded v2 MPR имеет приоритет.

1. Получить отдельное поручение на live replacement. Под ui-tests.lock проверить
   отсутствие редактора/игры, использующих карты, и повторно проверить source SHA
   обоих MPR из таблицы выше. Проверить кандидаты по полным SHA и нужные atlas/profile
   bindings. Если карта/ресурс/профиль изменились, остановить применение этих копий
   и подготовить новый согласованный набор, сохранив новую работу владельца.
2. Ресурсная база этих кандидатов — оригинальный HDPack:
   **775817323 bytes**, SHA `2d20abdd3a753cd00671c84798a2bda030b7f192b71de58e0e09860f5bfe488c`.
   Проверить также Base textures по квитанции live-before. **Не совмещать слепо
   с будущим HDPack restore v2.** Embedded MPR v2 имеет приоритет, поэтому миграция
   одних legacy semantic в RES не обновит его atlas/profile binding. После смены
   физических изображений потребуется новая проверка/перепривязка embedded поля.
   Особенно это относится к Zone2. Задачи137 и HDPack restore этим отчётом не закрыты.
3. Создать отдельную датированную резервную папку вне живой установки и Git;
   скопировать оба исходных MPR до первой замены, проверить SHA/размеры и сохранить
   receipt с исходными/candidate/resource identities. MOB можно включить в backup
   как сопутствующий снимок; заменять его не нужно.
4. Подготовить оба соседних `.tmp` на том же томе, скопировав именно проверенные
   MPR-кандидаты; проверить SHA, native parse, semantic binding и payload conservation.
   Повторно проверить source/process guards перед заменой.
5. Заменять в порядке **Zone2 → Zone3obr**, каждый через атомарный `File.Replace`
   с **не-null** displaced/backup path. Сразу после каждой замены проверить полный
   live SHA/readback. Две замены не считать одной атомарной транзакцией: при сбое
   второй откатить уже заменённую первую при точном candidate SHA.
6. Свежим процессом открыть фактические live пути/активные ресурсы, проверить
   embedded binding, точные массивы, активную палитру и все 41194 клетки.
   Снова сравнить SEC/MP/MOB и полные RES SHA с backup baseline. Resave stability
   выполнять на новой контрольной копии после применения, чтобы проверка сама
   не добавляла незапрошенные live записи. Вручную редактор/игру принимает владелец.
7. Откат: только при live SHA, равном соответствующему candidate, и отсутствии
   использующих процессов. Проверить backup, подготовить соседний temp с original,
   атомарно заменить с сохранением вытесненного кандидата и проверить original SHA.
   При посторонних новых правках **не затирать карту**: сохранить состояние и
   передать конфликт владельцу. RES не восстанавливать — он здесь не менялся.

Откат/применение живых файлов не выполнялись. План подготовлен; runtime/игровая
приёмка владельцем не подменяется проверкой байтов и скрытыми открытиями.

## Неуспешные попытки и оставшаяся граница

Первые два скрытых вызова plain changed-save остановились по 58-секундному timeout
без итоговой квитанции. Для Zone2 после добавления диагностики подтверждён модальный
отказ пустой SEC-транзакции; причина первоначального таймаута Zone3obr отдельно
не установлена. Затем один диагностический plain-save Zone2 вернул exit22 и оставил
исходную копию прежней. Эти результаты **FAIL**, не PASS.
Ожидания занятого lock137 не запускали редактор и не являются проверкой карты.
Одна сборка собственной обвязки не прошла из-за отсутствующего include полного
`SResArchiveNewMember`; исправлен только bootstrap, исходники136 не менялись.

Требуемые копии, field/cell binding, сохранность, fresh reopen и стабильность
проверены полностью через существующее production-ядро136. Обычная пользовательская
команда сохранения semantic-only без физических правок ограничена описанным gate;
это отдельный программный вопрос вне разрешённых изменений этой задачи.
Scoped-коммит содержит только этот отчёт; private игровые копии и обвязка не входят в Git.
