# EI-HDPACK-RESTORE-APPLY-20261010 — v2 подготовлен; применение остановлено

Дата: 2026-10-10. **Статус: BLOCKED BEFORE LIVE REPLACEMENT. Живой файл не заменён.**
Ветка main; база входа/неизменённых исходников обвязки `d832153bf0711f4ca00fea8979e081835b41c63f`,
VERSION 136. Подготовка: `1eb7d803ec55c56cbcd0ce0f05b8a6e1b0d96d37`,
[RESTORE_PREP](ATAMAN_HDPACK_RESTORE_PREP_20261010.md), [COMPARE](ATAMAN_HDPACK_COMPARE_20261010.md).
Параллельная EI-FINALIZE-136 завершила собственную передачу и продвинула main
до `c2e528834b1a0e7872887b5b79a2eadd41436018`; её изменения сохранены. Эта задача установку не изменяла.

## Решение перед заменой

Поручение владельца через Claude 2026-10-10 «Оба шага» явно разрешает data-only
перенос 12 embedded-профилей/10 semantic-документов и guarded replacement только
`C:/GOG Games/PZ/MODS/Dejavu/res/HDPack_Terrain.res`. Пересборка опубликованных карт
в HD — отдельный будущий шаг. В поручении также явно сказано: если без изменения
кода нельзя, остановиться перед заменой. Этот стоп применён по двум доказанным причинам:

