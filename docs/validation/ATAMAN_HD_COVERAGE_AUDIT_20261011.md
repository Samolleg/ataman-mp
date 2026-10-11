# EI-HD-COVERAGE-AUDIT — однородные тайлы и страницы HDPack

Дата: 2026-10-11. Аудит только для чтения; версия программы не менялась, коммит не создавался.

## Краткий ответ

- Текущий stock-банк Dejavu: из 114 комбинаций «материал / проверенный класс / слой» ≥1 / ≥2 / ≥4 различных HD-изображения имеют **95 / 56 / 17**; у **19** HD-изображений не найдено. Если исключить анимационные кадры и неизвестный статус анимации: **91 / 52 / 13** (консервативная оценка статических вариантов).
- Исходный stock-банк JamaisVu135: из 114 комбинаций «материал / проверенный класс / слой» ≥1 / ≥2 / ≥4 различных HD-изображения имеют **99 / 56 / 17**; у **15** HD-изображений не найдено. Если исключить анимационные кадры и неизвестный статус анимации: **95 / 52 / 13** (консервативная оценка статических вариантов).

Для вопроса именно об однородных материалах: исходный банк содержит **100** комбинаций материал/класс/слой с uniform, остальные **14** представлены только переходами/углами. Среди исходных uniform без HD: **Stone / Desert, класс 3, land**. В текущих точных профилях Dejavu подтверждено **95** uniform-комбинаций. Разные классы одного имени не объединяются (например, Stone / Desert класса 2 имеет HD).

Исходные uniform-комбинации, не подтверждённые как uniform в текущих профилях Dejavu: Grass / Faded [класс 11, land]; Ground / Dry [класс 2, land]; Ground / Water [класс 0, land]; Rock / Underwater emerald [класс 2, land]; Stone / Desert [класс 3, land]. Это разница подтверждённой семантики банков, а не доказательство отсутствия похожих пикселей.


**Ответ «все материалы уже имеют несколько однородных HD-вариантов» не подтверждается.** Точные отсутствующие материалы и материалы только с угловыми/переходными сигнатурами перечислены ниже. Семантические варианты, runtime-классы и анимационные фазы не взаимозаменяемы.

Страницы Dejavu HDPack: карты Dejavu ссылаются на **571.365 МиБ**, только Base — на **220.674 МиБ**, ни одна из проверенных карт — на **422.054 МиБ**. Категории взаимоисключающие, при общей ссылке приоритет у Dejavu.

Если целиком относить namespace к одной категории: Dejavu **571.365 МиБ**, только Base **253.345 МиБ**, без ссылок **389.383 МиБ**. Разница **32.671 МиБ** — страницы сверх textureCount внутри используемых namespace.

## Метод и границы

- Декодирование согласовано с текущим `source/resources/mmp_codec.cpp:169`: DXT1 сохраняет alpha. Старый `scripts/generate_terrain_atlas_presets.py` принудительно ставит alpha=255; его RGB-декодер переиспользован с восстановлением корректной alpha. После этого все 76 привязок (38×2) подтверждены. Первоначальное подозрение на неверные SHA профилей снято; это была разница декодеров.
- Использован существующий признак uniform: назначенная, не ambiguous сигнатура с четырьмя одинаковыми материалами. Код: `source/render/terrain_atlas_profile.cpp:1511`, `source/documents/terrain_mixed_edge.cpp:11`, `source/documents/terrain_edit_model.cpp:53`. Встроенные семейства: `assets/resources/terrain_atlas_presets.json`; глобальная группировка: `source/render/terrain_atlas_presets.cpp:117`.
- Класс в таблицах материалов — проверенный `family.tileType` (0..15). Физический класс взят отдельно из MP соответствующего namespace: для исходного stock — Base, для текущего Dejavu — текущий MP, либо Base только при подтверждённом неизменном stock-префиксе. Они могут различаться: mixed-edge 139 явно различает physical source class и reviewed output-cell class (`terrain_mixed_edge.cpp:156`). Неизвестные физические классы обозначены `?`, а не подставлены из имени материала.
- Вариант в таблицах материалов — уникальный SHA декодированных RGBA-пикселей вместе с размером. Точные дубликаты в пределах строки и повороты не увеличивают число. Для сводки одинаковые изображения из разных namespace также объединены. Совпадение после поворота и визуальное сходство не проверялись; оригинальность рисунка и художественное качество не оценивались.
- Колонки 256/128 означают физический размер тайла (страница 2048/1024 с сеткой 8×8), а не имя HDPack. 128 — сохранённые в 128 px варианты; это не утверждение, что материал целиком SD. Анимационные кадры входят в полное число изображений и вынесены отдельно. Для 139 требуется неанимированный opaque источник того же физического класса; наличие в этой таблице само по себе не обещает доступность всех вариантов в каждой конкретной карте.
- Семантика применяется только после точного пиксельного SHA всего атласа или исходного stock-префикса. Вложенный профиль с неверным SHA не используется. Исходный bank JamaisVu135 не подменяется расширенными/перепакованными слотами Dejavu. Добавленные страницы текущих stock namespace учитываются по точному вложенному профилю; опубликованные namespace карт в таблицы stock-материалов не смешиваются.
- HD-оригинал конкретного SD-варианта подтверждается проверенной stock-позицией или provenance `sourceNamespace/sourceSlots` точного профиля при совпадении материала. «Не установлен» означает нехватку такой связи; HD другого изображения того же материала не считается оригиналом этого слота. Пиксельное восстановление происхождения после произвольного масштабирования не выполняется.
- Ссылки читаются из единственного `.MP` внутри каждого MPR, регистр namespace несущественен. Страницы 0..textureCount−1 считаются ссылками MP, даже если SEC сейчас не использует все слоты. Это консервативный анализ зависимости атласа, не анализ видимых клеток. Пути/названия внешних MPR не используются для угадывания namespace.
- МиБ = сумма размеров MMP-членов архива / 1048576 (включая заголовки и mip-данные), не видеопамять и не размер RGBA. JSON-профили, семантика и служебная структура RES подсчитаны отдельно.
- Обследованы только Maps Dejavu и Base, рекурсивно, без saves/backup/других модов и внешних карт. «Нет ссылок» — кандидат для последующего анализа переноса, не разрешение удалять. Stock-страницы нужны глобальной палитре маппера; старые published-страницы могут быть историческими остатками.
- Навык evil-islands-modding не найден в доступном каталоге и локальных plugins/skills. Приватные исходники движка не использовались. AGENTS.md и docs/PROJECT_CONTRACT.md прочитаны. Сеть, установка зависимостей, интерактивный редактор, системный буфер и сборка не использовались.

## Снимок и консистентность

База чтения: main `87d2aec3c4abc1dff2180c5f38b42976bd4f5ba0`. Начало: `2026-10-11T11:36:08.976183+00:00`; завершение чтения MPR: `2026-10-11T11:36:42.165605+00:00`. Все времена UTC. Пакеты читаются только из приватных копий.

**Это последовательный снимок, не единая атомарная транзакция пак+все MPR.** Владелец переводит карты во время аудита. Для каждого RES совпали SHA живого файла до копирования, SHA копии и SHA живого файла после копирования; каждый MPR прочитан в память, его SHA сверён повторным чтением. SHA и время каждой карты приведены ниже. Уже после своего чтения файл мог измениться. После завершения перевода карт следует выполнить новый полный запуск.

| Копия | Байт | SHA256 (до = копия = после) | Время копирования UTC |
|---|---|---|---|
| dejavu_hd | 1286206772 | `59bb40569aea33a6a04d7758f74b48b7e98b9dcbc0412f691e1c10e239e7c4df` | 2026-10-11T11:36:09.003690+00:00 — 2026-10-11T11:36:11.528904+00:00 |
| jamaisvu_hd | 682308638 | `50c285225fb1745ee70d280e57a214f346481e6d380a78d55475f730d11a81b2` | 2026-10-11T11:36:11.528904+00:00 — 2026-10-11T11:36:12.854296+00:00 |
| jamaisvu_sd | 763801000 | `2e8ca42fb75fac12e5659c563c547fee0f72d6c1b6e2e1c454b3072e3be0703b` | 2026-10-11T11:36:12.855296+00:00 — 2026-10-11T11:36:14.414907+00:00 |
| base_sd | 107649220 | `79548fbd94e797fe9edc3e990eb0231a350571b2cef575b009fa972f0002268c` | 2026-10-11T11:36:14.415749+00:00 — 2026-10-11T11:36:14.651657+00:00 |

SHA каталога семантики: `eca998d6955b9835da720143df266d6c3f6547a01468afa60c4e24f3e480f84c`. Приватные manifest/audit: `build-tests-hd-coverage\snapshot-20261011T113608976183Z`. Игровые копии и подробный JSON не включать в коммит.

## Материалы: сводка по уникальным изображениям

Имена сохранены точно как в существующем каталоге; land = земля, overlay = верхний слой. Порядок классов не является рейтингом качества. Нулевая строка может означать материал, представленный только в углах переходов. Последняя колонка — статические HD с известным статусом.

| Материал | Класс | Слой | Dejavu 256 | Dejavu 128 | JMV 256 | JMV 128 | Статические HD D/J |
|---|---|---|---|---|---|---|---|
| Grass / Burnt | 0 | land | 1 | 1 | 1 | 0 | 1/1 |
| Grass / Common | 0 | land | 9 | 9 | 9 | 0 | 9/9 |
| Grass / Dark | 0 | land | 2 | 3 | 2 | 0 | 2/2 |
| Grass / Faded | 0 | land | 1 | 1 | 1 | 0 | 1/1 |
| Grass / Faded | 11 | land | 0 | 0 | 1 | 0 | 0/1 |
| Grass / Fresh | 0 | land | 6 | 8 | 6 | 0 | 6/6 |
| Grass / Irregular | 0 | land | 17 | 19 | 20 | 0 | 17/20 |
| Grass / Sparse dark | 0 | land | 1 | 1 | 1 | 0 | 1/1 |
| Grass / Sparse dark | 2 | land | 1 | 0 | 1 | 0 | 1/1 |
| Grass / Sparse light | 0 | land | 1 | 2 | 1 | 0 | 1/1 |
| Grass / Swamp | 0 | land | 7 | 5 | 7 | 0 | 7/7 |
| Grass / Tundra | 0 | land | 1 | 0 | 1 | 0 | 1/1 |
| Grass / Winter faded | 0 | land | 1 | 0 | 1 | 0 | 1/1 |
| Grass / Winter normal | 0 | land | 1 | 1 | 1 | 0 | 1/1 |
| Grass / Yellow lichen | 0 | land | 2 | 0 | 2 | 0 | 2/2 |
| Ground / Brown | 0 | land | 2 | 0 | 2 | 0 | 2/2 |
| Ground / Brownish | 1 | land | 1 | 2 | 1 | 0 | 1/1 |
| Ground / Common | 0 | land | 1 | 2 | 1 | 0 | 1/1 |
| Ground / Common | 1 | land | 1 | 0 | 1 | 0 | 1/1 |
| Ground / Dark | 0 | land | 1 | 0 | 1 | 0 | 1/1 |
| Ground / Dark brown | 1 | land | 2 | 0 | 2 | 0 | 2/2 |
| Ground / Dark underwater | 1 | land | 1 | 0 | 1 | 0 | 1/1 |
| Ground / Dead | 1 | land | 18 | 5 | 18 | 0 | 18/18 |
| Ground / Desert algae | 1 | land | 3 | 6 | 3 | 6 | 3/3 |
| Ground / Dirt | 0 | land | 0 | 0 | 0 | 0 | 0/0 |
| Ground / Dirt | 1 | land | 1 | 0 | 1 | 0 | 1/1 |
| Ground / Dirt | 2 | land | 1 | 1 | 1 | 0 | 1/1 |
| Ground / Dry | 0 | land | 1 | 1 | 1 | 0 | 1/1 |
| Ground / Dry | 2 | land | 0 | 0 | 1 | 0 | 0/1 |
| Ground / Gravel | 1 | land | 1 | 2 | 1 | 0 | 1/1 |
| Ground / Gravel greenish | 1 | land | 1 | 1 | 1 | 0 | 1/1 |
| Ground / Gray | 3 | land | 1 | 0 | 1 | 0 | 1/1 |
| Ground / Light | 1 | land | 1 | 0 | 1 | 0 | 1/1 |
| Ground / Light | 9 | land | 1 | 0 | 1 | 0 | 1/1 |
| Ground / Plant soil | 1 | land | 2 | 3 | 2 | 3 | 2/2 |
| Ground / Plant soil | 2 | land | 0 | 0 | 0 | 0 | 0/0 |
| Ground / Red | 1 | land | 2 | 4 | 2 | 4 | 2/2 |
| Ground / Stony gray | 2 | land | 4 | 0 | 4 | 0 | 4/4 |
| Ground / Tan | 1 | land | 2 | 0 | 2 | 0 | 2/2 |
| Ground / Topsoil | 1 | land | 2 | 2 | 2 | 2 | 2/2 |
| Ground / Topsoil | 3 | land | 0 | 0 | 0 | 0 | 0/0 |
| Ground / Water | 0 | land | 0 | 0 | 1 | 0 | 0/1 |
| Ground / Water | 1 | land | 1 | 1 | 1 | 0 | 1/1 |
| Ice / Common | 10 | land | 5 | 0 | 5 | 0 | 5/5 |
| Ice / Irregular | 1 | land | 2 | 0 | 2 | 0 | 2/2 |
| Lava / Orange | 13 | overlay | 3 | 0 | 3 | 0 | 3/3 |
| Lava / Red | 13 | overlay | 3 | 0 | 3 | 0 | 3/3 |
| Paving / Cobblestone | 7 | land | 1 | 1 | 1 | 0 | 1/1 |
| Paving / Radial | 7 | land | 1 | 0 | 1 | 0 | 1/1 |
| Paving / Sand ruins | 7 | land | 2 | 4 | 2 | 4 | 2/2 |
| Paving / Square | 7 | land | 3 | 0 | 3 | 0 | 3/3 |
| Rock / Black | 2 | land | 3 | 0 | 3 | 0 | 3/3 |
| Rock / Greenish | 4 | land | 1 | 1 | 1 | 0 | 1/1 |
| Rock / Snow | 9 | land | 1 | 1 | 1 | 0 | 1/1 |
| Rock / Underwater emerald | 2 | land | 0 | 0 | 1 | 0 | 0/1 |
| Rock / Underwater emerald | 3 | land | 1 | 0 | 1 | 0 | 1/1 |
| Rubble / Dry | 1 | land | 2 | 2 | 2 | 2 | 2/2 |
| Rubble / Dry | 7 | land | 0 | 0 | 0 | 0 | 0/0 |
| Rubble / Gray | 2 | land | 2 | 0 | 2 | 0 | 2/2 |
| Rubble / Orange | 1 | land | 2 | 2 | 2 | 2 | 2/2 |
| Rubble / Red | 1 | land | 4 | 0 | 4 | 0 | 4/4 |
| Sand / Common | 3 | land | 1 | 1 | 1 | 0 | 1/1 |
| Sand / Common grayish | 3 | land | 1 | 2 | 1 | 2 | 1/1 |
| Sand / Dark | 3 | land | 1 | 2 | 1 | 2 | 1/1 |
| Sand / Dark pink | 3 | land | 2 | 0 | 2 | 0 | 2/2 |
| Sand / Grainy | 2 | land | 0 | 0 | 0 | 0 | 0/0 |
| Sand / Grainy | 3 | land | 2 | 0 | 2 | 0 | 2/2 |
| Sand / Grass | 3 | land | 2 | 9 | 2 | 4 | 2/2 |
| Sand / Gravel | 3 | land | 2 | 2 | 2 | 2 | 2/2 |
| Sand / Irregular | 2 | land | 3 | 0 | 3 | 0 | 3/3 |
| Sand / Light | 3 | land | 1 | 0 | 1 | 0 | 1/1 |
| Sand / Underwater | 3 | land | 1 | 0 | 1 | 0 | 1/1 |
| Sand / Yellow | 3 | land | 1 | 1 | 1 | 1 | 1/1 |
| Slabs / Carved | 2 | land | 3 | 3 | 3 | 0 | 3/3 |
| Slabs / Faces | 2 | land | 3 | 0 | 3 | 0 | 3/3 |
| Slabs / Faces | 7 | land | 3 | 0 | 3 | 0 | 3/3 |
| Slabs / Faces irregular | 7 | land | 1 | 0 | 1 | 0 | 1/1 |
| Slabs / Square | 2 | land | 13 | 0 | 13 | 0 | 13/13 |
| Snow / Common | 9 | land | 18 | 8 | 18 | 0 | 18/18 |
| Snow / Dark | 0 | land | 0 | 0 | 0 | 0 | 0/0 |
| Snow / Dark | 9 | land | 2 | 2 | 2 | 0 | 2/2 |
| Snow / Dark | 10 | land | 1 | 0 | 1 | 0 | 1/1 |
| Stone / Beige | 2 | land | 2 | 0 | 2 | 0 | 2/2 |
| Stone / Beige | 2 | land,overlay | 0 | 0 | 0 | 0 | 0/0 |
| Stone / Black | 2 | land | 2 | 0 | 2 | 0 | 2/2 |
| Stone / Dark | 2 | land | 3 | 3 | 3 | 0 | 3/3 |
| Stone / Dark gray | 2 | land | 2 | 0 | 2 | 0 | 2/2 |
| Stone / Dark green | 2 | land | 1 | 0 | 1 | 0 | 1/1 |
| Stone / Desert | 1 | land | 0 | 0 | 0 | 0 | 0/0 |
| Stone / Desert | 2 | land | 1 | 2 | 1 | 1 | 1/1 |
| Stone / Desert | 3 | land | 0 | 0 | 0 | 2 | 0/0 |
| Stone / Gray | 0 | land | 0 | 0 | 0 | 0 | 0/0 |
| Stone / Gray | 2 | land | 0 | 0 | 0 | 0 | 0/0 |
| Stone / Gray | 9 | land | 2 | 1 | 2 | 0 | 2/2 |
| Stone / Grayish | 0 | land | 0 | 0 | 0 | 0 | 0/0 |
| Stone / Grayish | 2 | land | 2 | 2 | 2 | 0 | 2/2 |
| Stone / Grayish | 10 | land | 0 | 0 | 0 | 0 | 0/0 |
| Stone / Greenish | 0 | land | 0 | 0 | 0 | 0 | 0/0 |
| Stone / Greenish | 2 | land | 2 | 0 | 2 | 0 | 2/2 |
| Stone / Light | 2 | land | 2 | 3 | 2 | 0 | 2/2 |
| Stone / Lime | 0 | land | 5 | 5 | 5 | 0 | 5/5 |
| Stone / Lime | 2 | land | 2 | 0 | 2 | 0 | 2/2 |
| Stone / Lime | 9 | land | 0 | 0 | 0 | 0 | 0/0 |
| Stone / Mossy | 0 | land | 1 | 1 | 1 | 0 | 1/1 |
| Stone / Orange | 2 | land | 2 | 6 | 2 | 6 | 2/2 |
| Stone / Pink | 0 | land | 4 | 0 | 4 | 0 | 4/4 |
| Stone / Red | 2 | land | 4 | 0 | 4 | 0 | 4/4 |
| Stone / Underwater | 2 | land | 2 | 0 | 2 | 0 | 2/2 |
| Stone / Weathered | 2 | land | 3 | 3 | 3 | 3 | 3/3 |
| Swamp | 14 | overlay | 1 | 0 | 1 | 0 | 1/1 |
| Water / Dark | 6 | overlay | 64 | 0 | 64 | 0 | 0/0 |
| Water / Emerald | 6 | overlay | 32 | 0 | 32 | 0 | 0/0 |
| Water / Still | 6 | overlay | 64 | 96 | 64 | 64 | 0/0 |
| Water / Still dark | 6 | overlay | 32 | 0 | 32 | 0 | 0/0 |

