# EI-TERRAIN-HD-REPUBLISH-138 — HD-перепубликация атласов Dejavu

Поручение2026-10-10, продолжение и проверки2026-10-11. Реализована версия138; установлена137, живое применение не выполнялось. База137 `cd329528d0ad446085703e095a8bc23cec8c5ae1`, сохранён WIP `b7f7bd8d67bde3075ac350688f1edf5049272e48`; продолжение коммитится поверх него в main.

## Функция и способ восстановления

**Ландшафт → Дополнительно → Пересобрать атлас карты в HD…**, objectName `terrainHdRepublish`. Команда доступна для сохранённого опубликованного атласа Dejavu; Base/легаси отказываются. UI анализирует источники, показывает HD/переходы/остаток SD и публикует штатными staged/backup/atomic coordinators. Перед записью проверяются исходные страницы и MPR; отказ MPR вызывает hash-owned rollback RES. В этой задаче кнопку на живой карте не нажимали.

Пакет: `--terrain-hd-republish-check <fixture.json>`; JSON: `sources` (Dejavu, затем JamaisVu135), `archive`, `maps`, `receipt`. Check без `mode` не пишет карты. `mode:"build"`, новый `output` создают отдельные MPR и atlas RES; существующий output и размещение рядом с входными картами/архивами отказываются. `pngMaps` задаёт экспорт сравнения. [Фикстура полного набора](../../build-tests-138-hdrepublish/published-copies-fixture-v2.json); [итоговые14 receipts](../../build-tests-138-hdrepublish/published-copies-results-final.json).

- Индекс всех stock namespaces обоих HDPack, primary HD имеет приоритет, primary SD не скрывает fallback HD. Точное совпадение decoded mip128, BC1 publication generations0…3, recursive sourceNamespace/sourceSlots provenance промежуточных опубликованных профилей. Разные HD при одном SD без однозначного происхождения не подменяются.
- Нечёткий путь: native mip1 и box/smooth2:1 из HD, все4 поворота. SEC не кодирует отражения, их не вводили. 4×4 RGB block means дают нижнюю границу MSE; сравниваются ВСЕ кандидаты в пределах ошибки, без top-N отсечения. Затем full RGB MSE/PSNR/global RGB SSIM и точное alpha. Лучший должен пройти MSE≤64, SSIM≥0.95 и запас перед другим HD ≥max(1.8×MSE,MSE+4); уникальное проверенное provenance ограничивает конкуренцию вариантами того же источника, пороги качества не ослабляет.
- Калибровка3824 контролируемых DXT-перекомпрессий известных stock: MSE p50=0.7254, p95=59.5608, p99=77.8270, SSIM p01=0.95089. Верхняя MSE64 намеренно ниже p99; часть исторических перерисовок/масштабирований не принимается. Калибровка и выбранный/второй кандидат доступны по каждому слоту. Это evidence matching, не доказательство художественной тождественности любой похожей картинки.
- Modern: прежняя сигнатура, class/route owner, маска/variation/blend, выбранные uniform источники; полный SD witness должен пройти те же границы и margin разных HD-рецептов. Сравниваются first/preferred/local/corner-patch source policies. Принятый рецепт пересчитывается в256 тем же production generator.
- Mixed-edge: общий production `buildStockBrushes`, crisp-stock-v1 hash/variation/runtime; SD ownership bank фиксируется и применяется к HD. Для старого непрозрачного hash без воспроизводимого bank — отказ и SD. В реальных14 профилях mixedEdgeRecipes отсутствуют; успешная поддержка этого пути подтверждена синтетикой, не реальной картой.
- Stock256 получает исходные BC1 blocks без перекомпрессии, включая перестановку блоков/селекторов для поворота. Shared самые мелкие mip, где один блок обслуживает несколько слотов, собираются целой страницей; сохранить независимые исходные shared blocks одновременно формат не позволяет.

