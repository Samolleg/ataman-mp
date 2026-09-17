# EI Mapper Ataman — roadmap

Current Ataman version: `15` (реализация; установленная версия пока 14).

## Текущая задача: EI-NEIGHBOR-012 — соседние карты как полная статическая сцена

- Запрос: освещение и статические здания/деревья/камни соседей, подготовка
  в фоновых потоках CPU и отрисовка на GPU без блокировки редактирования.
- Причина: текущий фон загружает только MPR синхронно, отключает тени и
  приглушает цвет; MOB соседей не загружается. Чёрная область на снимке —
  отдельный неподтверждённый симптом, не доказательство ошибки освещения.
- Разрешено: код, тесты, версия 12 и обновление постоянной установки.
  Ветка codex/neighbor-scenes-v12 от версии 11. Игровые данные не менять.
- Критерии: статические объекты из соседнего MOB с правильными transform/высотой,
  общий свет и тени; объекты не попадают в выбор/сохранение/экспорт активной карты;
  фоновая подготовка, отказ от устаревших результатов, безопасный GPU lifecycle.
- Статус: реализованы CPU-снимки MPR/MOB в двух потоках, статические объекты
  в общей render queue, свет/тени с мировым смещением; исходники версии 12.
  Документы 5 PASS, шейдер 3 PASS, сценарии 47 PASS; сборка прошла.
  Доказательства: docs/validation/ATAMAN_NEIGHBOR_SCENES_12.md.
- Установлена версия 12 из `4b41818983ebdc919ecd8d711cf7d59d20fa8f53`:
  100 программных файлов проверены, 1486 пользовательских сохранены по хешам.
  Общий UI-check снова timeout 120 секунд, не PASS.
- Следующий шаг: ручная проверка исходной zone9, переключения карт и времени
  суток. Родитель не закрыт:
  визуальный результат и ускорение не измерены. Отдельный GPU-контекст
  не реализован: ресурсы создаются порциями в существующем render context.
- История основной задачи: EI-FLATTEN-007/008 и последующие ручные приёмки
  сохранены ниже; текущий запрос владельца переводит фокус на сцену соседей.

### EI-PERF-013 — оптимизация соседних статических сцен

- Решение владельца: реализовать пункты 2, 3, 4 и 5 вместе. EI-PERF-013,
  версия 13 в codex/neighbor-performance-v13 от
  47303bbc43f59cbd3fbe99c67a72ba497239a7e6. Разрешены код, тесты,
  сборка и обновление прежней установки; игровые данные не менять.
- Критерии: пространственное отсечение групп; кэш transforms/команд с
  инвалидированием при загрузке, перемещении и скульптинге; CPU-подготовка
  моделей/текстур без GL в workers; дистанционная детализация и бюджет теней.
  Сохранение, выбор и ближняя активная карта не должны меняться.
- Статус: все четыре механизма реализованы. Группы 32×32, кэш готовых
  команд и bounds, CPU-подготовка обычных моделей/текстур, дальняя геометрия
  с гистерезисом и сокращённой дальностью теней. Особые humanoid/inventory
  visual paths сохранены ради совместимости; для них подготовка остаётся прежней.
- Проверки: алгоритмы 6 PASS, render queue 14 PASS; скрытая интеграция на
  zone9/zone1/zone4 проверяет CPU worker без GL, GPU upload, 24 варианта
  геометрии каждой карты, кэш, смену поколений и неизменность MPR/MOB.
  Доказательства: docs/validation/ATAMAN_NEIGHBOR_PERFORMANCE_13.md.
- Финальный регресс: 17/17 на каждой из трёх карт, сценарии 47/47; общий
  UI-check — прежний timeout 120 секунд. Пакет версии 13 проверен: 100 хешей,
  источник 68f847e232b7d6165347d435fa5c060d3450af29.
- После сообщения владельца «закрыл» выполнен install13.ps1: установлена
  версия 13, проверены 100 программных файлов и неизменность 2317
  пользовательских файлов; заменены 6 программных файлов, удалений нет.
  EXE совпадает с проверенной сборкой; сохранён rollback версии 12,
  старый rollback версии 11 проверен по хешам и удалён.
  Следующий шаг: ручная проверка исходной сцены и сравнение производительности.
  Исходная пользовательская сцена ещё требует ручной приёмки; FPS не измерен.

- История до разрешения реализации: вопрос владельца, как увеличить производительность; анализ
  и рекомендации, без новой реализации или сборки.
- Подтверждено в коде версии 12: два CPU-потока разбирают MPR/MOB;
  создание геометрии/текстур остаётся в render context; соседние объекты
  обходятся каждый кадр. Instancing в render_queue уже существует.
- Гипотезы для измерения: обход/подготовка объектов на CPU, загрузка ресурсов,
  стоимость теней и дальних объектов на GPU. Узкое место пока не измерено.
- Следующий шаг: сравнить одну сцену без соседей, с рельефом соседей,
  с объектами и с тенями; отдельно холодную загрузку и установившийся кадр.
  По результатам выбирать пространственные группы, кэш статической геометрии,
  фоновые декодеры ресурсов, дистанционные уровни детализации и бюджет теней.
  Цель сохраняется: полноценные соседние сцены с отзывчивым редактированием.

### EI-WORLD-ADD-015 — функция «Добавить карту в мир»

- Решение владельца: реализовать предложенный сценарий, полностью убрать
  зависимость состава/координат мира от map.txt; единственный источник —
  world-map-layouts.json. Игровую регистрацию и переходы не создавать.
- Версия 15, ветка codex/world-json-v15 от edbc82ecf0b0ec4d9fc07e418e4740d8995c2816.
  Разрешены код, проверка и обновление постоянной программы. Реальные карты
  и JSON профиля в ходе разработки не редактировать; операции проверять на fixtures.
- Критерии: выбор MPR/необязательного MOB, размещение ЛКМ/отмена Esc,
  повторное открытие мира, перемещение, список всех карт и удаление членства,
  сохранение пользовательских файлов и старых координат; мир работает без map.txt.
- Статус: JSON v2 и независимый контекст мира реализованы, доступны размещение,
  список всех карт, восстановление ссылок, координаты и удаление членства.
  Сборка прошла; документ 17 PASS, соседи 7 PASS, скрытая интеграция 23 PASS,
  сценарии 47 PASS. Общий UI-check: прежний timeout 120 секунд, не PASS.
  Доказательства: docs/validation/ATAMAN_WORLD_JSON_15.md.
  Следующий шаг: упаковка/обновление прежней программы; ручная приёмка полного
  пользовательского сценария. Родительская визуальная/FPS-приёмка не закрыта.
- История проектирования (уточнение объёма ниже уже разрешено владельцем):

- Новое поручение: добавить функцию и продумать правильную реализацию.
  Проектирование: docs/WORLD_MAP_ADD_DESIGN.md. Ожидаемый результат — выбор
  карты, размещение, сохранение состава мира, повторное открытие и удаление
  из раскладки без удаления исходных файлов. Основная сцена соседей сохраняется.
- Открыт вопрос владельцу: только мир редактора или также игровая регистрация
  с переходами. Это определяет модель данных и необходимость записи texts.res.
  До ответа выполнено исследование; код и игровые ресурсы не менялись.
- Найдены обязательные части: отделить world identity от m_mapTestEntry,
  расширить схему JSON без потери старых координат, учесть MPR без MOB,
  дубликаты путей и приоритет добавленной карты при лимите пяти соседей.
- Следующий шаг: после выбора объёма реализовать модель/миграцию, UI размещения,
  интеграцию загрузки и проверки из документа; версия 15 ещё не назначена.
- История предыдущей консультации:

- Разрешена консультация, реализация и изменение игровых ресурсов не запрошены.
  Подтверждено по map_catalog.cpp и neighbor_map_layout.cpp: каталог берётся
  из map.txt в texts.res, соседи — до пяти ближайших карт того же аллода
  и связного компонента. Отдельного импорта произвольного MPR в мир нет.
- «Расставить карты» перемещает уже доступных соседей и сохраняет координаты
  профиля. Для новой зоны нужны запись каталога, ресурсы и связь с нужным миром.
- Следующий шаг при продолжении: уточнить, это существующая зона или новый
  MPR/MOB; основной результат соседних сцен и ручные приёмки не закрыты.

### EI-PALETTE-CATALOG-014 — доступность тайлов в палитре

- Новое решение владельца: добавить галочку для переключения между всеми
  материалами и используемыми на карте. Разрешены реализация и передача
  обновления в постоянную папку; игровые данные не менять.
- Версия 14, ветка codex/palette-filter-v14 от переданной версии 13,
  база 37c1c550a97cd4b9482fad6a38bf54d77f0d5342. Критерии: по умолчанию весь
  каталог единым списком семейств; галочка оставляет только используемые,
  работает для земли/покрытий и сохраняет настройку. Ручной атлас отдельный.
- Статус: реализовано, свежая сборка 14 готова. Виджет 10 PASS,
  сценарии 47 PASS; общий UI-check timeout 120 секунд, не PASS.
  Доказательства: docs/validation/ATAMAN_PALETTE_FILTER_14.md.
  Источник: 0ad6d7f9708b658358d8d4efe0c18d2b790660e4.
  Установлена 14 в прежнюю папку: 100 программных файлов проверены,
  2393 пользовательских файла сохранены по хешам, заменены 6 программных.
  Rollback версии 13 сохранён, старый 12 проверен и удалён.
  Следующий шаг: ручная приёмка переключателя в пользовательской сцене.
- История диагностики до разрешения реализации:
- Запрос: объяснить, почему на снимке доступны не все тайлы; разрешена
  диагностика, новая реализация не запрошена. EI-PERF-013 сохраняется;
  после последующего закрытия редактора постоянная установка обновлена до 13.
- Подтверждено: source/render/view.cpp формирует Modern из общего каталога
  материалов (на снимке 81 земля / 7 покрытий), а не из всех слотов атласа.
  source/ui/landscape_tools.cpp:rebuildMaterialPalette выводит сначала
  используемые материалы, затем неиспользуемые ниже в той же прокрутке.
  Ручной режим показывает физические слоты с валидным runtime type и identity;
  пустые, нетипизированные и padding-слоты исключены.
- Результат диагностики: 1559 активных слотов не означают 1559 материалов;
  сюда входят варианты и переходы. Снимок показывает верх первого раздела,
  но не доказывает отсутствие конкретного материала во всём каталоге.
- Следующий шаг при подтверждении пропуска: получить название/пример
  недостающего тайла и его исходную карту, проверить присутствие в каталоге.
  Код, игровые данные и версия не менялись.

### EI-MOB-SAVE-013 — объяснение отказа сохранения MOB

- Запрос 2026-09-17: объяснить ошибку на снимке; разрешена диагностика,
  исправление и сборка этим вопросом не запрошены. Основная EI-NEIGHBOR-012
  сохраняется; это отдельный связанный дефект сохранения.
- Подтверждено по source/mob/mob.cpp:1995 и :2315: для объекта mapId 181
  не найден подходящий исходный raw-поддереву источник либо переносимый
  шаблон; кандидат MOB отклонён до создания бэкапа и записи назначения.
  source/documents/endurance_document.cpp:2197: структура записей изменилась,
  поэтому простой перенос изменений отдельных полей неприменим.
- Статус: смысл диагностики установлен; причина потери привязки и действия
  перед ошибкой неизвестны. Снимок не доказывает повреждение исходной карты.
- Критерий ответа: объяснить отказ и ID, отделить факты от гипотез.
  Следующий шаг для исправления: установить карту и последовательность действий
  с объектом 181, воспроизвести потерю raw-привязки. Код и данные не менялись.

## Ожидает приёмки: EI-FLATTEN-007 — новый режим «По границе»

- EI-FLATTEN-008, исправление непринятого результата (2026-09-17): на новой
  записи режим «По границе», сила 1, постоянный спад, радиус 10, общая кисть
  zone1/zone2/zone3obr не устраняет внутренний обрыв. Причина в версии 7:
  несовпадающие края карт ошибочно объявлены неподвижным контуром внутри круга.
- Уточнение владельца: поднимать/опускать всю внутренность, неподвижны вершины
  окружности; сходство со «Сгладить» не является выполнением задачи.
- Версия 7 не принята. Исправление версии 8 ведётся от
  `0f446ea67e29e63c8abe4ef777b826aa4a137e01` в `codex/boundary-fill-seams-v8`.
  Разрешение на исправление/проверки/обновление той же программы сохранено.
- Статус EI-FLATTEN-008: исправлено, собрано и установлено как версия 8.
  Регресс на старом коде FAIL (пустая команда), на исправленном PASS;
  terrain/world 19 PASS, 1 SKIP. Контур, движение обеих сторон, отличие от
  Smooth, отсутствие дрейфа, уклон, порядок карт и undo проверены.
  Пакет 100 файлов проверен, 375 пользовательских файлов сохранены по хешам.
  Источник `1397d5365453c1fa5ce693eca65330176596cede`.