### Где HD нет

**Dejavu:** Grass / Faded [класс 11, land] (нет однородных сигнатур в подтверждённом банке); Ground / Dirt [класс 0, land] (нет однородных сигнатур в подтверждённом банке); Ground / Dry [класс 2, land] (нет однородных сигнатур в подтверждённом банке); Ground / Plant soil [класс 2, land] (нет однородных сигнатур в подтверждённом банке); Ground / Topsoil [класс 3, land] (нет однородных сигнатур в подтверждённом банке); Ground / Water [класс 0, land] (нет однородных сигнатур в подтверждённом банке); Rock / Underwater emerald [класс 2, land] (нет однородных сигнатур в подтверждённом банке); Rubble / Dry [класс 7, land] (нет однородных сигнатур в подтверждённом банке); Sand / Grainy [класс 2, land] (нет однородных сигнатур в подтверждённом банке); Snow / Dark [класс 0, land] (нет однородных сигнатур в подтверждённом банке); Stone / Beige [класс 2, land,overlay] (нет однородных сигнатур в подтверждённом банке); Stone / Desert [класс 1, land] (нет однородных сигнатур в подтверждённом банке); Stone / Desert [класс 3, land] (нет однородных сигнатур в подтверждённом банке); Stone / Gray [класс 0, land] (нет однородных сигнатур в подтверждённом банке); Stone / Gray [класс 2, land] (нет однородных сигнатур в подтверждённом банке); Stone / Grayish [класс 0, land] (нет однородных сигнатур в подтверждённом банке); Stone / Grayish [класс 10, land] (нет однородных сигнатур в подтверждённом банке); Stone / Greenish [класс 0, land] (нет однородных сигнатур в подтверждённом банке); Stone / Lime [класс 9, land] (нет однородных сигнатур в подтверждённом банке).

**JamaisVu135:** Ground / Dirt [класс 0, land] (нет однородных сигнатур в подтверждённом банке); Ground / Plant soil [класс 2, land] (нет однородных сигнатур в подтверждённом банке); Ground / Topsoil [класс 3, land] (нет однородных сигнатур в подтверждённом банке); Rubble / Dry [класс 7, land] (нет однородных сигнатур в подтверждённом банке); Sand / Grainy [класс 2, land] (нет однородных сигнатур в подтверждённом банке); Snow / Dark [класс 0, land] (нет однородных сигнатур в подтверждённом банке); Stone / Beige [класс 2, land,overlay] (нет однородных сигнатур в подтверждённом банке); Stone / Desert [класс 1, land] (нет однородных сигнатур в подтверждённом банке); Stone / Desert [класс 3, land]; Stone / Gray [класс 0, land] (нет однородных сигнатур в подтверждённом банке); Stone / Gray [класс 2, land] (нет однородных сигнатур в подтверждённом банке); Stone / Grayish [класс 0, land] (нет однородных сигнатур в подтверждённом банке); Stone / Grayish [класс 10, land] (нет однородных сигнатур в подтверждённом банке); Stone / Greenish [класс 0, land] (нет однородных сигнатур в подтверждённом банке); Stone / Lime [класс 9, land] (нет однородных сигнатур в подтверждённом банке).

## Runtime-классы 0..15

Числа — однородные физические слоты (включая фазы), по реальному значению MP. Семантический класс семейства выше не подменяет этот столбец.

| Физический класс | Dejavu 256 | Dejavu 128 | JMV 256 | JMV 128 |
|---|---|---|---|---|
| 0 | 193 | 13 | 208 | 0 |
| 1 | 151 | 24 | 153 | 20 |
| 2 | 161 | 17 | 163 | 13 |
| 3 | 62 | 11 | 62 | 11 |
| 4 | 23 | 0 | 23 | 0 |
| 5 | 0 | 0 | 0 | 0 |
| 6 | 800 | 64 | 832 | 64 |
| 7 | 31 | 4 | 31 | 4 |
| 8 | 0 | 0 | 0 | 0 |
| 9 | 247 | 6 | 247 | 0 |
| 10 | 46 | 0 | 46 | 0 |
| 11 | 2 | 0 | 2 | 0 |
| 12 | 0 | 0 | 0 | 0 |
| 13 | 15 | 0 | 15 | 0 |
| 14 | 67 | 0 | 67 | 0 |
| 15 | 0 | 0 | 0 | 0 |
| ? | 0 | 139 | 0 | 0 |

## Материалы по исходным зонам (stock namespace)

В каждой строке считаются уникальные изображения текущего Dejavu stock namespace. Числа JMV относятся к исходной зоне без добавленных страниц. «HD для 128» — сколько уникальных SD имеют подтверждённый HD-оригинал / сколько имеют установленное происхождение, но оригинал тоже 128 / сколько не установлены. «Фазы D» — анимационные слоты 256/128 по доступному MP; `?` — слоты без MP-доказательства. «Физ. D» — реально найденные физические классы соответствующих uniform-слотов.

