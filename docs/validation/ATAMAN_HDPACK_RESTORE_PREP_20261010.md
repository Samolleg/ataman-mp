# EI-HDPACK-RESTORE-PREP-20261010 — кандидат восстановления HDPack Dejavu

Дата: 2026-10-10. Статус: **подготовка выполнена; живой файл не заменён**.
База: `main`, `c41d97552b37862dcc1204610c006d770a175356`, программа 135.
Поручение владельца через Claude: вернуть исходное качество с сохранением поздних
страниц, профилей и семантики; только кандидат, проверки и план применения.

## Результат и ограничение прямой замены

Кандидат возвращает верхний authored mip ровно **236 MMP страницам в 34 namespaces**.
Это исходные BC1/DXT1 bytes JamaisVu135, включая полную цепочку, без декодирования
и повторного сжатия при записи. **986 остальных членов**, в том числе все
**131 JSON (68 профилей + 63 semantic-документа)**, оставлены побайтно прежними.
Добавленные Zone19 и опубликованные атласы карт сохраняются.

Кандидат структурно готов. Однако побайтная сохранность JSON **не сохраняет
его привязку к изменившимся пикселям**: 12 embedded-профилей и 10 semantic-документов
потеряют exact atlas binding. Четыре нынешние карты Dejavu требуют переноса:
**bz10k, zone12, zone2, zone8**. Прямую замену без этого переноса нельзя считать
полным восстановлением с рабочей палитрой/семантикой. Изменять JSON в данном
кандидате запрещено условием «только 236 страниц», поэтому миграция описана,
не выполнена.

Нынешние Zone6, Zone9, Zone3obr и Zone19 уже используют опубликованные namespaces.
Их фактические атласы останутся 128 px. Восстановятся их stock-источники; автоматической
перепубликации активных карт в 256 px или увеличения числа вариантов здесь нет.

## Кандидат, исходники и структура RES

Путь: `C:\GOG Games\maped\ei-mapper-ataman\build-tests-hdpack-restore\HDPack_Terrain.res`.

SHA256: `6d1911235f30e79e1fe98ee1ff9c3eaac7ac6c9a2bf5a779579f7eeaa69d55b5`.

| Файл | Байты | МиБ | SHA256 |
|---|---:|---:|---|
| Dejavu сейчас | 775817323 | 739.877 | `2d20abdd3a753cd00671c84798a2bda030b7f192b71de58e0e09860f5bfe488c` |
| Кандидат | 1270745195 | 1211.877 | `6d1911235f30e79e1fe98ee1ff9c3eaac7ac6c9a2bf5a779579f7eeaa69d55b5` |
| JamaisVu135 | 682308638 | 650.700 | `50c285225fb1745ee70d280e57a214f346481e6d380a78d55475f730d11a81b2` |

Рост: **494927872 байт = 472 МиБ**, или 63.79%.
Зафиксирован исходный SHA Dejavu, поэтому кандидат не предназначен для применения
поверх будущих публикаций без повторного сравнения/пересборки.

Ресурс записан потоково: заменены payloads в исходной области данных;
скорректированы только их lengths, необходимые DataOffset и TableOffset.
Старой таблицы внутри области данных и добавленного каталога за старой таблицей нет.
236 дельт ровно по 2 097 152 байта сохраняют исходное выравнивание.

- Members/table slots: **1222**, MMP **1091**, JSON **131**, namespaces **91**.
- MMP после восстановления: **236 × 2048² / 12 mip**, **855 × 1024² / 11 mip**.
  На каждой странице 64 физических слота; имена/порядок/число слотов прежние.
- Побайтное равенство JamaisVu135: **236/236** заменённых payloads.
  Сжатые mip ≥1 кандидата равны прежним уровням 0…10 Dejavu: **2596/2596 уровней**.
  MMP headers заменённых страниц взяты целиком из JamaisVu135; это не фиктивное
  увеличение width/height старых данных.
- Остальные payloads: **986/986 равны**. Все 131 JSON равны; нет удаления/добавления.
- Metadata 1222 entries: NextIndex, timestamp, NameLength, NameOffset и encoded names
  прежние; таблица имён **31191 байт** прежняя. Order members прежний.
- Все **1223 промежутка данных / 14067 байт** сохранены; modulo 16 offsets прежний.
  TableOffset: **775759248 → 1270687120**;
  table **26884 байта**, tail **0 байт** в обоих файлах.
- **1222/1222** имён найдены проходом bucket/hash-chain: lowercase CP1251,
  sum(bytes) mod tableSize, NextIndex до совпадения. Выходов за границы,
  циклов и неоднозначных имён нет.

Независимые receipts: [archive-proof.json](../../build-tests-hdpack-restore/archive-proof.json),
[layout-proof.json](../../build-tests-hdpack-restore/layout-proof.json). В первом есть
все 236 имён, old/new payload SHA и доказательство suffix equality.

## Загрузчики, карты, SEC и вода

Собственная консольная обвязка использует неизменённые исходники указанной базы:
`CResArchiveDocument`, `CResArchiveIndex`, `inspectMmp`, `decodeDxtImage`,
`decodeMmpDxt1AtlasTile`, `CMprDocument`, native profile/semantic parsers.
Полная загрузка RES и потоковое чтение каждого member прошли для обоих паков.
Декодированы **12001 mip до / 12237 mip после**, ни одного неполного уровня или
trailing byte MMP. Native logical tile decoder проверил все **69824 физических
слота** каждого пакета при пересчёте atlas hashes.