MMP задаёт один размер на64 слота. Чтобы доступное HD не блокировалось SD-соседом, недоступный слот переносится в256 carrier: исходные BC1 endpoints повторяются, selectors дублируются; decoded256 равен точному nearest-neighbor исходного128, mip128…4 содержит прежние BC1 blocks. **Это SD128 детализация в native256 ячейке, не восстановленный HD.** Полностью невосстановленная страница остаётся побайтно native128. `upgradePartialPages:false` включает консервативный whole-page128 режим; он также проверен синтетикой.

Namespace — hex SHA страниц, canonical profile и tile topology. Profile сохраняет все свойства кроме namespace/pixel binding; история восстановления хранится отдельным `ei_atlas_hd_recovery_<namespace>.json`, чтобы сериализация137 не теряла unknown поля и семантика оставалась активной. Semantic land/overlay/random seed сохранены, bindings перевычислены. В MPR меняются namespace имен MP/SEC, MP textureSize/tileSize и semantic binding; SEC payloads/classes/materials/animations неизменны.

## Все опубликованные карты

Всего **11584 слота / 181 страниц: stock HD 6371, перегенерировано256 1907, SD128 детализация 3306**. Из SD: native128 576, carrier256 2730. Учитываются физические слоты, включая неиспользуемый padding; BaseGipat здесь карта MODS/Dejavu, не Base.

| Карта | Слоты/страницы | HD stock | Ген256 | SD128 detail | native128 / carrier256 |
|---|---:|---:|---:|---:|---:|
| basegipat | 448/7 | 445 | 0 | 3 | 0/3 |
| zone1 | 960/15 | 672 | 76 | 212 | 0/212 |
| zone11 | 512/8 | 455 | 14 | 43 | 0/43 |
| zone12dun2 | 256/4 | 181 | 15 | 60 | 0/60 |
| zone13 | 704/11 | 490 | 41 | 173 | 0/173 |
| zone14 | 512/8 | 462 | 12 | 38 | 0/38 |
| zone18 | 576/9 | 12 | 9 | 555 | 448/107 |
| zone19 | 960/15 | 45 | 100 | 815 | 128/687 |
| zone3obr | 1472/23 | 793 | 368 | 311 | 0/311 |
| zone4 | 1024/16 | 618 | 206 | 200 | 0/200 |
| zone6 | 1216/19 | 632 | 334 | 250 | 0/250 |
| zone7 | 960/15 | 628 | 179 | 153 | 0/153 |
| zone9 | 1600/25 | 700 | 474 | 426 | 0/426 |
| zonemainmenunew | 384/6 | 238 | 79 | 67 | 0/67 |

| Остаток SD по карте | Exact stock128 без HD identity | SD stock с неподтверждённой HD identity | Неоднозначно | Старый рецепт не восстановлен | Нет calibrated HD кандидата |
|---|---:|---:|---:|---:|---:|
| basegipat | 3 | 0 | 0 | 0 | 0 |
| zone1 | 151 | 2 | 21 | 38 | 0 |
| zone11 | 8 | 0 | 34 | 1 | 0 |
| zone12dun2 | 56 | 0 | 2 | 2 | 0 |
| zone13 | 108 | 0 | 65 | 0 | 0 |
| zone14 | 37 | 0 | 1 | 0 | 0 |
| zone18 | 533 | 0 | 0 | 22 | 0 |
| zone19 | 582 | 0 | 1 | 232 | 0 |
| zone3obr | 271 | 0 | 39 | 1 | 0 |
| zone4 | 144 | 0 | 36 | 19 | 1 |
| zone6 | 126 | 0 | 39 | 84 | 1 |
| zone7 | 125 | 0 | 16 | 12 | 0 |
| zone9 | 228 | 2 | 73 | 121 | 2 |
| zonemainmenunew | 51 | 14 | 2 | 0 | 0 |