1. **Mixed-resolution atlas не принимается живым редакторским pipeline.**
   Native resolver находит 68/68 exact profiles по namespace/physical slot count/decoded SHA.
   Но скрытые открытия bz10k/zone12/zone2/zone8 на v2 дают неактивные палитры и
   `terrain.atlas_profile_hash_mismatch`, затем отказ semantic restore. В
   [terrain_atlas_archive_binding.cpp:244](../../source/render/terrain_atlas_archive_binding.cpp#L244)
   `canReuseDecodedAtlasTiles` требует одинакового QSize каждого изображения.
   Здесь stock tiles 256², поздние tiles 128²: точная native identity не проходит этот
   дополнительный критерий. В [view.cpp:9611](../../source/render/view.cpp#L9611)
   поэтому отвергается prevalidated tile adoption и выполняется `fillTable`;
   повторный hash tile picker отличается от измеренного native full-atlas SHA.
   Logs: `prevalidatedTilesAdopted:false`, `prevalidatedBindingReused:false`,
   usable palette false. Это ограничение кода, а не ошибка миграции JSON.
   Менять SHA на hash ошибочного picker означало бы потерять требуемый exact native
   binding; уменьшать/заменять другие страницы запрещено контрактом v2.
2. **Zone2: уже существующая недействительная MPR-привязка semantic v1.**
   `ei_terrain_semantics_zone2_4ec9cef40f80630c.json` хранит
   `mprSha256=c96290af87dbf0386151862abe41552ede39541cc62c420c3b8809cf24c4c2a9`.
   Реальный Zone2 MPR:
   `6c42b54bba0e94578d9d2c50b31947381014edc49785335a444c2edf1228c0e3`.
   Именно этот SHA был в maps-native подготовки; нынешняя карта не менялась.
   Native `validateBinding` отвергает map binding после переноса atlas/profile SHA.
   Скрытая baseline-проба с оригинальным живым паком подтверждает: профиль/palette
   активны, semantic vertices не восстановлены. Автоматически подменять mprSha256
   без старой карты и доказательства соответствия authored вершин нельзя.

Дополнительно опубликованная Zone3obr уже имеет semantic binding failure:
baseline и v2 дают одинаковый активный профиль, но отвергают тот же semantic member.
Её MMP/profile/semantic payloads в v2 побайтно прежние. Эта связанная находка
записана как предсуществующее ограничение, данные не ремонтировались.

## Файлы и хеши

| Файл | Байты | SHA256 |
|---|---:|---|
| Живой до работы | 775817323 | `2d20abdd3a753cd00671c84798a2bda030b7f192b71de58e0e09860f5bfe488c` |
| Живой после работы — без замены | 775817323 | `2d20abdd3a753cd00671c84798a2bda030b7f192b71de58e0e09860f5bfe488c` |
| v1 | 1270745195 | `6d1911235f30e79e1fe98ee1ff9c3eaac7ac6c9a2bf5a779579f7eeaa69d55b5` |
| v2 кандидат | 1270745195 | `b3b123dde10513cf6dcc40d60ff4a9e21177017a51989b991c2445eb3f0868cf` |
| Резервная копия оригинала | 775817323 | `2d20abdd3a753cd00671c84798a2bda030b7f192b71de58e0e09860f5bfe488c` |

Кандидат: `C:/GOG Games/maped/ei-mapper-ataman/build-tests-hdpack-restore-v2/HDPack_Terrain.res`.
Backup: `C:/GOG Games/maped/ei-mapper-ataman/build-tests-hdpack-restore-v2/backup/HDPack_Terrain.res`.
Копия создана и проверена под `build-locks/ui-tests.lock`; host Win32_Process inventory
для game.exe/EIMapperEndurance.exe был пуст. Живой SHA проверен до/после копирования.
Соседний temp живого ресурса, его atomic replace и post-install reopen **не выполнялись**.

## Доказанная data-only миграция

Штатные `STerrainAtlasProfile::fromBytes/toBytes` и
`STerrainSemanticFieldDocument::fromBytes/toBytes` из неизменённых исходников 136.
`profileSha256` — SHA256 штатно сериализованного профиля; atlas hash — штатный
`CTerrainAtlasProfileInference::pixelSha256` с native physical slot images.

- Изменены ровно **12 profiles + 10 semantic documents**. В profile JSON меняется
  только `atlasPixelSha256`; в semantic JSON — только `atlasPixelSha256/profileSha256`.
  Parsed JSON вне этих ключей совпадает полностью; decoded land/overlay arrays
  совпадают до/после native roundtrip для всех 63 документов.
- Сохранены families/IDs/names, signatures/corners, thumbnails, allowedLayers,
  selectability, tile types, route owners, source aliases/provenance, weights и
  authored edits. Не менялись filenames/hash chains, topology/map hashes,
  namespace, dimensions, MP/SEC, повороты, вода и анимационные records.
- **1200/1200 прочих payloads равны v1**, включая **1091/1091 MMP** и 109 JSON.
  **236/236** восстановленных MMP имеют прежние имена/физические слоты и точный
  compressed suffix: mip≥1 равны прежним mip0…10, **2596/2596 уровней**.
  Для остальных страниц old→new — byte identity. Native old atlas hashes каждого
  из 12 профилей сначала проверены по оригинальным изображениям, затем новые —
  по v1/v2; никакие неизвестные пары не разрешены.
- RES: **1222 entries**, прежние порядок/NextIndex/timestamp/name metadata,
  name buffer **31191 bytes**, **1223 gaps / 14067 bytes** сохранены.
  JSON lengths не изменились (delta 0), поэтому offsets/tableOffset тоже как в v1.
  Все 1222 имени найдены проходом bucket/hash-chain; добавлений/удалений нет.

Квитанции: [archive-proof.json](../../build-tests-hdpack-restore-v2/archive-proof.json),
[native migration](../../build-tests-hdpack-restore-v2/members/native-results.json),
[native verification](../../build-tests-hdpack-restore-v2/verified/native-results.json).

| Перенесённый namespace | Physical slots | Old decoded atlas SHA256 | New decoded atlas SHA256 | New canonical profile SHA256 |
|---|---:|---|---|---|
| zone9 | 1152 | `2e3a9465ed10944bba31f3f39de8e40fd0ec81014206fa993906d2fca734116e` | `bebe7262558f097eba3bcb54de94682224400af826d05cbe9efda09e83d78aa5` | `87ed5d8118e396acaeb6f460f6fd22aafa1bf45973b8756f06016050bd08be0d` |
| bz10k | 448 | `0253fc15bc6eec4bd04db82bb033e8730975c8f2bc6b25268ee819b061b8acc1` | `1e0b2cd54c61b69dfd855934cf4f003ab2715026fad13dedca4d26a1e050101b` | `90efff2ce6c2119d568ed2826a180d4cfd87df3e48f6f1c0d2176cdbc7de7153` |
| zone4 | 704 | `c83de1764993186aaa4dc3c58cac941722950eb52198cdf45ed9e55d8fae7e6a` | `a11f9c796238ad034cfbd806ad7753a0b520c80ae52bd054557b9919817ce712` | `cd6f6dd671a0c7b3a02b050db682df5f2ca81ac5c49250fa475e0550978adf8e` |
| zone6 | 832 | `7910780af8a9656998a1a01c79604e312ec17c1da610468623a84c7274b64beb` | `709cd6264d9a4af7efd8dd4dc90fd1d1f64942b28c35adf9d1eb0871cc9e859a` | `c742757379ff84606c90e635b98ab44440da728ad4aaacbbb94425547d051d80` |
| zone13 | 512 | `69fc113563f8d113881c0625827b110bf013a9e2229596d27b04f901982b223a` | `a8b2299c4e8738702f496be75c2d0ff29cb462d8d6040d0a8ed83173500b087e` | `a7d7a19f4c2513b244a32e818da3900f778e76bd45be243adf2294fb981c5cb7` |
| zone1 | 960 | `68c805f412ab38886d066b1c75e3ce56dbb5081685e7350730d03bc5e2b2cdf2` | `8fe898f8a95b29b43a2df6797da3dfe7a6668045a634737eae204db1680b6a47` | `7cc8f6346cfb9ccb0d7f7ed4779894d384fcf6598ee1da493805110fc622fb44` |
| zone8 | 896 | `93decc73e3963e03e2008b82dc1d92ce9d1bc09651581fdcb4aace3ec79449a4` | `93ae27c5ced50c7aa0eef7411ddcd3366dc5c62b41221e89e14f322eb7d03b97` | `b8082975af0cd5153018765fcfdae654899964aeaf52bcadbce8252cd25ce138` |
| zone11 | 576 | `082adf217123a03a10a2a2431b643d5cd5b1491dcc7c9503922b72f10d89d902` | `cb3c136f7627098169ccf684417d075632bad545eff8266feb724974d8b96e8d` | `eef71d48e829e454aaf68c4855380d459249b037ed07fa9ad88e28f98a1d3fd0` |
| zone7 | 960 | `ed329a067bb5196d36a16ec1b4393965f5463639b99b32dd564474157008035e` | `38870e1b88fc5053dce9427590a9dfcdb75ed3360aa3b3642a787ca36400dd24` | `9f23d91a8daef8f0daf3a63c191965e5d40e2cc69522cd81fe47a5acb8277cf1` |
| zone2 | 1024 | `3b8deab3a188943074f239a45f2dd38da855a83bcea55b0aba0ae6703be2a332` | `1b59da26b824810105d0018f11a3e30c5e8715577cb03bc80f2141b8a3e103c6` | `2088fa29acad879664b36cf247b74355b750827630617503599f1b6c51aaf727` |
| zone3obr | 1280 | `e5a9da250d5299eb6e9785b5755d92115d024b951d052b8d196259ec90e54bad` | `a28d3ddbe2aeca0ba486e86a98749d0f9a297518b96fddf4553ca41813d5e03c` | `0acc58a4ac3bb627029079970197afe911904e88e173dd321029343fe7b00791` |
| Zone12 | 448 | `786d1dbc7a2092dc8567baf5e3141b6c5d3bf6cc1084f1809a0f13e449305950` | `b688d491732c29ee42e3e4ee971c22bda289dfc2c7d3638d7058cebd57110557` | `5bd24a3c3f3681ef6b961f3fc9b8602a1a0887f1d7216cf205e4487625de8cd6` |

Перенесённые semantic namespaces: bz10k, zone1, zone2, zone3obr, zone4, zone6,
zone7, zone8, zone9, zone12. У zone11/zone13 такого документа нет.
Устаревший portable override Zone7 в постоянной установке не менялся.

## Проверки и их границы

| Проверка | Результат |
|---|---|
| Разбор native профилей/semantic documents | 68/68 + 63/63 |
| Exact native embedded bindings v2 | **68/68 PASS** |
| Atlas/profile identity semantic documents | **63/63 PASS**; это не map binding |
| Сохранность authored semantic arrays | **63/63 PASS** |
| Native MPR load | **79/79 PASS**, 41 Dejavu + 38 Base |
| SEC/physical и authored slots/quarter-turn | 5351 SEC; invalid=0, повороты 0…3 |
| Land/active overlay references | 1369856 / 220115; карты hash-identical подготовке |
| Вода/анимации | 74 диапазона в 62 картах, 1984 кадровых позиций; bounds PASS |
| Active map binding + точное восстановление native vertices | bz10k/zone12/zone8 PASS, **Zone2 FAIL** |
| Полный native verification command | **FAIL, exit 3** из-за Zone2 map binding |
| Скрытые открытия v2, полный editor initialization | **3 PASS / 5 FAIL**, подробнее ниже |
| Применение live + post-install reopen | **НЕ ВЫПОЛНЕНО**, обязательный precondition FAIL |

Для неизменных MMP повторно не декодировались все 12237 mip: доказанная побайтная
идентичность v1 сохраняет проверку его всех mip из подготовки. В v2 заново измерены
physical images 68 embedded atlases и их exact resolver bindings; maps/SEC проверены
загрузчиками 136. Stock prefix bindings сохраняются по неизменности страниц v1.

Скрытые открытия — собственная read-only обвязка, original 136 objects из
`build-release-136-random`, зафиксированные private object copies; bootstrap
изолирует QSettings/backup/cache в собственной папке и вызывает штатную отложенную
инициализацию. Исходники программы и original objects не изменены.
Запуск на отдельном невидимом WinSta0 desktop с desktop OpenGL, только private MPR
copies; никакого интерактивного окна владельца. Под одним UI lock прочитаны точный
profile/palette state и actual `CTerrainEditModel` land/overlay vertices; они сравнены
с соответствующим native semantic member. Exit 22 — функциональный FAIL, не SKIP.
Пробы с живым оригиналом были только read-only baseline, не запуском игры.

| Карта/пак | Palette/profile exact | Semantic vertices exact | Итог |
|---|---|---|---|
| zone2 / baseline | да | нет | FAIL (exit 22) |
| zone8 / baseline | да | да | PASS (exit 0) |
| bz10k / v2 | нет | нет | FAIL (exit 22) |
| zone12 / v2 | нет | нет | FAIL (exit 22) |
| zone2 / v2 | нет | нет | FAIL (exit 22) |
| zone8 / v2 | нет | нет | FAIL (exit 22) |
| zone6 / v2 | да | да | PASS (exit 0) |
| zone9 / v2 | да | да | PASS (exit 0) |
| zone3obr / v2 | да | нет | FAIL (exit 22) |
| zone19 / v2 | да | да | PASS (exit 0) |
| zone3obr / baseline | да | нет | FAIL (exit 22) |

Общий сбор hidden-results завершился exit 1, потому что обязательные открытия v2
не все PASS. Evidence: [hidden-results.json](../../build-tests-hdpack-restore-v2/hidden-results.json),
[baseline Zone3obr](../../build-tests-hdpack-restore-v2/hidden-baseline-zone3obr.json),
`build-tests-hdpack-restore-v2/hidden-<baseline|v2>-<map>.log`.
Executable audit SHA256 `6b1e124e16ae8b750f09d9e96036c900d96c53d1b2700fedf122360c75f93fab`;
[object manifest](../../build-tests-hdpack-restore-v2/mapper-object-identity.json).

Ошибки окружения/обвязки отделены от результатов: первоначально UI lock принадлежал
EI-FINALIZE-136, попытка закончилась LOCK BUSY без запуска. После освобождения lock
первая обвязка неправильно ждала уже загруженный CMprDocument до deferred initialization
(одна baseline-проба timeout; следующая прервана на собственном процессе).
Исправлен только guard диагностической обвязки, свои PID/path/parent и lock-token
проверены перед завершением/cleanup. Эти попытки не PASS; сохранены
`infrastructure-deferred-load-hidden-baseline-zone2.*`. Итоговые 11 открытий после
исправления без timeout; карта/пак не менялись между baseline и v2.

## Команда отката — подготовлена, не выполнялась

Из PowerShell:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "C:/GOG Games/maped/ei-mapper-ataman/build-tests-hdpack-restore-v2/rollback.ps1"
```

Исполняемая версия: [rollback.ps1](../../build-tests-hdpack-restore-v2/rollback.ps1).
Режим `-ValidateOnly` выполнен: backup SHA верен, live равен original baseline,
процессов владельцев нет, lock взят/освобождён; actual rollback не выполнялся.
Сейчас команда вернёт NOOP, потому что исходный пак уже на месте. После будущего
успешного применения именно v2 она допустит откат только при live SHA `b3b123dde10513cf6dcc40d60ff4a9e21177017a51989b991c2445eb3f0868cf`.
Посторонний SHA/активный процесс/занятый lock/неверная копия — отказ. Откат сохраняет
v2 в private displaced backup, проверяет temp и readback; `File.Replace` получает
не-null backup path. Другие игровые файлы не затрагиваются.

Проверена механика atomic replace и обратной замены только на собственных маленьких
fixtures в `rollback-fixture/`, не на живом ресурсе.
[validation](../../build-tests-hdpack-restore-v2/rollback-validation.log),
[backup receipt](../../build-tests-hdpack-restore-v2/backup-receipt.json).

## Что меняется для владельца и следующий шаг

**Сейчас ничего в игре/живых ресурсах не изменилось.** V2 — кандидат, применять его
нельзя до решения описанных blocker. После безопасного применения восстановятся
исходные HD stock pages и полные mip chains, при прежних MP/SEC/слотах/анимациях.
Потребление памяти может вырасти; фактический выбранный игровой mip/renderer residency
здесь не проверены. В маппере stock-источники станут 256 px, но нынешние опубликованные
Zone6/Zone9/Zone3obr/Zone19 останутся 128 px до будущей HD пересборки.
Это не увеличение числа разных рисунков, не исправление всех повторов покраски.

Следующий необходимый шаг для сопровождающего/владельца: отдельное поручение на
mixed-resolution editor binding/picker correction с native slot/pixel identity,
и установление доказуемой map binding семантики Zone2 (предсуществующую Zone3obr
учесть отдельно). Код в этой задаче запрещён и не изменён; map hashes не угадывались.
Затем повторить затронутые проверки и полный guarded apply с новым актуальным live
SHA, lock, owner guards, backup, atomic replace/readback и post-install reopen.
Само разрешение на один live RES сохраняется; данный отчёт его не расширяет.

Final guard: живой RES и backup равны исходному SHA, candidate SHA верен,
**79/79 MPR + 8/8 private copies** прежние; source snapshot соответствует checkout,
AGENTS SHA `f4b7fdd5ea82fcdf43d002d23d64ec36be853650f054edf70376527cd6432ec3`.
[final-guard.json](../../build-tests-hdpack-restore-v2/final-guard.json).

Ни сети/push, ни зависимостей, ни новых чатов/подагентов, ни интерактивного
редактора/игры. Специализированные evil-islands-modding/private-engine-reference
skills недоступны; приватные исходники движка не читались. Рабочие данные/обвязки
только в игнорируемом build-tests-hdpack-restore-v2. VERSION/CHANGELOG и постоянную
установку эта задача не изменяла; чужая EI-FINALIZE-136 сохранена. В scoped-коммит
идут только этот отчёт и собственное обновление пункта ROADMAP; игровые данные вне Git.