Все MPR двух map roots профиля проверены, включая fallback Base:
**41 Dejavu + 38 Base = 79/79 открываются**, **5351 SEC**,
**1369856 land + 220115 активных overlay ссылок**. Ошибок страницы/слота — **0**,
ссылок за authored tileCount — **0**. Packed quarter-turn имеет прежние два бита
и допустимые значения 0…3. Все требуемые страницы кандидата найдены, defaults
не нужны по структурной проверке. MPR/SEC не переписывались: SHA всех **79**
живых карт повторно совпал, поэтому namespace, material table, tile classes,
число слотов, раскладка и повороты не могли измениться.

Анимированные диапазоны **74** в **62 картах**, всего **1984 кадровых позиции**:
все start/count внутри физического и authored набора, ни один диапазон не разрезан.
Затронутые кадры получают исходный верхний mip той же страницы/слота;
прежняя цепочка цела. Незатронутые кадры byte-identical. MP animation records,
SEC overlay material indices и их интервалы сохранились.

Native parsers открыли **68/68 profiles + 63/63 semantic documents**, failures **0**.
Это проверка разбора; изменение exact binding ниже не названо PASS.

Evidence: [candidate-native.json](../../build-tests-hdpack-restore/candidate-native.json),
[current-native.json](../../build-tests-hdpack-restore/current-native.json),
[maps-native.json](../../build-tests-hdpack-restore/maps-native.json),
[metadata-native.json](../../build-tests-hdpack-restore/metadata-native.json).
Нативная работа игры **не запускалась**. Hash-chain и format limits проверены
для игрового формата, но это не исполнение загрузчика game.exe/EID3D11.

## Exact atlas binding и сохранность семантики

Хеши рассчитываются штатной `CTerrainAtlasProfileInference::pixelSha256`:
число физических tile images + newline; для каждого слота native RGBA8888,
width x height + newline и scanlines, logical row = 7 − slot/8.
`CTerrainAtlasPresetCatalog::exactProfile` проверяет namespace, decoded hash
и **физическое число слотов**, не только authored MP tileCount.
Измерены полные namespaces и отдельно нужные map/preset prefixes: дополнительные
страницы нельзя ошибочно отбросить, чтобы получить старый stock hash.

### Встроенный all-map-hd набор

**34/34 восстановленных stock-prefix namespaces совпали с уже включённым HD binding**.
Они не потеряют stock-палитру; добавление их SHA в assets не требуется.
Zone17, Zone18, Zone19 прежние и также известны. Исключение ZoneMainMenuNew
не затронуто: его текущие первые три страницы отличаются от JamaisVu135,
exact built-in profile не совпадает **уже до восстановления**; автоматически
заменять меню или разрешать неизвестный hash нельзя.

Все 38 stock prefixes и measured hashes —
[current-bindings.json](../../build-tests-hdpack-restore/current-bindings.json),
[candidate-bindings.json](../../build-tests-hdpack-restore/candidate-bindings.json).
Ниже указаны новые HD hashes восстановленных prefixes (слоты — размер preset).

| Namespace | Слотов | Новый decoded SHA256 | Встроенная привязка |
|---|---:|---|---|
| basegipat | 512 | `f24371251f84f277c1eb474771ae61c86e482f6593b6a2b2d1e7bd1e8107b396` | совпала |
| bz10k | 320 | `eef8f7940f5e28967d65d6fc17e0450bd78a2e4671a0be71da178e3ca8fb89f7` | совпала |
| bz11k | 512 | `bd1873929c5ba9d3406d8be7d642dfa37fcb42a1a48edc914d5dd97610ee00d2` | совпала |
| bz13h | 512 | `30eb16d617c63989bb43591526ea2b3c31c386c4aa179c1342d9e9b086ca61a8` | совпала |
| bz14h | 256 | `746dafd4e7d2c684211b3f8115257197f6c4efc868e79c932bc35e6433346331` | совпала |
| bz15h | 256 | `ae4c815f3c93015ebac0ffb26c4760a6eaa14fa935be97559467bc0e66658734` | совпала |
| bz16h | 512 | `9695dad9bb05c5547d7924fcc1f2ee30875d951e891d6506e427c62772f7a1f6` | совпала |
| bz18h | 256 | `746dafd4e7d2c684211b3f8115257197f6c4efc868e79c932bc35e6433346331` | совпала |
| bz2g | 512 | `ed0bc69bd4abbb8f9f90fab44574a24b0b806521b6bc36501eea7c5add258612` | совпала |
| bz3g | 512 | `6752a5b4cefd3aedbd6889516b424f4e13afafe288e99406b500404aed62b014` | совпала |
| bz4g | 512 | `4279b31201a5d960d870988415550782e9bae5b28c65cea04095eaf2714d41ed` | совпала |
| bz5g | 512 | `4ad67630a39a00a6d5fb1d421e7873c76faa64707a4c5565744a84476dfdc35e` | совпала |
| bz6g | 512 | `4ad67630a39a00a6d5fb1d421e7873c76faa64707a4c5565744a84476dfdc35e` | совпала |
| bz7g | 256 | `c23dab43dfb7fd60f59a6e65200faad049b9c53a251cb8db3dea216011c3852e` | совпала |
| bz8k | 512 | `2005d946276a6094e4a7f1fcd03f75c6b4410611a890016a43dc9dfdd141124a` | совпала |
| bz9k | 512 | `2005d946276a6094e4a7f1fcd03f75c6b4410611a890016a43dc9dfdd141124a` | совпала |
| zone1 | 512 | `35cc3b90985888b5c5206f8c5980321beb46f0be7b1e96b4a3467677ac2a4665` | совпала |
| zone10 | 512 | `9c609f8fcfbb1683dc7c0bd31371ddc621d572df713cf41bb620d04116f990ee` | совпала |
| zone11 | 512 | `2005d946276a6094e4a7f1fcd03f75c6b4410611a890016a43dc9dfdd141124a` | совпала |
| zone12 | 320 | `eef8f7940f5e28967d65d6fc17e0450bd78a2e4671a0be71da178e3ca8fb89f7` | совпала |
| zone13 | 512 | `a8b2299c4e8738702f496be75c2d0ff29cb462d8d6040d0a8ed83173500b087e` | совпала |
| zone14 | 512 | `bd1873929c5ba9d3406d8be7d642dfa37fcb42a1a48edc914d5dd97610ee00d2` | совпала |
| zone15 | 512 | `30eb16d617c63989bb43591526ea2b3c31c386c4aa179c1342d9e9b086ca61a8` | совпала |
| zone16 | 256 | `746dafd4e7d2c684211b3f8115257197f6c4efc868e79c932bc35e6433346331` | совпала |
| zone2 | 512 | `ed0bc69bd4abbb8f9f90fab44574a24b0b806521b6bc36501eea7c5add258612` | совпала |
| zone20 | 192 | `89e83d13c2fc8a007bb694bb33c2f14bbc7669cba3e4a51c5675a607e360935a` | совпала |
| zone3obr | 512 | `33d297edc193f88356c766772a84f5c6db27cf6cd9831d241cdafa9497cb5b4a` | совпала |
| zone4 | 512 | `4279b31201a5d960d870988415550782e9bae5b28c65cea04095eaf2714d41ed` | совпала |
| zone5_1 | 192 | `2908a9617277f4e0ff663bf032325941fc57879af573c918e13f249e4de54635` | совпала |
| zone6 | 512 | `4aff5893f635e48e0c5ec005172e81e7e69584d9a0da8b4c285d16031b3cc799` | совпала |
| zone6_2 | 512 | `916e18f19c3b648d9951af2e9930f3fa7e9d67c97fd44bf09d2f5ceebeee6f56` | совпала |
| zone7 | 512 | `dacf1b6653cee1f4afc908b1ff3fe219784f4b4a1ff8d38fd46efb38aa9cd897` | совпала |
| zone8 | 512 | `4ad67630a39a00a6d5fb1d421e7873c76faa64707a4c5565744a84476dfdc35e` | совпала |
| zone9 | 512 | `bd5f43f33363351807dd6069ab1eb861fc1b66177500dc0d29710474253d77fe` | совпала |