| Namespace | Материал | Класс | Слой | D 256 | D 128 | HD для 128 да/нет/? | JMV 256/128 | Фазы D | Физ. D |
|---|---|---|---|---|---|---|---|---|---|
| basegipat | Grass / Common | 0 | land | 5 | 0 | 0/0/0 | 5/0 | 0/0 | 0 |
| basegipat | Grass / Dark | 0 | land | 1 | 0 | 0/0/0 | 1/0 | 0/0 | 0 |
| basegipat | Grass / Irregular | 0 | land | 1 | 0 | 0/0/0 | 1/0 | 0/0 | 0 |
| basegipat | Ground / Common | 0 | land | 1 | 0 | 0/0/0 | 1/0 | 0/0 | 1 |
| basegipat | Ground / Water | 0 | land | 0 | 0 | 0/0/0 | 0/0 | 0/0 | — |
| basegipat | Stone / Gray | 0 | land | 0 | 0 | 0/0/0 | 0/0 | 0/0 | — |
| basegipat | Stone / Lime | 0 | land | 2 | 0 | 0/0/0 | 2/0 | 0/0 | 2 |
| basegipat | Swamp | 14 | overlay | 1 | 0 | 0/0/0 | 1/0 | 8/0 | 14 |
| basegipat | Water / Still | 6 | overlay | 0 | 0 | 0/0/0 | 0/0 | 0/0 | — |
| basegipat | Water / Still dark | 6 | overlay | 32 | 0 | 0/0/0 | 32/0 | 32/0 | 6 |
| bz10k | Grass / Winter faded | 0 | land | 1 | 0 | 0/0/0 | 1/0 | 0/0 | 0 |
| bz10k | Grass / Winter normal | 0 | land | 1 | 0 | 0/0/0 | 1/0 | 0/0 | 0 |
| bz10k | Ground / Light | 9 | land | 1 | 0 | 0/0/0 | 1/0 | 0/0 | 1 |
| bz10k | Rock / Snow | 9 | land | 1 | 0 | 0/0/0 | 1/0 | 0/0 | 2 |
| bz10k | Snow / Common | 9 | land | 13 | 1 | 0/0/1 | 13/0 | 0/0 | 9 |
| bz10k | Snow / Dark | 9 | land | 2 | 0 | 0/0/0 | 2/0 | 0/0 | 9 |
| bz10k | Stone / Gray | 9 | land | 2 | 1 | 0/0/1 | 2/0 | 0/0 | 2 |
| bz10k | Water / Still | 6 | overlay | 32 | 0 | 0/0/0 | 32/0 | 32/0 | 6 |
| bz11k | Ground / Light | 1 | land | 1 | 0 | 0/0/0 | 1/0 | 0/0 | 1 |
| bz11k | Ground / Stony gray | 2 | land | 4 | 0 | 0/0/0 | 4/0 | 0/0 | 1 |
| bz11k | Ice / Common | 10 | land | 5 | 0 | 0/0/0 | 5/0 | 0/0 | 10 |
| bz11k | Rock / Black | 2 | land | 3 | 0 | 0/0/0 | 3/0 | 0/0 | 2 |
| bz11k | Sand / Irregular | 2 | land | 3 | 0 | 0/0/0 | 3/0 | 0/0 | 3 |
| bz11k | Snow / Dark | 10 | land | 1 | 0 | 0/0/0 | 1/0 | 0/0 | 9 |
| bz11k | Stone / Black | 2 | land | 2 | 0 | 0/0/0 | 2/0 | 0/0 | 2 |
| bz11k | Stone / Dark | 2 | land | 3 | 0 | 0/0/0 | 3/0 | 0/0 | 2 |
| bz11k | Stone / Dark green | 2 | land | 1 | 0 | 0/0/0 | 1/0 | 0/0 | 2 |
| bz11k | Stone / Grayish | 10 | land | 0 | 0 | 0/0/0 | 0/0 | 0/0 | — |
| bz11k | Water / Dark | 6 | overlay | 32 | 0 | 0/0/0 | 32/0 | 32/0 | 6 |
| bz13h | Ground / Red | 1 | land | 2 | 0 | 0/0/0 | 2/0 | 0/0 | 1 |
| bz13h | Paving / Sand ruins | 7 | land | 0 | 0 | 0/0/0 | 0/0 | 0/0 | — |
| bz13h | Rubble / Orange | 1 | land | 2 | 0 | 0/0/0 | 2/0 | 0/0 | 1 |
| bz13h | Sand / Common grayish | 3 | land | 1 | 0 | 0/0/0 | 1/0 | 0/0 | 3 |
| bz13h | Sand / Dark | 3 | land | 1 | 0 | 0/0/0 | 1/0 | 0/0 | 3 |
| bz13h | Sand / Gravel | 3 | land | 2 | 0 | 0/0/0 | 2/0 | 0/0 | 3 |
| bz13h | Sand / Yellow | 3 | land | 1 | 0 | 0/0/0 | 1/0 | 0/0 | 3 |
| bz13h | Stone / Desert | 1 | land | 0 | 0 | 0/0/0 | 0/0 | 0/0 | — |
| bz13h | Stone / Orange | 2 | land | 2 | 0 | 0/0/0 | 2/0 | 0/0 | 2 |
| bz13h | Stone / Weathered | 2 | land | 3 | 0 | 0/0/0 | 3/0 | 0/0 | 2 |
| bz13h | Water / Still | 6 | overlay | 32 | 0 | 0/0/0 | 32/0 | 32/0 | 6 |
| bz14h | Ground / Tan | 1 | land | 2 | 0 | 0/0/0 | 2/0 | 0/0 | 1 |
| bz14h | Lava / Red | 13 | overlay | 3 | 0 | 0/0/0 | 3/0 | 0/0 | 13 |
| bz14h | Rubble / Red | 1 | land | 4 | 0 | 0/0/0 | 4/0 | 0/0 | 1 |
| bz14h | Stone / Beige | 2 | land | 2 | 0 | 0/0/0 | 2/0 | 0/0 | 2 |
| bz14h | Stone / Desert | 2 | land | 0 | 0 | 0/0/0 | 0/0 | 0/0 | — |
| bz14h | Stone / Red | 2 | land | 4 | 0 | 0/0/0 | 4/0 | 0/0 | 1,2 |
| bz15h | Ground / Plant soil | 2 | land | 0 | 0 | 0/0/0 | 0/0 | 0/0 | — |
| bz15h | Ground / Red | 1 | land | 2 | 0 | 0/0/0 | 2/0 | 0/0 | 1 |
| bz15h | Ground / Tan | 1 | land | 0 | 0 | 0/0/0 | 0/0 | 0/0 | — |
| bz15h | Ground / Topsoil | 1 | land | 2 | 0 | 0/0/0 | 2/0 | 0/0 | 1 |
| bz15h | Rubble / Dry | 1 | land | 2 | 0 | 0/0/0 | 2/0 | 0/0 | 1 |
| bz15h | Rubble / Orange | 1 | land | 2 | 0 | 0/0/0 | 2/0 | 0/0 | 1 |
| bz15h | Stone / Orange | 2 | land | 2 | 0 | 0/0/0 | 2/0 | 0/0 | 2 |
| bz16h | Ground / Desert algae | 1 | land | 3 | 0 | 0/0/0 | 3/0 | 0/0 | 1 |
| bz16h | Ground / Plant soil | 1 | land | 2 | 0 | 0/0/0 | 2/0 | 0/0 | 1 |
| bz16h | Ground / Red | 1 | land | 2 | 0 | 0/0/0 | 2/0 | 0/0 | 1 |
| bz16h | Ground / Topsoil | 3 | land | 0 | 0 | 0/0/0 | 0/0 | 0/0 | — |
| bz16h | Paving / Sand ruins | 7 | land | 2 | 0 | 0/0/0 | 2/0 | 0/0 | 7 |
| bz16h | Rubble / Dry | 7 | land | 0 | 0 | 0/0/0 | 0/0 | 0/0 | — |
| bz16h | Sand / Common grayish | 3 | land | 1 | 0 | 0/0/0 | 1/0 | 0/0 | 3 |
| bz16h | Sand / Dark | 3 | land | 1 | 0 | 0/0/0 | 1/0 | 0/0 | 3 |
| bz16h | Sand / Grass | 3 | land | 2 | 0 | 0/0/0 | 2/0 | 0/0 | 3 |
| bz16h | Sand / Gravel | 3 | land | 0 | 0 | 0/0/0 | 0/0 | 0/0 | — |
| bz16h | Stone / Beige | 2 | land | 0 | 0 | 0/0/0 | 0/0 | 0/0 | — |
| bz16h | Stone / Desert | 2 | land | 1 | 0 | 0/0/0 | 1/0 | 0/0 | 2 |
| bz16h | Stone / Orange | 2 | land | 2 | 0 | 0/0/0 | 2/0 | 0/0 | 2 |
| bz16h | Stone / Weathered | 2 | land | 3 | 0 | 0/0/0 | 3/0 | 0/0 | 2 |
| bz16h | Water / Still | 6 | overlay | 32 | 0 | 0/0/0 | 32/0 | 32/0 | 6 |
| bz18h | Ground / Tan | 1 | land | 2 | 0 | 0/0/0 | 2/0 | 0/0 | 1 |
| bz18h | Lava / Red | 13 | overlay | 3 | 0 | 0/0/0 | 3/0 | 0/0 | 13 |
| bz18h | Rubble / Red | 1 | land | 4 | 0 | 0/0/0 | 4/0 | 0/0 | 1 |
| bz18h | Stone / Beige | 2 | land | 2 | 0 | 0/0/0 | 2/0 | 0/0 | 2 |
| bz18h | Stone / Desert | 2 | land | 0 | 0 | 0/0/0 | 0/0 | 0/0 | — |
| bz18h | Stone / Red | 2 | land | 4 | 0 | 0/0/0 | 4/0 | 0/0 | 1,2 |
| bz2g | Grass / Burnt | 0 | land | 1 | 0 | 0/0/0 | 1/0 | 0/0 | 0 |
| bz2g | Grass / Common | 0 | land | 3 | 0 | 0/0/0 | 3/0 | 0/0 | 0 |
| bz2g | Grass / Dark | 0 | land | 1 | 0 | 0/0/0 | 1/0 | 0/0 | 0 |
| bz2g | Grass / Faded | 0 | land | 1 | 0 | 0/0/0 | 1/0 | 0/0 | 0 |
| bz2g | Grass / Fresh | 0 | land | 6 | 0 | 0/0/0 | 6/0 | 0/0 | 0 |
| bz2g | Grass / Irregular | 0 | land | 0 | 0 | 0/0/0 | 0/0 | 0/0 | — |
| bz2g | Ground / Brown | 0 | land | 2 | 0 | 0/0/0 | 2/0 | 0/0 | 1 |
| bz2g | Ground / Dark | 0 | land | 1 | 0 | 0/0/0 | 1/0 | 0/0 | 1 |
| bz2g | Ground / Dirt | 0 | land | 0 | 0 | 0/0/0 | 0/0 | 0/0 | — |
| bz2g | Stone / Gray | 0 | land | 0 | 0 | 0/0/0 | 0/0 | 0/0 | — |
| bz2g | Stone / Grayish | 0 | land | 0 | 0 | 0/0/0 | 0/0 | 0/0 | — |
| bz2g | Stone / Lime | 0 | land | 5 | 0 | 0/0/0 | 5/0 | 0/0 | 2 |
| bz2g | Stone / Mossy | 0 | land | 1 | 0 | 0/0/0 | 1/0 | 0/0 | 2 |
| bz2g | Stone / Pink | 0 | land | 4 | 0 | 0/0/0 | 4/0 | 0/0 | 2 |
| bz2g | Swamp | 14 | overlay | 1 | 0 | 0/0/0 | 1/0 | 8/0 | 14 |
| bz3g | Grass / Common | 0 | land | 2 | 0 | 0/0/0 | 2/0 | 0/0 | 0 |
| bz3g | Grass / Dark | 0 | land | 1 | 0 | 0/0/0 | 1/0 | 0/0 | 0 |
| bz3g | Grass / Sparse dark | 0 | land | 1 | 0 | 0/0/0 | 1/0 | 0/0 | 0 |
| bz3g | Grass / Sparse light | 0 | land | 1 | 0 | 0/0/0 | 1/0 | 0/0 | 0 |
| bz3g | Ground / Common | 1 | land | 1 | 0 | 0/0/0 | 1/0 | 0/0 | 1 |
| bz3g | Ground / Dark underwater | 1 | land | 1 | 0 | 0/0/0 | 1/0 | 0/0 | 1 |
| bz3g | Ground / Dead | 1 | land | 11 | 0 | 0/0/0 | 11/0 | 0/0 | 1,4 |
| bz3g | Ground / Gravel | 1 | land | 1 | 0 | 0/0/0 | 1/0 | 0/0 | 1 |
| bz3g | Ground / Gray | 3 | land | 1 | 0 | 0/0/0 | 1/0 | 0/0 | 3 |
| bz3g | Paving / Radial | 7 | land | 1 | 0 | 0/0/0 | 1/0 | 0/0 | 7 |
| bz3g | Sand / Dark pink | 3 | land | 1 | 0 | 0/0/0 | 1/0 | 0/0 | 3 |
| bz3g | Stone / Grayish | 2 | land | 2 | 0 | 0/0/0 | 2/0 | 0/0 | 2 |
| bz3g | Stone / Lime | 2 | land | 2 | 0 | 0/0/0 | 2/0 | 0/0 | 2 |
| bz3g | Swamp | 14 | overlay | 1 | 0 | 0/0/0 | 1/0 | 8/0 | 14 |
| bz3g | Water / Still | 6 | overlay | 32 | 0 | 0/0/0 | 32/0 | 32/0 | 6 |
| bz4g | Grass / Common | 0 | land | 2 | 0 | 0/0/0 | 2/0 | 0/0 | 0 |
| bz4g | Grass / Dark | 0 | land | 1 | 0 | 0/0/0 | 1/0 | 0/0 | 0 |
| bz4g | Grass / Fresh | 0 | land | 1 | 0 | 0/0/0 | 1/0 | 0/0 | 0 |
| bz4g | Grass / Irregular | 0 | land | 6 | 0 | 0/0/0 | 6/0 | 0/0 | 0 |
| bz4g | Ground / Common | 1 | land | 1 | 0 | 0/0/0 | 1/0 | 0/0 | 1 |
| bz4g | Ground / Dark underwater | 1 | land | 1 | 0 | 0/0/0 | 1/0 | 0/0 | 1 |
| bz4g | Ground / Dirt | 1 | land | 0 | 0 | 0/0/0 | 0/0 | 0/0 | — |
| bz4g | Ground / Gravel greenish | 1 | land | 1 | 0 | 0/0/0 | 1/0 | 0/0 | 1 |
| bz4g | Ground / Gray | 3 | land | 1 | 0 | 0/0/0 | 1/0 | 0/0 | 3 |
| bz4g | Paving / Cobblestone | 7 | land | 1 | 0 | 0/0/0 | 1/0 | 0/0 | 7 |
| bz4g | Paving / Sand ruins | 7 | land | 0 | 0 | 0/0/0 | 0/0 | 0/0 | — |
| bz4g | Rock / Greenish | 4 | land | 1 | 0 | 0/0/0 | 1/0 | 0/0 | 4 |
| bz4g | Sand / Common | 3 | land | 1 | 0 | 0/0/0 | 1/0 | 0/0 | 3 |
| bz4g | Sand / Dark | 3 | land | 0 | 0 | 0/0/0 | 0/0 | 0/0 | — |
| bz4g | Sand / Dark pink | 3 | land | 1 | 0 | 0/0/0 | 1/0 | 0/0 | 3 |
| bz4g | Sand / Grainy | 2 | land | 0 | 0 | 0/0/0 | 0/0 | 0/0 | — |
| bz4g | Stone / Lime | 2 | land | 2 | 0 | 0/0/0 | 2/0 | 0/0 | 2 |
| bz4g | Water / Still | 6 | overlay | 32 | 0 | 0/0/0 | 32/0 | 32/0 | 6 |
| bz5g | Grass / Common | 0 | land | 5 | 0 | 0/0/0 | 5/0 | 0/0 | 0 |
| bz5g | Grass / Dark | 0 | land | 1 | 0 | 0/0/0 | 1/0 | 0/0 | 0 |
| bz5g | Grass / Swamp | 0 | land | 1 | 0 | 0/0/0 | 1/0 | 0/0 | 0 |
| bz5g | Ground / Common | 1 | land | 1 | 0 | 0/0/0 | 1/0 | 0/0 | 1 |
| bz5g | Ground / Gravel | 1 | land | 1 | 0 | 0/0/0 | 1/0 | 0/0 | 1 |
| bz5g | Rock / Underwater emerald | 3 | land | 1 | 0 | 0/0/0 | 1/0 | 0/0 | 2 |
| bz5g | Sand / Common | 3 | land | 1 | 0 | 0/0/0 | 1/0 | 0/0 | 3 |
| bz5g | Sand / Dark pink | 3 | land | 2 | 0 | 0/0/0 | 2/0 | 0/0 | 3 |
| bz5g | Sand / Grainy | 3 | land | 2 | 0 | 0/0/0 | 2/0 | 0/0 | 3 |
| bz5g | Sand / Light | 3 | land | 1 | 0 | 0/0/0 | 1/0 | 0/0 | 3 |
| bz5g | Sand / Underwater | 3 | land | 1 | 0 | 0/0/0 | 1/0 | 0/0 | 3 |
| bz5g | Stone / Light | 2 | land | 2 | 0 | 0/0/0 | 2/0 | 0/0 | 2 |
| bz5g | Water / Emerald | 6 | overlay | 32 | 0 | 0/0/0 | 32/0 | 32/0 | 6 |
| bz6g | Grass / Common | 0 | land | 5 | 0 | 0/0/0 | 5/0 | 0/0 | 0 |
| bz6g | Grass / Dark | 0 | land | 1 | 0 | 0/0/0 | 1/0 | 0/0 | 0 |
| bz6g | Grass / Swamp | 0 | land | 1 | 0 | 0/0/0 | 1/0 | 0/0 | 0 |
| bz6g | Ground / Common | 1 | land | 1 | 0 | 0/0/0 | 1/0 | 0/0 | 1 |
| bz6g | Ground / Gravel | 1 | land | 1 | 0 | 0/0/0 | 1/0 | 0/0 | 1 |
| bz6g | Rock / Underwater emerald | 3 | land | 1 | 0 | 0/0/0 | 1/0 | 0/0 | 2 |
| bz6g | Sand / Common | 3 | land | 1 | 0 | 0/0/0 | 1/0 | 0/0 | 3 |
| bz6g | Sand / Dark pink | 3 | land | 2 | 0 | 0/0/0 | 2/0 | 0/0 | 3 |
| bz6g | Sand / Grainy | 3 | land | 2 | 0 | 0/0/0 | 2/0 | 0/0 | 3 |
| bz6g | Sand / Light | 3 | land | 1 | 0 | 0/0/0 | 1/0 | 0/0 | 3 |
| bz6g | Sand / Underwater | 3 | land | 1 | 0 | 0/0/0 | 1/0 | 0/0 | 3 |
| bz6g | Stone / Light | 2 | land | 2 | 0 | 0/0/0 | 2/0 | 0/0 | 2 |
| bz6g | Water / Emerald | 6 | overlay | 32 | 0 | 0/0/0 | 32/0 | 32/0 | 6 |
| bz7g | Grass / Sparse dark | 2 | land | 1 | 0 | 0/0/0 | 1/0 | 0/0 | 0 |
| bz7g | Ground / Dirt | 2 | land | 1 | 0 | 0/0/0 | 1/0 | 0/0 | 1 |
| bz7g | Lava / Orange | 13 | overlay | 3 | 0 | 0/0/0 | 3/0 | 0/0 | 13 |
| bz7g | Slabs / Faces | 2 | land | 3 | 0 | 0/0/0 | 3/0 | 0/0 | 7 |
| bz7g | Stone / Dark | 2 | land | 0 | 0 | 0/0/0 | 0/0 | 0/0 | — |
| bz7g | Stone / Dark gray | 2 | land | 2 | 0 | 0/0/0 | 2/0 | 0/0 | 2 |
| bz7g | Stone / Greenish | 2 | land | 2 | 0 | 0/0/0 | 2/0 | 0/0 | 2 |
| bz8k | Grass / Winter faded | 0 | land | 1 | 0 | 0/0/0 | 1/0 | 0/0 | 0 |
| bz8k | Grass / Winter normal | 0 | land | 1 | 0 | 0/0/0 | 1/0 | 0/0 | 0 |
| bz8k | Grass / Yellow lichen | 0 | land | 2 | 0 | 0/0/0 | 2/0 | 0/0 | 0 |
| bz8k | Ground / Light | 1 | land | 1 | 0 | 0/0/0 | 1/0 | 0/0 | 1 |
| bz8k | Paving / Square | 7 | land | 3 | 0 | 0/0/0 | 3/0 | 0/0 | 7 |
| bz8k | Snow / Common | 9 | land | 16 | 0 | 0/0/0 | 16/0 | 0/0 | 9 |
| bz8k | Snow / Dark | 9 | land | 2 | 0 | 0/0/0 | 2/0 | 0/0 | 9 |
| bz8k | Stone / Gray | 9 | land | 2 | 0 | 0/0/0 | 2/0 | 0/0 | 2 |
| bz8k | Stone / Lime | 9 | land | 0 | 0 | 0/0/0 | 0/0 | 0/0 | — |
| bz8k | Water / Still | 6 | overlay | 32 | 0 | 0/0/0 | 32/0 | 32/0 | 6 |
| bz9k | Grass / Winter faded | 0 | land | 1 | 0 | 0/0/0 | 1/0 | 0/0 | 0 |
| bz9k | Grass / Winter normal | 0 | land | 1 | 0 | 0/0/0 | 1/0 | 0/0 | 0 |
| bz9k | Grass / Yellow lichen | 0 | land | 2 | 0 | 0/0/0 | 2/0 | 0/0 | 0 |
| bz9k | Ground / Light | 1 | land | 1 | 0 | 0/0/0 | 1/0 | 0/0 | 1 |
| bz9k | Paving / Square | 7 | land | 3 | 0 | 0/0/0 | 3/0 | 0/0 | 7 |
| bz9k | Snow / Common | 9 | land | 16 | 0 | 0/0/0 | 16/0 | 0/0 | 9 |
| bz9k | Snow / Dark | 9 | land | 2 | 0 | 0/0/0 | 2/0 | 0/0 | 9 |
| bz9k | Stone / Gray | 9 | land | 2 | 0 | 0/0/0 | 2/0 | 0/0 | 2 |
| bz9k | Stone / Lime | 9 | land | 0 | 0 | 0/0/0 | 0/0 | 0/0 | — |
| bz9k | Water / Still | 6 | overlay | 32 | 0 | 0/0/0 | 32/0 | 32/0 | 6 |
| zone1 | Grass / Burnt | 0 | land | 0 | 1 | 0/0/1 | 0/0 | 0/0 | ? |
| zone1 | Grass / Common | 0 | land | 5 | 0 | 0/0/0 | 5/0 | 0/0 | 0 |
| zone1 | Grass / Dark | 0 | land | 1 | 0 | 0/0/0 | 1/0 | 0/0 | 0 |
| zone1 | Grass / Irregular | 0 | land | 3 | 0 | 0/0/0 | 3/0 | 0/0 | 0 |
| zone1 | Grass / Sparse light | 0 | land | 0 | 1 | 0/0/1 | 0/0 | 0/0 | ? |
| zone1 | Ground / Common | 0 | land | 1 | 0 | 0/0/0 | 0/0 | 0/0 | 1 |
| zone1 | Ground / Common | 1 | land | 0 | 0 | 0/0/0 | 1/0 | 0/0 | — |
| zone1 | Ground / Dark underwater | 1 | land | 1 | 0 | 0/0/0 | 1/0 | 0/0 | 1 |
| zone1 | Ground / Dry | 0 | land | 0 | 1 | 0/0/1 | 0/0 | 0/0 | ? |
| zone1 | Ground / Water | 1 | land | 1 | 0 | 0/0/0 | 1/0 | 0/0 | 1 |
| zone1 | Sand / Grass | 3 | land | 0 | 6 | 0/0/6 | 0/0 | 0/0 | ? |
| zone1 | Slabs / Carved | 2 | land | 0 | 3 | 0/0/3 | 0/0 | 0/0 | ? |
| zone1 | Stone / Light | 2 | land | 0 | 2 | 0/0/2 | 0/0 | 0/0 | ? |
| zone1 | Stone / Lime | 0 | land | 2 | 0 | 0/0/0 | 0/0 | 0/0 | 2 |
| zone1 | Stone / Lime | 2 | land | 0 | 0 | 0/0/0 | 2/0 | 0/0 | — |
| zone1 | Stone / Mossy | 0 | land | 0 | 1 | 0/0/1 | 0/0 | 0/0 | ? |
| zone1 | Swamp | 14 | overlay | 1 | 0 | 0/0/0 | 1/0 | 8/0 | 14 |
| zone1 | Water / Still | 6 | overlay | 32 | 0 | 0/0/0 | 32/0 | 32/0 | 6 |
| zone10 | Grass / Sparse dark | 0 | land | 1 | 0 | 0/0/0 | 1/0 | 0/0 | 0 |
| zone10 | Grass / Swamp | 0 | land | 1 | 0 | 0/0/0 | 1/0 | 0/0 | 0 |
| zone10 | Ground / Dirt | 1 | land | 1 | 0 | 0/0/0 | 1/0 | 0/0 | 1 |
| zone10 | Lava / Orange | 13 | overlay | 3 | 0 | 0/0/0 | 3/0 | 0/0 | 13 |
| zone10 | Slabs / Carved | 2 | land | 3 | 0 | 0/0/0 | 3/0 | 0/0 | 2 |
| zone10 | Slabs / Faces | 7 | land | 3 | 0 | 0/0/0 | 3/0 | 0/0 | 7 |
| zone10 | Slabs / Faces irregular | 7 | land | 1 | 0 | 0/0/0 | 1/0 | 0/0 | 2 |
| zone10 | Slabs / Square | 2 | land | 13 | 0 | 0/0/0 | 13/0 | 0/0 | 2,7 |
| zone10 | Stone / Dark gray | 2 | land | 2 | 0 | 0/0/0 | 2/0 | 0/0 | 2 |
| zone10 | Stone / Greenish | 2 | land | 2 | 0 | 0/0/0 | 2/0 | 0/0 | 2 |
| zone11 | Grass / Winter faded | 0 | land | 1 | 0 | 0/0/0 | 1/0 | 0/0 | 0 |
| zone11 | Grass / Winter normal | 0 | land | 1 | 0 | 0/0/0 | 1/0 | 0/0 | 0 |
| zone11 | Grass / Yellow lichen | 0 | land | 2 | 0 | 0/0/0 | 2/0 | 0/0 | 0 |
| zone11 | Ground / Light | 1 | land | 0 | 0 | 0/0/0 | 1/0 | 0/0 | — |
| zone11 | Ground / Light | 9 | land | 1 | 0 | 0/0/0 | 0/0 | 0/0 | 1 |
| zone11 | Paving / Square | 7 | land | 3 | 0 | 0/0/0 | 3/0 | 0/0 | 7 |
| zone11 | Snow / Common | 9 | land | 16 | 0 | 0/0/0 | 16/0 | 0/0 | 9 |
| zone11 | Snow / Dark | 9 | land | 2 | 0 | 0/0/0 | 2/0 | 0/0 | 9 |
| zone11 | Stone / Gray | 9 | land | 2 | 0 | 0/0/0 | 2/0 | 0/0 | 2 |
| zone11 | Stone / Lime | 9 | land | 0 | 0 | 0/0/0 | 0/0 | 0/0 | — |
| zone11 | Water / Still | 6 | overlay | 32 | 0 | 0/0/0 | 32/0 | 32/0 | 6 |
| zone12 | Grass / Winter faded | 0 | land | 1 | 0 | 0/0/0 | 1/0 | 0/0 | 0 |
| zone12 | Grass / Winter normal | 0 | land | 1 | 0 | 0/0/0 | 1/0 | 0/0 | 0 |
| zone12 | Ground / Light | 9 | land | 1 | 0 | 0/0/0 | 1/0 | 0/0 | 1 |
| zone12 | Rock / Snow | 9 | land | 1 | 0 | 0/0/0 | 1/0 | 0/0 | 2 |
| zone12 | Snow / Common | 9 | land | 13 | 2 | 0/0/2 | 13/0 | 0/0 | 9 |
| zone12 | Snow / Dark | 9 | land | 2 | 2 | 0/0/2 | 2/0 | 0/0 | 9 |
| zone12 | Stone / Gray | 9 | land | 2 | 0 | 0/0/0 | 2/0 | 0/0 | 2 |
| zone12 | Water / Still | 6 | overlay | 32 | 0 | 0/0/0 | 32/0 | 32/0 | 6 |
| zone13 | Grass / Tundra | 0 | land | 1 | 0 | 0/0/0 | 1/0 | 0/0 | 0 |
| zone13 | Ground / Light | 1 | land | 0 | 0 | 0/0/0 | 1/0 | 0/0 | — |
| zone13 | Ground / Light | 9 | land | 1 | 0 | 0/0/0 | 0/0 | 0/0 | 1 |
| zone13 | Paving / Square | 7 | land | 1 | 0 | 0/0/0 | 1/0 | 0/0 | 7 |
| zone13 | Sand / Common grayish | 3 | land | 1 | 0 | 0/0/0 | 1/0 | 0/0 | 3 |
| zone13 | Snow / Common | 9 | land | 12 | 0 | 0/0/0 | 12/0 | 0/0 | 9 |
| zone13 | Snow / Dark | 9 | land | 2 | 0 | 0/0/0 | 2/0 | 0/0 | 9 |
| zone13 | Stone / Gray | 9 | land | 2 | 0 | 0/0/0 | 2/0 | 0/0 | 2 |
| zone13 | Stone / Lime | 9 | land | 0 | 0 | 0/0/0 | 0/0 | 0/0 | — |
| zone13 | Water / Still | 6 | overlay | 32 | 0 | 0/0/0 | 32/0 | 32/0 | 6 |
| zone14 | Ground / Light | 1 | land | 1 | 0 | 0/0/0 | 1/0 | 0/0 | 1 |
| zone14 | Ground / Stony gray | 2 | land | 4 | 0 | 0/0/0 | 4/0 | 0/0 | 1 |
| zone14 | Ice / Common | 10 | land | 5 | 0 | 0/0/0 | 5/0 | 0/0 | 10 |
| zone14 | Rock / Black | 2 | land | 3 | 0 | 0/0/0 | 3/0 | 0/0 | 2 |
| zone14 | Sand / Irregular | 2 | land | 3 | 0 | 0/0/0 | 3/0 | 0/0 | 3 |
| zone14 | Snow / Dark | 10 | land | 1 | 0 | 0/0/0 | 1/0 | 0/0 | 9 |
| zone14 | Stone / Black | 2 | land | 2 | 0 | 0/0/0 | 2/0 | 0/0 | 2 |
| zone14 | Stone / Dark | 2 | land | 3 | 0 | 0/0/0 | 3/0 | 0/0 | 2 |
| zone14 | Stone / Dark green | 2 | land | 1 | 0 | 0/0/0 | 1/0 | 0/0 | 2 |
| zone14 | Stone / Grayish | 10 | land | 0 | 0 | 0/0/0 | 0/0 | 0/0 | — |
| zone14 | Water / Dark | 6 | overlay | 32 | 0 | 0/0/0 | 32/0 | 32/0 | 6 |
| zone15 | Ground / Red | 1 | land | 2 | 0 | 0/0/0 | 2/0 | 0/0 | 1 |
| zone15 | Paving / Sand ruins | 7 | land | 0 | 0 | 0/0/0 | 0/0 | 0/0 | — |
| zone15 | Rubble / Orange | 1 | land | 2 | 0 | 0/0/0 | 2/0 | 0/0 | 1 |
| zone15 | Sand / Common grayish | 3 | land | 1 | 0 | 0/0/0 | 1/0 | 0/0 | 3 |
| zone15 | Sand / Dark | 3 | land | 1 | 0 | 0/0/0 | 1/0 | 0/0 | 3 |
| zone15 | Sand / Gravel | 3 | land | 2 | 0 | 0/0/0 | 2/0 | 0/0 | 3 |
| zone15 | Sand / Yellow | 3 | land | 1 | 0 | 0/0/0 | 1/0 | 0/0 | 3 |
| zone15 | Stone / Desert | 1 | land | 0 | 0 | 0/0/0 | 0/0 | 0/0 | — |
| zone15 | Stone / Orange | 2 | land | 2 | 0 | 0/0/0 | 2/0 | 0/0 | 2 |
| zone15 | Stone / Weathered | 2 | land | 3 | 0 | 0/0/0 | 3/0 | 0/0 | 2 |
| zone15 | Water / Still | 6 | overlay | 32 | 0 | 0/0/0 | 32/0 | 32/0 | 6 |
| zone16 | Ground / Tan | 1 | land | 2 | 0 | 0/0/0 | 2/0 | 0/0 | 1 |
| zone16 | Lava / Red | 13 | overlay | 3 | 0 | 0/0/0 | 3/0 | 0/0 | 13 |
| zone16 | Rubble / Red | 1 | land | 4 | 0 | 0/0/0 | 4/0 | 0/0 | 1 |
| zone16 | Stone / Beige | 2 | land | 2 | 0 | 0/0/0 | 2/0 | 0/0 | 2 |
| zone16 | Stone / Desert | 2 | land | 0 | 0 | 0/0/0 | 0/0 | 0/0 | — |
| zone16 | Stone / Red | 2 | land | 4 | 0 | 0/0/0 | 4/0 | 0/0 | 1,2 |
| zone17 | Ground / Plant soil | 2 | land | 0 | 0 | 0/0/0 | 0/0 | 0/0 | — |
| zone17 | Ground / Red | 1 | land | 0 | 2 | 0/2/0 | 0/2 | 0/0 | 1 |
| zone17 | Ground / Tan | 1 | land | 0 | 0 | 0/0/0 | 0/0 | 0/0 | — |
| zone17 | Ground / Topsoil | 1 | land | 0 | 2 | 0/2/0 | 0/2 | 0/0 | 1 |
| zone17 | Rubble / Dry | 1 | land | 0 | 2 | 0/2/0 | 0/2 | 0/0 | 1 |
| zone17 | Rubble / Orange | 1 | land | 0 | 2 | 0/2/0 | 0/2 | 0/0 | 1 |
| zone17 | Stone / Orange | 2 | land | 0 | 2 | 0/2/0 | 0/2 | 0/0 | 2 |
| zone18 | Ground / Desert algae | 1 | land | 0 | 6 | 0/6/0 | 0/6 | 0/0 | 1 |
| zone18 | Ground / Plant soil | 1 | land | 0 | 3 | 0/3/0 | 0/3 | 0/0 | 1 |
| zone18 | Ground / Red | 1 | land | 0 | 2 | 0/2/0 | 0/2 | 0/0 | 1 |
| zone18 | Ground / Tan | 1 | land | 0 | 0 | 0/0/0 | 0/0 | 0/0 | — |
| zone18 | Ground / Topsoil | 3 | land | 0 | 0 | 0/0/0 | 0/0 | 0/0 | — |
| zone18 | Paving / Sand ruins | 7 | land | 0 | 2 | 0/2/0 | 0/2 | 0/0 | 7 |
| zone18 | Rubble / Dry | 7 | land | 0 | 0 | 0/0/0 | 0/0 | 0/0 | — |
| zone18 | Sand / Common grayish | 3 | land | 0 | 1 | 0/1/0 | 0/1 | 0/0 | 3 |
| zone18 | Sand / Dark | 3 | land | 0 | 1 | 0/1/0 | 0/1 | 0/0 | 3 |
| zone18 | Sand / Grass | 3 | land | 0 | 2 | 0/2/0 | 0/2 | 0/0 | 3 |
| zone18 | Sand / Gravel | 3 | land | 0 | 0 | 0/0/0 | 0/0 | 0/0 | — |
| zone18 | Stone / Beige | 2 | land,overlay | 0 | 0 | 0/0/0 | 0/0 | 0/0 | — |
| zone18 | Stone / Desert | 2 | land | 0 | 1 | 0/1/0 | 0/1 | 0/0 | 2 |
| zone18 | Stone / Orange | 2 | land | 0 | 2 | 0/2/0 | 0/2 | 0/0 | 2 |
| zone18 | Stone / Weathered | 2 | land | 0 | 3 | 0/3/0 | 0/3 | 0/0 | 2 |
| zone18 | Water / Still | 6 | overlay | 0 | 32 | 0/32/0 | 0/32 | 0/32 | 6 |
| zone19 | Paving / Sand ruins | 7 | land | 0 | 2 | 0/2/0 | 0/2 | 0/0 | 7 |
| zone19 | Sand / Common grayish | 3 | land | 0 | 1 | 0/1/0 | 0/1 | 0/0 | 3 |
| zone19 | Sand / Dark | 3 | land | 0 | 1 | 0/1/0 | 0/1 | 0/0 | 3 |
| zone19 | Sand / Grass | 3 | land | 0 | 2 | 0/2/0 | 0/2 | 0/0 | 3 |
| zone19 | Sand / Gravel | 3 | land | 0 | 2 | 0/2/0 | 0/2 | 0/0 | 3 |
| zone19 | Sand / Yellow | 3 | land | 0 | 1 | 0/1/0 | 0/1 | 0/0 | 3 |
| zone19 | Stone / Desert | 2 | land | 0 | 2 | 0/2/0 | 0/0 | 0/0 | 2 |
| zone19 | Stone / Desert | 3 | land | 0 | 0 | 0/0/0 | 0/2 | 0/0 | — |
| zone19 | Stone / Orange | 2 | land | 0 | 2 | 0/2/0 | 0/2 | 0/0 | 2 |
| zone19 | Water / Still | 6 | overlay | 0 | 32 | 0/32/0 | 0/32 | 0/32 | 6 |
| zone2 | Grass / Burnt | 0 | land | 1 | 0 | 0/0/0 | 1/0 | 0/0 | 0 |
| zone2 | Grass / Common | 0 | land | 3 | 3 | 0/0/3 | 3/0 | 0/0 | 0 |
| zone2 | Grass / Dark | 0 | land | 1 | 1 | 0/0/1 | 1/0 | 0/0 | 0 |
| zone2 | Grass / Faded | 0 | land | 1 | 0 | 0/0/0 | 1/0 | 0/0 | 0 |
| zone2 | Grass / Fresh | 0 | land | 6 | 2 | 0/0/2 | 6/0 | 0/0 | 0 |
| zone2 | Grass / Irregular | 0 | land | 0 | 1 | 0/0/1 | 0/0 | 0/0 | 0 |
| zone2 | Ground / Brown | 0 | land | 2 | 0 | 0/0/0 | 2/0 | 0/0 | 1 |
| zone2 | Ground / Common | 0 | land | 0 | 1 | 0/0/1 | 0/0 | 0/0 | 0 |
| zone2 | Ground / Dark | 0 | land | 1 | 0 | 0/0/0 | 1/0 | 0/0 | 1 |
| zone2 | Ground / Dirt | 0 | land | 0 | 0 | 0/0/0 | 0/0 | 0/0 | — |
| zone2 | Ground / Dirt | 2 | land | 0 | 1 | 0/0/1 | 0/0 | 0/0 | 2 |
| zone2 | Rock / Snow | 9 | land | 0 | 1 | 0/0/1 | 0/0 | 0/0 | 9 |
| zone2 | Stone / Gray | 0 | land | 0 | 0 | 0/0/0 | 0/0 | 0/0 | — |
| zone2 | Stone / Grayish | 0 | land | 0 | 0 | 0/0/0 | 0/0 | 0/0 | — |
| zone2 | Stone / Lime | 0 | land | 5 | 1 | 0/0/1 | 5/0 | 0/0 | 2 |
| zone2 | Stone / Mossy | 0 | land | 1 | 0 | 0/0/0 | 1/0 | 0/0 | 2 |
| zone2 | Stone / Pink | 0 | land | 4 | 0 | 0/0/0 | 4/0 | 0/0 | 2 |
| zone2 | Swamp | 14 | overlay | 1 | 0 | 0/0/0 | 1/0 | 8/0 | 14 |
| zone20 | Slabs / Carved | 2 | land | 3 | 0 | 0/0/0 | 3/0 | 0/0 | 2 |
| zone20 | Slabs / Faces | 2 | land | 3 | 0 | 0/0/0 | 3/0 | 0/0 | 7 |
| zone20 | Stone / Dark | 2 | land | 0 | 0 | 0/0/0 | 0/0 | 0/0 | — |
| zone20 | Stone / Dark gray | 2 | land | 2 | 0 | 0/0/0 | 2/0 | 0/0 | 2 |
| zone20 | Stone / Greenish | 2 | land | 2 | 0 | 0/0/0 | 2/0 | 0/0 | 2 |
| zone3obr | Grass / Burnt | 0 | land | 0 | 1 | 0/0/1 | 0/0 | 0/0 | ? |
| zone3obr | Grass / Common | 0 | land | 5 | 0 | 0/0/0 | 5/0 | 0/0 | 0 |
| zone3obr | Grass / Dark | 0 | land | 1 | 0 | 0/0/0 | 1/0 | 0/0 | 0 |
| zone3obr | Grass / Faded | 0 | land | 0 | 1 | 0/0/1 | 0/0 | 0/0 | ? |
| zone3obr | Grass / Fresh | 0 | land | 0 | 6 | 0/0/6 | 0/0 | 0/0 | ? |
| zone3obr | Grass / Irregular | 0 | land | 10 | 0 | 0/0/0 | 10/0 | 0/0 | 0 |
| zone3obr | Grass / Sparse dark | 0 | land | 0 | 1 | 0/0/1 | 0/0 | 0/0 | ? |
| zone3obr | Grass / Sparse light | 0 | land | 0 | 1 | 0/0/1 | 0/0 | 0/0 | ? |
| zone3obr | Grass / Winter normal | 0 | land | 0 | 1 | 0/0/1 | 0/0 | 0/0 | ? |
| zone3obr | Ground / Dry | 0 | land | 1 | 0 | 0/0/0 | 1/0 | 0/0 | 1 |
| zone3obr | Ground / Gravel | 1 | land | 0 | 1 | 0/0/1 | 0/0 | 0/0 | ? |
| zone3obr | Ground / Water | 1 | land | 1 | 0 | 0/0/0 | 1/0 | 0/0 | 1 |
| zone3obr | Stone / Gray | 2 | land | 0 | 0 | 0/0/0 | 0/0 | 0/0 | — |
| zone3obr | Stone / Grayish | 2 | land | 0 | 1 | 0/0/1 | 0/0 | 0/0 | ? |
| zone3obr | Stone / Greenish | 0 | land | 0 | 0 | 0/0/0 | 0/0 | 0/0 | — |
| zone3obr | Stone / Light | 2 | land | 0 | 1 | 0/0/1 | 0/0 | 0/0 | ? |
| zone3obr | Stone / Lime | 0 | land | 2 | 0 | 0/0/0 | 0/0 | 0/0 | 2 |
| zone3obr | Stone / Lime | 2 | land | 0 | 0 | 0/0/0 | 2/0 | 0/0 | — |
| zone3obr | Swamp | 14 | overlay | 1 | 0 | 0/0/0 | 1/0 | 8/0 | 14 |
| zone3obr | Water / Still | 6 | overlay | 32 | 0 | 0/0/0 | 32/0 | 32/0 | 6 |
| zone4 | Grass / Burnt | 0 | land | 0 | 1 | 0/0/1 | 0/0 | 0/0 | ? |
| zone4 | Grass / Common | 0 | land | 2 | 0 | 0/0/0 | 2/0 | 0/0 | 0 |
| zone4 | Grass / Dark | 0 | land | 1 | 0 | 0/0/0 | 1/0 | 0/0 | 0 |
| zone4 | Grass / Fresh | 0 | land | 1 | 0 | 0/0/0 | 1/0 | 0/0 | 0 |
| zone4 | Grass / Irregular | 0 | land | 6 | 0 | 0/0/0 | 6/0 | 0/0 | 0 |
| zone4 | Ground / Common | 0 | land | 1 | 0 | 0/0/0 | 0/0 | 0/0 | 1 |
| zone4 | Ground / Common | 1 | land | 0 | 0 | 0/0/0 | 1/0 | 0/0 | — |
| zone4 | Ground / Dark underwater | 1 | land | 1 | 0 | 0/0/0 | 1/0 | 0/0 | 1 |
| zone4 | Ground / Dead | 1 | land | 0 | 5 | 0/0/5 | 0/0 | 0/0 | ? |
| zone4 | Ground / Dirt | 1 | land | 0 | 0 | 0/0/0 | 0/0 | 0/0 | — |
| zone4 | Ground / Gravel greenish | 1 | land | 1 | 0 | 0/0/0 | 1/0 | 0/0 | 1 |
| zone4 | Ground / Gray | 3 | land | 1 | 0 | 0/0/0 | 1/0 | 0/0 | 3 |
| zone4 | Paving / Cobblestone | 7 | land | 1 | 0 | 0/0/0 | 1/0 | 0/0 | 7 |
| zone4 | Paving / Sand ruins | 7 | land | 0 | 0 | 0/0/0 | 0/0 | 0/0 | — |
| zone4 | Rock / Greenish | 4 | land | 1 | 0 | 0/0/0 | 1/0 | 0/0 | 4 |
| zone4 | Sand / Common | 3 | land | 1 | 0 | 0/0/0 | 1/0 | 0/0 | 3 |
| zone4 | Sand / Dark | 3 | land | 0 | 0 | 0/0/0 | 0/0 | 0/0 | — |
| zone4 | Sand / Dark pink | 3 | land | 1 | 0 | 0/0/0 | 1/0 | 0/0 | 3 |
| zone4 | Sand / Grainy | 2 | land | 0 | 0 | 0/0/0 | 0/0 | 0/0 | — |
| zone4 | Stone / Lime | 0 | land | 2 | 0 | 0/0/0 | 0/0 | 0/0 | 2 |
| zone4 | Stone / Lime | 2 | land | 0 | 0 | 0/0/0 | 2/0 | 0/0 | — |
| zone4 | Water / Still | 6 | overlay | 32 | 0 | 0/0/0 | 32/0 | 32/0 | 6 |
| zone5_1 | Grass / Swamp | 0 | land | 7 | 0 | 0/0/0 | 7/0 | 0/0 | 0 |
| zone5_1 | Ground / Dirt | 2 | land | 1 | 0 | 0/0/0 | 1/0 | 0/0 | 1 |
| zone5_1 | Stone / Dark gray | 2 | land | 2 | 0 | 0/0/0 | 2/0 | 0/0 | 2 |
| zone5_1 | Stone / Greenish | 2 | land | 2 | 0 | 0/0/0 | 2/0 | 0/0 | 2 |
| zone5_1 | Stone / Underwater | 2 | land | 2 | 0 | 0/0/0 | 2/0 | 0/0 | 1 |
| zone5_1 | Water / Dark | 6 | overlay | 32 | 0 | 0/0/0 | 32/0 | 32/0 | 6 |
| zone6 | Grass / Common | 0 | land | 5 | 0 | 0/0/0 | 5/0 | 0/0 | 0 |
| zone6 | Grass / Dark | 0 | land | 2 | 0 | 0/0/0 | 2/0 | 0/0 | 0 |
| zone6 | Grass / Fresh | 0 | land | 1 | 0 | 0/0/0 | 1/0 | 0/0 | 0 |
| zone6 | Grass / Irregular | 0 | land | 1 | 0 | 0/0/0 | 1/0 | 0/0 | 0 |
| zone6 | Grass / Sparse dark | 0 | land | 1 | 0 | 0/0/0 | 1/0 | 0/0 | 0 |
| zone6 | Grass / Sparse light | 0 | land | 1 | 0 | 0/0/0 | 1/0 | 0/0 | 0 |
| zone6 | Ground / Common | 0 | land | 1 | 0 | 0/0/0 | 0/0 | 0/0 | 1 |
| zone6 | Ground / Common | 1 | land | 0 | 0 | 0/0/0 | 1/0 | 0/0 | — |
| zone6 | Ground / Dark underwater | 1 | land | 1 | 0 | 0/0/0 | 1/0 | 0/0 | 1 |
| zone6 | Ground / Dead | 1 | land | 5 | 0 | 0/0/0 | 5/0 | 0/0 | 1,4 |
| zone6 | Ground / Dry | 0 | land | 0 | 1 | 0/0/1 | 0/0 | 0/0 | ? |
| zone6 | Ground / Gravel | 1 | land | 1 | 0 | 0/0/0 | 1/0 | 0/0 | 1 |
| zone6 | Stone / Grayish | 2 | land | 0 | 2 | 0/0/2 | 0/0 | 0/0 | ? |
| zone6 | Stone / Light | 2 | land | 0 | 1 | 0/0/1 | 0/0 | 0/0 | ? |
| zone6 | Stone / Lime | 0 | land | 2 | 0 | 0/0/0 | 0/0 | 0/0 | 2 |
| zone6 | Stone / Lime | 2 | land | 0 | 0 | 0/0/0 | 2/0 | 0/0 | — |
| zone6 | Water / Still | 6 | overlay | 32 | 0 | 0/0/0 | 32/0 | 32/0 | 6 |
| zone6_2 | Grass / Common | 0 | land | 5 | 0 | 0/0/0 | 5/0 | 0/0 | 0 |
| zone6_2 | Grass / Dark | 0 | land | 2 | 0 | 0/0/0 | 2/0 | 0/0 | 0 |
| zone6_2 | Grass / Fresh | 0 | land | 1 | 0 | 0/0/0 | 1/0 | 0/0 | 0 |
| zone6_2 | Grass / Irregular | 0 | land | 1 | 0 | 0/0/0 | 1/0 | 0/0 | 0 |
| zone6_2 | Grass / Sparse dark | 0 | land | 1 | 0 | 0/0/0 | 1/0 | 0/0 | 0 |
| zone6_2 | Grass / Sparse light | 0 | land | 1 | 0 | 0/0/0 | 1/0 | 0/0 | 0 |
| zone6_2 | Ground / Common | 1 | land | 1 | 0 | 0/0/0 | 1/0 | 0/0 | 1 |
| zone6_2 | Ground / Dark underwater | 1 | land | 1 | 0 | 0/0/0 | 1/0 | 0/0 | 1 |
| zone6_2 | Ground / Dead | 1 | land | 5 | 0 | 0/0/0 | 5/0 | 0/0 | 1,4 |
| zone6_2 | Ground / Gravel | 1 | land | 1 | 0 | 0/0/0 | 1/0 | 0/0 | 1 |
| zone6_2 | Ice / Irregular | 1 | land | 2 | 0 | 0/0/0 | 2/0 | 0/0 | 1 |
| zone6_2 | Snow / Dark | 0 | land | 0 | 0 | 0/0/0 | 0/0 | 0/0 | — |
| zone6_2 | Stone / Lime | 2 | land | 2 | 0 | 0/0/0 | 2/0 | 0/0 | 2 |
| zone6_2 | Water / Still | 6 | overlay | 32 | 0 | 0/0/0 | 32/0 | 32/0 | 6 |
| zone7 | Grass / Common | 0 | land | 9 | 0 | 0/0/0 | 9/0 | 0/0 | 0 |
| zone7 | Grass / Dark | 0 | land | 1 | 0 | 0/0/0 | 1/0 | 0/0 | 0 |
| zone7 | Grass / Faded | 0 | land | 1 | 0 | 0/0/0 | 0/0 | 0/0 | 11 |
| zone7 | Grass / Faded | 11 | land | 0 | 0 | 0/0/0 | 1/0 | 0/0 | — |
| zone7 | Grass / Fresh | 0 | land | 1 | 0 | 0/0/0 | 1/0 | 0/0 | 11 |
| zone7 | Grass / Irregular | 0 | land | 6 | 0 | 0/0/0 | 6/0 | 0/0 | 0 |
| zone7 | Grass / Sparse light | 0 | land | 0 | 1 | 0/0/1 | 0/0 | 0/0 | ? |
| zone7 | Ground / Brownish | 1 | land | 1 | 0 | 0/0/0 | 1/0 | 0/0 | 1 |
| zone7 | Ground / Common | 0 | land | 0 | 1 | 0/0/1 | 0/0 | 0/0 | ? |
| zone7 | Ground / Dark brown | 1 | land | 2 | 0 | 0/0/0 | 2/0 | 0/0 | 1 |
| zone7 | Ground / Dirt | 1 | land | 0 | 0 | 0/0/0 | 0/0 | 0/0 | — |
| zone7 | Rock / Underwater emerald | 2 | land | 0 | 0 | 0/0/0 | 1/0 | 0/0 | — |
| zone7 | Rock / Underwater emerald | 3 | land | 1 | 0 | 0/0/0 | 0/0 | 0/0 | 2 |
| zone7 | Rubble / Gray | 2 | land | 2 | 0 | 0/0/0 | 2/0 | 0/0 | 2 |
| zone7 | Sand / Common | 3 | land | 1 | 0 | 0/0/0 | 1/0 | 0/0 | 3 |
| zone7 | Sand / Dark pink | 3 | land | 0 | 0 | 0/0/0 | 0/0 | 0/0 | — |
| zone7 | Sand / Grainy | 3 | land | 2 | 0 | 0/0/0 | 2/0 | 0/0 | 3 |
| zone7 | Sand / Light | 3 | land | 0 | 0 | 0/0/0 | 0/0 | 0/0 | — |
| zone7 | Sand / Underwater | 3 | land | 1 | 0 | 0/0/0 | 1/0 | 0/0 | 3 |
| zone7 | Stone / Light | 2 | land | 2 | 0 | 0/0/0 | 2/0 | 0/0 | 2 |
| zone7 | Swamp | 14 | overlay | 1 | 0 | 0/0/0 | 1/0 | 8/0 | 14 |
| zone7 | Water / Emerald | 6 | overlay | 32 | 0 | 0/0/0 | 32/0 | 32/0 | 6 |
| zone8 | Grass / Common | 0 | land | 5 | 1 | 0/0/1 | 5/0 | 0/0 | 0 |
| zone8 | Grass / Dark | 0 | land | 1 | 1 | 0/0/1 | 1/0 | 0/0 | 0 |
| zone8 | Grass / Sparse light | 0 | land | 0 | 2 | 0/0/2 | 0/0 | 0/0 | 0 |
| zone8 | Grass / Swamp | 0 | land | 1 | 1 | 0/0/1 | 1/0 | 0/0 | 0 |
| zone8 | Ground / Brownish | 1 | land | 0 | 2 | 0/0/2 | 0/0 | 0/0 | 1 |
| zone8 | Ground / Common | 0 | land | 1 | 1 | 0/0/1 | 0/0 | 0/0 | 1 |
| zone8 | Ground / Common | 1 | land | 0 | 0 | 0/0/0 | 1/0 | 0/0 | — |
| zone8 | Ground / Gravel | 1 | land | 1 | 1 | 0/0/1 | 1/0 | 0/0 | 1 |
| zone8 | Rock / Underwater emerald | 3 | land | 1 | 0 | 0/0/0 | 1/0 | 0/0 | 2 |
| zone8 | Sand / Common | 3 | land | 1 | 0 | 0/0/0 | 1/0 | 0/0 | 3 |
| zone8 | Sand / Dark pink | 3 | land | 2 | 0 | 0/0/0 | 2/0 | 0/0 | 3 |
| zone8 | Sand / Grainy | 3 | land | 2 | 0 | 0/0/0 | 2/0 | 0/0 | 3 |
| zone8 | Sand / Light | 3 | land | 1 | 0 | 0/0/0 | 1/0 | 0/0 | 3 |
| zone8 | Sand / Underwater | 3 | land | 1 | 0 | 0/0/0 | 1/0 | 0/0 | 3 |
| zone8 | Stone / Light | 2 | land | 2 | 1 | 0/0/1 | 2/0 | 0/0 | 2 |
| zone8 | Water / Emerald | 6 | overlay | 32 | 0 | 0/0/0 | 32/0 | 32/0 | 6 |
| zone9 | Grass / Common | 0 | land | 2 | 0 | 0/0/0 | 2/0 | 0/0 | 0 |
| zone9 | Grass / Dark | 0 | land | 1 | 0 | 0/0/0 | 1/0 | 0/0 | 0 |
| zone9 | Grass / Irregular | 0 | land | 0 | 10 | 0/0/10 | 0/0 | 0/0 | ? |
| zone9 | Grass / Sparse dark | 0 | land | 1 | 0 | 0/0/0 | 1/0 | 0/0 | 0 |
| zone9 | Grass / Sparse light | 0 | land | 1 | 0 | 0/0/0 | 1/0 | 0/0 | 0 |
| zone9 | Grass / Swamp | 0 | land | 0 | 4 | 0/0/4 | 0/0 | 0/0 | ? |
| zone9 | Ground / Common | 0 | land | 1 | 0 | 0/0/0 | 0/0 | 0/0 | 1 |
| zone9 | Ground / Common | 1 | land | 0 | 0 | 0/0/0 | 1/0 | 0/0 | — |
| zone9 | Ground / Dark underwater | 1 | land | 1 | 0 | 0/0/0 | 1/0 | 0/0 | 1 |
| zone9 | Ground / Dead | 1 | land | 10 | 0 | 0/0/0 | 10/0 | 0/0 | 1,4 |
| zone9 | Ground / Gravel | 1 | land | 1 | 0 | 0/0/0 | 1/0 | 0/0 | 1 |
| zone9 | Ground / Gravel greenish | 1 | land | 0 | 1 | 0/0/1 | 0/0 | 0/0 | ? |
| zone9 | Ground / Gray | 3 | land | 1 | 0 | 0/0/0 | 1/0 | 0/0 | 3 |
| zone9 | Paving / Cobblestone | 7 | land | 0 | 1 | 0/0/1 | 0/0 | 0/0 | ? |
| zone9 | Paving / Radial | 7 | land | 1 | 0 | 0/0/0 | 1/0 | 0/0 | 7 |
| zone9 | Sand / Common | 3 | land | 0 | 1 | 0/0/1 | 0/0 | 0/0 | ? |
| zone9 | Sand / Dark pink | 3 | land | 1 | 0 | 0/0/0 | 1/0 | 0/0 | 3 |
| zone9 | Stone / Grayish | 2 | land | 2 | 0 | 0/0/0 | 2/0 | 0/0 | 2 |
| zone9 | Stone / Lime | 0 | land | 2 | 0 | 0/0/0 | 0/0 | 0/0 | 2 |
| zone9 | Stone / Lime | 2 | land | 0 | 0 | 0/0/0 | 2/0 | 0/0 | — |
| zone9 | Swamp | 14 | overlay | 1 | 0 | 0/0/0 | 1/0 | 8/0 | 14 |
| zone9 | Water / Still | 6 | overlay | 32 | 0 | 0/0/0 | 32/0 | 32/0 | 6 |
| zonemainmenunew | Grass / Common | 0 | land | 0 | 5 | 5/0/0 | 5/0 | 0/0; ? 5 | ? |
| zonemainmenunew | Grass / Dark | 0 | land | 0 | 1 | 1/0/0 | 1/0 | 0/0; ? 1 | ? |
| zonemainmenunew | Grass / Irregular | 0 | land | 0 | 17 | 17/0/0 | 9/0 | 0/0; ? 21 | ? |
| zonemainmenunew | Grass / Sparse light | 0 | land | 0 | 1 | 1/0/0 | 0/0 | 0/0; ? 1 | ? |
| zonemainmenunew | Ground / Dry | 0 | land | 0 | 1 | 1/0/0 | 0/0 | 0/0; ? 2 | ? |
| zonemainmenunew | Ground / Dry | 2 | land | 0 | 0 | 0/0/0 | 1/0 | 0/0 | — |
| zonemainmenunew | Ground / Water | 0 | land | 0 | 0 | 0/0/0 | 1/0 | 0/0 | — |
| zonemainmenunew | Ground / Water | 1 | land | 0 | 1 | 1/0/0 | 0/0 | 0/0; ? 1 | ? |
| zonemainmenunew | Paving / Cobblestone | 7 | land | 0 | 1 | 1/0/0 | 0/0 | 0/0; ? 1 | ? |
| zonemainmenunew | Rock / Greenish | 4 | land | 0 | 1 | 1/0/0 | 0/0 | 0/0; ? 1 | ? |
| zonemainmenunew | Snow / Common | 9 | land | 0 | 5 | 5/0/0 | 0/0 | 0/0; ? 5 | ? |
| zonemainmenunew | Stone / Dark | 2 | land | 0 | 3 | 3/0/0 | 0/0 | 0/0; ? 3 | ? |
| zonemainmenunew | Stone / Light | 2 | land | 0 | 2 | 2/0/0 | 0/0 | 0/0; ? 2 | ? |
| zonemainmenunew | Stone / Lime | 0 | land | 0 | 4 | 4/0/0 | 0/0 | 0/0; ? 6 | ? |
| zonemainmenunew | Stone / Lime | 2 | land | 0 | 0 | 0/0/0 | 2/0 | 0/0 | — |
| zonemainmenunew | Water / Still | 6 | overlay | 0 | 32 | 32/0/0 | 32/0 | 0/0; ? 32 | ? |