[Полный список каждого оставленного слота/источников/причин/SHA](../../build-tests-138-hdrepublish/summary138.json). Группы SD: `{"known-SD-stock-source": 2423, "ambiguous": 329, "SD-stock-with-unqualified-HD-identity": 18, "unreproduced-legacy-recipe": 532, "no-calibrated-HD-match": 4}`. Exact-SD источник без HD означает: в обоих заданных архивах найден pixel-exact stock128, соответствующей HD identity нет; это не утверждение об отсутствии похожего художественного материала где-либо. SD-переход с такой stock identity также отнесён к этой группе, даже если попытка Modern-рецепта отказалась. Остальные: неоднозначные кандидаты, непродуцируемый старый рецепт, отсутствие кандидата в калиброванной ошибке. «Ручной рисунок» без доказательств не заявляется. Анимация сама по себе не является причиной отказа; диапазоны и все frames сохраняются.

## Открытия и визуальные сравнения

Отдельно выделены 18 SD-stock слотов: одноимённая HD identity существует, но сопоставление не прошло; это не «HD отсутствует». [Аудит identities в обоих HDPack](../../build-tests-138-hdrepublish/sd-origin-check138.json). Полный список и причины всех 3 306 оставленных слотов приведены выше.

Все14 исходных копий и14 новых MPR открыты настоящим MainWindow/CView138 на изолированном скрытом desktop под ui-lock. Активен точный approved profile из нужного RES, usable catalog/палитры и сохранённая semantic field (где была). Числа палитр, land/overlay vertex hashes, все свойства profile кроме binding, сектора, camera, animations совпали. Отдельный разбор всех MPR members подтверждает неизменность SEC payloads и остальных member payloads; MP изменён только в разрешённых полях. Вода подтверждена ranges/classes/frames, без запуска игры. [28 openings, память, camera, PNG receipts](../../build-tests-138-hdrepublish/copy-openings138.json).

PNG5 карт: Ground projection 6×4 клетки,1536×1024,256px/клетка; крупный выбранный восстановленный переход1024×1024. Исходная128 показана nearest-neighbor в том же масштабе. SEC rotation применяется одинаково. Overlay/lighting в projection исключены явно. Дополнительный headless Qt framebuffer capture1024×768 (в том числе с наведением camera и explicit paint/swap) остался чёрным. Этот helper FAIL не является визуальным PASS и не доказывает дефект данных/HD; причина не подтверждена. Black captures сохранены в visuals/, но не используются как before/after доказательства. Это авторский просмотр артефактов, не художественная/игровая приёмка владельца.

| Карта | Участок до / после | Переход до / после |
|---|---|---|
| zone6 | [до](../../build-tests-138-hdrepublish/published-copies-v3-remaining/zone6/before.png) / [после](../../build-tests-138-hdrepublish/published-copies-v3-remaining/zone6/after.png) | [до](../../build-tests-138-hdrepublish/published-copies-v3-remaining/zone6/transition-before.png) / [после](../../build-tests-138-hdrepublish/published-copies-v3-remaining/zone6/transition-after.png) |
| zone9 | [до](../../build-tests-138-hdrepublish/published-copies-v3-remaining/zone9/before.png) / [после](../../build-tests-138-hdrepublish/published-copies-v3-remaining/zone9/after.png) | [до](../../build-tests-138-hdrepublish/published-copies-v3-remaining/zone9/transition-before.png) / [после](../../build-tests-138-hdrepublish/published-copies-v3-remaining/zone9/transition-after.png) |
| zone11 | [до](../../build-tests-138-hdrepublish/published-copies-v2/zone11/before.png) / [после](../../build-tests-138-hdrepublish/published-copies-v2/zone11/after.png) | [до](../../build-tests-138-hdrepublish/published-copies-v2/zone11/transition-before.png) / [после](../../build-tests-138-hdrepublish/published-copies-v2/zone11/transition-after.png) |
| zone13 | [до](../../build-tests-138-hdrepublish/published-copies-v2/zone13/before.png) / [после](../../build-tests-138-hdrepublish/published-copies-v2/zone13/after.png) | [до](../../build-tests-138-hdrepublish/published-copies-v2/zone13/transition-before.png) / [после](../../build-tests-138-hdrepublish/published-copies-v2/zone13/transition-after.png) |
| zone4 | [до](../../build-tests-138-hdrepublish/published-copies-v2/zone4/before.png) / [после](../../build-tests-138-hdrepublish/published-copies-v2/zone4/after.png) | [до](../../build-tests-138-hdrepublish/published-copies-v2/zone4/transition-before.png) / [после](../../build-tests-138-hdrepublish/published-copies-v2/zone4/transition-after.png) |