### Расширенные embedded profiles

До: **68/68** exact full-atlas bindings. После: **56/68**; потеряют привязку **12**.
Все 53 опубликованных namespace атласов побайтно сохранены; их собственные hashes
не меняются. Stock-prefix, например zone9/512, остаётся известным, но full
zone9/1152 получает другой hash и не подменяется этим stock profile.

Таблица содержит full physical atlas SHA кандидата, а не hash первых stock страниц.
«Текущая карта» означает карту Dejavu в нынешнем corpus; остальные профили тоже
нужно сохранить при миграции источников, если они должны остаться доступными.

| Namespace | Слотов | Новый full decoded SHA256 | Текущая карта Dejavu | Semantic binding потеряется |
|---|---:|---|---|---|
| bz10k | 448 | `1e0b2cd54c61b69dfd855934cf4f003ab2715026fad13dedca4d26a1e050101b` | bz10k | да |
| zone1 | 960 | `8fe898f8a95b29b43a2df6797da3dfe7a6668045a634737eae204db1680b6a47` | исторический источник | да |
| zone11 | 576 | `cb3c136f7627098169ccf684417d075632bad545eff8266feb724974d8b96e8d` | исторический источник | нет такого документа |
| zone12 | 448 | `b688d491732c29ee42e3e4ee971c22bda289dfc2c7d3638d7058cebd57110557` | zone12 | да |
| zone13 | 512 | `a8b2299c4e8738702f496be75c2d0ff29cb462d8d6040d0a8ed83173500b087e` | исторический источник | нет такого документа |
| zone2 | 1024 | `1b59da26b824810105d0018f11a3e30c5e8715577cb03bc80f2141b8a3e103c6` | zone2 | да |
| zone3obr | 1280 | `a28d3ddbe2aeca0ba486e86a98749d0f9a297518b96fddf4553ca41813d5e03c` | исторический источник | да |
| zone4 | 704 | `a11f9c796238ad034cfbd806ad7753a0b520c80ae52bd054557b9919817ce712` | исторический источник | да |
| zone6 | 832 | `709cd6264d9a4af7efd8dd4dc90fd1d1f64942b28c35adf9d1eb0871cc9e859a` | исторический источник | да |
| zone7 | 960 | `38870e1b88fc5053dce9427590a9dfcdb75ed3360aa3b3642a787ca36400dd24` | исторический источник | да |
| zone8 | 896 | `93ae27c5ced50c7aa0eef7411ddcd3366dc5c62b41221e89e14f322eb7d03b97` | zone8 | да |
| zone9 | 1152 | `bebe7262558f097eba3bcb54de94682224400af826d05cbe9efda09e83d78aa5` | исторический источник | да |

Semantic bytes сохранены **63/63**, но atlas binding: **63/63 → 53/63**.
Полный список десяти: bz10k, zone1, zone2, zone3obr, zone4, zone6, zone7,
zone8, zone9, zone12. `atlasPixelSha256` проверяется вместе с namespace,
MP/tile topology и hash сериализованного профиля. После переноса profile меняется
и `profileSha256`; сохранить лишь массивы land/overlay без этих обновлений недостаточно.
Результаты: [analysis.json](../../build-tests-hdpack-restore/analysis.json),
[semantic-bindings.json](../../build-tests-hdpack-restore/semantic-bindings.json).

### Локальные overrides