- Доказательства: [исправление стыка](docs/validation/ATAMAN_BOUNDARY_FILL_8.md).
- Общий UI-check снова timeout 120 секунд (EI-BUILD-001-UI), не PASS.
- Следующий шаг: ручная приёмка исходного обрыва; родительская задача открыта.
  Полная окружность должна быть покрыта разрешёнными картами. При неполном
  покрытии остаётся ограниченный расчёт; отсутствующие данные не выдумываются.
- История справки до версии 11: положения карт хранит
  `%LOCALAPPDATA%/EI Mapper Endurance/world-map-layouts.json`, поля origin.x/y.
  Путь подтверждён defaultPath() в world_map_layout_document.cpp; файл есть.
  Это отдельное хранилище вне artifacts; данные в рамках вопроса не менялись.

### EI-LAYOUT-011 — мировые координаты рядом с аддоном профиля

- Запрос: хранить world-map-layouts.json на уровень выше Maps с MOB, рядом
  с аддоном; для JMV — MODS/JamaisVu135/world-map-layouts.json.
- Разрешены изменение редактора, перенос координат, тесты и обновление одной
  программы. Сами MOB/MPR/RES не менять. База — переданная версия 10;
  ветка codex/profile-world-layout-v11. Родительские приёмки остаются открыты.
- Подтверждено: файл в указанном корне уже есть, хеш совпадает со старым
  AppData-файлом. Оба содержат записи jamais-vu и deja-vu; существующий файл
  имеет приоритет и не заменяется копией при запуске.
- Критерии: JMV/DejaVu используют modRoot, Base — gameRoot; смена профиля
  переключает хранилище. При отсутствии нового файла импортируется только
  выбранный профиль из старого. Повреждённые файлы не перезаписываются.
- Реализация: привязка к modRoot/gameRoot, сброс состояния при смене пути,
  импорт выбранного профиля только при отсутствии целевого файла. Тесты:
  15 PASS, 0 FAIL. Существующий JMV JSON не менялся; оригинал в AppData сохранён.
- Доказательства: [хранилище версии 11](docs/validation/ATAMAN_PROFILE_LAYOUT_11.md).
- Версия 11 собрана и установлена: источник
  `80727ba4e2439fc75002709f4eaba8bb7bdaaf9d`. Проверены 100 файлов пакета,
  1234 пользовательских файла сохранены по хешам. Создание объектов/ресурсы
  PASS, сценарии 47/47 PASS. Оба реальных JSON остались неизменными.
- Общая проверка UI: прежний таймаут 120 секунд, не PASS; ручная приёмка открыта.
- Следующий шаг: ручная проверка расстановки и записи JSON в папке профиля.

### Связанный дефект EI-PALETTE-010 — палитра при включении рисования

- Запись 20260917-0953-39: после перехода из рельефа в рисование материалы
  растянуты по одному на строку; переключение «Классический → Современный»
  восстанавливает сетку. Ожидается правильная сетка сразу, без переключения.
- Объём: исправление и регресс инициализации/изменения ширины палитры,
  версия 10, сборка и обновление постоянной программы; игровые данные не менять.
  База `737b74d8094ae4202612ce817772b1ae3db113af`, ветка
  `codex/paint-palette-layout-v10`. Родительские приёмки остаются открыты.
- Причина подтверждена: расчёт колонок по скрытому viewport, затем отсутствие
  обновления при показе без изменения размера всей панели. Регресс на старом
  коде: 4 FAIL. Исправление: события показа/размера viewport вызывают отложенный
  пересчёт по видимой области. Focused widgets: 21 PASS, 0 FAIL.
- Критерии: правильная сетка Modern/Classic, земля/покрытия при первом показе,
  сохранение выбора и фильтра, корректное изменение ширины.
- Доказательства: [палитра версии 10](docs/validation/ATAMAN_PALETTE_10.md).
- Сборка Release/runtime и обновление выполнены. Источник:
  `0ec4420f128b3a7fb511610d4f3e5f4b4e4e9e3a`. Проверены 100 файлов пакета,
  1051 пользовательский файл сохранён по хешам; заменены 6 программных файлов.
- Версия/создание объектов/ресурсы PASS, сценарии 47/47 PASS. Общий
  ui-widget-check: прежний таймаут 120 секунд, не PASS.
- Следующий шаг: ручная приёмка первого включения рисования на исходной карте.
- Уточнение сохранности установки: проверены правила и install10.ps1 —
  постоянную папку в artifacts не очищать; обновлять только файлы манифеста,
  сохранять пользовательские файлы. Git её игнорирует и не хранит резервную
  копию; имя artifacts само по себе не запускает удаление. Перенос не запрошен.

### Связанный дефект EI-SAVE-009 — обычное сохранение после рисования

- Статус реализации: версия 9 собрана и установлена в прежнюю папку.
  Обычный Save сам выбирает имя и связанный RES, делает бэкап и сохраняет;
  готовые тайлы не запускают переупаковку изображений. Проверки: документы
  14 PASS, RES 17 PASS, route-cost 3 PASS, сценарии 47/47 PASS.
  Все 100 файлов пакета проверены, 866 пользовательских файлов сохранены.
  Общий ui-widget-check: таймаут 120 секунд, не PASS; ручная приёмка открыта.
  Источник `607417b0853567577df10eeabb8f0358acfe1dfb`.
- Доказательства: [обычное сохранение](docs/validation/ATAMAN_SIMPLE_SAVE_9.md).
- Следующий шаг: ручная приёмка «нарисовал → Сохранить MPR → переоткрыл»
  для готовых и новых текстур. Игровые ресурсы агентом не изменялись.

- Разрешение реализации: «сделай исправление» (2026-09-17). Текущий шаг —
  обычное сохранение без служебных диалогов, с сохранением бэкапа/проверок.
  Версия 9, ветка codex/simple-mpr-save-v9 от
  `597347c151924f62c6e09bb6578da46c67b6ea21`. Исправление/сборка/обновление
  постоянной установки разрешены; игровые ресурсы в ходе тестов не изменять.
- Критерии: готовые тайлы без переупаковки изображений; новые текстуры в
  известный RES с отдельными именами без замены чужих страниц; отмена/
  ошибки сохраняют данные; сохранение MPR и нужного RES одной командой.
  Неизвестный архив требует выбора, а не догадки. Прежняя задача кисти открыта.

- Запрос 2026-09-17: объяснить диалоги на записи 12,3 секунды и почему нельзя
  просто нарисовать и сохранить MPR. Ожидаемый пользовательский процесс —
  обычное сохранение без ручного выбора технического идентификатора атласа.
- Диагностика: видео показывает «Выберите идентификатор ресурса рельефа»,
  затем выбор textures.res. saveLand маршрутизирует modern painting в
  finalizeVirtualTerrainAtlas; эта функция при пустом selectedIdentity всегда
  открывает диалог имени, затем безусловно QFileDialog для архива textures.res.
  workflow.log подтверждает publish для текущей zone3obr.mpr, persistedDraft:false,
  identityOnly:false. Это путь публикации атласа, не ошибка кисти/геометрии.
- Уточнение по источнику текстур: workflow.log:173 и далее подтверждает загрузку
  zone3obr000.mmp и следующих страниц из MODS/JamaisVu135/res/HDPack_Terrain.res.
  MMP — изображения; RES — архив. Предыдущее объяснение через textures.res
  было слишком узким: это имя диалога экспорта, не фактический исходный архив
  этой карты. При реализации сначала установить корректную привязку источника.
- История диагностики до разрешения реализации: объяснение причины без правок.
  Сохранение новых текстур требует ресурса помимо MPR, но ручной выбор имени
  и известного архива не является требованием формата. Следующий шаг реализации:
  автоматизировать обычное сохранение с корректной привязкой архива и бэкапом,
  отдельно проверить случай новых текстур и правок только высоты/готовых тайлов.
- Пункт не закрывает приёмку EI-FLATTEN-007/008. Вопрос не разрешает переписать
  установленные игровые ресурсы в ходе диагностики.

### История реализации версии 7

- Запрос 2026-09-17: добавить отдельный режим по контуру окружности вместо
  произвольной постоянной высоты; прежнее «Выровнять» сохранить.
- Разрешено: реализация, тесты, версия 7, локальный коммит и обновление одной
  постоянной программы. Игровые карты/данные не изменять; main не интегрировать.
- База: `403b8025f5cbb840b157c7438dc8212bc2081df1`, ветка
  `codex/boundary-fill-v7`, продолжение переданной версии 6.
- Критерии: внешний ряд вершин внутри круга неизменен; внутренность плавно
  восстанавливается по контуру, сохраняет уклон, не зависит от поля 747;
  сила/спад/undo работают; локальная, общая кисть и оверлеи проверены.
- История версии 7 (не принята): реализовано, собрано и установлено. Использовалось
  дискретное гармоническое заполнение;
  неполные и несовпадающие края карт удерживаются как часть контура.
- Проверки: terrain/world 16 PASS, 1 SKIP; focused widgets 5 PASS;
  Release/runtime, версия, создание объектов, ресурсы и сценарии 47/47 PASS.
  Все 100 файлов пакета проверены, 331 пользовательский файл сохранён;
  заменено 6 программных файлов, удалений нет. Источник:
  `5a4f06dc8618644d80ccb24d432e135432634c59`.
- Доказательства: [новый режим](docs/validation/ATAMAN_BOUNDARY_FILL_7.md).
- Следующий шаг: пользовательская приёмка на исходной карте — «По границе»,
  сила 1, постоянный спад, кисть охватывает яму и неповреждённый край; undo.
  Полная ручная приёмка не заявлена. Общий UI-timeout отслеживается отдельно.
- Предыдущая EI-UI-004 остаётся ожидающей пользовательской визуальной приёмки.

## Ожидает приёмки: EI-UI-004 — графитовая шапка, камера и деревья

- История диагностики EI-FLATTEN-007 (2026-09-17, до разрешения реализации): на видео
  10,7 секунды «Выровнять» использует цель 747, силу 1, постоянный спад;
  владелец ожидает поверхность по высотам края круга. Во всех просмотренных
  кадрах цель уже 747, момент её задания не показан. Код landscape_tools.cpp
  читает ui/landscape/flattenHeight, сохраняет изменения; view.cpp выбирает
  цель правым кликом либо передаёт поле в makeFlatten. terrain_edit_model.cpp
  строит одну постоянную высоту, не интерполяцию по границе. Общая кисть
  view_world_terrain.cpp также передаёт фиксированную высоту с пересчётом maxZ.
  Следующий шаг при разрешении реализации: определить и проверить заполнение
  по сохранённому контуру кисти. Код/установка/карты в диагностике не менялись;
  вопрос не отменяет ожидающую ручной приёмки задачу интерфейса ниже.

- Запрос 2026-09-17: графитовая системная шапка; не переносить камеру к входу
  при загрузке; убрать отступы вложенных строк, например Карты/Объекты.
- База: `e3391f936aad032488474ca17b8f8b10eb603e3e`, ветка
  `codex/graphite-camera-trees`. Работа только в Ataman, без изменения main.
- Разрешено: эти три изменения, проверки, локальные коммиты и обновление
  постоянной программы по принятому порядку. Игровые данные не изменять.
- Критерии: графитовый нативный заголовок; отсутствие автоматического вызова
  перехода к входу при открытии; ручной переход/соседние карты сохранены;
  строки дерева без сдвига с работающим раскрытием, выбором и фильтрацией.
- Причина камеры: отложенный callback openMapPairWithCameraPolicy вызывает
  moveCameraToActiveSpawn; обычный загрузчик ландшафта этого не требует.
- Статус: реализовано, собрано и установлено как версия 6. Три исправления
  сохранены как версии 4/5/6. Focused UI 4/4, создание объектов/ресурсы/сценарии
  PASS; 100 файлов пакета проверены, 286 пользовательских файлов сохранены
  с прежними хешами. Заменено 6 программных файлов, удалений нет.
- Доказательства: [проверки и установка](docs/validation/ATAMAN_WORKSPACE_UI_6.md).
- Следующий шаг: пользовательская приёмка шапки, камеры на исходной карте
  и деревьев. До неё полная визуальная приёмка не заявлена. Общий offscreen
  UI-тест снова timeout 120 секунд; отдельный открытый EI-BUILD-001-UI, не PASS.

## История: EI-SINGLE-INSTALL-003 — одна обновляемая программа

- Решение владельца (2026-09-17): собирать и заменять одну программу, не выдавать
  каждый раз новую папку. Это заменяет прежнее правило неизменяемых сборок
  только для постоянной установки Ataman.
- Постоянная папка: `artifacts/EI-Mapper-Ataman-2-first-test`; существующий путь
  сохраняется. VERSION определяет версию, имя папки остаётся постоянным.
- Разрешено: закрепить новый порядок в правилах. Следующие запросы сборки
  включают замену программных файлов здесь без повторного разрешения, сохраняя
  пользовательские настройки/данные. Ни main, ни Endurance не затрагиваются.