[Проверка размеров и непустых изображений всех 20 PNG](../../build-tests-138-hdrepublish/png-validation138.json). Все эти PNG просмотрены; чёрные дополнительные Qt captures не входят в данный набор.

В Zone11 отчётливо прибавились трещины/фактура грунта. В Zone13 у пересозданного перехода снег/светлый грунт край менее ступенчатый и мелкая фактура другая; в Zone4 граница растительности и рисунок травы плотнее. В Zone6 аналогичный травяной переход сохраняет крупную область и угол границы, но отдельные стебли256 заметно иначе распределены, чем128. В Zone9 граница трава/грунт сохраняет общую форму, но кромка имеет более мелкую ступень, а детали грунта/стеблей выглядят иначе. Вытянутые краевые полосы некоторых прежних рецептов видны и до, и после; HD не исправляет этот художественный недостаток. Сохранение recipe/signature/ownership не означает одинаковый рисунок мелких деталей старой128 генерации. Неоднозначные переходы сохранены SD, произвольный HD рецепт для них не выбран.

## Проверки и артефакты

Подготовка дополнительного UI helper: первая попытка TIMEOUT300s, русский заголовок диалога был записан в неправильной кодировке. После исправления кодировки попытка exit26 не завершила публикацию: прямой `done(Yes)` заменён настоящим нажатием стандартной кнопки Yes. Обе попытки не PASS; входная приватная пара перед повтором проверена как неизменная. Исправленный helper завершил публикацию, adopt и fresh reopen с exit0.

Fresh Release138 и runtime build wrappers exit0; `--version`138. Production-linked HD synthetic **4/0/0**,52 assertion sites: исходные BC1, повтор детерминирован, rotation/recompression, неправильный provenance и неоднозначный HD отказ, missing HD exact SD, partial carrier pixels/mips, conservative mode, prepare/publication SEC/classes/animations/semantic/seed и стабильный MPR, Modern256 и mixed-edge256 с тем же key/ownership. [QtTest](../../build-tests-138-hdrepublish/terrain_hd_republish_tests/qtest.txt).

- terrain_virtual_atlas_tests: 146/0/0
- terrain_resolve_audit_tests: 5/0/0
- world_terrain_sculpt_tests: 48/0/0
- recipe: PASS; подробные counts в receipt
- ui-widget: exit0, sentinel nativeAttempts=0
- scenario: exit0, sentinel nativeAttempts=0
- dark-theme: exit0, sentinel nativeAttempts=0
- gallery: exit0, sentinel nativeAttempts=0
- terrain_mixed_resolution_tests: 4/0/0
- res_reader_drain_tests: 23/0/0
- random: PASS; подробные counts в receipt
- mixed: PASS; подробные counts в receipt

Сквозной UI тест новой кнопки на приватной копии BaseGipat и целого1.27GB HDPack: путь MPR/RES перед вызовом проверен canonical-prefix внутри test-root; hidden confirmation Yes; штатные preflight/RES commit/MPR commit/adopt и fresh reopen PASS. 1222 прежних RES members сохранили точные payloads; новые MMP/profile/semantic members совпали с пакетной сборкой, MPR побайтно совпал с batch output (реальный детерминизм двух entry points). Recovery evidence различается набором настроенных источников, не входит в canonical profile binding. Live archive unchanged; sentinel nativeAttempts=0. [UI receipt](../../build-tests-138-hdrepublish/ui-function138.json).