В постоянной папке найден один portable override: `terrain-atlas-profiles/zone7-544f6169e4e8.semantic-atlas.json`,
namespace zone7, **576 слотов**, provenance `user-authored-custom-profile`;
SHA файла `045f0a458147a093734014fda5b390acebca3d31dfdaae23a0604addb70c2580`.
Его recorded pixel hash `1771ea94447b5017e91a8ee907fb9d8e11099e2d4fe89eee489ac174525ed5fd`
уже не совпадает с нынешним zone7 prefix/576:
`1f6cff4d9efd11b8640c7666039bdd7f844a5e85000c16e9e513d80865711f35`;
кандидат даёт `ff40a3903268e82b9027d073aeba240f58ef275f8aafd0c8fe3c0a89ab60963d`.
Нынешняя Zone7 использует другой namespace и 960 физических слотов, Base Zone7 — 512.
Этот override уже не активен для исследованных карт; его нельзя автоматически
«перенести» простой заменой hash без выяснения исходного соответствия слотов.
Файл не изменён. В map-adjacent `terrain-atlas-profiles` двух корней профиля файлов нет.
AppData legacy overrides по действующему контракту не читаются и не мигрируются.

## Оценка каждой карты профиля и игровой бюджет

Ниже все 79 MPR. «MP/физ.» — authored tileCount / pages×64; оба числа прежние.
256/128 — число страниц кандидата каждого разрешения, «рендер» — минимальный
native размер тайла, используемый нынешним маппером для общего GL-атласа.
BC1 — сумма сжатых полных mip chains реально требуемых страниц, без MMP headers;
это не весь RES, не GPU/VA замер игры. Указан полный доступный набор страниц,
включая резерв/анимации, а не только страницы с видимыми SEC ссылками.

Коды риска: **B** — новый hash известен built-in; **M** — active embedded binding
потерян, нужен перенос profile/semantics; **U** — atlas identity неизменна;
**H** — прежний stock-prefix уже без exact builtin и не меняется.
Во всех строках структура/слоты/повороты/вода валидны; при U существующие
profile/semantic identities не меняются. «B» не означает новую игровую приёмку.

### Dejavu: 41 MPR

| Карта | Namespace | MP/физ. слоты | Страницы 256/128 | Рендер px | BC1 МиБ | Привязка |
|---|---|---:|---:|---:|---:|---|
| basegipat | `basegipat_213430e77bcf` | 445/448 | 0/7 | 128 | 4.667 | U |
| bz10k | `bz10k` | 399/448 | 5/2 | 128 | 14.667 | M |
| bz11k | `bz11k` | 512/512 | 8/0 | 256 | 21.333 | B |
| bz13h | `bz13h` | 512/512 | 8/0 | 256 | 21.333 | B |
| bz14h | `bz14h` | 256/256 | 4/0 | 256 | 10.667 | B |
| bz15h | `bz15h` | 256/256 | 4/0 | 256 | 10.667 | B |
| bz16h | `bz16h` | 512/512 | 8/0 | 256 | 21.333 | B |
| bz18h | `bz18h` | 256/256 | 4/0 | 256 | 10.667 | B |
| bz19g | `zone10` | 512/512 | 8/0 | 256 | 21.333 | B |
| bz2g | `bz2g` | 512/512 | 8/0 | 256 | 21.333 | B |
| bz3g | `bz3g` | 512/512 | 8/0 | 256 | 21.333 | B |
| bz4g | `bz4g` | 512/512 | 8/0 | 256 | 21.333 | B |
| bz5g | `bz5g` | 512/512 | 8/0 | 256 | 21.333 | B |
| bz6g | `bz6g` | 512/512 | 8/0 | 256 | 21.333 | B |
| bz7g | `bz7g` | 256/256 | 4/0 | 256 | 10.667 | B |
| bz8k | `bz8k` | 512/512 | 8/0 | 256 | 21.333 | B |
| bz9k | `bz9k` | 512/512 | 8/0 | 256 | 21.333 | B |
| zone1 | `zone1_17763baa57df` | 911/960 | 0/15 | 128 | 10.000 | U |
| zone10 | `zone10` | 512/512 | 8/0 | 256 | 21.333 | B |
| zone11 | `zone11_0132fbfdd188` | 504/512 | 0/8 | 128 | 5.333 | U |
| zone12 | `zone12` | 427/448 | 5/2 | 128 | 14.667 | M |
| zone12dun2 | `zone14_975e928d3190` | 200/256 | 0/4 | 128 | 2.667 | U |
| zone13 | `zone13_63226eeedc0c` | 692/704 | 0/11 | 128 | 7.333 | U |
| zone14 | `zone14_60f652260b04` | 475/512 | 0/8 | 128 | 5.333 | U |
| zone15 | `zone15` | 512/512 | 8/0 | 256 | 21.333 | B |
| zone16 | `zone16` | 256/256 | 4/0 | 256 | 10.667 | B |
| zone17 | `zone17` | 256/256 | 0/4 | 128 | 2.667 | U |
| zone18 | `zone18_55491c494740` | 558/576 | 0/9 | 128 | 6.000 | U |
| zone18dun1 | `zone18` | 512/512 | 0/8 | 128 | 5.333 | U |
| zone19 | `zone19_bb84f59c5962` | 908/960 | 0/15 | 128 | 10.000 | U |
| zone2 | `zone2` | 966/1024 | 8/8 | 128 | 26.667 | M |
| zone20 | `zone20` | 192/192 | 3/0 | 256 | 8.000 | B |
| zone3obr | `zone3obr_0bcba8177f9a` | 1415/1472 | 0/23 | 128 | 15.334 | U |
| zone4 | `zone4_fc5a0ba98c52` | 1021/1024 | 0/16 | 128 | 10.667 | U |
| zone5_1 | `zone5_1` | 192/192 | 3/0 | 256 | 8.000 | B |
| zone6 | `zone6_fe645a375f96` | 1193/1216 | 0/19 | 128 | 12.667 | U |
| zone6_2 | `zone6_2` | 512/512 | 8/0 | 256 | 21.333 | B |
| zone7 | `zone7_44a43bfdda10` | 929/960 | 0/15 | 128 | 10.000 | U |
| zone8 | `zone8` | 868/896 | 8/6 | 128 | 25.334 | M |
| zone9 | `zone9_f24337452d99` | 1593/1600 | 0/25 | 128 | 16.667 | U |
| zonemainmenunew | `zonemainmenunew_91d1b040b2b8` | 350/384 | 0/6 | 128 | 4.000 | U |
### Base: 38 MPR