## Переходы и полнота классификации по зонам

Здесь единица — физический слот, без объединения одинаковых пикселей. U = uniform, T = переход, ? = непроверенная/отсутствующая семантика. Полный набор страниц, включая padding, сверяется по сумме U+T+?. Подтверждённые профилем padding-слоты здесь остаются физическими слотами; это не дополнительные уникальные рисунки.

| Банк | Uniform 256 | Uniform 128 | Переходы 256 | Переходы 128 | Вне классификации |
|---|---|---|---|---|---|
| Dejavu | 1798 | 278 | 13306 | 4893 | 589 |
| JamaisVu135 | 1849 | 112 | 13447 | 1168 | 0 |

Для uniform-слотов 128 px в Dejavu (здесь считаются физические слоты, до объединения дубликатов): HD есть: **81**; исходник тоже 128: **112**; происхождение не установлено: **85**. Для **85/85** слотов с неустановленным происхождением в JMV есть HD того же материала и проверенного класса; это подтверждает наличие альтернативы, но не HD-оригинал того же рисунка. Дополнительная проверка JMV textures.res для исходных зон с SD-страницами: zone17: 0 одноимённых страниц 256 px/тайл; zone18: 0 одноимённых страниц 256 px/тайл; zone19: 0 одноимённых страниц 256 px/тайл. Варианты этих же материалов в других зонах могут иметь HD.