[Полный регресс](../../build-tests-138-hdrepublish/regression-summary138.json). UI/scenario/dark/gallery и native widget suites используют in-memory clipboard sentinel; OpenClipboard владельца не вызывается. Gallery benchmark137/138:5 повторов,80 measurements,8/8 ready каждого вида, unavailable0; все8 медианных сравнений проходят≤+15ms или≤10%. [Числа](../../build-tests-138-hdrepublish/gallery-summary138.json).

Ранний WIP binary-SHA namespace исправлен `.toHex()`; это собственный дефект, не несовместимость данных. Незавершённые preliminary/v1 runs superseded. v2 timeout1800s после10 готовых карт **не PASS всего запуска**; v3 завершил оставшиеся4, итог объединён без повторной сборки готовых. До раскрытия EXE зависимостей own version probe зависал; windeploy исправлен, актуальная version проверка PASS. Эти preparation события не представлены как успешные проверки.

Другой процесс EI-TEST-TAILS20261011 занимал общий UI-lock; его lock ожидался. Сопровождающий затем закоммитил world `02e01e55b6f6c608d4519053114e1e627c97e91a` и backup isolation `408aa5eb6d7010082991626c22d17336e3569b29` как ships with138. Финальная fresh combined сборка включает оба принятых изменения, новый HD synthetic/regression/audit/benchmark пересвязан с ней. HD batch pages/prepare логика ими не затронута; результаты копий сохранены без повторной пересборки. World теперь48/0/0; прежний45/1 не заявляется результатом актуального артефакта. Эти принятые коммиты сохранены в истории, чужие файлы не включаются в новый scoped HD-коммит.

Проверенный EXE: `build-release-138-hdrepublish-combined/release/EIMapperEndurance.exe`, SHA256 `43370e542d89f39e6d4e42435413f59d14773e505ef188666629f12f80bf22de`. [Итоговый integrity receipt](../../build-tests-138-hdrepublish/final-integrity.json). Private данные/PNG/сборки ignored, не входят в Git или раздаваемый пакет.

## Размеры, память и бюджет

RES в таблице — отдельный atlas-only кандидат каждой карты, не копия всего live HDPack. MMP BC1+mips и RGBA projection отражают actual native размеры; RGBA =8×BC1 payload приближённо (мелкие BC1 блоки/headers дают небольшую погрешность). Peak — sampled working set реального x64 mapper opening, без измерения VRAM/GPU, не память игры.

| Карта | MMP MiB до→после | Новый RES MiB | MPR KiB до→после | BC1 MiB после / RGBA MiB projection | Mapper peak MiB до→после |
|---|---:|---:|---:|---:|---:|
| basegipat | 4.67→18.67 | 18.88 | 204.2→207.4 | 18.67 / 149.33 | 723.6→836.8 |
| zone1 | 10.00→40.00 | 40.97 | 1509.2→1515.3 | 40.00 / 320.00 | 930.3→1295.1 |
| zone11 | 5.33→21.33 | 21.64 | 2132.9→2157.4 | 21.33 / 170.67 | 905.9→1120.0 |
| zone12dun2 | 2.67→10.67 | 11.20 | 298.4→301.1 | 10.67 / 85.33 | 657.0→765.2 |
| zone13 | 7.33→29.33 | 30.25 | 1939.4→1957.4 | 29.33 / 234.67 | 925.0→1213.0 |
| zone14 | 5.33→21.33 | 21.83 | 532.5→540.0 | 21.33 / 170.67 | 734.3→931.3 |
| zone18 | 6.00→10.00 | 10.35 | 4152.2→4179.2 | 10.00 / 80.00 | 1091.9→1264.2 |
| zone19 | 10.00→36.00 | 36.93 | 2975.8→2976.0 | 36.00 / 288.00 | 1065.1→1486.3 |
| zone3obr | 15.34→61.34 | 62.82 | 1473.8→1481.3 | 61.33 / 490.67 | 981.0→1590.3 |
| zone4 | 10.67→42.67 | 43.99 | 1850.9→1870.2 | 42.67 / 341.33 | 962.4→1329.1 |
| zone6 | 12.67→50.67 | 51.74 | 1031.4→1044.0 | 50.67 / 405.34 | 892.0→1356.6 |
| zone7 | 10.00→40.00 | 40.65 | 2609.2→2629.0 | 40.00 / 320.00 | 1030.1→1419.6 |
| zone9 | 16.67→66.67 | 67.51 | 1693.3→1697.7 | 66.67 / 533.34 | 1012.5→1699.9 |
| zonemainmenunew | 4.00→16.00 | 16.47 | 240.9→222.9 | 16.00 / 128.00 | 691.1→854.3 |