| Карта | Namespace | MP/физ. слоты | Страницы 256/128 | Рендер px | BC1 МиБ | Привязка |
|---|---|---:|---:|---:|---:|---|
| basegipat | `basegipat` | 512/512 | 8/0 | 256 | 21.333 | B |
| bz10k | `bz10k` | 320/320 | 5/0 | 256 | 13.333 | B |
| bz11k | `bz11k` | 512/512 | 8/0 | 256 | 21.333 | B |
| bz13h | `bz13h` | 512/512 | 8/0 | 256 | 21.333 | B |
| bz14h | `bz14h` | 256/256 | 4/0 | 256 | 10.667 | B |
| bz15h | `bz15h` | 256/256 | 4/0 | 256 | 10.667 | B |
| bz16h | `bz16h` | 512/512 | 8/0 | 256 | 21.333 | B |
| bz18h | `bz18h` | 256/256 | 4/0 | 256 | 10.667 | B |
| bz2g | `bz2g` | 512/512 | 8/0 | 256 | 21.333 | B |
| bz3g | `bz3g` | 512/512 | 8/0 | 256 | 21.333 | B |
| bz4g | `bz4g` | 512/512 | 8/0 | 256 | 21.333 | B |
| bz5g | `bz5g` | 512/512 | 8/0 | 256 | 21.333 | B |
| bz6g | `bz6g` | 512/512 | 8/0 | 256 | 21.333 | B |
| bz7g | `bz7g` | 256/256 | 4/0 | 256 | 10.667 | B |
| bz8k | `bz8k` | 512/512 | 8/0 | 256 | 21.333 | B |
| bz9k | `bz9k` | 512/512 | 8/0 | 256 | 21.333 | B |
| zone1 | `zone1` | 512/512 | 8/0 | 256 | 21.333 | B |
| zone10 | `zone10` | 512/512 | 8/0 | 256 | 21.333 | B |
| zone11 | `zone11` | 512/512 | 8/0 | 256 | 21.333 | B |
| zone12 | `zone12` | 320/320 | 5/0 | 256 | 13.333 | B |
| zone13 | `zone13` | 512/512 | 8/0 | 256 | 21.333 | M |
| zone14 | `zone14` | 512/512 | 8/0 | 256 | 21.333 | B |
| zone15 | `zone15` | 512/512 | 8/0 | 256 | 21.333 | B |
| zone16 | `zone16` | 256/256 | 4/0 | 256 | 10.667 | B |
| zone17 | `zone17` | 256/256 | 0/4 | 128 | 2.667 | U |
| zone18 | `zone18` | 512/512 | 0/8 | 128 | 5.333 | U |
| zone19 | `zone19` | 512/512 | 0/8 | 128 | 5.333 | U |
| zone2 | `zone2` | 512/512 | 8/0 | 256 | 21.333 | B |
| zone20 | `zone20` | 192/192 | 3/0 | 256 | 8.000 | B |
| zone3obr | `zone3obr` | 512/512 | 8/0 | 256 | 21.333 | B |
| zone4 | `zone4` | 512/512 | 8/0 | 256 | 21.333 | B |
| zone5_1 | `zone5_1` | 192/192 | 3/0 | 256 | 8.000 | B |
| zone6 | `zone6` | 512/512 | 8/0 | 256 | 21.333 | B |
| zone6_2 | `zone6_2` | 512/512 | 8/0 | 256 | 21.333 | B |
| zone7 | `zone7` | 512/512 | 8/0 | 256 | 21.333 | B |
| zone8 | `zone8` | 512/512 | 8/0 | 256 | 21.333 | B |
| zone9 | `zone9` | 512/512 | 8/0 | 256 | 21.333 | B |
| zonemainmenunew | `zonemainmenunew` | 192/192 | 0/3 | 128 | 2.000 | H |

В Dejavu hashes изменятся у **25 карт**: **21** полностью HD и **4** со смешанными
размерами; **16** карт не меняются. В Base изменятся **34**, четыре прежние.
Во всех активных затронутых картах всего ≤**1024 физических слотов**, а непосредственно
256 px ≤**512 слотов / 8 страниц**. Наибольший смешанный случай — Zone2:
**16 страниц / 1024 слота = 8 HD (512) + 8 старых (512)**,
BC1 цепочки **26.667 МиБ**, decoded RGBA+mips страниц **213.333 МиБ**.
Геометрически тяжёлая опубликованная Zone19 — **256 SEC / 15 страниц / 960 слотов**,
полностью прежние 128 px, BC1 **10.000 МиБ**. Наибольшая текущая физическая палитра —
Zone9, **1600 слотов / 25 страниц**, также прежние 128 px.