| Namespace | D U 256/128 | D T 256/128 | D ? 256/128 | JMV U 256/128 | JMV T 256/128 | JMV ? 256/128 |
|---|---|---|---|---|---|---|
| basegipat | 53/0 | 459/0 | 0/0 | 53/0 | 459/0 | 0/0 |
| bz10k | 70/2 | 250/43 | 0/83 | 70/0 | 250/0 | 0/0 |
| bz11k | 76/0 | 436/0 | 0/0 | 76/0 | 436/0 | 0/0 |
| bz13h | 46/0 | 466/0 | 0/0 | 46/0 | 466/0 | 0/0 |
| bz14h | 16/0 | 240/0 | 0/0 | 16/0 | 240/0 | 0/0 |
| bz15h | 10/0 | 246/0 | 0/0 | 10/0 | 246/0 | 0/0 |
| bz16h | 52/0 | 460/0 | 0/0 | 52/0 | 460/0 | 0/0 |
| bz18h | 16/0 | 240/0 | 0/0 | 16/0 | 240/0 | 0/0 |
| bz2g | 39/0 | 473/0 | 0/0 | 39/0 | 473/0 | 0/0 |
| bz3g | 67/0 | 445/0 | 0/0 | 67/0 | 445/0 | 0/0 |
| bz4g | 54/0 | 458/0 | 0/0 | 54/0 | 458/0 | 0/0 |
| bz5g | 59/0 | 453/0 | 0/0 | 59/0 | 453/0 | 0/0 |
| bz6g | 59/0 | 453/0 | 0/0 | 59/0 | 453/0 | 0/0 |
| bz7g | 12/0 | 244/0 | 0/0 | 12/0 | 244/0 | 0/0 |
| bz8k | 92/0 | 420/0 | 0/0 | 92/0 | 420/0 | 0/0 |
| bz9k | 92/0 | 420/0 | 0/0 | 92/0 | 420/0 | 0/0 |
| zone1 | 54/15 | 458/382 | 0/51 | 54/0 | 458/0 | 0/0 |
| zone10 | 31/0 | 481/0 | 0/0 | 31/0 | 481/0 | 0/0 |
| zone11 | 92/0 | 420/11 | 0/53 | 92/0 | 420/0 | 0/0 |
| zone12 | 70/4 | 250/83 | 0/41 | 70/0 | 250/0 | 0/0 |
| zone13 | 78/0 | 434/0 | 0/0 | 78/0 | 434/0 | 0/0 |
| zone14 | 76/0 | 436/0 | 0/0 | 76/0 | 436/0 | 0/0 |
| zone15 | 46/0 | 466/0 | 0/0 | 46/0 | 466/0 | 0/0 |
| zone16 | 16/0 | 240/0 | 0/0 | 16/0 | 240/0 | 0/0 |
| zone17 | 0/10 | 0/246 | 0/0 | 0/10 | 0/246 | 0/0 |
| zone18 | 0/56 | 0/456 | 0/0 | 0/56 | 0/456 | 0/0 |
| zone19 | 0/46 | 0/518 | 0/12 | 0/46 | 0/466 | 0/0 |
| zone2 | 39/11 | 473/405 | 0/96 | 39/0 | 473/0 | 0/0 |
| zone20 | 10/0 | 182/0 | 0/0 | 10/0 | 182/0 | 0/0 |
| zone3obr | 62/14 | 450/698 | 0/56 | 62/0 | 450/0 | 0/0 |
| zone4 | 54/6 | 458/153 | 0/33 | 54/0 | 458/0 | 0/0 |
| zone5_1 | 49/0 | 143/0 | 0/0 | 49/0 | 143/0 | 0/0 |
| zone6 | 54/4 | 458/264 | 0/52 | 54/0 | 458/0 | 0/0 |
| zone6_2 | 56/0 | 456/0 | 0/0 | 56/0 | 456/0 | 0/0 |
| zone7 | 73/2 | 439/430 | 0/16 | 73/0 | 439/0 | 0/0 |
| zone8 | 59/10 | 453/339 | 0/35 | 59/0 | 453/0 | 0/0 |
| zone9 | 66/17 | 446/596 | 0/27 | 66/0 | 446/0 | 0/0 |
| zonemainmenunew | 0/81 | 0/269 | 0/34 | 51/0 | 141/0 | 0/0 |