Дополнительная проверка реальных exact stock witnesses: 6308 пар на14 картах; MSE p50/p95/p99/max {'50': 0.3892822265625, '95': 1.7879231770833333, '99': 2.741165568033855, '100': 3.5986531575520835}, SSIM p01 0.9945828196056778, alpha mismatch0. [Все ошибки](../../build-tests-138-hdrepublish/real-exact-calibration138.json). Обычная DXT-перекомпрессия здесь существенно ближе, чем mip/box различия controlled corpus; порог64 учитывает именно второй случай, не объявляет такие отличия обычной DXT ошибкой. Historical downscale объяснение — гипотеза, fuzzy source/score/margin доступны для ревью.

Исходный общий HDPack 1270745195 bytes (1211.88MiB), остался неизменным. Суммарно14 отдельных кандидатов 498331599 bytes (475.25MiB); при добавлении members без удаления старых общий HDPack вырастет примерно до1687.12MiB. В таблице нет отдельного старого atlas RES: исходные карты читали общий пакет, поэтому до показана их MMP доля. Full-file mapping общего RES может дополнительно расходовать VA; реальная стратегия game reader не проверена, её нельзя считать покрытой одним per-map BC1 бюджетом.

Максимум Zone9:1600 slots/25 pages, около66.667MiB BC1 или533.333MiB RGBA+mips. Slot/page counts не выросли. Это меньше8192×256 из [синтетического бюджета нового сжатого рендера](ATAMAN_TERRAIN_MEMORY_SYNTHETIC_20261010.md), где compressed/mapped profile дал24/24 PASS и в тяжёлом случае largest1130.1/free1345.6MiB. Этот результат исторический, не новый game PASS138 и не доказательство GPU residency. Для старого RGBA/assembly пути рекомендация1024×256 остаётся: карты с большим бюджетом нельзя объявить безопасными по compressed результату. Перед будущим применением проверить фактический новый renderer/BC1 upload+CPU release и карты в игре.

## План применения к живому Dejavu — только описание

1. Принять PNG/неоднозначный SD остаток и проверить нужный compressed renderer в игре на отдельных копиях. Получить отдельное поручение на live apply; подтвердить программу138 (эта задача установку не обновляет).
2. При закрытых игре/редакторе собрать manifest актуальных SHA всех14 Dejavu MPR, live HDPack, кандидатов и программы. Несовпадение исходника с проверенным baseline требует новой подготовки на копии. Base Maps/Res, MOB и другие mods вне записи.
3. В отдельной backup папке сохранить hash-verified оригиналы14 MPR и целого `Dejavu/res/HDPack_Terrain.res`, manifest и receipts. Не удалять старые atlas members.
4. Штатно добавить в HDPack новые namespace MMP pages, canonical profile, semantic member при наличии и recovery evidence. Старые stock и опубликованные страницы сохраняются. Затем заменить соответствующие MPR: internal MP/SEC names, два MP размера и semantic binding. Отдельные atlas RES в build-tests — промежуточные; не копировать их в игру поверх общего HDPack и не менять resource priority молча. Общий HDPack вырастет примерно на сумму новых members; карты используют лишь собственный namespace.
5. Перед каждой записью SHA guard; staged parse/reopen, backup, atomic replace/readback обязательны. RES публикуется раньше новых MPR. Между файлами общей атомарности нет: при MPR отказе откатывать RES только если SHA принадлежит этой публикации.
6. Проверить actual consumer, palette/profile/semantic field, все SEC/classes/animations/water, PNG и gameplay. Откат набора: сначала MPR из проверенных original backups, затем HDPack; чужие последующие SHA не перезаписывать, сохранить обе версии для разбора.