Сопоставление с [синтетическим бюджетом](ATAMAN_TERRAIN_MEMORY_SYNTHETIC_20261010.md):
условный верхний 1024×256 для неизвестного RGBA/assembly fallback соблюдён даже
при оценке всех слотов затронутых активных карт как 256 px. Неизменённые активные
атласы ≤1600×128 укладываются в default2048. Для сжатого EID3D11 сценария
8192×256 было 24/24 PASS в синтетической матрице; нынешние карты существенно меньше.
Это не доказывает прохождение этой ветви именно в игре, не замер VRAM и не общий
production-предел. Старые исторические source namespaces могут иметь больше
1024 физических слотов (например zone3obr/1280), но это смесь 8 HD + 12 страниц128;
их нельзя объявлять 1280×256 или путать с активной Zone3obr/1472×128.

По исходному факту владельца JamaisVu135 с этими полными страницами уже используется
в игре; их формат не новый. Это сильнее синтетического рисунка текстур, но не
проверка нового гибридного Dejavu pack, его нынешних MPR, DLL и загрузки соседних карт.
Добавленные ресурсы, другое состояние кампании и archive/source mappings меняют фон.

**Отдельный риск большого архива:** RES теперь 1.183 ГиБ вместо 0.723 ГиБ.
Все offsets <2³¹ и прочитаны проектом. Однако если игра удерживает/mapping-ит весь
RES, рост **472 МиБ** может занять дополнительный VA x86 процесса даже при малом
наборе загруженных terrain страниц. Синтетический D+M budget не доказывает отсутствие
такого mapping или постоянного RGBA fallback. Peak VA игры/VRAM не измерены.

## Время открытия и память процесса маппера

Изолированная mapper135 обвязка: **x64, Qt 5.12.12**, исходный main и **271** Release135 object до
замены backup object; все **475** исходных файлов снимка сверены с источником той
сборки с нормализацией CRLF/LF (расхождений 0). Обвязка перенаправляет только
QSettings и корень backup в рабочую папку, код загрузки/атласа не изменён.
Это исследовательский EXE, не новая поставка/установка программы.

SHA EXE: `8804956748ccdb7de29179ea11e8460f52b2731b0c0953876e0cb62695f019da`.

Каждый запуск — новый процесс, отдельный невидимый Windows desktop через
CreateDesktop/CreateProcess, под `build-locks/ui-tests.lock`; desktop не переключался.
Qt windows + штатный desktop OpenGL **4.6 / RTX 4070 SUPER**; без интерактивного
управления, без MOB. Lock освобождён. QSettings, backups, options, caches и логи
только в `build-tests-hdpack-restore/mapper`. Для исключения живого HDPack из
fallback выполнены проверенные частные копии 68 остальных mod RES (3618723493 байта)
и baseline HDPack. Base читается из разрешённого корня. Автопересборка resourceRoots
по gameRoot отключена пустым gameRoot в тестовых options; переданы те же ресурсы
Dejavu/Base. Это controlled terrain benchmark, а не полный профильный игровой запуск.

По три пары before→after на карту, прогретый OS file cache, без сброса системного
кэша. В logs каждого из **24/24 PASS** все страницы имеют ожидаемый путь,
есть atlas-ready, smoke-complete и штатное закрытие/exit0. Сбои предварительной
подготовки ниже в этот итог не включены.

Время — **`MPR visible-load ... returnReadyMs`**, включая binding preflight,
GL atlas и сектора. Это готовность видимой карты; log явно говорит
`editorInitializationDeferred:true`. Полная отложенная готовность редакторской
палитры/Modern не измерена. Start-to-exit включает запуск Qt/общие ресурсы/закрытие.
PeakWorkingSet и PeakPagefileUsage — kernel counters GetProcessMemoryInfo;
PrivateUsage — максимум выборки раз в 20 мс. Пики x64 маппера не являются
VA бюджетом x86 игры из синтетического отчёта. ASLR/драйвер/процессы фона дают разброс,
три пробы не являются оценкой редких пиков или cold HDD.

| Карта / случай | Видимая загрузка до→после, мс (медиана; min–max) | Start→exit до→после, с, медиана | Peak WS до→после, МиБ (медиана / максимум пиков) | Peak commit до→после, МиБ (максимум kernel peak) |
|---|---|---:|---:|---:|
| Zone10: типичная, 48 SEC, 8 HD страниц | 373→586 (360–409 / 552–605) | 3.945→4.259 | 302.50 / 302.54→323.71 / 324.33 | 1151.89→1163.52 |
| Zone12: 160 SEC, mixed, потеря binding | 534→1121 (528–710 / 920–1122) | 4.129→5.220 | 427.38 / 428.06→397.56 / 398.64 | 1300.82→1261.69 |
| Zone19: тяжёлая, 256 SEC, atlas прежний | 622→606 (593–628 / 605–609) | 4.271→4.172 | 569.56 / 571.06→570.55 / 570.67 | 1444.30→1445.27 |
| Zone2: 1024 физических слота, mixed | 515→1065 (506–524 / 1058–1078) | 4.151→4.640 | 356.28 / 356.41→401.59 / 402.28 | 1200.75→1256.75 |

Zone10: видимая загрузка +213 мс / +57%, median peak WS +21.21 МиБ.
Zone19: 622→606 мс — сопоставимо с разбросом, median WS +0.98 МиБ;
нет основания приписывать ускорение неизменённому атласу.
Zone12 после выглядит экономнее по WS, потому что точная embedded привязка
больше не переиспользуется; это **не полезная оптимизация**, профильная работа
неэквивалентна. Zone2 +550 мс, median WS +45.32 МиБ. Полученные времена после
миграции профилей могут измениться; её функциональная/временная проверка ещё нужна.

Сводка и все phases: [benchmark-results.json](../../build-tests-hdpack-restore/benchmark-results.json);
полные 24 logs — `build-tests-hdpack-restore/benchmark-<map>-<before|after>-<1..3>.log`.
До основной матрицы обнаружены и исправлены ошибки окружения проверки:

- Отсутствующий qoffscreen plugin: exit `0xC0000602`, открытия не было.
  После добавления локального plugin offscreen дал `0xC0000005` при GL upload;
  [offscreen-failure.json](../../build-tests-hdpack-restore/offscreen-failure.json),
  [log](../../build-tests-hdpack-restore/offscreen-failure.log). Не PASS.
- Первый native hidden launch успешно открыл карту, но checker искал другое
  имя timing-marker. Исправлено чтение реально зарегистрированного returnReadyMs;
  этот trial не входит в итоговые три пары.
- gameRoot автоматически возвращал живые resourceRoots;
  [отклонённая проба](../../build-tests-hdpack-restore/resource-root-override-failure.json).
  Затем generic sources подтягивали live HDPack через родительские каталоги;
  [отклонённая проба](../../build-tests-hdpack-restore/generic-source-fallback-failure.json).
  Оба «after» отклонены по path guard, не объявлены замерами кандидата.
  Частные RES-копии убрали fallback; все итоговые paths проверены.

## Что нужно сделать с профилями/кодом — без применения

1. **Встроенные stock HD bindings не менять:** 34 восстановленных prefix SHA уже
   известны assets/resources/terrain_atlas_presets.json и all-map-hd каталогу.
   Не добавлять неизвестные hashes по одной форме атласа и не отключать exact checks.
2. **Подготовить контролируемую миграцию 12 embedded profiles**: old SHA/namespace/
   slotCount → измеренный candidate SHA при доказательстве тех же физических слотов
   и BC1 mip suffix. Сохранить families/IDs/names, signatures/corners, thumbnails,
   allowedLayers/selectability, tile types, route owners и authored edits.
   Для четырёх активных расширений недостаточно добавить новый SHA в старый stock
   preset: **448/1024/896 слотов не равны stock 320/512**. Нужен complete exact profile
   с прежней расширенной семантикой или явный local override такого размера.
3. **Перенести 10 semantic documents**: сохранить land/overlay arrays и hashes
   topology, namespace, dimensions; обновить atlasPixelSha256 и profileSha256,
   рассчитанный через штатный fromBytes/toBytes. Для active bz10k/zone12/zone2/zone8
   проверить точное восстановление semantic vertices. Нельзя выдавать новый
   inferred draft за сохранённые authored данные.
4. Если выбирается решение в коде вместо отдельно согласованного изменения JSON,
   нужны exact migration mapping/sidecar в `terrain_atlas_profile`,
   `terrain_atlas_archive_binding`, `terrain_semantic_field_store` и их подключение
   в `CView::loadLandscape`/restoreSemanticField. Разрешать старую identity только
   для проверенной old→new пары, с явным rebinding и сохранением исходника.
   До реализации такого кода кандидат содержит прежние JSON и всё ещё имеет
   перечисленные binding gaps. Более простой data-only путь — следующая отдельная
   версия ресурса с пересчитанными JSON; она уже не соответствует нынешнему
   контракту «меняются только 236 MMP» и требует отдельного поручения.
5. Единственный zone7 local override уже stale: сохранить копию, найти его
   исходный atlas/slot correspondence; без этого не менять hash и не переносить
   автоматически. Актуальные активные overrides двух map roots не обнаружены.
6. Смешанные 256/128 страницы: `CTextureList::buildLandTex` выбирает минимальный
   source mip. Для качества 256 px на всей такой карте потребуется отдельный
   согласованный pipeline (например, page textures/array с независимым resolution,
   либо explicit upsampling младших страниц); последний не вернёт отсутствующую
   детализацию. Сохранить native binding hash и layout, проверить previews/render/
   edit/save/undo/animation. Простая смена этого RES не решает этот вопрос.
7. Для текущих published Zone6/Zone9/Zone3obr сохранение data/labels подтверждено,
   но качество 256 px требует отдельной перепубликации/remap; она здесь запрещена.
   База135 mixed-edge масштабирует к128 и выбирает один источник материала;
   восстановление RES не исправляет этот алгоритм. Параллельная
   EI-TERRAIN-RANDOM-PAINT-136 меняет покраску/атлас/profiles; её код не входит в эту
   проверку. Интеграцию миграции и HD pipeline нужно проверять уже на принятой136.
   `sourcePixelSha256` у provenance слотов — hash изображения тайла с dimensions,
   не общий atlas hash; aliases нельзя считать одинаковыми HD pixels только
   из-за равенства низкого mip. Их пересчёт — часть следующей миграции, не массовая
   замена строк SHA.

## Точный план замены живого HDPack — только описание

**Сейчас план не выполнен.** Перед заменой требуется рабочее решение переноса
четырёх active profiles/semantics; при сохранении всех исторических источников —
всех 12/10. Нынешний кандидат предназначен для проверки исходных страниц,
не объявлен беспроблемным установочным пакетом.

1. Остановить игру и маппер владельцем. Проверить на хосте владельцев файлов по
   PID, executable path и времени старта, включая запуск из постоянной установки.
   Не заменять архив при активном редакторском writer/read-cache. Взять UI lock
   для относящихся проверок маппера; он не блокирует игровые/ресурсные writers сам.
2. Повторно прочитать actual живой файл
   `C:/GOG Games/PZ/MODS/Dejavu/res/HDPack_Terrain.res` и сверить
   SHA **2d20abdd3a753cd00671c84798a2bda030b7f192b71de58e0e09860f5bfe488c**,
   size **775817323**. Если mismatch — остановить замену, построить новый кандидат
   на свежей базе; не потерять более поздние публикации. Проверить источник236 и
   candidate SHA **6d1911235f30e79e1fe98ee1ff9c3eaac7ac6c9a2bf5a779579f7eeaa69d55b5**.