## Страницы и ссылки MPR

D/B/— в последней колонке — МиБ страниц, нужных Dejavu / только Base / ни одной карте. Страница внутри используемого namespace может оказаться вне textureCount всех карт. Ссылки на один namespace приведены полностью.

| Namespace | Тип | Страниц | Всего МиБ | MPR Dejavu | MPR Base | МиБ D/B/— |
|---|---|---|---|---|---|---|
| basegipat | stock | 8 | 21.334 | — | basegipat.mpr | 0.000/21.334/0.000 |
| basegipat_0c2dfc8150c3 | published | 8 | 19.334 | basegipat.mpr | — | 19.334/0.000/0.000 |
| bz10k | stock | 7 | 14.667 | bz10k.mpr | bz10k.mpr | 14.667/0.000/0.000 |
| bz11k | stock | 8 | 21.334 | bz11k.mpr | bz11k.mpr | 21.334/0.000/0.000 |
| bz13h | stock | 8 | 21.334 | bz13h.mpr | bz13h.mpr | 21.334/0.000/0.000 |
| bz14h | stock | 4 | 10.667 | bz14h.mpr | bz14h.mpr | 10.667/0.000/0.000 |
| bz15h | stock | 4 | 10.667 | bz15h.mpr | bz15h.mpr | 10.667/0.000/0.000 |
| bz16h | stock | 8 | 21.334 | bz16h.mpr | bz16h.mpr | 21.334/0.000/0.000 |
| bz18h | stock | 4 | 10.667 | bz18h.mpr | bz18h.mpr | 10.667/0.000/0.000 |
| bz2g | stock | 8 | 21.334 | bz2g.mpr | bz2g.mpr | 21.334/0.000/0.000 |
| bz3g | stock | 8 | 21.334 | bz3g.mpr | bz3g.mpr | 21.334/0.000/0.000 |
| bz4g | stock | 8 | 21.334 | bz4g.mpr | bz4g.mpr | 21.334/0.000/0.000 |
| bz5g | stock | 8 | 21.334 | bz5g.mpr | bz5g.mpr | 21.334/0.000/0.000 |
| bz6g | stock | 8 | 21.334 | bz6g.mpr | bz6g.mpr | 21.334/0.000/0.000 |
| bz7g | stock | 4 | 10.667 | bz7g.mpr | bz7g.mpr | 10.667/0.000/0.000 |
| bz8k | stock | 8 | 21.334 | bz8k.mpr | bz8k.mpr | 21.334/0.000/0.000 |
| bz9k | stock | 8 | 21.334 | bz9k.mpr | bz9k.mpr | 21.334/0.000/0.000 |
| zone1 | stock | 15 | 26.001 | — | zone1.mpr | 0.000/21.334/4.667 |
| zone10 | stock | 8 | 21.334 | bz19g.mpr, zone10.mpr | zone10.mpr | 21.334/0.000/0.000 |
| zone11 | stock | 9 | 22.001 | — | zone11.mpr | 0.000/21.334/0.667 |
| zone11_0132fbfdd188 | published | 8 | 5.334 | zone11.mpr | — | 5.334/0.000/0.000 |
| zone11_2e2ffed3c80c | published | 8 | 5.334 | — | — | 0.000/0.000/5.334 |
| zone11_96774500b145 | published | 11 | 7.334 | — | — | 0.000/0.000/7.334 |
| zone12 | stock | 7 | 14.667 | zone12.mpr | zone12.mpr | 14.667/0.000/0.000 |
| zone13 | stock | 8 | 21.334 | — | zone13.mpr | 0.000/21.334/0.000 |
| zone13_63226eeedc0c | published | 11 | 7.334 | zone13.mpr | — | 7.334/0.000/0.000 |
| zone13_bf1e1c5cb46d | published | 8 | 5.334 | — | — | 0.000/0.000/5.334 |
| zone14 | stock | 8 | 21.334 | — | zone14.mpr | 0.000/21.334/0.000 |
| zone14_60f652260b04 | published | 8 | 5.334 | zone14.mpr | — | 5.334/0.000/0.000 |
| zone14_975e928d3190 | published | 4 | 2.667 | zone12dun2.mpr | — | 2.667/0.000/0.000 |
| zone15 | stock | 8 | 21.334 | zone15.mpr | zone15.mpr | 21.334/0.000/0.000 |
| zone16 | stock | 4 | 10.667 | zone16.mpr | zone16.mpr | 10.667/0.000/0.000 |
| zone17 | stock | 4 | 2.667 | zone17.mpr | zone17.mpr | 2.667/0.000/0.000 |
| zone18 | stock | 8 | 5.334 | zone18dun1.mpr | zone18.mpr | 5.334/0.000/0.000 |
| zone18_55491c494740 | published | 9 | 6.001 | zone18.mpr | — | 6.001/0.000/0.000 |
| zone19 | stock | 9 | 6.001 | — | zone19.mpr | 0.000/5.334/0.667 |
| zone19_08f8ef9682b6 | published | 14 | 9.335 | — | — | 0.000/0.000/9.335 |
| zone19_0a70846ab68e | published | 9 | 6.001 | — | — | 0.000/0.000/6.001 |
| zone19_0fa7788602d5 | published | 14 | 9.335 | — | — | 0.000/0.000/9.335 |
| zone19_39d702eb035c | published | 14 | 9.335 | — | — | 0.000/0.000/9.335 |
| zone19_4392666f08a6 | published | 11 | 7.334 | — | — | 0.000/0.000/7.334 |
| zone19_45089fe2a566 | published | 14 | 9.335 | — | — | 0.000/0.000/9.335 |
| zone19_67fb91e54d28 | published | 14 | 9.335 | — | — | 0.000/0.000/9.335 |
| zone19_95860c68f0ca | published | 14 | 9.335 | — | — | 0.000/0.000/9.335 |
| zone19_bb84f59c5962 | published | 15 | 10.001 | zone19.mpr | — | 10.001/0.000/0.000 |
| zone19_beb70ab2cea1 | published | 10 | 6.668 | — | — | 0.000/0.000/6.668 |
| zone19_bec0a5882400 | published | 11 | 7.334 | — | — | 0.000/0.000/7.334 |
| zone19_c22bbd8a36d5 | published | 9 | 6.001 | — | — | 0.000/0.000/6.001 |
| zone19_c502976ab91a | published | 14 | 9.335 | — | — | 0.000/0.000/9.335 |
| zone19_e9a6232793be | published | 15 | 10.001 | — | — | 0.000/0.000/10.001 |
| zone19_ec49d46d77a3 | published | 14 | 9.335 | — | — | 0.000/0.000/9.335 |
| zone19_f16fd8f53625 | published | 14 | 9.335 | — | — | 0.000/0.000/9.335 |
| zone1_17763baa57df | published | 15 | 10.001 | zone1.mpr | — | 10.001/0.000/0.000 |
| zone1_1939d94241f0 | published | 13 | 8.668 | — | — | 0.000/0.000/8.668 |
| zone1_427ae8df4834 | published | 13 | 8.668 | — | — | 0.000/0.000/8.668 |
| zone1_6eb00234c949 | published | 13 | 8.668 | — | — | 0.000/0.000/8.668 |
| zone1_a3f36e0c7bbe | published | 13 | 8.668 | — | — | 0.000/0.000/8.668 |
| zone2 | stock | 16 | 26.668 | zone2.mpr | zone2.mpr | 26.668/0.000/0.000 |
| zone20 | stock | 3 | 8.000 | zone20.mpr | zone20.mpr | 8.000/0.000/0.000 |
| zone3obr | stock | 20 | 29.335 | — | zone3obr.mpr | 0.000/21.334/8.001 |
| zone3obr_0bcba8177f9a | published | 23 | 15.335 | zone3obr.mpr | — | 15.335/0.000/0.000 |
| zone4 | stock | 11 | 23.334 | — | zone4.mpr | 0.000/21.334/2.000 |
| zone4_7d4d040d058a | published | 12 | 8.001 | — | — | 0.000/0.000/8.001 |
| zone4_fc5a0ba98c52 | published | 16 | 10.668 | zone4.mpr | — | 10.668/0.000/0.000 |
| zone5_1 | stock | 3 | 8.000 | zone5_1.mpr | zone5_1.mpr | 8.000/0.000/0.000 |
| zone6 | stock | 13 | 24.668 | — | zone6.mpr | 0.000/21.334/3.334 |
| zone6_2 | stock | 8 | 21.334 | zone6_2.mpr | zone6_2.mpr | 21.334/0.000/0.000 |
| zone6_400428ea1c7e | published | 14 | 9.335 | — | — | 0.000/0.000/9.335 |
| zone6_fdae989f80c9 | published | 17 | 11.335 | — | — | 0.000/0.000/11.335 |
| zone6_fe645a375f96 | published | 19 | 12.668 | zone6.mpr | — | 12.668/0.000/0.000 |
| zone7 | stock | 15 | 26.001 | — | zone7.mpr | 0.000/21.334/4.667 |
| zone7_44a43bfdda10 | published | 15 | 10.001 | zone7.mpr | — | 10.001/0.000/0.000 |
| zone7_fdc1f7279bb7 | published | 14 | 9.335 | — | — | 0.000/0.000/9.335 |
| zone8 | stock | 14 | 25.335 | zone8.mpr | zone8.mpr | 25.335/0.000/0.000 |
| zone9 | stock | 18 | 28.002 | — | zone9.mpr | 0.000/21.334/6.668 |
| zone9_02dc971bd194 | published | 22 | 14.669 | — | — | 0.000/0.000/14.669 |
| zone9_10db87221e5b | published | 18 | 12.002 | — | — | 0.000/0.000/12.002 |
| zone9_35f861c41800 | published | 20 | 13.335 | — | — | 0.000/0.000/13.335 |
| zone9_3647e43c7d07 | published | 27 | 18.002 | — | — | 0.000/0.000/18.002 |
| zone9_633d2eb4e4a3 | published | 21 | 14.002 | — | — | 0.000/0.000/14.002 |
| zone9_6f20bf2ff2de | published | 20 | 13.335 | — | — | 0.000/0.000/13.335 |
| zone9_844d6cb4ad7e | published | 19 | 12.668 | — | — | 0.000/0.000/12.668 |
| zone9_95bfefad13d2 | published | 25 | 16.669 | — | — | 0.000/0.000/16.669 |
| zone9_a22bc0eb01be | published | 20 | 13.335 | — | — | 0.000/0.000/13.335 |
| zone9_b332871016bf | published | 21 | 14.002 | — | — | 0.000/0.000/14.002 |
| zone9_b9cc244100fa | published | 18 | 12.002 | — | — | 0.000/0.000/12.002 |
| zone9_f24337452d99 | published | 25 | 16.669 | zone9.mpr | — | 16.669/0.000/0.000 |
| zone9_fbb16e663da6 | published | 20 | 13.335 | — | — | 0.000/0.000/13.335 |
| zonemainmenunew | stock | 6 | 4.001 | — | zonemainmenunew.mpr | 0.000/2.000/2.000 |
| zonemainmenunew_2007d54d51e5 | published | 6 | 4.001 | — | — | 0.000/0.000/4.001 |
| zonemainmenunew_91d1b040b2b8 | published | 6 | 4.001 | zonemainmenunew.mpr | — | 4.001/0.000/0.000 |

| Категория | По страницам MP, МиБ | Консервативно весь namespace, МиБ |
|---|---|---|
| Есть ссылка Dejavu | 571.365 | 571.365 |
| Только Base | 220.674 | 253.345 |
| Нет ссылок | 422.054 | 389.383 |

Контроль размера: страницы **1273068816 байт** + прочие члены **13065650 байт** (132 JSON/других членов; 12.460 МиБ) + структура/свободное пространство RES **72306 байт** = **1286206772 байт**. Взаимоисключающие категории страниц покрывают весь MMP-банк.

### Ограничения и следующий этап переноса

- Недостающих в снимке Dejavu HDPack страниц, объявленных MP проверенных карт: **0**. Отсутствие здесь не доказывает невозможность загрузки: игра может разрешать ресурс из другого архива.
- Вне uniform/transition: dejavu_hd/bz10k: 83; dejavu_hd/zone1: 51; dejavu_hd/zone11: 53; dejavu_hd/zone12: 41; dejavu_hd/zone19: 12; dejavu_hd/zone2: 96; dejavu_hd/zone3obr: 56; dejavu_hd/zone4: 33; dejavu_hd/zone6: 52; dejavu_hd/zone7: 16; dejavu_hd/zone8: 35; dejavu_hd/zone9: 27; dejavu_hd/zonemainmenunew: 34. Причины по слотам: {'unassigned': 589}. Unassigned — профиль сознательно не назначил сигнатуру этому слоту; такие слоты не считаются переходами или вариантами для покраски. Все применённые профили при этом имеют точный SHA.
- Перенос пока не выполнялся. Для будущего пака маппера требуется отдельно подтвердить порядок поиска ресурсов, доступность глобальной палитры, вложенные profile/semantic JSON, внешние карты и совместимость игрового загрузчика. Проверка MPR-ссылок сама по себе не разрешает удаление страниц, которыми пользуется только редактор.
- После всех переводов повторить полный захват. В текущем отчёте нет игровой/визуальной приёмки, проверки швов и доказательства, что произвольный материал доступен в каждой карте.

## Проверенные привязки семантики