Этот план не выполнялся. Живые84 защищённых входа и14 входных copied MPR неизменны; installed137. Live HDPack SHA `b3b123dde10513cf6dcc40d60ff4a9e21177017a51989b991c2445eb3f0868cf`. Все38 Base MPR/texture RES проверены без изменения. Сеть, зависимости, новые чаты/агенты, интерактивный редактор/игра и push не использовались.

AGENTS актуальность SHA `F4B7FDD5EA82FCDF43D002D23D64EC36BE853650F054EDF70376527CD6432EC3`; договоры и назначенные MVP/4 отчёта прочитаны. Private engine skills отсутствуют, приватный engine не исследовался. Автоматические gates и подготовка копий завершены; ручная художественная/игровая приёмка остаётся владельцу.


## Передача 138 — 2026-10-11

По отдельному поручению владельца через Claude «применяй» установка137 обновлена
до138 и план выше применён к живым данным. Предшествующие записи «installed137» /
«план не выполнялся» относятся к этапу подготовки2026-10-10.
Программа получена свежей Release сборкой из `git archive`
`57b998d3ee1f7b207cc866ca68879687eca97e49`;
EXE SHA256 `eafaa4899cf5a8987c7dda12dd60b43a28b4168a7f47770df4b85832903d8f31`.

Полный согласованный регресс PASS, required FAIL0/SKIP0, world48/0/0,
UI/CLI sentinel nativeAttempts=0. Замер миниатюр против137:8/8 допусков,
максимум+2.81%/+1.228ms. Staging127 файлов, finalizer/manifest PASS;
постоянная установка `artifacts/EI-Mapper-Ataman-2-first-test` проверена127 SHA.
23357 пользовательских файлов/кэшей сохранены, rollback137 и прежние rollback не удалены.

Свежие14 candidates получены установленной138 из текущих live входов;
после внешнего прерывания восьми готовых карт сверены входы и fingerprint результатов,
завершены только оставшиеся6. Новые MPR/MMP/profile/semantic payloads совпали с
проверенными копиями этого отчёта. Backup15 файлов и rollback preflight проверены;
atomic/readback выполнены сначала для HDPack, затем для14 MPR.
Все прежние members общего HDPack сохранены. Base и309 прочих защищённых файлов
сохранили SHA; RES-компаньоны не заменялись.

41/41 Dejavu карта открыта установленной138 без сохранения, затем41/41 проверена
production-object MainWindow/CView audit. Палитра/profile активны14/14,
13 существовавших semantic fields exact/активны; у карты меню поле отсутствовало и не добавлялось.
SEC/classes/animations/water сохранены; установленный renderer загрузил256 px
атлас каждой из14 карт, смешанные128 страницы Zone18/Zone19 сохранены.
SD carrier256 остаётся детализацией128.

Хеши до/после, размеры, таблица карт, backup, готовая команда отката MPR → HDPack,
проверки и ограничения: [итог применения138](ATAMAN_APPLY_138_HD_REPUBLISH_20261011.md).
Откат подготовлен и проверен без выполнения. Сеть/push/новые агенты/интерактивная игра
и редактор/системный буфер не использовались. Художественная и игровая приёмка,
GPU residency/memory budget и старый RGBA путь остаются владельцу.