- База: `36ebd3e681f71f793b57f39d84f3f27b15451165`, ветка
  `codex/single-installation-v3`; версия правил/исходников 3, текущая установка 2.
- Критерии: единый путь, отсутствие требования новой выдаваемой папки, безопасная
  замена закрытой программы с проверкой, технический откат и сохранность данных.
- Статус: правила обновлены в AGENTS, CONTRIBUTING, BUILDING_ENDURANCE и
  TEST_BUILD_HANDOFF (раздел 3). Проверены согласованность версии 3 и diff;
  установленная версия 2 и код/скрипты сохранены. Задача изменения правил завершена.
- Следующий шаг: применять обновление на месте при следующем запросе сборки.
  Сейчас бинарники не пересобирались и установленная папка не изменялась.
  Ранее найденный UI-timeout EI-BUILD-001-UI остаётся открытым.

## История: EI-BUILD-001 — первая тестовая сборка Ataman

- Запрос: собрать первую сборку (2026-09-17). Ожидаемый результат: отдельная
  запускаемая папка версии 2 с проверенным составом, номером и хешами.
- База: `4ace67e70ac14608e0b6f17c00c00b73c7d356ff`, ветка `codex/rules-fix-v2`.
  Это сборка текущего результата, без повышения версии и интеграции main.
- Разрешено: Release-сборка, headless-проверки, новая staging-папка и передача.
  Старые сборки, Endurance и игровые данные не изменять; GUI и ZIP не нужны.
- Критерии: свежие exe/bridge/host, версия 2, протокол результатов проверок,
  финализация, полный SHA256-манифест и совпадение exe до/после упаковки.
- Статус: первая тестовая папка подготовлена и проверена:
  `artifacts/EI-Mapper-Ataman-2-first-test`. Release/версия/создание объектов/
  навигация ресурсов PASS; сценарии 47/47 вне sandbox. Финализация PASS,
  100 файлов манифеста проверены независимо, exe/runtime совпадают с билдом.
- EI-BUILD-001-UI: offscreen UI-проверка timeout 120 секунд, причина неизвестна;
  это открытый пробел приёмки, не PASS. Исходники не исправлялись ради сборки.
- Доказательства: [первая сборка](docs/validation/ATAMAN_FIRST_BUILD.md).
- Следующий шаг: ручная проверка пользователем; тестовый пакет передаётся
  с ограничениями. Готовность стабильного релиза и полная UI-приёмка не заявлены.

## История: EI-RULES-FIX-002 — исправление правил после аудита

- Запрос владельца: «исправь правила» (2026-09-17), продолжение аудита версии 1.
- Исходная проблема: нет явного назначения документов, руководство сборки
  предполагает старое окружение, порядок независимой проверки пакета неполон.
- Ожидаемый результат: согласованные правила Ataman, выполнимые инструкции
  проверки окружения и пакета, сохранённые ограничения и открытые дефекты кода.
- Разрешено: исправление правил/документации, версия 2, локальный коммит;
  реализация packaging-валидаторов, сборка, установка и main не входят в запрос.
- База: только что переданный результат
  `e65e72e9496dd4e0f1575d97ea895f02a1fb7290`; ветка `codex/rules-fix-v2`.
  Зависит от ветки версии 1, пока не интегрированной в main.
- Критерии: согласованные полномочия документов, отсутствие утверждений о
  непроверенном kit/пути, точный порядок контроля манифеста, версия 2 во всех
  текущих указателях; исторические доказательства и цели сохранены.
- Статус: завершено в рабочей ветке. Проверены указатели версии 2, diff и
  сохранность унаследованной карты, исходников, скриптов и контракта данных.
  Сборка/запуск не выполнялись: изменение ограничено документами и VERSION.