3. Сделать новую timestamped резервную копию, например
   `.../res/backups/hdpack-restore-YYYYMMDD-HHMMSS/HDPack_Terrain.res`, без перезаписи
   прежних backups. Проверить её SHA/size = current. Сохранить receipt input/candidate
   и все мигрируемые profile/semantic/local files с собственными hashes отдельно.
4. Копировать candidate в **новый temp-файл рядом с live destination на том же
   томе**, проверить SHA/size, Flush(true), закрыть handles. Требуется место минимум
   на старый backup (~740 МиБ) + временный candidate (~1212 МиБ), с запасом.
   Ещё раз rehash live перед commit и проверить owner processes.
5. Выполнить единственный атомарный `System.IO.File.Replace(temp, live,
   nonNullRollbackPath)`. Rollback path — уникальный соседний файл, **не `$null`**;
   это сохранит прежний live кроме заранее проверенного backup. Не использовать
   «удалить live, потом копировать» или overwrite поток в открытый live.
6. После commit закрыть/сбросить собственные resource caches, повторно открыть live
   через index/document loaders, rehash = candidate, 1222 members, 236/full suffix,
   986 unchanged, 79 MPR/SEC/animation checks. Проверить уже согласованную
   exact migration: profiles/semantic field binding, palette/preview и нужные
   map namespaces. Перезапуск consumers важен: часть cache keys учитывает путь,
   открытый до замены handle может продолжать читать старый файл.
7. При ошибке — остановить consumers, скопировать проверенный old backup в новый
   соседний temp, сверить old SHA, выполнить File.Replace с **другим ненулевым**
   forensic backup для отклонённого candidate, rehash live = old SHA; вернуть только
   принадлежащие этой операции перенесённые profiles/semantic files по их receipts.
   Сохранить candidate/логи/rollback для диагностики, чужие изменившиеся файлы не
   переписывать. Не менять MPR/SEC или постоянную установку в рамках этой операции.

Что увидит владелец:

- В 21 полностью HD карте Dejavu — прежний layout, слоты, quarter-turn и вода,
  более детальные stock тайлы256; stock palette узнается встроенным preset.
  Изображений новых материалов не добавлено. В Base fallback изменятся соответствующие
  34 карты, поскольку active profile отдаёт HDPack раньше обычных textures.
- bz10k/zone12/zone2/zone8 без migration: точный embedded profile отвергается,
  возможен draft/недоступность части материалов и semantic restore warning;
  при наличии пользовательского recovery snapshot возможен запрос восстановления
  прежнего exact atlas. Его нельзя автоматически принимать как HD migration:
  это может вернуть прежние страницы. Даже с migration общий GL renderer этих
  смешанных карт останется128 до отдельного разрешённого изменения pipeline.
- Нынешние published Zone6/Zone9/Zone3obr/Zone19 — прежняя визуальная детализация128,
  их собственные palette/semantic bindings сохраняются; доступны восстановленные
  stock источники. Zone17/18/19 stock и изменённое меню не заменялись.
- В игре затронуты карты, которые реально разрешают 236 исходных page names;
  MPR/SEC, материалы и анимационные индексы прежние, BC1 mips исходные.
  Больший верхний mip может повысить детализацию и память, но actual selected mip,
  fallback, renderer residency, соседние карты и художественный вид в этой задаче
  не проверены. Editor profile JSON/semantic fields игре не нужны; их отказ —
  риск инструмента редактирования, а не автоматическое повреждение игрового SEC.

## Сохранность, границы и передача

AGENTS.md проверен с диска; SHA256
`f4b7fdd5ea82fcdf43d002d23d64ec36be853650f054edf70376527cd6432ec3`,
при final guard прежний. Прочитаны CONTRIBUTING, PROJECT_CONTRACT,
HDPACK_COMPARE(c41d9755), TERRAIN_ATLAS_REVIEW, all-map-hd-bindings-r8,
синтетический memory report и относящиеся текущие ROADMAP зависимости.
Проектная карта не изменялась: последнее поручение разрешает Git-запись только
этого отчёта; следующий пункт для сопровождающего сформулирован ниже.

Нет сети, новых зависимостей/чатов/подагентов, интерактивного редактора/игры,
публикации или установки. `evil-islands-modding` и
`evil-islands-private-engine-reference` недоступны; приватные исходники движка
не читались. Использованы обычные исходники проекта и разрешённые данные.
Код программы checkout, VERSION, CHANGELOG, ROADMAP, чужие build-*-136-* и
постоянная установка не изменялись. Исследовательские wrappers, baseline snapshot,
RES/карты/receipts/logs находятся только в игнорируемом build-tests-hdpack-restore;
обвязка не выдаётся как новая сборка маппера. Finalizer/установка не применялись.
В Git — только этот отчёт, без игровых изображений и payloads.

Final guard: **2/2 живых RES hashes**, **79/79 MPR hashes**, **4/4 private map copies**
и candidate SHA прежние, **24/24** benchmark source guards PASS. Receipt:
[final-guard.json](../../build-tests-hdpack-restore/final-guard.json).
Полные количества/slot/rotation/animation rows:
[analysis.json](../../build-tests-hdpack-restore/analysis.json).

Предлагаемый текущий пункт ROADMAP (не записан):
**EI-HDPACK-RESTORE-PREP-20261010** — кандидат236 готов и проверен; прямое применение
имеет известный blocker сохранности editor identity (12 profiles/10 fields,
active4). Разрешение только подготовка, живой файл не менять. Следующий шаг —
отдельно поручить exact profile/semantic migration с сохранением authored данных,
учесть принятую136, затем применять guarded replacement. Полную256 детализацию
уже опубликованных карт и mixed-resolution rendering вести отдельными подзадачами;
нынешняя подготовка их не закрывает.