| Пак | Namespace | Слотов | SHA пикселей | Авторитет | Stock-префикс |
|---|---|---|---|---|---|
| jamaisvu_hd | basegipat | 512 | `f24371251f84f277c1eb474771ae61c86e482f6593b6a2b2d1e7bd1e8107b396` | built-in preset (verified stock prefix) | да |
| dejavu_hd | basegipat | 512 | `f24371251f84f277c1eb474771ae61c86e482f6593b6a2b2d1e7bd1e8107b396` | built-in preset (verified stock prefix) | да |
| jamaisvu_hd | bz10k | 320 | `eef8f7940f5e28967d65d6fc17e0450bd78a2e4671a0be71da178e3ca8fb89f7` | built-in preset (verified stock prefix) | да |
| dejavu_hd | bz10k | 448 | `1e0b2cd54c61b69dfd855934cf4f003ab2715026fad13dedca4d26a1e050101b` | ei_atlas_profile_bz10k_87a957b5bd41fe78.json | да |
| jamaisvu_hd | bz11k | 512 | `bd1873929c5ba9d3406d8be7d642dfa37fcb42a1a48edc914d5dd97610ee00d2` | built-in preset (verified stock prefix) | да |
| dejavu_hd | bz11k | 512 | `bd1873929c5ba9d3406d8be7d642dfa37fcb42a1a48edc914d5dd97610ee00d2` | built-in preset (verified stock prefix) | да |
| jamaisvu_hd | bz13h | 512 | `30eb16d617c63989bb43591526ea2b3c31c386c4aa179c1342d9e9b086ca61a8` | built-in preset (verified stock prefix) | да |
| dejavu_hd | bz13h | 512 | `30eb16d617c63989bb43591526ea2b3c31c386c4aa179c1342d9e9b086ca61a8` | built-in preset (verified stock prefix) | да |
| jamaisvu_hd | bz14h | 256 | `746dafd4e7d2c684211b3f8115257197f6c4efc868e79c932bc35e6433346331` | built-in preset (verified stock prefix) | да |
| dejavu_hd | bz14h | 256 | `746dafd4e7d2c684211b3f8115257197f6c4efc868e79c932bc35e6433346331` | built-in preset (verified stock prefix) | да |
| jamaisvu_hd | bz15h | 256 | `ae4c815f3c93015ebac0ffb26c4760a6eaa14fa935be97559467bc0e66658734` | built-in preset (verified stock prefix) | да |
| dejavu_hd | bz15h | 256 | `ae4c815f3c93015ebac0ffb26c4760a6eaa14fa935be97559467bc0e66658734` | built-in preset (verified stock prefix) | да |
| jamaisvu_hd | bz16h | 512 | `9695dad9bb05c5547d7924fcc1f2ee30875d951e891d6506e427c62772f7a1f6` | built-in preset (verified stock prefix) | да |
| dejavu_hd | bz16h | 512 | `9695dad9bb05c5547d7924fcc1f2ee30875d951e891d6506e427c62772f7a1f6` | built-in preset (verified stock prefix) | да |
| jamaisvu_hd | bz18h | 256 | `746dafd4e7d2c684211b3f8115257197f6c4efc868e79c932bc35e6433346331` | built-in preset (verified stock prefix) | да |
| dejavu_hd | bz18h | 256 | `746dafd4e7d2c684211b3f8115257197f6c4efc868e79c932bc35e6433346331` | built-in preset (verified stock prefix) | да |
| jamaisvu_hd | bz2g | 512 | `ed0bc69bd4abbb8f9f90fab44574a24b0b806521b6bc36501eea7c5add258612` | built-in preset (verified stock prefix) | да |
| dejavu_hd | bz2g | 512 | `ed0bc69bd4abbb8f9f90fab44574a24b0b806521b6bc36501eea7c5add258612` | built-in preset (verified stock prefix) | да |
| jamaisvu_hd | bz3g | 512 | `6752a5b4cefd3aedbd6889516b424f4e13afafe288e99406b500404aed62b014` | built-in preset (verified stock prefix) | да |
| dejavu_hd | bz3g | 512 | `6752a5b4cefd3aedbd6889516b424f4e13afafe288e99406b500404aed62b014` | built-in preset (verified stock prefix) | да |
| jamaisvu_hd | bz4g | 512 | `4279b31201a5d960d870988415550782e9bae5b28c65cea04095eaf2714d41ed` | built-in preset (verified stock prefix) | да |
| dejavu_hd | bz4g | 512 | `4279b31201a5d960d870988415550782e9bae5b28c65cea04095eaf2714d41ed` | built-in preset (verified stock prefix) | да |
| jamaisvu_hd | bz5g | 512 | `4ad67630a39a00a6d5fb1d421e7873c76faa64707a4c5565744a84476dfdc35e` | built-in preset (verified stock prefix) | да |
| dejavu_hd | bz5g | 512 | `4ad67630a39a00a6d5fb1d421e7873c76faa64707a4c5565744a84476dfdc35e` | built-in preset (verified stock prefix) | да |
| jamaisvu_hd | bz6g | 512 | `4ad67630a39a00a6d5fb1d421e7873c76faa64707a4c5565744a84476dfdc35e` | built-in preset (verified stock prefix) | да |
| dejavu_hd | bz6g | 512 | `4ad67630a39a00a6d5fb1d421e7873c76faa64707a4c5565744a84476dfdc35e` | built-in preset (verified stock prefix) | да |
| jamaisvu_hd | bz7g | 256 | `c23dab43dfb7fd60f59a6e65200faad049b9c53a251cb8db3dea216011c3852e` | built-in preset (verified stock prefix) | да |
| dejavu_hd | bz7g | 256 | `c23dab43dfb7fd60f59a6e65200faad049b9c53a251cb8db3dea216011c3852e` | built-in preset (verified stock prefix) | да |
| jamaisvu_hd | bz8k | 512 | `2005d946276a6094e4a7f1fcd03f75c6b4410611a890016a43dc9dfdd141124a` | built-in preset (verified stock prefix) | да |
| dejavu_hd | bz8k | 512 | `2005d946276a6094e4a7f1fcd03f75c6b4410611a890016a43dc9dfdd141124a` | built-in preset (verified stock prefix) | да |
| jamaisvu_hd | bz9k | 512 | `2005d946276a6094e4a7f1fcd03f75c6b4410611a890016a43dc9dfdd141124a` | built-in preset (verified stock prefix) | да |
| dejavu_hd | bz9k | 512 | `2005d946276a6094e4a7f1fcd03f75c6b4410611a890016a43dc9dfdd141124a` | built-in preset (verified stock prefix) | да |
| jamaisvu_hd | zone1 | 512 | `35cc3b90985888b5c5206f8c5980321beb46f0be7b1e96b4a3467677ac2a4665` | built-in preset (verified stock prefix) | да |
| dejavu_hd | zone1 | 960 | `8fe898f8a95b29b43a2df6797da3dfe7a6668045a634737eae204db1680b6a47` | ei_atlas_profile_zone1_a34f924efa72cd01.json | да |
| jamaisvu_hd | zone10 | 512 | `9c609f8fcfbb1683dc7c0bd31371ddc621d572df713cf41bb620d04116f990ee` | built-in preset (verified stock prefix) | да |
| dejavu_hd | zone10 | 512 | `9c609f8fcfbb1683dc7c0bd31371ddc621d572df713cf41bb620d04116f990ee` | built-in preset (verified stock prefix) | да |
| jamaisvu_hd | zone11 | 512 | `2005d946276a6094e4a7f1fcd03f75c6b4410611a890016a43dc9dfdd141124a` | built-in preset (verified stock prefix) | да |
| dejavu_hd | zone11 | 576 | `cb3c136f7627098169ccf684417d075632bad545eff8266feb724974d8b96e8d` | ei_atlas_profile_zone11_ef1533c95f07e992.json | да |
| jamaisvu_hd | zone12 | 320 | `eef8f7940f5e28967d65d6fc17e0450bd78a2e4671a0be71da178e3ca8fb89f7` | built-in preset (verified stock prefix) | да |
| dejavu_hd | zone12 | 448 | `b688d491732c29ee42e3e4ee971c22bda289dfc2c7d3638d7058cebd57110557` | ei_atlas_profile_zone12_9ad06595d6f4fc24.json | да |
| jamaisvu_hd | zone13 | 512 | `a8b2299c4e8738702f496be75c2d0ff29cb462d8d6040d0a8ed83173500b087e` | built-in preset (verified stock prefix) | да |
| dejavu_hd | zone13 | 512 | `a8b2299c4e8738702f496be75c2d0ff29cb462d8d6040d0a8ed83173500b087e` | ei_atlas_profile_zone13_80462b1e06a3c51c.json | да |
| jamaisvu_hd | zone14 | 512 | `bd1873929c5ba9d3406d8be7d642dfa37fcb42a1a48edc914d5dd97610ee00d2` | built-in preset (verified stock prefix) | да |
| dejavu_hd | zone14 | 512 | `bd1873929c5ba9d3406d8be7d642dfa37fcb42a1a48edc914d5dd97610ee00d2` | built-in preset (verified stock prefix) | да |
| jamaisvu_hd | zone15 | 512 | `30eb16d617c63989bb43591526ea2b3c31c386c4aa179c1342d9e9b086ca61a8` | built-in preset (verified stock prefix) | да |
| dejavu_hd | zone15 | 512 | `30eb16d617c63989bb43591526ea2b3c31c386c4aa179c1342d9e9b086ca61a8` | built-in preset (verified stock prefix) | да |
| jamaisvu_hd | zone16 | 256 | `746dafd4e7d2c684211b3f8115257197f6c4efc868e79c932bc35e6433346331` | built-in preset (verified stock prefix) | да |
| dejavu_hd | zone16 | 256 | `746dafd4e7d2c684211b3f8115257197f6c4efc868e79c932bc35e6433346331` | built-in preset (verified stock prefix) | да |
| jamaisvu_hd | zone17 | 256 | `673be26955c437511886016441eaa4f03b2b5318531a0d99f7d1ae3d214f6b5d` | built-in preset (verified stock prefix) | да |
| dejavu_hd | zone17 | 256 | `673be26955c437511886016441eaa4f03b2b5318531a0d99f7d1ae3d214f6b5d` | built-in preset (verified stock prefix) | да |
| jamaisvu_hd | zone18 | 512 | `8628607116ecfbc4fedd1c20e97690b3c55088d433c9719d6e95f650cfd73e81` | built-in preset (verified stock prefix) | да |
| dejavu_hd | zone18 | 512 | `8628607116ecfbc4fedd1c20e97690b3c55088d433c9719d6e95f650cfd73e81` | ei_atlas_profile_zone18_5dacf3d99c04f402.json | да |
| jamaisvu_hd | zone19 | 512 | `34d5773fbcd07d962a19c07a29b68712f3493126cf55b8ca66d631ad876f4462` | built-in preset (verified stock prefix) | да |
| dejavu_hd | zone19 | 576 | `41483cb5c25081ed8c1c99307306c7635f81622068fa221d624dfebfc26abec8` | ei_atlas_profile_zone19_9590ba074c60bd3f.json | да |
| jamaisvu_hd | zone2 | 512 | `ed0bc69bd4abbb8f9f90fab44574a24b0b806521b6bc36501eea7c5add258612` | built-in preset (verified stock prefix) | да |
| dejavu_hd | zone2 | 1024 | `1b59da26b824810105d0018f11a3e30c5e8715577cb03bc80f2141b8a3e103c6` | ei_atlas_profile_zone2_930c5a9adbeb8410.json | да |
| jamaisvu_hd | zone20 | 192 | `89e83d13c2fc8a007bb694bb33c2f14bbc7669cba3e4a51c5675a607e360935a` | built-in preset (verified stock prefix) | да |
| dejavu_hd | zone20 | 192 | `89e83d13c2fc8a007bb694bb33c2f14bbc7669cba3e4a51c5675a607e360935a` | built-in preset (verified stock prefix) | да |
| jamaisvu_hd | zone3obr | 512 | `33d297edc193f88356c766772a84f5c6db27cf6cd9831d241cdafa9497cb5b4a` | built-in preset (verified stock prefix) | да |
| dejavu_hd | zone3obr | 1280 | `a28d3ddbe2aeca0ba486e86a98749d0f9a297518b96fddf4553ca41813d5e03c` | ei_atlas_profile_zone3obr_68a9aff25b0fef3a.json | да |
| jamaisvu_hd | zone4 | 512 | `4279b31201a5d960d870988415550782e9bae5b28c65cea04095eaf2714d41ed` | built-in preset (verified stock prefix) | да |
| dejavu_hd | zone4 | 704 | `a11f9c796238ad034cfbd806ad7753a0b520c80ae52bd054557b9919817ce712` | ei_atlas_profile_zone4_64d6e4a84209b4b0.json | да |
| jamaisvu_hd | zone5_1 | 192 | `2908a9617277f4e0ff663bf032325941fc57879af573c918e13f249e4de54635` | built-in preset (verified stock prefix) | да |
| dejavu_hd | zone5_1 | 192 | `2908a9617277f4e0ff663bf032325941fc57879af573c918e13f249e4de54635` | built-in preset (verified stock prefix) | да |
| jamaisvu_hd | zone6 | 512 | `4aff5893f635e48e0c5ec005172e81e7e69584d9a0da8b4c285d16031b3cc799` | built-in preset (verified stock prefix) | да |
| dejavu_hd | zone6 | 832 | `709cd6264d9a4af7efd8dd4dc90fd1d1f64942b28c35adf9d1eb0871cc9e859a` | ei_atlas_profile_zone6_29ba19fc3b366599.json | да |
| jamaisvu_hd | zone6_2 | 512 | `916e18f19c3b648d9951af2e9930f3fa7e9d67c97fd44bf09d2f5ceebeee6f56` | built-in preset (verified stock prefix) | да |
| dejavu_hd | zone6_2 | 512 | `916e18f19c3b648d9951af2e9930f3fa7e9d67c97fd44bf09d2f5ceebeee6f56` | built-in preset (verified stock prefix) | да |
| jamaisvu_hd | zone7 | 512 | `dacf1b6653cee1f4afc908b1ff3fe219784f4b4a1ff8d38fd46efb38aa9cd897` | built-in preset (verified stock prefix) | да |
| dejavu_hd | zone7 | 960 | `38870e1b88fc5053dce9427590a9dfcdb75ed3360aa3b3642a787ca36400dd24` | ei_atlas_profile_zone7_544f6169e4e8cc8f.json | да |
| jamaisvu_hd | zone8 | 512 | `4ad67630a39a00a6d5fb1d421e7873c76faa64707a4c5565744a84476dfdc35e` | built-in preset (verified stock prefix) | да |
| dejavu_hd | zone8 | 896 | `93ae27c5ced50c7aa0eef7411ddcd3366dc5c62b41221e89e14f322eb7d03b97` | ei_atlas_profile_zone8_bd4830cd58873ac6.json | да |
| jamaisvu_hd | zone9 | 512 | `bd5f43f33363351807dd6069ab1eb861fc1b66177500dc0d29710474253d77fe` | built-in preset (verified stock prefix) | да |
| dejavu_hd | zone9 | 1152 | `bebe7262558f097eba3bcb54de94682224400af826d05cbe9efda09e83d78aa5` | ei_atlas_profile_zone9_446a292101d05de4.json | да |
| jamaisvu_hd | zonemainmenunew | 192 | `20275e1d9ce2a32894755a8aed11ec1d8e7fd31c96779fbf871cbba46dd871ec` | built-in preset (verified stock prefix) | да |
| dejavu_hd | zonemainmenunew | 384 | `a73c1282963b825f064eb01edfaebab3993bd458a6e76919960486352be1f2cb` | ei_atlas_profile_zonemainmenunew_9ef730bee1022799.json | нет |

## SHA MPR на момент чтения