- Доказательства: [дополнение к аудиту](docs/validation/ATAMAN_RULES_AUDIT.md#исправление-правил-в-версии-2).
- Следующий шаг: решение владельца об интеграции в main или следующей задаче.
  Программный дефект A остаётся открытым.

## История: EI-ATAMAN-VERSION-001 — нумерация и аудит правил

- Запрос владельца (2026-09-17): считать текущее состояние первой версией,
  увеличивать версию при каждом исправлении и провести аудит правил проекта.
- Результат: VERSION = 1, последовательность 1 → 2 → 3, правило повышения,
  журнал изменений и проверяемый отчёт аудита.
- Разрешено: метаданные версии, относящиеся правила и документация, локальная
  ветка и scoped-коммит. Аудит не разрешает ремонт найденных дефектов кода,
  сборку/замену пользовательских пакетов, интеграцию в main или публикацию.
- База: main `ce9dc97473b9efc64c8052602116940dcada3586`; ветка
  `codex/ataman-version-1-rules-audit`.
- Критерии: единый номер в VERSION/Readme/карте; прослежена передача номера
  приложению; правила повышения и сохранности пакетов согласованы; найденные
  пробелы имеют доказательство, влияние и следующий шаг.
- Статус: завершено в рабочей ветке; версия 1, правила повышения и отчёт аудита
  подготовлены. Проверены согласованность указателей, ссылки, цепочка передачи
  версии и git diff --check. Изменений C++/скриптов/игровых данных нет;
  бинарная сборка и игровые/UI-проверки не выполнялись.
- Следующий шаг: отдельное решение владельца об интеграции в main и выборе
  следующего исправления (оно получает версию 2). Замечания A/B остаются открыты.

## Связанные отложенные пункты аудита

- EI-RULES-AUDIT-001-A: упаковочные скрипты проверяют changelist только для
  alpha.N, а валидатор не сравнивает файлы с SHA256SUMS.txt. Риск неполной
  проверки пакета сохраняется в версии 1. Следующий шаг: отдельное исправление
  скриптов с негативными проверками; до него обязательны ручные проверки договора.
- EI-RULES-AUDIT-001-B: исправлено в версии 2 — предположения о наличии kit и
  Unicode-пути заменены проверкой фактического окружения. Наличие инструментов
  по-прежнему требуется проверить при запросе сборки, не изменяя Endurance.
- Доказательства и остальные замечания: [аудит правил](docs/validation/ATAMAN_RULES_AUDIT.md).

## Предыдущая задача: EI-LOCAL-FORK-001

- Решение владельца (2026-09-17): отдельный локальный проект ei-mapper-ataman,
  без GitHub, от 19168b91a10562525fb6c83b665275c2fd43cf4c.
- Результат: независимая Git-копия с историей основы, без remotes и зависимости
  от Git object store исходного проекта; согласованные локальные правила.
- Разрешено: создать отдельную папку, настроить локальную основу и закрепить
  правила коммитом. Не разрешены публикация, удаление исходного проекта/сборок,
  изменение игровых данных, перенос новых изменений UI и правки кода редактора.
- Статус: локальный форк настроен; эти правила сохраняются первым локальным
  коммитом поверх основы в main. Будущая реализация — в codex/<task>.
- Проверено: HEAD перед коммитом совпадает с основой; нет remotes,
  remote-tracking refs и object alternates; git fsck --full --no-dangling
  успешен; diff затрагивает только семь документов; LICENSE, NOTICE и код
  совпадают с основой. Старый репозиторий, его связи и сборки сохранены.
  Сборка/тесты редактора не повторялись: в этой задаче код не менялся.
- Известные пробелы основы: при прежней пересборке полные анимационные/UI
  тесты завершались таймаутом, сценарная интеграция — ошибкой. Создание локального
  форка эти дефекты не исправляет и не подтверждает готовность релиза.
- Следующий шаг после настройки: открыть папку Ataman как отдельный проект и
  выбрать следующую задачу. Принятый локальный main — текущая база развития.
- Отложено: исправление самих packaging-валидаторов из аудита правил.
  До этого применять ручные проверки из docs/TEST_BUILD_HANDOFF.md.

## Унаследованный план и доказательства Endurance

Ниже сохранена история и незавершённые продуктовые пункты выбранной основы.
Старые указания отправить PR, ждать upstream или работать от origin/main
не действуют в Ataman. Завершённость старых пунктов — историческая запись,
не результат повторного тестирования этого форка. Новые текущие задачи вести
выше, отдельно от этой истории; не закрывать родительские цели по подзадачам.

This file tracks product goals, not development diaries. Completed goals link
to the approved commits, changelists, or validation reports that implemented
them. Investigation notes and rejected experiments belong in `docs/` and Git
history. The permanent safety and compatibility rules are in
[Project contract](docs/PROJECT_CONTRACT.md).

<details open>
<summary><strong>General wishlist — not assigned to a release</strong></summary>

## Authoring and workflow

- [x] Complete the object-property label/tooltip audit and expose previously
  hidden safe fields plus all five unit logic slots. Serialized names and values
  remain unchanged; structural object type stays read-only
  ([`55b8510`](https://github.com/Ilufus/ei-mapper-endurance/commit/55b8510)).
- [ ] Finish structural validation and remapping for Behavior attachments:
  detect orphan/wrong-parent relationships and complete cross-owner compound
  copy/remap transactions without weakening the implemented authoring UI.
- [x] Add Russian localization and an in-app language switch
  ([`b3854b0e`](https://github.com/Ilufus/ei-mapper-endurance/commit/b3854b0e)).
- [ ] Add opt-in Windows associations for `.mpr`, `.mob`, and `.collection`.
- [ ] Add a viewport action that copies the current camera transform as
  ready-to-paste game script calls.
- [ ] Add baseline/candidate swapping, 3D perspective comparison, deterministic
  JSON export, and explicit ambiguous-match handling to MOB Diff.
- [ ] Add optional external MOB diff-tool integration.
- [ ] Add optional MPR-only Test Current Map by staging a structurally valid
  empty MOB; keep profile-specific spawn requirements explicit.
- [ ] Add an opt-in MCP server whose editing operations use normal Endurance
  transactions, undo, validation, backup, and confirmation boundaries.

## Resources, performance, and diagnostics

- [ ] Move the remaining cold RES index/decode work off the GUI thread and
  persist archive indexes by path, size, timestamp, and header hash.
- [ ] Surface existing phase timings, shadowed assets, missing resources, and
  decode failures through the workflow/message-log panel.
- [ ] Cover every observed Base/JMV MMP format with either correct decoding or
  an explicit asset/archive/format diagnostic.
- [ ] Support explicitly configured ZIP archives as read-only loose texture
  sources without extracting into the game tree.
- [ ] Continue measured heavy-map loading and renderer optimization; retain the
  current five-second complete-workspace target on the reference machine.

## Validation and recovery policy

- [x] Add profile/assembly-aware, non-blocking character-mask diagnostics for
  the verified helmet-hair algorithm plus model-local FIG/BON crown evidence
  on every database-equipped character,
  dangerous wearable empty/missing masks,
  missing core anatomy, dotted alternative overlap, and duplicates while
  preserving unknown tokens and creature conventions
  ([validation](docs/validation/CHARACTER_BODYPART_VALIDATION.md)).
- [ ] Design an explicit rescue workflow for known game-tolerated malformed
  records. Opening stays non-destructive; repair is opt-in; knowingly preserving
  a defect requires explicit consent and logging.
- [ ] Extend guided NID repair to per-script-reference assignment and
  coordinated multi-MOB save cohorts.
- [ ] Stress repeated large-map open/edit/undo/redo/save/close and abnormal-exit
  recovery across scripts, model parts, multi-selection, and compound objects.
- [ ] Convert every reproducible format regression into a minimized synthetic
  fixture where licensing permits.

## Long-term integrations

- [ ] Add advanced local runtime-variant approval based on complete guarded
  layout/signature validation, never a user-entered hash alone.
- [ ] Support other mods through optional profile/runtime plugins only when
  requested by their authors.
- [ ] Restore MOB VSS as a post-baseline feature: lossless parser/viewer first,
  then proven VSS/script synchronization, visual editing, and transactional
  conflict handling.

</details>

<details>
<summary><strong>1.0.x and later — production release and compatibility</strong></summary>

## Goals

- [ ] Publish the first stable JMV-ready release with complete source, GPL and
  attribution notices, reproducible build instructions, validation report,
  checksums, and rollback guidance.
- [ ] Complete the representative Base/JMV editor and runtime matrices.
- [ ] Establish release signing/reputation guidance for the unsigned editor and
  its process-owned runtime bridge.
- [x] Confirm Windows 7 compatibility for the current Qt5 editor build without
  weakening the default modern-Windows renderer path.
- [ ] Keep Base support independent from JMV behavior and keep Test Current Map
  optional for profiles without a validated provider.
- [ ] Consider Qt6/CMake modernization only after 1.0; use a major version if
  compatibility breaks.

</details>

<details open>
<summary><strong>0.5.x — beta content and resource authoring</strong></summary>

## Release goal

Promote Endurance into beta with production terrain painting and integrated,
profile-aware database/resource authoring. Database and RES changes must retain
the same review, validation, preservation, backup, and atomic-publication
boundaries as MOB and MPR authoring.

## Completed foundation

- [x] Add a schema-aware Database Editor for all 26 physical Evil Islands
  tables, including typed and structured fields, lossless opaque-field
  preservation, clone/delete, undo/redo, review, validation, XLSX interchange,
  and native database publication.
- [x] Add a dockable Resource Browser for active-profile and external archives
  with provider provenance, search/filtering, metadata and format-aware
  previews, export, and reviewed Save As mutation transactions.
- [x] Integrate both tools: protected `database.res` opens in the Database
  Editor, while established database resource fields open the exact winning
  active-profile member in the Resource Browser.
- [x] Preserve dock ownership and multi-window stacking during cross-tool
  navigation; bring the destination tool and its host window forward without
  silently undocking either tool.
- [x] Make wide database tables interactive at normal UI latency, including
  Race Models: keep the literal `#` column frozen by default, preserve explicit
  freeze/layout state, softly highlight the selected row, and avoid full-grid
  work during paint, selection, context-menu, and horizontal-scroll events.
- [x] Consolidate viewport visibility into a collapsed-by-default **Visuals**
  switcher with synchronized entrance, static-object, dynamic-object,
  object-helper, scene-light, terrain, overlay, object-shadow, environment,
  particle, and animation-freeze controls.

## Remaining beta gates

- [x] Ship the generated-blend terrain painter and explicit **Finalize Atlas**
  workflow tracked in 0.4.x.
- [ ] Run the representative Base/JMV beta acceptance matrix for database and
  RES review, mutation, reopen, preservation, and cross-tool navigation.

## 0.5 release-candidate known issues and boundaries

- [ ] **Lava + Resolve Blends:** direct Modern painting follows the selected
  Lava material, but **Shift+R** cannot yet preserve one visually consistent
  route owner across every rare connected Lava-on-Water transition without
  regressing ordinary Ground/Overlay repair. The bounded generic resolver is
  retained while this remains under review.
- The shipped 32-bit runtime is validated through **4,096 terrain-atlas slots / 64
  pages**. Endurance can represent the packed-format maximum of 16,384 slots,
  but sizes above 4,096 are not release-supported: the controlled 8,192-slot
  run failed from 32-bit address-space pressure.
- Test Current Map hot-reloads ordinary MPR/MOB changes in-process. A terrain
  atlas byte/page/slot change deliberately restarts only the owned test process
  so the game cannot retain a stale resource identity.
- [ ] Clear or explicitly waive the remaining headless UI-check failures for
  compact Landscape Tools layout and floating-dock native-window identity.
  Their corresponding interactive workflows have passed tester review, but the
  automated release gate is not yet clean.
- [ ] Isolate the recurring ANGLE compatibility error after
  `frame/overlays-complete`; no equivalent default desktop-OpenGL defect is
  currently confirmed.

## Approved changelists and evidence

- [Database and resource authoring beta foundation](docs/releases/0.5.0-beta-database-resource-foundation.md).
- Database/Resource Browser integration and navigation: `6940613d` through
  `4038ba23`.
- Frozen-column, paint-latency, and multi-window fixes: `e0873c97` and
  `0eebd4b1`; the r8 tester build was user-confirmed.
- [Integrated Database Editor contract](docs/DATABASE_EDITOR_VERTICAL_SLICE.md).
- [Viewport Visuals, Noise sculpt, and live-stroke responsiveness](packaging/ALPHA26_SCULPT_PERFORMANCE_R4.md).
- [Full database editor coverage](docs/releases/0.3.0-alpha.26-full-database-editor.md).
- [Expanded terrain-atlas shipped-runtime boundary](docs/validation/EXPANDED_TERRAIN_ATLAS_RUNTIME.md).
- Resource Browser validation:
  [r1](packaging/ALPHA26_RESOURCE_BROWSER_R1.md),
  [r2](packaging/ALPHA26_RESOURCE_BROWSER_R2.md), and
  [r3](packaging/ALPHA26_RESOURCE_BROWSER_R3.md).

</details>

<details open>
<summary><strong>0.4.x — lossless landscape and AI navigation</strong></summary>

## Release goal

Deliver safe production landscape editing and profile-aware AI navigation
authoring. Changed MPRs must use the lossless document path and must never fall
back to the legacy rebuilding serializer; graph persistence remains explicit.

## Completed foundation

- [x] Preserve untouched archive members, names, order, metadata, alignment,
  namespaces, and raw bytes.
- [x] Patch only edited MP/SEC/overlay payloads and verify untouched-member
  equality.
- [x] Stage, freshly reopen, semantically validate, back up, atomically replace,
  and rebind every changed MPR save.
- [x] Add canonical 33x33 sector ownership, shared-seam synchronization,
  affected-normal updates, undoable sculpting, and a deterministic per-stroke
  Noise brush for terrain and overlays.
- [x] Add MPR settings, global height operations, `maxZ` handling, crop/expand,
  object translation, new-MPR creation, and MPR recovery autosaves.
- [x] Add direct tile placement and smart painting constrained to the existing
  atlas.
- [x] Add undoable overlay creation/removal/sculpting/material assignment with
  exact neutral geometry during editing.
- [x] Add `map.txt` catalog editing, new-MOB creation, minimap generation,
  grading, export, viewport navigation, and runtime staging.
- [x] Add AI graph preview/regeneration/staging/persistence foundations and
  graph-aware Play from Here.
- [x] Preserve and parse embedded `AI_GRAPH` and external `.grp` sources with
  explicit ownership and lossless Test Current Map staging.
- [x] Add Navigation viewport layer selection, node/link/isolation preview, and
  loaded-versus-regenerated source override.
- [x] Implement deterministic terrain/liquid/profile regeneration for all eight
  movement profiles with authored object/bridge navigation rasterization.
- [x] Make graph persistence an explicit previewed, backed-up, freshly
  reparsed, atomic bake/export/ownership-switch operation.
- [x] Add deterministic regeneration, component recoloring, structural reopen,
  and graph-aware spawn/path projection tests.

## Remaining goals

- [x] Integrate the approved three-mode terrain painter: Modern resolves exact
  authored identities before deterministic generation, Classic Auto remains
  non-mutating with Safe enabled by default, and Manual writes the selected
  physical slot/rotation directly. All modes share the provenance-aware working
  atlas and one undoable stroke contract.
- [x] Complete the reviewed semantic-atlas authority and explicit draft/finalize
  publication flow, including paired MPR+RES identity, expanded addressing,
  deterministic resolve, undo/redo persistence, and runtime hot reload.
- [x] Add typed overlay-material editing with live preview, lossless undoable
  application, animation records, and per-cell material-slot inspection.
- [ ] Add validated Base/JMV surface-material presets.
- [ ] Runtime-confirm landscape edit-mode neutral geometry, overlay reset after
  unload/reload, traversal, liquid interaction, and changed-map save/load.
- [ ] Offer explicit follow-up `map.txt` registration after new-MPR creation.
- [x] Add cost/component inspection and deterministic loaded/regenerated graph
  comparison, backed by the native graph parity audit.
- [x] Runtime-validate representative Base/JMV traversal against generated and
  persisted graphs.

## Approved changelists and evidence

- Completed landscape/MPR integration: `8751b58`.
- Graph-aware Play from Here: `c369086`.
- [AI graph profile policy](docs/validation/AI_GRAPH_PROFILE_POLICY.md).
- [AI graph persistence validation](packaging/ALPHA26_AI_GRAPH_PERSISTENCE_R3.md).
- [AI graph runtime validation](packaging/ALPHA26_AI_GRAPH_RUNTIME_R1.md).
- [AI graph parity validation](packaging/ALPHA26_AI_GRAPH_PARITY_R4.md) and the
  preserved 47-graph native audit corpus (`9739e1e`).
- [Public graph research reference](https://habr.com/ru/articles/769050/).
- [MPR settings and height-range contract](docs/MPR_SETTINGS_AND_HEIGHT_RANGE.md).
- [MPR settings validation](docs/validation/MPR_SETTINGS_HEIGHT_OPERATIONS_R1.md).
- [MPR transaction foundation](docs/validation/MPR_MAXZ_TRANSACTION_FOUNDATION.md).
- [Overlay authoring validation](docs/validation/OVERLAY_SURFACE_AUTHORING_R1.md).
- [New-MOB creation validation](docs/validation/NEW_MOB_CREATION_R1.md).
- [Terrain atlas review contract](docs/TERRAIN_ATLAS_REVIEW.md).
- [Modern terrain editor contract](docs/MODERN_TERRAIN_EDITOR_MVP.md) and
  [R180 integration test scope](packaging/ALPHA26_TERRAIN_PAINT_MODES_R180_MODERN_DRAFT_UI_SEMANTIC_TEST.md).
- Minimap changelists:
  [generation](docs/releases/0.3.0-alpha.19-minimap-generation.md),
  [viewport/export](docs/releases/0.3.0-alpha.20-minimap-viewport-and-export.md),
  [fidelity/reload](docs/releases/0.3.0-alpha.21-minimap-fidelity-and-reload.md),
  [authored profiles](docs/releases/0.3.0-alpha.22-minimap-authored-profile.md),
  [runtime controls](docs/releases/0.3.0-alpha.23-minimap-control-and-runtime-header.md),
  [grading](docs/releases/0.3.0-alpha.24-modeless-minimap-grading.md),
  [navigation/locking](docs/releases/0.3.0-alpha.25-minimap-navigation-and-locking.md),
  and [polish](docs/releases/0.3.0-alpha.26-minimap-polish.md).

</details>

<details open>
<summary><strong>0.3.x — integrated editor and direct-gameplay release</strong></summary>

## Release goal

Publish the hardened editor, profile-correct rendering, collections, runtime
validation, Test Current Map, hot reload, and the integrated landscape
foundation as one coherent `0.3.0` release.

## Completed goals

- [x] Lossless MOB editing for supported properties, transforms, scripts,
  model parts, patrol/look points, copy/paste, create/delete, and conversion.
- [x] Runtime-family range/NID validation, copied-workspace overlays,
  non-blocking findings, source-object navigation, and conservative repair.
- [x] Independent MPR/MOB opening, portable collections, Maps and Collections,
  Loaded Map ownership/visibility/locking, and safe targeted unload.
- [x] Versioned autosaves, backups, atomic saves, crash diagnostics, interrupted
  test recovery, and clean-package validation.
- [x] Parameter-aware outliner search, dockable workspace panels, transactional
  property/component/model-part copy, configurable shortcuts, and internal/
  external script workflows.
- [x] Structured Behavior-mode authoring for all unit Logic slots, patrol/look
  hierarchies, and magic-trap activation/cast arrays: outliner and property
  collections, viewport selection, disabled-slot preview, contextual creation,
  copy/paste/duplicate, drag reordering, parent/sibling navigation, and a
  diplomacy-derived read-only trap-target table with a symbolic
  Ally/Neutral/War MOB diplomacy matrix.
- [x] Profile-correct humanoid appearance, equipment, animation, texture/UV
  inspection, camera-priority preparation, session caches, culling, and render
  queues.
- [x] Add indexed RES lookup, negative resource caches, asynchronous character
  preparation, persistent derived visual caches, bounded GUI-thread GPU work,
  and load/render phase timings.
- [x] Resource-driven environment lighting, authored point lights, particle
  reconstruction/batching, liquid material animation, time-of-day controls,
  and desktop OpenGL overlay confirmation.
- [x] JMV Test Current Map with selected entrances, synthetic unmapped-map
  identity, supplemental MOBs, graph/resource staging, isolated saves,
  Godmode, Play from Here, and exact runtime ownership.
- [x] State-aware hot reload with immutable generations, same-map position/
  facing restoration, transition deferral, orphan recovery, and cleanup.
- [x] Startup splash, UI scaling, accessibility fixes, camera-speed controls,
  frame-rate-independent movement, and clean first-run setup.
- [x] Dockable, filterable Message Log and MOB Family Validation with shared
  dock/floating tabs, persistent multi-monitor layout, correct View toggles,
  newest-message following, and main-window-owned shutdown.
- [x] Integrate the completed lossless landscape/MPR/minimap/AI graph foundation.

## Release gate

- [x] Runtime-confirm Test Current Map and hot reload begin at the selected
  editor time while the game clock continues normally
  ([`7401874`](https://github.com/Ilufus/ei-mapper-endurance/commit/7401874)).
- [x] Confirm splash/taskbar/setup focus on an existing profile, clean first
  run, and multi-display setup
  ([`3af2099`](https://github.com/Ilufus/ei-mapper-endurance/commit/3af2099)).
- [ ] Run the final representative Base/JMV editor corpus matrix: open,
  no-op/changed saves, undo/redo, autosave/recovery, validation, and shutdown.
- [ ] Run the final JMV runtime matrix: gameplay/dungeon/briefing maps,
  entrances/custom spawn, supplemental MOBs, generation Save/Load, hot reload,
  scripts, `drawall`, transitions, return, and graceful exit.
- [ ] Runtime-confirm immediate, conditional, and nested supplemental `AddMob`
  chains load exactly once when a deterministic fixture exists.
- [ ] Runtime-confirm World Object to Lever conversion with both previously
  shared and previously unused models.
- [x] Complete the native Vulcansmoke runtime-failure investigation and retain
  the non-blocking Critical validation warning; the controlled A/B identifies
  the hazardous record while the absent `Vulkan.mmp` remains the leading, not
  proven, mechanism
  ([investigation](docs/investigations/2026-07-23-particle-runtime.md#full-catalog-editor-freeze-and-vulcansmoke-runtime-control),
  [status](docs/PARTICLE_RENDERING_STATUS.md#runtime-safety-and-performance)).
- [ ] Runtime-confirm the complete synthetic map identity used by unmapped
  MPR/MOB testing.
- [ ] Reconfirm the distribution contract, resolve concrete closed-group
  blockers, then tag and package immutable `v0.3.0`.

## Non-blocking follow-ups

- [ ] Isolate the recurring ANGLE error after `frame/overlays-complete` before
  attributing it to any renderer subsystem.
- [ ] Continue the measured renderer plan: persistent/GPU-driven submission,
  dirty-state preparation, cached static/dynamic shadows, and evidence-gated
  raster reduction.
- [ ] Finish deeper persistent resource indexing and broader diff presentation.

## Approved changelists and evidence

- MOB hardening: [`aaef298`](https://github.com/Ilufus/ei-mapper-endurance/commit/aaef298),
  [`843ba9c`](https://github.com/Ilufus/ei-mapper-endurance/commit/843ba9c),
  [`11698ad`](https://github.com/Ilufus/ei-mapper-endurance/commit/11698ad),
  and [`db3fa31`](https://github.com/Ilufus/ei-mapper-endurance/commit/db3fa31).
- Runtime capabilities and providers:
  [`97b6991`](https://github.com/Ilufus/ei-mapper-endurance/commit/97b6991),
  [`b467d87`](https://github.com/Ilufus/ei-mapper-endurance/commit/b467d87),
  and [`eff6f99`](https://github.com/Ilufus/ei-mapper-endurance/commit/eff6f99).
- Collections and workspace UI:
  [`4e62424`](https://github.com/Ilufus/ei-mapper-endurance/commit/4e62424),
  [`51d9ad0`](https://github.com/Ilufus/ei-mapper-endurance/commit/51d9ad0),
  [`517cc1f`](https://github.com/Ilufus/ei-mapper-endurance/commit/517cc1f),
  and [`a06e5fd`](https://github.com/Ilufus/ei-mapper-endurance/commit/a06e5fd).
- Behavior hierarchy and collection authoring:
  [`e2015b1`](https://github.com/Ilufus/ei-mapper-endurance/commit/e2015b1),
  [`0b220d9`](https://github.com/Ilufus/ei-mapper-endurance/commit/0b220d9),
  [`36230d4`](https://github.com/Ilufus/ei-mapper-endurance/commit/36230d4),
  [`a4bb00e`](https://github.com/Ilufus/ei-mapper-endurance/commit/a4bb00e),
  and [`86e6ae0`](https://github.com/Ilufus/ei-mapper-endurance/commit/86e6ae0)
  ([interaction validation](packaging/ALPHA26_OBJECT_UI_REVIEW_R20.md),
  [trap-target table](packaging/ALPHA26_TRAP_TARGET_TABLE_R23.md),
  [diplomacy labels](packaging/ALPHA26_DIPLOMACY_LABELS_R24.md),
  [directional diplomacy](packaging/ALPHA26_DIPLOMACY_DIRECTION_R25.md),
  [diplomacy display](packaging/ALPHA26_DIPLOMACY_DISPLAY_R26.md),
  [diplomacy layout](packaging/ALPHA26_DIPLOMACY_LAYOUT_R27.md),
  [responsive diplomacy](packaging/ALPHA26_DIPLOMACY_RESPONSIVE_R28.md)).
- Profile visuals, particles, and environment:
  [`37e0231`](https://github.com/Ilufus/ei-mapper-endurance/commit/37e0231),
  [`aa465c3`](https://github.com/Ilufus/ei-mapper-endurance/commit/aa465c3),
  [`13639ca`](https://github.com/Ilufus/ei-mapper-endurance/commit/13639ca),
  and [`2994805`](https://github.com/Ilufus/ei-mapper-endurance/commit/2994805).
- Heavy-map loading and visual-cache work: `ab09e34`, `89405d0`, `0d3676d`,
  and `e24e31b`.
- Time, splash, and startup:
  [`7401874`](https://github.com/Ilufus/ei-mapper-endurance/commit/7401874)
  and [`3af2099`](https://github.com/Ilufus/ei-mapper-endurance/commit/3af2099).
- Message Log and validation-window workflow:
  [`fb2a854`](https://github.com/Ilufus/ei-mapper-endurance/commit/fb2a854),
  [`42949a2`](https://github.com/Ilufus/ei-mapper-endurance/commit/42949a2),
  [`c9d5214`](https://github.com/Ilufus/ei-mapper-endurance/commit/c9d5214),
  [`3049773`](https://github.com/Ilufus/ei-mapper-endurance/commit/3049773),
  and [`14e972a`](https://github.com/Ilufus/ei-mapper-endurance/commit/14e972a).
- Current landscape/MPR integration: `8751b58`.
- [MOB hardening validation](docs/validation/MOB_HARDENING_VALIDATION.md).
- [Particle rendering status](docs/PARTICLE_RENDERING_STATUS.md).
- [Environment rendering validation](docs/ENVIRONMENT_RENDERING_VALIDATION.md).
- [Overlay animation evidence](docs/investigations/2026-07-26-overlay-material-animation.md).

</details>

<details>
<summary><strong>0.2.x — completed hardened workflow and direct-gameplay milestone</strong></summary>

## Goals delivered

- [x] Capability-based Base/JMV profiles and deterministic resource discovery.
- [x] Runtime-family NID/range validation and workspace-overlay resolution.
- [x] Portable collections, multi-MOB viewing, one active editable MOB, and
  cross-MOB copy into the active destination.
- [x] JMV direct gameplay, process-owned bridge/host, isolated staging, and
  generation-scoped saves.
- [x] State-aware hot reload, cleanup journals, orphan detection, and Stop Test.
- [x] Transactional script/model-part/property workflows, undo/redo safety,
  autosave, backups, loading/saving progress, and outliner filtering.
- [x] Profile-correct resources/rendering baseline and deterministic MOB Diff.

## Approved changelists

- [`97b6991`](https://github.com/Ilufus/ei-mapper-endurance/commit/97b6991) — profile capabilities.
- [`fd30be0`](https://github.com/Ilufus/ei-mapper-endurance/commit/fd30be0) and
  [`0a4624f`](https://github.com/Ilufus/ei-mapper-endurance/commit/0a4624f) — MOB Diff.
- [`4e62424`](https://github.com/Ilufus/ei-mapper-endurance/commit/4e62424) — collections.
- [`c1ea993`](https://github.com/Ilufus/ei-mapper-endurance/commit/c1ea993) — supplemental MOB staging.
- [`517cc1f`](https://github.com/Ilufus/ei-mapper-endurance/commit/517cc1f),
  [`90eb899`](https://github.com/Ilufus/ei-mapper-endurance/commit/90eb899), and
  [`8a0badd`](https://github.com/Ilufus/ei-mapper-endurance/commit/8a0badd) — Loaded Map workflow.

</details>

<details>
<summary><strong>0.1.x — completed repository and MOB-safety foundation</strong></summary>

## Goals delivered

- [x] Independent private Endurance repository, branding, versioning, GPL-3.0
  attribution, isolated settings, and upstream history.
- [x] Complete raw-section MOB parsing, unknown-data preservation, semantic
  diff, provenance-checked edits, exact subtree copy/delete, and fail-closed
  saving.
- [x] Fresh reopen validation, backup, atomic replacement, crash diagnostics,
  rotating logs, and child-window lifecycle safety.
- [x] Installation-based Base/JMV profiles, indexed resources, independent
  MPR/MOB opening, neutral-grid MOB mode, configurable undo, and script tools.

## Approved changelists

- [`054959b`](https://github.com/Ilufus/ei-mapper-endurance/commit/054959b) — product/repository foundation.
- [`2568e38`](https://github.com/Ilufus/ei-mapper-endurance/commit/2568e38) — diagnostics and rotating logs.
- [`8a7bc3c`](https://github.com/Ilufus/ei-mapper-endurance/commit/8a7bc3c) — recoverable backups.
- [`aaef298`](https://github.com/Ilufus/ei-mapper-endurance/commit/aaef298) — raw-section MOB document and diff.
- [`843ba9c`](https://github.com/Ilufus/ei-mapper-endurance/commit/843ba9c) — generic raw-leaf saves.
- [`11698ad`](https://github.com/Ilufus/ei-mapper-endurance/commit/11698ad) — exact copy/delete/patrol/look saves.
- [`db3fa31`](https://github.com/Ilufus/ei-mapper-endurance/commit/db3fa31) — redo-safe provenance.

</details>

## Roadmap maintenance

### EI-MOB-SCENARIO-VISUAL-001 — read-only map scenario layer

- Follow-up EI-NPC-IDLE-015 (2026-09-14): user requests `cidle15` instead of the
  current default NPC animation on the map. Scope: editor idle selection, preserve
  old fallback for models without this cycle, no saved MOB/game data changes.
  Installed Base/JMV action tables confirm `cidle15` is an IDLE cycle of length
  115 with allowedState=0, currently excluded by JMV's standing-only filter.
  Expected: prefer this valid named cycle, including state 0, for Base/JMV;
  retain deterministic prior selection if absent or invalid. Status: implemented;
  original regression reproduced before the fix, all 39 animation tests pass.
  JMV male/female and Base human select and parse the installed cidle15 clip;
  creation/save/reopen/history checks remain clean for all five tested actors.
  [Evidence and limits](docs/validation/NPC_IDLE_015.md). Staging and permanent
  `C:/GOG Games/maped/EI-Mapper-Endurance-0.5.0-alpha-db-npc-r2-test` finalized,
  all 92 files match. Next: user visually checks motion/equipment on their map;
  continuation is included in PR #8 for maintainer review. No game data changed.
- Acceptance and PR handoff (2026-09-14): user confirms "everything works" for
  the delivered build and explicitly requests a PR. This supersedes the earlier
  local-only publication restriction for this contributor branch. Current
  delivered scope is accepted; submit the scenario layer, connected-MOB fixes,
  Russian/right-click navigation and linked DB-NPC creation fix together for
  maintainer review. Exact gameplay/backend coverage was not specified by the
  user; retain the documented broad UI diagnostic and static-analysis limits.
  Refreshed origin/main remains `af24d453ac3a5a86f1664a4b1af7cefaec1a9a08`;
  no rebase was needed. Contributor branch pushed and ready-for-review
  [PR #8](https://github.com/Ilufus/ei-mapper-endurance/pull/8) created.
  Next: maintainer reviews the accepted changes and documented coverage gaps.
  Merge/main writes remain outside this handoff.
- Linked fix EI-DB-NPC-PLACEMENT-001 (2026-09-14): Jun Male Hero created from DB
  first rejects its raw texture, then (after manual x4 selection and placement)
  receives critical `mob.semantic.wearable_all_parts_enabled` on NID 14.
  Originally diagnostic-only; promoted to implementation when the user confirmed
  this was the editor's DB creation defect. Expected: fresh supported DB creatures
  place without manual skin/parts repair, save/reopen without newly authored empty
  wearable-mask errors. Scope: fresh descriptors, focused Base/JMV and item/lever
  regressions, local tester delivery; no existing/installed map mutation or
  publication. Parent scenario scope is preserved.
  First incorrect values: Base-style JMV skin name and default empty Object parts,
  masked by independently resolved preview. Descriptor and actual picker tests
  reproduced rejection before the fix. New creation supplies JMV x4 skins and
  explicit wearable anatomy with model-appropriate DB hair. Integral creatures
  and the validator retain their semantics.
  Status: implementation, release and focused checks pass; clean staging and
  permanent `C:/GOG Games/maped/EI-Mapper-Endurance-0.5.0-alpha-db-npc-r1-test`
  packages finalized, all 92 files match. Four JMV creatures pass Desktop/ANGLE placement, save/reopen, zero
  family criticals and exact undo/redo; Base human passes too. Seven focused
  database tests, creature/item/lever creation and 51 scenario checks pass.
  [Evidence and remaining acceptance](docs/validation/DATABASE_NPC_CREATION_R1.md).
  Next: user checks a newly added actor on their map and in the game. Old NID 14
  is not automatically repaired. Package executable SHA-256:
  `289c2cac8ae200de65c4109e31e3c7d58d7846e37037137f2a54b81fc1435d08`.
  Earlier terrain-ray message was passed by the user's subsequent placement;
  the exact camera failure was not reproduced and ray behavior is unchanged.
  Full UI-widget check remains an inherited gap; parent task stays open.
- Original goal: reveal placed NPC/object/lever participants and verified
  script-created entities; source/function/line inspector, object/code navigation,
  creation/movement/use commands, trustworthy virtual spawn/destination markers,
  conditions, dynamic/unknown references, family/external/duplicate-ID statuses.
  Graphs and runtime tracing are deferred; static presence never proves execution
  and no references never proves an object unused.
- Authority: isolated local implementation, synthetic tests, release build and a
  new permanent unpacked tester folder. No push, PR, merge or publication;
  installed data and other worktrees are read-only. No script execution/rewriting,
  live game/map edits or game-file copies for editing.
- Baseline: refreshed origin/main `af24d453ac3a5a86f1664a4b1af7cefaec1a9a08`,
  local branch `contrib/Samolleg/mob-scenario-layer`, initially clean.
- Completion: repeatable parsing/navigation/markers/conditions/unknowns/scope/
  duplicate/lifecycle/nonmutation tests, draft-buffer line fidelity, map and
  object/script undo refresh, Russian UI, release and finalized tester package.
- Status: first-stage implementation and permanent tester delivery complete;
  parent remains open for representative interactive map acceptance.
- Follow-up EI-MOB-SCENARIO-VISUAL-001-B (2026-09-14): user requests Russian as
  the default language and immediate object navigation on scenario-row right
  click. Preserve explicitly saved language and read-only navigation. Screenshot
  shows a family target with no navigation option (unopened source); asked whether
  to load its MOB or focus a coordinate marker. Acceptance: fresh/missing-language
  startup and settings use Russian; saved English remains valid; right click
  selects the clicked row and navigates without document/history mutation;
  unresolved/ambiguous rows never guess an object. Status: both requests implemented
  and delivered in R3; parent remains open for representative interactive acceptance.
  Before: 4 native failures for language/right click, then a separate reproduced
  cross-MOB camera failure and unopened-target failure. After: release, localization
  contract, native desktop 55/55 (with capture) and ANGLE 58/58 with read-only JMV
  zone6 corpus. Russian panel visually checked. Camera now
  focuses the target position instead of searching selection only in active MOB;
  unknown rows cannot reuse a prior target. Existing saved English is preserved.
  User approved automatic MOB load and object selection (2026-09-14).
  Auto-load checks file hash and exact object provenance, reuses open documents
  and preserves file bytes/dirty state/history. Stale files do not navigate.
  [R3 evidence and audit](docs/validation/MOB_SCENARIO_R3.md).
  Delivered `C:/GOG Games/maped/EI-Mapper-Endurance-0.5.0-alpha-mob-scenario-r3-test`;
  finalizers passed, 92 files hash-identical to staging. Earlier folders preserved.
  Next: user checks the same screenshot case via [R3 steps](packaging/MOB_SCENARIO_R3.md).
  No pending material choice; actual session acceptance and the R1 broad UI
  diagnostic remain evidence gaps. No publication or packaged GUI launch.
- Linked follow-up EI-MOB-SCENARIO-VISUAL-001-A (2026-09-14): user reports that
  connected MOBs appear omitted. Two reproduced defects fixed locally: optional
  `.mob` normalization and omission of an inspected but unopened owning primary.
  Before: 1 model failure and 4 native panel failures. After: model 15/15, desktop
  integration 38/38, ANGLE plus JMV zone3obr corpus 42/42. Recursive loaded/unloaded
  source participation, draft pruning and unrelated-map isolation pass.
  [Follow-up evidence and uncertainty audit](docs/validation/MOB_SCENARIO_R2.md).
  Delivered R2: `C:/GOG Games/maped/EI-Mapper-Endurance-0.5.0-alpha-mob-scenario-r2-test`;
  finalizer passed, 92 files hash-identical to staging. R1 preserved with the user's
  settings/logs for comparison instead of re-finalizing that used baseline.
  Exact missing participant is not identified; relation of these fixes to the
  reported observation remains unconfirmed. Next: user verifies the same case
  with [R2 tester steps](packaging/MOB_SCENARIO_R2.md); parent and follow-up stay
  open for acceptance. No publication or installed data mutation.
- Original failure: legacy analyzer returned one numeric lookup for the original
  three-operation probe. Implemented value-only call/condition/scope analysis,
  live draft inspector and navigation, conservative identity/prototype resolution,
  and transient participant/spawn/destination overlays. No document mutation or
  execution inference. Evidence and acceptance: [research](docs/research/mob-scenario-layer.md)
  and [validation](docs/validation/MOB_SCENARIO_R1.md).
- Verified: release build; model 14, legacy references 6, family 32, viewport 8,
  shadow 4 tests; localization contract; hidden native desktop integration 39
  and ANGLE integration 33 checks. Read-only Base/JMV corpus hashes unchanged.
  ANGLE overlay VAO reset was traced to the unsupported ES2 core call and fixed;
  both backend glyph pixels/state now pass.
- Delivered: `C:/GOG Games/maped/EI-Mapper-Endurance-0.5.0-alpha-mob-scenario-r1-test`,
  92 files identical to finalized staging, destination finalizer passed. Packaged
  GUI was not launched; no push, PR, merge or publication.
- Linked blocker/evidence gap: full `--ui-widget-check` remains not passed
  (offscreen/minimal access violation, native Windows stalls after Russian layout).
  Focused native scenario/dialog checks pass, but do not replace that broad check.
- Bounds: arbitrary symbolic aliases/runtime execution and uninspected new source
  dependencies remain unresolved; named EI skills were unavailable. No private
  source was inspected. Next: user runs [tester steps](packaging/MOB_SCENARIO_R1.md)
  on representative maps; investigate the broad UI diagnostic separately before
  claiming full UI regression coverage. No remaining material low-confidence
  implementation choice is presented as proven runtime behavior.

### EI-WORLD-SCULPT-001 — shared world-coordinate terrain brush

- Original problem: the sculpt brush edits only the active MPR; neighboring
  landscapes are read-only render instances. Approved result: raise, lower,
  flatten and smooth in world coordinates over active and explicitly enabled
  neighboring maps, one position/radius/strength and one undo/redo per stroke.
- Preserve shared boundary heights and order-independent cross-edge smoothing;
  retain dirty documents across switching/close/reload decisions; review all
  affected saves, validate/reopen/back up every MPR and provide explicit recovery
  for partial failure. Do not claim multi-file atomicity without a protocol.
- Authority: isolated implementation, tests, release tester folder, scoped commit,
  contributor-branch push and PR; no main merge. Installed resources/layout are
  read-only; no game-file copying for editing, textures, objects, auto-stitching,
  map repositioning, unrelated changes or deletion of previous builds.
- Baseline: `contrib/Samolleg/world-terrain-sculpt` from fetched `origin/main`
  `ffdc28ea9f749896ddffdd583e2a6a5e44d5201a`, initially clean.
- Status: owner accepted combined R5 on 2026-09-12 ("Всё работает как надо")
  and requested the final PR. Painting and reported world-seam scenarios accepted;
  ready for review. Known broad UI diagnostic failure remains explicitly open.
  Evidence and repeatable baseline
  acceptance scenario: [investigation](docs/research/world-terrain-sculpt.md).
- Confirmed: zone1/zone2 share 161 XY boundary vertices, three unequal in Z
  (maximum 0.54200836); zone1/zone3obr share none; zone6/zone9 nominal rectangles
  overlap by 0.25. Maps have different maxZ/uint16 height quantization.
- Approved follow-up: independently sculpt misaligned grids; average initially
  unequal coincident vertices only within the stroke; accept format-bounded
  saved-height rounding. No map movement or out-of-stroke stitching.
- Implemented: four world-space modes, explicit map consent, retained documents,
  compound world history, transition guards, validated cohort saving and recovery.
  [Validation and remaining acceptance](docs/validation/WORLD_TERRAIN_SCULPT_R1.md):
  core 9 passed; widgets 16 passed/1 OpenGL skip; release + x86 components built.
- Related blocker: full headless UI check reproduces `0xC0000005`; not fixed or
  waived. Native viewport interaction and representative-map performance remain
  unverified. The portable Qt plugin discovery issue was resolved for tests.
- Necessary build support: runtime scripts accept `EI_VS_DEV_CMD` and installed
  BuildTools, replacing the unavailable hardcoded Community-only assumption.
- Completion requires the full original scenario plus regression/build evidence,
  permanent unpacked tester folder and PR; a UI change or build alone is partial.
- Review: [draft PR #7](https://github.com/Ilufus/ei-mapper-endurance/pull/7).
  Implementation commit: `b7078b086a5436370412916c53f44f7aa5ee7a95`.
- Delivery: permanent unpacked `EI-Mapper-Endurance-0.5.0-alpha-world-terrain-sculpt-r1-test`;
  package finalizer and distribution validator passed, permanent copy hashes
  matched the staged package. No packaged GUI launch or ZIP.
- Next: manual
  acceptance of cross-map dragging, switching, save/reopen and close decisions.
  Do not close this parent task until the remaining acceptance is verified.
- History: ambiguous "Давай так" was clarified; user explicitly answered "да"
  to the complete independent-grid/averaging/rounding policy above.
- EI-WORLD-SCULPT-001-GAPS (open, necessary acceptance investigation): user
  recording `20260912-1215-58.5560116.mp4` shows reported black gaps during
  smoothing. Reproduced first incorrect state: neighbor refresh releases the GL
  context before deferred active-map GPU upload. Corrected context ownership;
  native GPU regression failed before and passes after (3/0/0), core 9 passed,
  widgets 16 passed/2 offscreen skips, release built. [R2 evidence and limits](docs/validation/WORLD_TERRAIN_SCULPT_R2.md).
  R2 staging finalizer/validator passed; it preserves R1 for A/B comparison.
  Delivery target: `EI-Mapper-Endurance-0.5.0-alpha-world-terrain-sculpt-r2-test`.
  Permanent R2 finalizer/distribution validation passed; all copied file hashes
  match staging. Packaged implementation: `439a46376c8f14d75fac88fe81be18ca5020809a`
  in draft PR #7. Next: repeat the recorded scene in R2;
  User now confirms the original two-map problem disappeared in R2. This
  recorded two-map regression is accepted; full parent acceptance remains open.
- EI-WORLD-SCULPT-001-TRIPLE (open, necessary follow-up): recording
  `20260912-1239-41.0343493.mp4` shows a gap with zone1, zone2 and zone3obr
  modified, both neighbors enabled. R2 logs confirm origins (0,0), (192,288),
  (352,63.5). Read-only boundary audit: zone1/zone2 share 161 XY vertices;
  zone3 shares zero with either because its grid is shifted by half a step.
  Current stamp groups exact coincident boundary vertices only; zone3 therefore
  smooths independently, as previously approved. This is a distinct geometric
  limitation from the accepted R2 GPU fix. No new implementation or installed
  map mutation during diagnosis. Owner now explicitly authorizes coordinated
  boundary heights inside the brush, including local straightening; this replaces
  the prior independent-edge policy for touching shifted grids. Map placement,
  protected maps and vertices outside the brush stay unchanged. Implemented
  affine profiles on connected complete shifted boundary segments inside the
  brush. Synthetic before-failure/after-pass, history and cohort save/reopen,
  both axes/four modes/range saturation/order/protection checks passed. Installed
  triple calculated in memory: max covered gap 0.000254583, within quantization.
  Core 12 passed; widgets 16 passed/2 offscreen skips; native GPU 3 passed;
  release built. [R3 validation and scope](docs/validation/WORLD_TERRAIN_SCULPT_R3.md).
  Brush-rim crossing cells retain a transition to fixed outside geometry; they
  are not guaranteed gap-free. Permanent
  `EI-Mapper-Endurance-0.5.0-alpha-world-terrain-sculpt-r3-test` finalized and
  distribution-validated; all copy hashes matched. Packaged implementation
  `62eb8516303c924b6a5fbab55585726fa9a38faa` is in draft PR #7. R2 preserved
  for comparison. Next: user repeats the video scene in R3; parent remains open.
- EI-WORLD-SCULPT-001-MOVING-RIM (open, blocks original outcome): owner reports
  remaining strips in `20260912-1312-16.8301064.mp4`. R3 visual acceptance failed.
  Added local failing regression `movingStaggeredStrokeKeepsWholeTouchedSeamClosed`:
  initially equal flat boundary heights, an interior peak, three radius-5 smooth
  stamps with centers at y=32/33/34. Whole touched seam sampled every 1/8 unit;
  maximum gap 0.940718700 at y=27.5. Existing center-only checks missed this.
  First wrong decision: excluding rim-crossing segments leaves incompatible
  fixed endpoints as a new affine profile replaces the covered segment chain.
  No new fix/build shipped. Regression is intentionally failing locally.
  Removing this limitation by height-only reconciliation requires permission to
  adjust the whole connected staggered boundary beyond the brush (potentially
  straightening a long edge). Prior authority explicitly fixes outside vertices.
  Owner explicitly authorized the whole connected seam beyond the brush.
  Implemented full enabled-boundary traversal; only components reached by the
  stamp are reconciled. Outside interiors and unrelated seams remain unchanged.
  Moving-stroke regression now measures 0.000762951 (height quantization);
  full installed triple seam over Y=63.5..383.5: 0.000494528 after eight moved
  stamps. Core 13 passed including whole-stroke history/save/reopen; widgets
  16 passed/2 offscreen skips and native GPU 3 passed. Release/x86 built,
  staged R4 finalizer passed. [R4 evidence](docs/validation/WORLD_TERRAIN_SCULPT_R4.md).
  Full UI check still fails with `0xC0000005` in normal Windows (sandbox loader
  failure separately diagnosed). Permanent
  `EI-Mapper-Endurance-0.5.0-alpha-world-terrain-sculpt-r4-test` finalized and
  validated; all copy hashes matched staging. Packaged implementation
  `397c5b79e8a057f9de69d8a47e534006650ecfb9` is in draft PR #7. Previous builds
  preserved. Next: original-scene acceptance with moving brush and far-edge
  inspection. R3 is not marked fixed; parent remains open.
- Related deferred robustness hypothesis: exact double XY grouping can differ
  for mathematically equivalent offset vertices (e.g. `(192+1)+16/254` versus
  `192+(1+16/254)`). Read-only inspection found zero such key mismatches on the
  recorded zone1/zone2 boundary. Assess separately with an offset fixture after
  the recorded gap is verified; do not attribute this recording to it.
- EI-WORLD-SCULPT-001-BASELINE (resolved in owner-accepted R5): user reports
  painting with other textures unavailable and asks about the latest PR baseline.
  Confirmed R4 uses main `ffdc28ea` plus PR #7; it omits PR #6 branch
  `origin/contrib/Samolleg/add-from-db` (head `be440676`), including exact HD atlas
  approvals `721693c3` and `7cb1190f` (38 namespaces/41 maps/88 material sources).
  Fresh fetch shows main has not incorporated those changes. Prior statement
  about latest PR code meant #7 only and did not establish inclusion of #6.
  This explains a missing texture-availability fix; exact reported texture/build
  remains unspecified and R4 has no local logs yet. No merge or texture changes
  made during diagnosis. Owner confirms integration on the exact baseline
  `be4406766c19257d86f85c2e02ab7dd45a96ff4d`. Integrate that history into the
  existing contributor branch without rewriting published commits; preserve
  painting/database/profile changes and all world-sculpt fixes. Integrated exact
  baseline with automatic code merge; roadmap and translations retain both sides,
  binary translation regenerated. Presets/resolver match be440676 exactly.
  Native HD coverage: 38 namespaces, 41 maps, 88 material families; focused
  document suite 10 passed, sculpt 13 passed, widgets 16 passed/2 skips, native
  GPU 3 passed. Release/x86 built; full UI check still `0xC0000005`.
  [Combined R5 evidence](docs/validation/WORLD_TERRAIN_SCULPT_R5.md).
  Delivered `EI-Mapper-Endurance-0.5.0-alpha-world-terrain-sculpt-r5-test`, package
  commit `e327897ad94c8f1fb1b0a0292a0871791a7c0b0d`; requested be440676 is a
  verified ancestor and explicitly recorded in BUILD_INFO. Finalizer/distribution
  validation passed and all permanent-copy hashes matched. PR #7 updated;
  no main merge or force-push. Owner confirms R5 works as intended and authorizes
  final PR submission. This acceptance resolves the reported painting, triple
  junction and moving-rim scenarios; older pending statements above are history.
  PR #7 is open and ready for review (draft=false verified); final description
  includes owner acceptance, the exact PR #6 baseline and known limitations.
  Next: maintainer review/integration. Broad UI diagnostic
  `0xC0000005` and the separate offset-coordinate robustness hypothesis remain
  disclosed unresolved items; this acceptance is not a claim they were fixed.
### Related experiment: EI-TERRAIN-PALETTE-001

- User authorized modern-palette pixel-hash relaxation on the latest PR (#6,
  `contrib/Samolleg/add-from-db`), confirmed open and unmerged on 2026-09-12.
  Canonical support diagnosis read from the main checkout roadmap: the loaded
  Zone1 HD atlas has different pixels from the built-in reviewed preset, so
  uniform thumbnails and transition-source hydration fail despite matching names.
- Expected: use the current complete atlas by namespace/structure for modern
  thumbnails and generation, with active-atlas priority and coherent caching.
  Retain page/slot/decode checks and all exact checks outside this experiment,
  including MOB and save integrity. Tile semantics are not guaranteed.
- Authority: code/tests in this branch, same-PR commit/push, a new permanent
  unpacked tester folder. Installed data/profiles/maps and old builds stay intact.
- Status: experimental implementation validated on 2026-09-12. The original
  repaint test failed before the change and now passes; eight focused cases pass,
  including strict embedded/save checks. Read-only installed-resource probe selects
  the active Zone1 repaint and generates material 1712759006 from bz2g slot 195.
  Release editor/runtime and fresh database creation/preview diagnostics pass.
  Evidence: [r6 validation](docs/validation/terrain-palette-experiment-r6.md).
  Separate r6 tester folder delivered and distribution validation passed; PR #6
  is the review target. Next: user checks interactive palette appearance,
  semantic assignments and mixed transitions;
  the existing broad UI diagnostic access violation remains an explicit gap.
  This related experiment does not replace EI-ADD-FROM-DB-001.
- R1 load regression (2026-09-12): user reports Zone1 stuck at 66% in r6.
  The live process eventually completes: first landscape frame 1078 ms, workspace
  65416 ms, deferred material catalog 60990 ms. Log timestamps isolate about
  60 seconds between catalog phase reporting and hydration queue submission;
  a second launch reproduces the same pause. Hypothesis: source fingerprints
  scan all archive files for every repeated transition while assembling jobs on
  the GUI thread. Expected: source snapshot once per planning batch, preserve
  active-atlas priority/invalidation, and verify actual Zone1 load latency.
  Status: optimization abandoned by explicit user decision below.
- Scope change (2026-09-12): user requested rollback of the experiment and only
  adding their complete Zone1 pixel hash
  `a44f78f46933dfba29125ec6867c3541acb6be56d93083afa6a6ce9616b5f997`
  to the allowed painting bindings. This supersedes broad structure matching and
  its pending performance work. Restore renderer/binding/translation behavior
  from before r6; retain the prior database-object changes. Keep old HD bindings
  valid and unknown hashes rejected. Implemented: exact profile/material/transition
  eligibility and strict rejection checks pass (eight focused cases); renderer and
  translations match r5. Release/runtime builds, creation and JMV placement checks
  pass. Separate r7 package finalized and delivered; PR #6 remains the review target.
  Evidence: [r7 validation](docs/validation/zone1-approved-hash-r7.md).
  Next: user verifies Zone1 loading and painting; no automatic r7 full-map timing
  measurement and no approval of other modified atlas hashes are claimed.
- R2 follow-up (2026-09-12): r7 screenshot shows partial Modern thumbnails.
  Read-only live log confirms 22 material sources after hydration; other atlas
  revisions still fail exact binding. Separate confirmed catalog restriction:
  Zone1 Ground / Common slot 1 (type 1) and Stone / Lime slot 48 (type 2) are
  excluded because their global names already retain type 0 from another atlas.
  Adding the Zone1 pixel hash cannot remove that runtime-type conflict, and
  512/512 assigned slots does not mean every game-wide material has a source.
  This is explanation/diagnosis only: no other hashes or type-policy changes
  authorized by this question. Next: decide broader exact-hash coverage separately
  from runtime-type family handling if the user requests full palette repair.
  The same log confirms r7 workspace load 4987 ms and material catalog 256 ms,
  versus r6 65416 ms / 60990 ms: the minute-long load regression is removed.
- R3 authorized (2026-09-12): user requests access for all maps. Extend the same
  exact-hash allowlist approach to complete atlas namespaces in the installed HD
  pack, using the editor's decoder to measure actual pixels. Preserve strict
  binding, existing presets, runtime-type checks and database placement. Expected:
  all catalog maps with complete HD sources allowed and global palette source
  coverage verified; no generic hash bypass. Implemented: native probe first failed
  on 34 missing approvals; adding those measured hashes yields 38/38 approved
  namespaces, 41/41 installed MPRs covered and 88/88 global material sources.
  Nine focused cases, release/runtime build and database creation diagnostics pass.
  Separate r8 package finalized and delivered; PR #6 is the review target.
  Evidence: [r8 validation](docs/validation/all-map-hd-bindings-r8.md).
  Next: user checks interactive thumbnails/transitions and map switching. Per-slot
  semantic correctness, full r8 load timing and the pre-existing broad UI crash
  remain explicit coverage limits; parent database coverage gaps stay open.

### Authorized implementation: EI-PROFILE-DEJAVU-001

- Request (2026-09-12): add DejaVu alongside Jamais Vu and Base, using the
  supplied installed MODS/DejaVu directory. This is the active task; prior
  database placement and gameplay findings remain recorded below.
- Expected: independently selectable/detected profile, its own maps/resources/DB
  taking precedence over Base, compatible visual providers and map-test staging.
  Never select the neighboring mod when this profile is absent.
- Authority: editor changes and tests/build/handoff; read-only mod inspection.
  No live mod/map edits or interactive game launch.
- Evidence: installed config identifies DejaVu and uses the JMV resource layout.
  The original catalog lacked this profile and runtime launch checked JMV's ID.
- Completion: detection/isolation and launch-request/staging regressions pass,
  actual installed resources load through the selected profile, runnable tester
  package delivered; interactive game acceptance reported separately.
- Status: implemented and packaged as r10. Profile detection found 41 MPRs and
  69 resource archives including Base; profile isolation, runtime request guards,
  targeted launcher checks and DejaVu preview/placement diagnostics passed.
  [Validation and remaining limits](docs/validation/dejavu-profile-r10.md).
- Next: user selects DejaVu and verifies an actual map-test launch and hot reload.
  Interactive runtime acceptance remains pending. The installed DejaVu lacks the
  cheat-dagger map texture; its placement diagnostic used an authored temporary
  fixture, not a neighboring mod asset.

### Authorized implementation: EI-ADD-FROM-DB-001

- R11 interaction-distance investigation (2026-09-12): supplied 18-second video
  shows pickup through lever 336 on zone1 while the character remains away from
  the visible dagger. Expected: approach the item before interaction. Read-only
  diagnosis of the map, interaction script and model/lever parameters is
  authorized; no live map edits. Diagnosis: conversion can produce a lever whose
  model has no database approach-point definition; the native empty-points path
  skips approach before interaction. Pickup follows successful activation.
  This is a gameplay gap beyond the proven structural conversion/ID checks.
  Detailed local evidence is excluded from publication. Next: choose and validate
  a close-approach definition or fallback for arbitrary converted models; no
  game/database changes, build or installation performed. Parent remains open.
- R10 ID regression (2026-09-12): screenshot of added cheat dagger converted to
  lever shows NID 0. Read-only r8 log confirms positive allocation followed by
  zero-NID validation failures for that lever. Creation allocates positive IDs;
  the live conversion constructs a CLever from JSON (constructors intentionally
  leave ID allocation to the caller) but never transfers the original object's
  ID before replacement. Prior tests converted saved raw bytes, bypassing this
  live replacement path. Expected: retain the unique positive ID through live
  conversion, undo/redo and save/reopen; preserve creation allocation semantics.
  Related persistence regression: real command test then exposed lost raw
  provenance on redo after saving each side of conversion. Capture each outgoing
  source/clean subtree before replacement so guarded undo/redo saves remain valid.
  Implemented: conversion command retains the existing NID and preserves outgoing
  raw source/clean templates across saves. Real-command regression failed before
  the fix and passes for fresh and saved objects in JMV and Base, including
  undo/save/redo/save/reopen. All three creation categories retain unique positive
  IDs. Separate r9 staging and delivery packages pass finalization; see
  [R10 validation](docs/validation/object-conversion-id-r9.md).
  Next: user acceptance on the original map; existing saved zero IDs are not
  automatically migrated. Parent visual coverage and broad UI-check gap remain
  open. No live map edits.
- Original problem: Create Object requires technical fields and a selected
  same-type raw MOB template; there is no complete database-to-map workflow.
- Expected result: all three searchable categories (monster-prototype creatures,
  DB items as ordinary objects (latest user decision), usable objects/levers), names/models/preview, supported
  defaults, placement, undo/redo, guarded save/reopen; technical options secondary.
- Authority: dedicated contributor branch, project code in this worktree only;
  read-only installed-data research; tests/build, scoped commit/push/PR, permanent
  unpacked tester folder. No main merge, live game/mod/settings changes, or private
  source/data in repository, package, or publication.
- Base: `contrib/Samolleg/add-from-db` from fetched `origin/main`
  `ffdc28ea9f749896ddffdd583e2a6a5e44d5201a` (2026-09-11).
- Status: partial implementation on 2026-09-11: active database catalog,
  searchable picker, creature/item/lever preview and click placement, fresh validated
  creation records, and undo/save/redo retention implemented. Item placement is now
  an ordinary object; the user owns lever conversion and the interaction script.
  This explicitly supersedes R2's native pickup requirement. Complete automatic
  lever appearance and all-entry/gameplay acceptance remain open.
  Parent remains open until all three categories and their
  persistence/regression checks are delivered. Canonical task read from the
  main checkout roadmap; its local changes have not been overwritten.
- Original cause: the creation form had no database selection and required a
  selected same-type raw template. Database mode now uses its own independently
  generated, parsed/reopened record; the save guard still requires hashed bytes.
- Evidence and repeatable acceptance cases:
  [creation research](docs/research/add-from-database-001.md). Read-only traversal
  passed on 252 installed MOBs; both databases contain 37 lever prototype rows.
  [validation checkpoint](docs/validation/add-from-database-001.md): seven focused
  tests pass, including installed Base/JMV catalog checks and archive identity;
  the creation diagnostic passes real undo/redo and save/undo/save/redo/save,
  multiple fresh IDs, editable reopen and corrupt-hash rejection.
- R2 (resolved 2026-09-11): native item/bag pickup, no new scenario. The proposed
  generated-script route is rejected; all three categories remain required.
  R3 (necessary research): validated fresh raw records and
  creature appearance/lever defaults; current renderer forbids blind DB fallback.
- R4 (research gap, not an outstanding product decision): independent native
  item pickup remains unverified. The user reaffirmed creating the selected
  database object on the map; the proposed creature-loot substitution is not
  authorized and the follow-up choice is withdrawn. Persistence format is an
  implementation concern, not a prerequisite the user must resolve.
- R5 (necessary regression, fixed): a creation command lost its source after
  save/undo/save, so redo could not be saved. The original failure was reproduced;
  undo now retains the exact current source/clean bytes before removal, and the
  full sequence passes for creatures and levers.
- Delivery checkpoint: unpacked tester folder finalized outside this worktree;
  distribution validation passes and executable hash matches the tested build.
  [Tester notes](packaging/ADD_FROM_DATABASE_TEST.md) explicitly describe the
  incomplete scope; this is a draft contributor handoff, not parent closure.
- Next: validate the delivered ordinary-item workflow, supported automatic lever appearance,
  and interactive/gameplay validation. This checkpoint does not close the parent.
  No installed data or user settings changed; no game assets or private source
  were copied into implementation or distribution.
- R6/R7/R8 (fixed for the recorded cases, 2026-09-11 user video): JMV AncientCurseM25 and
  ArmoredTurtle110 have DB texture names but render as cyan silhouettes; the
  item `2h long spear` has no preview; Place leaves the window open. Expected:
  textured previews, including weapons, and immediate cursor-bound placement
  with the picker hidden, click to commit and Escape to cancel. Decoder installed
  only in ignored build-video-tools; sampled the supplied 30-second video.
  First incorrect decisions: preview reuses viewport shader uniforms (including
  solid customColor); placement checks a PNT3-only CPU atlas decoder for ordinary
  DXT textures; item placement eligibility suppresses preview; the custom placement
  event filter never draws a pending object. Reproduction and checks tracked in
  docs/validation/add-from-database-video-regressions.md. These are necessary
  fixes within EI-ADD-FROM-DB-001, not a change to its native-pickup requirement.
  The original shader regression was rebuilt and failed (two-color silhouette);
  the fixed renderer passes visually inspected JMV desktop/ANGLE and base-resource
  previews. The production picker/viewport transaction passes immediate placement,
  pointer motion, cancellation and one-action undo/redo. Thirteen focused tests
  and the original creation/save diagnostic pass. r2 preserves the user's used r1
  folder. Native item placement and all-entry/gameplay acceptance remain open.
  r2 is finalized in the permanent tester location; executable hashes match the
  tested build. The existing draft PR #6 remains the contributor review target.
  User acceptance on 2026-09-11: NPC placement works in r2. The supplied screenshot
  confirms the selected weapon/material preview, but item placement is disabled.
  This is the existing implementation gap R4: the catalog rejects all item
  creation and the form never enables its Place button for items. It is not a
  reported missing texture or failed user setup. Next remains native collectible
  creation and validation without a new scenario; parent scope is unchanged.
- R4 continuation (2026-09-11): user explicitly authorized item implementation.
  The screenshot acceptance case and first rejecting editor decisions are recorded
  in [native item checkpoint](docs/research/database-native-item-placement.md).
  Additional read-only research did not establish an independent native pickup
  path in the inspected Base/JMV facilities; the collection routes examined
  require a unit target. A proposed runtime extension changes map compatibility
  and remains unproven. Next: resolve whether a matching game runtime dependency
  is acceptable, then prove actual pickup and persistence before enabling the
  picker. Ordinary game compatibility cannot be silently dropped. No production
  code or installed files changed at this checkpoint; parent remains open.
- R4 scope change explicitly requested by user (2026-09-11): place the selected
  database item as an ordinary world object with its model/material. The user
  will convert it to a lever and attach an existing interaction script. This
  supersedes the native-pickup requirement for items; no runtime dependency or
  generated script is needed. Implement enabled Place, immediate mouse ghost,
  cancel/commit, history, save/reopen and retained appearance. Preserve the
  earlier requirement/research above as history. Status: implemented and checked
  for Base/JMV weapons; original disabled button reproduced before the fix.
  Pointer/cancel/commit/history and save/reopen pass; converting the placed sword
  to a lever retains model, selected parts and material, with identical rendered
  images. See [r3 validation](docs/validation/database-item-placement-r3.md).
  r3 is finalized in the permanent tester location; staged and delivered
  executable hashes match. Thirteen focused tests and Base/JMV desktop/ANGLE
  placement checks pass. The used r2 folder is preserved. Gameplay with the
  user's interaction script remains a user check, not automatic native pickup.
- R9 (2026-09-11, necessary regression): user placed `cheat dagger.bronze` on
  zone1; editor textured, game white. Read-only inspection confirms both texture
  fields include a terminal `.mmp`. The game appends the extension while the
  editor's permissive lookup strips it, masking the invalid saved identifier.
  Expected: persist extensionless texture identifiers while preserving `.x2`
  and material suffixes. New item creation is fixed; the red test reproduced the
  invalid filename before the fix. Thirteen focused tests, saved primary/secondary
  fields, exact resource lookup and Base/JMV desktop/ANGLE checks pass. r4 is
  finalized in staging and the permanent tester location, with matching hashes.
  See [r4 validation](docs/validation/database-item-texture-r4.md). The existing user map
  remains unchanged; its two texture fields can be corrected in the editor.
  Next: user reloads the corrected map for in-game appearance acceptance. This
  checkpoint does not claim a corrected runtime screenshot or close parent gaps.
  Follow-up audit requested by user: the current zone1 has 117 creatures and
  23 levers; neither category has a terminal `.mmp` in its saved texture fields.
  Among 1,527 ordinary objects only the reported dagger has that defect.
  Creature catalog texture names already normalize the extension; automatic
  lever selection also strips it. Conversion of an r3 item to a lever preserves
  its old bad name, and manually entering `.mmp` is still accepted by properties.
  This bounded check does not establish all in-game texture/appearance behavior.
  User workflow clarification: conversion is under Edit > Object > Convert World
  Object to Lever, for one selected ordinary object in the active map's object
  mode after placement is committed. Default free-open/two-state settings apply;
  interaction script remains user-authored. Menu and prerequisites verified in code.
- R9 reopened by gameplay acceptance (2026-09-11): both the corrected old dagger
  and a newly placed r4 dagger are white in the user's screenshot. Read-only
  inspection of zone1 confirms IDs 334/335 both persist extensionless primary
  and secondary names, model `unhuma`, part `rh3.dagger03`. The suffix fix is
  insufficient; editor preview is not evidence of game-compatible binding.
  First incorrect representation: the picker persists a PNT3 equipment layer as
  a directly loadable ordinary texture; preview also applies an unpersisted x2
  UV transform. A ready texture with the correct atlas layout is needed.
  [Investigation and acceptance](docs/validation/database-item-texture-r5-investigation.md).
  Next: obtain the required edited-resource-copy permission, generate a separate
  local map resource, then verify original gameplay and persistence/conversion.
  Installed maps/resources remain unchanged; parent scope stays open.
  Follow-up explanation: distinguish the MMP container, PNT3 layer compression,
  uncompressed 8888 pixels and DXT block compression; atlas layout is independent
  of encoding. The user's format question does not authorize asset-copy/export.
  User decision: use separately converted item textures with terminal `map`, e.g.
  `unhumadg_03.br.1.x2map`, instead of generating assets in this task. Implement
  exact lookup in map-item preview and both persisted texture fields, without
  silently returning to equipment layers. Existing objects remain user-editable;
  new creatures and database levers keep their current naming. Implemented: exact
  map resource selection, primary/secondary identity, missing-file diagnostic.
  User confirmed resources are alongside the originals; the actual bronze DXT1
  file was found and passed desktop/ANGLE placement, history, save/reopen and lever
  conversion checks. Thirteen focused tests and fresh creation/history pass;
  Base additionally passes with generated synthetic DXT1 fixtures.
  [r5 validation](docs/validation/database-item-map-texture-r5.md).
  r5 is finalized in staging and the permanent tester folder; executable hashes
  match. The used r4 folder/settings remain preserved; packaged GUI not launched.
  Next: user checks r5 with the saved map in the game, including atlas alignment.
  No asset or installed-map writes; earlier asset-generation proposal superseded.
  R9 gameplay follow-up: r5 loads the map texture, but the user's screenshots
  show the wrong dagger appearance in both game and editor. Read-only comparison
  confirms the converted 512x512 DXT1 retains the PNT3 layout (opaque RGB mean
  absolute error 3.191/255; zero differing alpha-mask pixels). The part uses a
  half-size UV quadrant; the former equipment preview applied a 2x repeat
  transform, whereas the ordinary map object uses authored UVs. Naming and format
  are now correct, but the atlas arrangement remains uncorrected. Next: prepare
  a separately authorized working copy of the map texture with the required
  repeated 2x2 atlas, then verify original appearance in the game. Do not hide
  the mismatch with an editor-only UV correction. No asset writes performed.
  The current saved dagger (336) retains model `unhuma` and part `rh3.dagger03`,
  but its figure complexion is [1,1,5]. This independently stretches the geometry;
  [1,1,1] is the default. Preserve the user's size choice unless asked to reset it.
  User requested a converter prompt: specify lossless decoding, 2x2 repetition
  into a double-width/double-height atlas for the verified packed-x2 dagger,
  terminal map naming, DXT1 alpha/mipmap handling and sampling validation.
  This delivers conversion instructions only; no asset modification authorized
  or performed here, and runtime appearance remains the acceptance step.
  User acceptance after converter instructions: the new mapper screenshot shows
  the recognizable bronze blade and wrapped handle; user confirms editor display
  and is checking the game next. Editor appearance accepted for this dagger.
  In-game appearance remains pending; no new implementation or asset writes.
  Subsequent user acceptance: the game now matches the corrected mapper dagger.
  R9 appearance case is accepted for this item after the user's atlas conversion;
  this does not close all-category coverage or the parent task. Related size
  concern: doubling both atlas dimensions quadruples the pixel area and DXT1
  storage at comparable mip settings. A 512x512 atlas of four 256x256 tiles trades
  half the linear source detail for approximately one-quarter storage; preserving
  a single 512x512 texture instead would require a separate model/UV change.
  Explain this tradeoff; no optimization or asset mutation has been requested.
  Handoff requested: user accepts the texture-size tradeoff for now and requests
  a PR with the current changes. Existing [PR #6](https://github.com/Ilufus/ei-mapper-endurance/pull/6)
  is updated and ready for review (not draft), including confirmed dagger gameplay
  acceptance and explicit remaining gaps. All changes are on the contributor branch.
  Next: maintainer review of the contributor branch; no merge/main write and no
  texture-size optimization in this handoff. Parent coverage gaps remain open.
- Related gap EI-ADD-FROM-DB-001-G1: prior offscreen `--ui-widget-check` crashed
  with 0xC0000005; reproduced under the minimal Qt platform. Interactive UI
  acceptance remains unverified, independently of the passing creation checks.
- Related research EI-ADD-FROM-DB-001-R1: named EI safety/reference skills absent
  from listed skills and searched local skill/plugin locations. Research followed
  explicit user authorization for read-only inspection; no private contents or
  provenance may enter the repository, package, or publication.

- Keep this file goal-oriented and concise.
- Put unassigned requests in the wishlist; assign accepted work to a version.
- Keep version blocks in descending order and collapsible.
- Link an approved commit, changelist, test, or validation report for completed
  goals.
- Put alpha-by-alpha narratives, investigations, and rejected approaches in
  release notes, validation reports, or Git history—not in this roadmap.
- Reopen a checked goal only for a concrete regression or missing acceptance
  criterion.