| Профиль | MPR | Namespace | Страниц MP | Байт | SHA256 | Чтение UTC |
|---|---|---|---|---|---|---|
| Dejavu | basegipat.mpr | basegipat_0c2dfc8150c3 | 8 | 212677 | `1491427a100e7e333bfee4d0b6b8760015a23cd20dabb986b86b51e48f37ec42` | 2026-10-11T11:36:41.943178+00:00 |
| Dejavu | bz10k.mpr | bz10k | 7 | 162782 | `e3f838075860b106bfeec6523c9a7f1b039798da2e0bd9d9cc62e558ea2a05aa` | 2026-10-11T11:36:41.944806+00:00 |
| Dejavu | bz11k.mpr | bz11k | 8 | 170206 | `9889bf92cd955a78d4ca46fc95efa6286ab7b196d1119ca1522ae8b0ba089692` | 2026-10-11T11:36:41.947320+00:00 |
| Dejavu | bz13h.mpr | bz13h | 8 | 179774 | `c2acd61e92001cd3ac3dd5f20994666e1ff694cfc97c416db09285e6930209f1` | 2026-10-11T11:36:41.948323+00:00 |
| Dejavu | bz14h.mpr | bz14h | 4 | 276020 | `62f5a6f94d99fa20e33b9f6a37dc27dc370aa8fafa4bceaf26395b1ed2a2a1a1` | 2026-10-11T11:36:41.950324+00:00 |
| Dejavu | bz15h.mpr | bz15h | 4 | 149470 | `d2d19ed56242525998a881b252988506338715c1318797cc893c7b59a70f960c` | 2026-10-11T11:36:41.952323+00:00 |
| Dejavu | bz16h.mpr | bz16h | 8 | 199262 | `8d4469ba7ada3db760530231c7990418e4949a699fd761e55b98391d2edd3818` | 2026-10-11T11:36:41.953323+00:00 |
| Dejavu | bz18h.mpr | bz18h | 4 | 246812 | `46234b3f8ceeb3cf3ccad30fb3db47c951b7609cbe5e8645493df7d8d2b39dda` | 2026-10-11T11:36:41.954323+00:00 |
| Dejavu | bz19g.mpr | zone10 | 8 | 150493 | `d044df1ec7f2522646a1a1241d726caa693799b96b77049163de9e9178dd1088` | 2026-10-11T11:36:41.956324+00:00 |
| Dejavu | bz2g.mpr | bz2g | 8 | 189501 | `1342f38d8e66b5e6a70754bb905cd74708f3fecf7684886db667d83284b69872` | 2026-10-11T11:36:41.958324+00:00 |
| Dejavu | bz3g.mpr | bz3g | 8 | 160397 | `3386410b195e53a96e4f937ba1a115cff3eaa440269677f1d3d777baffa65210` | 2026-10-11T11:36:41.959324+00:00 |
| Dejavu | bz4g.mpr | bz4g | 8 | 218813 | `4b7743c2bf9ba1a5c50cac58ac94a19e18a0679c506832430921095fad727700` | 2026-10-11T11:36:41.961324+00:00 |
| Dejavu | bz5g.mpr | bz5g | 8 | 257885 | `653aee8892d619f05b0bbda7f936660e765fe20cafbe826742e244177472b17a` | 2026-10-11T11:36:41.962827+00:00 |
| Dejavu | bz6g.mpr | bz6g | 8 | 150701 | `fce164fb818d52999badbd980a1f3ef83cd41814fb88ce3dc29bc2e3cbbd7f06` | 2026-10-11T11:36:41.965834+00:00 |
| Dejavu | bz7g.mpr | Bz7g | 4 | 217693 | `15cc764e53d87ea99f92e12fa337645cc6d0afcbbdde3ae9ca3be32776a09366` | 2026-10-11T11:36:41.966834+00:00 |
| Dejavu | bz8k.mpr | bz8k | 8 | 170013 | `7d70e5d6bd1904bce99e21991165c9e378fe3f9b19503ecc8df9705dffa5802c` | 2026-10-11T11:36:41.970337+00:00 |
| Dejavu | bz9k.mpr | bz9k | 8 | 199245 | `edcd5b93331c95f80dc592d1ccb39c120aadd4b3d28c4110b1628ea854c38ad9` | 2026-10-11T11:36:41.971905+00:00 |
| Dejavu | zone1.mpr | zone1_17763baa57df | 15 | 1545463 | `07cf12287dc8a911abad943e819ace2f9cacca47532b307f975680fdfce0b7cc` | 2026-10-11T11:36:41.972905+00:00 |
| Dejavu | zone10.mpr | zone10 | 8 | 544623 | `56f0a601330d88912efcc17ef58a2edb5168640d58e965cc040fc30e9076b925` | 2026-10-11T11:36:41.976905+00:00 |
| Dejavu | zone11.mpr | zone11_0132fbfdd188 | 8 | 2184060 | `0ce97df95e1d5d9f0a422976a5049b81c8a1bd6d157f823d03bdfaf16b82a415` | 2026-10-11T11:36:41.978905+00:00 |
| Dejavu | zone12.mpr | Zone12 | 7 | 1692735 | `135a8e359a99d3545852536021247807ba2a2b3192971ae0be0c54e33c98dde1` | 2026-10-11T11:36:41.982910+00:00 |
| Dejavu | zone12dun2.mpr | zone14_975e928d3190 | 4 | 305612 | `f4eec4ceb91fb29d90f11f6c1de57cd2b70ebba105909bf00efd7aa0ed5c0498` | 2026-10-11T11:36:41.986520+00:00 |
| Dejavu | zone13.mpr | zone13_63226eeedc0c | 11 | 1985940 | `1cd29ff01b50e2a5ac3c6d8686dc828c6780e0d4c94786139b958692f8c5ff18` | 2026-10-11T11:36:41.988525+00:00 |
| Dejavu | zone14.mpr | zone14_60f652260b04 | 8 | 545292 | `aa0848c0a47f0521330b7f703290596522c8d4b1b8b654c7e9201ae52e25d5b6` | 2026-10-11T11:36:41.993525+00:00 |
| Dejavu | zone15.mpr | zone15 | 8 | 1620537 | `2d217ab03e6905a4f5aaa605fbc92344d8301a1a861d4243b90aaed063a41b32` | 2026-10-11T11:36:41.999036+00:00 |
| Dejavu | zone16.mpr | zone16 | 4 | 727419 | `1e67b69121cf837d40b11618ade9fb791cdfb4db3546acc6cb24d9f679b63811` | 2026-10-11T11:36:42.003033+00:00 |
| Dejavu | zone17.mpr | zone17 | 4 | 1335601 | `636412e3aa299f0b47470191356a2ee588f2a332331f5dd03ce1a14f9dbda734` | 2026-10-11T11:36:42.005033+00:00 |
| Dejavu | zone18.mpr | zone18_55491c494740 | 9 | 4251868 | `c25f0f97318cecb27565ce44b28f258099939916a7cf73704ff91a4abda2ee0e` | 2026-10-11T11:36:42.007539+00:00 |
| Dejavu | zone18dun1.mpr | zone18 | 8 | 224719 | `07fe052f6bb9c05e4454d7dc57b1dce8ced5c475753f5e96c6cbefc12eba029d` | 2026-10-11T11:36:42.016795+00:00 |
| Dejavu | zone19.mpr | zone19_bb84f59c5962 | 15 | 3047218 | `6b7fe9de0c442e0b2c01057280bd2b767be578c0dbe5509131b74ce5077814e4` | 2026-10-11T11:36:42.018791+00:00 |
| Dejavu | zone2.mpr | zone2 | 16 | 357994 | `86392b9408830981c9334ae43de8f4b3243846fba2cb31f97c7385a376bfe802` | 2026-10-11T11:36:42.024793+00:00 |
| Dejavu | zone20.mpr | Zone20 | 3 | 232661 | `6124a37c67f49d7a2d631b110f75d713d9ac51a4ec13791a793bfd6863639b27` | 2026-10-11T11:36:42.026792+00:00 |
| Dejavu | zone3obr.mpr | zone3obr_0bcba8177f9a | 23 | 1509130 | `f567b860896d6084c2669046383d90dcbc59ae079b119e3dd21ff01d33e83caf` | 2026-10-11T11:36:42.028792+00:00 |
| Dejavu | zone4.mpr | zone4_fc5a0ba98c52 | 16 | 1895371 | `4af7620ef1960e029b57835fe4f05e9d72dceca01e908867e05bbc97b3f275a3` | 2026-10-11T11:36:42.031796+00:00 |
| Dejavu | zone5_1.mpr | Zone5_1 | 3 | 459528 | `e483a77db09f1ce5bcf9f564a599447f86f41745af9fbf185cb38430de49b871` | 2026-10-11T11:36:42.035796+00:00 |
| Dejavu | zone6.mpr | zone6_fe645a375f96 | 19 | 1056171 | `0676f3f559f4623b344b2773bdbae97dce02ec508965e6ba8cfba256613e3535` | 2026-10-11T11:36:42.037799+00:00 |
| Dejavu | zone6_2.mpr | zone6_2 | 8 | 1047774 | `caaf098a818ae2dfb58274e4a3c55e226823adf878ef51f8012acd1316cd5f3b` | 2026-10-11T11:36:42.040796+00:00 |
| Dejavu | zone7.mpr | zone7_44a43bfdda10 | 15 | 2671771 | `8e4a1dbbe6a373055e3f6c58eecd86d59aeea3faeb90366f19d11fd8ed9b36d1` | 2026-10-11T11:36:42.043795+00:00 |
| Dejavu | zone8.mpr | zone8 | 14 | 1649771 | `4621d378c2b9cf31934549263ec0516d389f664c1db49e3dba0fd7081c4bde15` | 2026-10-11T11:36:42.049303+00:00 |
| Dejavu | zone9.mpr | zone9_f24337452d99 | 25 | 1733977 | `d4b6914f190622a8d43bb53cc3df310b2f0ce33594ad5d952136708f584144a2` | 2026-10-11T11:36:42.054810+00:00 |
| Dejavu | zonemainmenunew.mpr | zonemainmenunew_91d1b040b2b8 | 6 | 246693 | `696c740368c6d90ccd2f210eb3f39bd3b325416b5eab397d30d2400ea5bfd023` | 2026-10-11T11:36:42.057813+00:00 |
| Base | basegipat.mpr | BaseGipat | 8 | 209122 | `1b96ad36ff70aae2ccbf5769f452934fe91a2aa6c0f4296bfc2cec3cba959cae` | 2026-10-11T11:36:42.060810+00:00 |
| Base | bz10k.mpr | bz10k | 5 | 159518 | `c166ad0a13be1f60f3d445e88d6eb3118bc990980829158f66647d545de3504a` | 2026-10-11T11:36:42.061809+00:00 |
| Base | bz11k.mpr | bz11k | 8 | 170206 | `9889bf92cd955a78d4ca46fc95efa6286ab7b196d1119ca1522ae8b0ba089692` | 2026-10-11T11:36:42.062809+00:00 |
| Base | bz13h.mpr | bz13h | 8 | 179774 | `c2acd61e92001cd3ac3dd5f20994666e1ff694cfc97c416db09285e6930209f1` | 2026-10-11T11:36:42.065318+00:00 |
| Base | bz14h.mpr | bz14h | 4 | 276174 | `0598be0f1cfc763a9e143f926d621db27f0f187e06ed159be1532b10bda092fe` | 2026-10-11T11:36:42.066320+00:00 |
| Base | bz15h.mpr | bz15h | 4 | 149470 | `d2d19ed56242525998a881b252988506338715c1318797cc893c7b59a70f960c` | 2026-10-11T11:36:42.068317+00:00 |
| Base | bz16h.mpr | bz16h | 8 | 199262 | `8d4469ba7ada3db760530231c7990418e4949a699fd761e55b98391d2edd3818` | 2026-10-11T11:36:42.070316+00:00 |
| Base | bz18h.mpr | bz18h | 4 | 246942 | `d2e2086af4d5c90f84fe394138b989a43080f5c638bd2777c6081372680d95d0` | 2026-10-11T11:36:42.072316+00:00 |
| Base | bz2g.mpr | bz2g | 8 | 189501 | `1342f38d8e66b5e6a70754bb905cd74708f3fecf7684886db667d83284b69872` | 2026-10-11T11:36:42.077317+00:00 |
| Base | bz3g.mpr | bz3g | 8 | 160397 | `3386410b195e53a96e4f937ba1a115cff3eaa440269677f1d3d777baffa65210` | 2026-10-11T11:36:42.078317+00:00 |
| Base | bz4g.mpr | bz4g | 8 | 218813 | `4b7743c2bf9ba1a5c50cac58ac94a19e18a0679c506832430921095fad727700` | 2026-10-11T11:36:42.080316+00:00 |
| Base | bz5g.mpr | bz5g | 8 | 257885 | `653aee8892d619f05b0bbda7f936660e765fe20cafbe826742e244177472b17a` | 2026-10-11T11:36:42.081317+00:00 |
| Base | bz6g.mpr | bz6g | 8 | 150701 | `fce164fb818d52999badbd980a1f3ef83cd41814fb88ce3dc29bc2e3cbbd7f06` | 2026-10-11T11:36:42.083320+00:00 |
| Base | bz7g.mpr | Bz7g | 4 | 217693 | `15cc764e53d87ea99f92e12fa337645cc6d0afcbbdde3ae9ca3be32776a09366` | 2026-10-11T11:36:42.084321+00:00 |
| Base | bz8k.mpr | bz8k | 8 | 170013 | `7d70e5d6bd1904bce99e21991165c9e378fe3f9b19503ecc8df9705dffa5802c` | 2026-10-11T11:36:42.086319+00:00 |
| Base | bz9k.mpr | bz9k | 8 | 199245 | `edcd5b93331c95f80dc592d1ccb39c120aadd4b3d28c4110b1628ea854c38ad9` | 2026-10-11T11:36:42.087319+00:00 |
| Base | zone1.mpr | Zone1 | 8 | 768782 | `ffa49c7fd896df7eb82d1bb55bf4e52ab8e4d348110de08dd24f9e6049349840` | 2026-10-11T11:36:42.089320+00:00 |
| Base | zone10.mpr | Zone10 | 8 | 544623 | `cb4dab1d36c30c19f6800be54b7b95e8f2252e5dfa567cb6fba57e0dee23dc9d` | 2026-10-11T11:36:42.093320+00:00 |
| Base | zone11.mpr | Zone11 | 8 | 2181583 | `4876ba0fb52a9b80be59bf19569492f1551de29844c21e8f2e219e5086ee2eab` | 2026-10-11T11:36:42.095324+00:00 |
| Base | zone12.mpr | Zone12 | 5 | 1689295 | `04a1e60832789bd8c93cdad98f98bee99e10dd1321b377c2679efa9c483e3917` | 2026-10-11T11:36:42.100833+00:00 |
| Base | zone13.mpr | Zone13 | 8 | 1699575 | `d7930ba3ac83cc681236fc4a27971c155d16e5f7547cb6af5f16f69fcfb1bc99` | 2026-10-11T11:36:42.104832+00:00 |
| Base | zone14.mpr | Zone14 | 8 | 544815 | `4d9af413ab731b118b4dab3c600c34aeeda2f5ec71dd86c98820b5e00eba09aa` | 2026-10-11T11:36:42.108348+00:00 |
| Base | zone15.mpr | Zone15 | 8 | 1621327 | `34816986dbceaca2e56df1330d70c3154c1dec835b5ada1913e9dc00011ea708` | 2026-10-11T11:36:42.110345+00:00 |
| Base | zone16.mpr | Zone16 | 4 | 727787 | `52c25ccfff7b076e2c4139b14146368600e43bbe2203ec701afd0c8c3c2971a2` | 2026-10-11T11:36:42.114345+00:00 |
| Base | zone17.mpr | Zone17 | 4 | 1336047 | `36dc60cddc6949eb05af45ee38136166a9ef25def8e44010e4d8a433723936a3` | 2026-10-11T11:36:42.117345+00:00 |
| Base | zone18.mpr | Zone18 | 8 | 1660303 | `711f823ee1cd6e04f9d576901baf6b7f75e2f7f74b1425ddb6f3eb97f62e8493` | 2026-10-11T11:36:42.120345+00:00 |
| Base | zone19.mpr | Zone19 | 8 | 2989231 | `0fa52ec7585e2a9755c7a5608f35ad5ef0a853b0d77bd2bb607724006c3e0262` | 2026-10-11T11:36:42.125345+00:00 |
| Base | zone2.mpr | Zone2 | 8 | 263670 | `27e546eab73f0edb1f8544f6460ea4b7711da2d341ad3baad298cf90d211f088` | 2026-10-11T11:36:42.132349+00:00 |
| Base | zone20.mpr | Zone20 | 3 | 232661 | `6124a37c67f49d7a2d631b110f75d713d9ac51a4ec13791a793bfd6863639b27` | 2026-10-11T11:36:42.133349+00:00 |
| Base | zone3obr.mpr | Zone3Obr | 8 | 1236449 | `2b5ce1d6523241564f2d340efe5c94ce39f043ac5058433fc97c4b2418bbd3e6` | 2026-10-11T11:36:42.135349+00:00 |
| Base | zone4.mpr | Zone4 | 8 | 1884862 | `584e9f448d5918119c5561aee4cc64389ccd6eaecdf4947583d0b74583f172b1` | 2026-10-11T11:36:42.138349+00:00 |
| Base | zone5_1.mpr | Zone5_1 | 3 | 459528 | `e483a77db09f1ce5bcf9f564a599447f86f41745af9fbf185cb38430de49b871` | 2026-10-11T11:36:42.142349+00:00 |
| Base | zone6.mpr | Zone6 | 8 | 936770 | `a76dee6332f171e24b359839af571011e2debc3f881510b89d3467e3cac75fa3` | 2026-10-11T11:36:42.144349+00:00 |
| Base | zone6_2.mpr | Zone6_2 | 8 | 936940 | `f36328b0135f322201474ba3e7c054eef88731b5a1d8cc1776c15cdbe0f48f60` | 2026-10-11T11:36:42.146349+00:00 |
| Base | zone7.mpr | Zone7 | 8 | 2483964 | `ae2823ba57e4d755f4fdb3715441744a4b03e454645bafcf3d8e54da920f484a` | 2026-10-11T11:36:42.150371+00:00 |
| Base | zone8.mpr | Zone8 | 8 | 1640395 | `c976178db752bfee2f65236df97045772be8915ff11af10599caaf6a964c021a` | 2026-10-11T11:36:42.157373+00:00 |
| Base | zone9.mpr | zone9 | 8 | 1504650 | `8a4f47ec8d444274f0c71b0ca0cea3d6a295bc2710a32a7cb79472aeee1ea9a5` | 2026-10-11T11:36:42.160371+00:00 |
| Base | zonemainmenunew.mpr | ZoneMainMenuNew | 3 | 67316 | `d4b12947907ff1c6778ce4b1febeb2df20c9ff782a338956bd82e76041266113` | 2026-10-11T11:36:42.163874+00:00 |

## Повторение и проверка результата

Запуск из корня checkout (создаёт новый приватный снимок, живые файлы только читает):

```powershell
& 'C:/Users/samol/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe' tests/hd_coverage_audit.py --report docs/validation/ATAMAN_HD_COVERAGE_AUDIT_20261011.md
```

`--snapshot build-tests-hd-coverage/<snapshot>` повторно анализирует проверенные копии и зафиксированные MP-метаданные; `--render-only` использует уже рассчитанный audit.json. `--capture-only` только фиксирует входы; `--self-test` проверяет разбор и правила подсчёта на синтетике без игры. При изменении RES во время копирования скрипт ждёт 60 секунд и повторяет, не более 10 попыток. Новый полный запуск всегда создаёт отдельный каталог.

Проверка: точные SHA RES до/после/копии; bounds/уникальность имён RES; MP-массивы; непрерывность страниц; привязка семантики по декодированным пикселям; суммы слотов и размеров. Непривязанный профиль даёт явное unknown, а не PASS классификации.

`--self-test`: PASS (RES bounds, DXT1 alpha/RGB, uniform/transition/ambiguous/unknown, объединение дубликатов, неизвестный статус анимации). Контроль сохранения сумм: PASS. Это аудит данных, а не тест установленного редактора или игровая приёмка.

Для коммита сопровождающим: `tests/hd_coverage_audit.py`, этот отчёт и только пункт `EI-HD-COVERAGE-AUDIT` в `ROADMAP.md`. `build-tests-hd-coverage/` приватен и исключён из Git.
