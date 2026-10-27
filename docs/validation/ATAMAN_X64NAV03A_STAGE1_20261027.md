# X64NAV03A — этап 1, перепроверка r13, 2026-10-27

**VERIFIED / PREPARED 227+x64nav.2. Блокер высот r12 снят на проверенных r13-данных.**
Без установки, push и merge. Интерактивная/игровая приёмка владельца отдельно.

## Идентичность и происхождение

- Только rebuild-x64, worktree `C:\GOG Games\maped\ei-mapper-ataman-rebuild-x64`.
  База main `adfcad6dd64c594fa183b563f9aa6eba1fea14d8`, установка227 сохранена.
- Источник финального Release: **88e74690e14be43d05f83ea8a971fbec6bedb18a**.
  Итоговый commit отчёта/ROADMAP не меняет код собранной программы.
- EIRebuild64 task/X64NAV01: **918cb6ff578bf09de4c28745c1cf17a37530a802**.
  Получены через git archive именно этого SHA, без использования dirty tree.
- Из19 vendor-файлов изменились только Navigation/Types.h и README.md;
  Detour1.6.0 (9 headers/7 cpp) и zlib License.txt остались побайтно прежними.
  Все19 копий сверены с Git blobs нового SHA, lock обновлён. Разрешение:
  «собственный код владельца, разрешение на перенос 2026-10-27».
  Types.h изменяет константу алгоритма, README — контракт; признаков приватного
  кода движка нет. Build.cpp/Geometry/Query/Recast не переносились.
- Vendor README оставлен неизменным: в старом абзаце Container3 сохранилась
  строка algorithm7. Действуют пользовательское поручение, Types.h, верхний
  раздел r13 и фактические заголовки файлов: algorithm8.

## Изменения маппера

- Поддерживается container3/algorithm8; алгоритм7 отклоняется с явным сообщением
  «устаревшая сетка — игра не примет, перепеките». Списки версий в одном модуле.
  LZMS/unshuffle, Detour ABI, embedded MPR/MOB/profile и ограничения прежние.
- Диагностика высот использует допуск r13: две вертикальные ячейки, обычно0,1м.
  Геометрия не зажимается/не исправляется. Проверяется и фактическая геометрия
  отрисовки относительно границ тайлов, включая polygon/detail vertices.
- PROFILE height_outliers показан отдельной строкой панели. Ненулевое либо
  отсутствующее значение предупреждает и не позволяет считать bake успешным.
  Успех по-прежнему требует exit0 и BAKE_DONE completed=maps/missing0/errors0.
- По умолчанию заливка **33%** с контурами. Переключатель «Заливка33% + контуры /
  Только контуры» меняет только отрисовку, без повторного чтения контейнера.
  Контуры без заливки контрастные зелёные; границы тайлов голубые.
- Сохранены8 профилей, старый AI-граф, острова/приблизительные узкие порталы,
  тайловое отсечение, фоновый baker/отмена/бэкапы. Cache64/Nav не используется.
  MPR/MOB и навигационные файлы слой не изменяет.

## Финальный прогон

**160 PASS,0 FAIL, exit0,70,690с**, production objects архивного Release.
[stdout](<C:/GOG Games/maped/ei-mapper-ataman-rebuild-x64/build-x64nav-r13/evidence/stdout.txt>),
[process](<C:/GOG Games/maped/ei-mapper-ataman-rebuild-x64/build-x64nav-r13/evidence/process.json>),
[сохранность](<C:/GOG Games/maped/ei-mapper-ataman-rebuild-x64/build-x64nav-r13/evidence/preservation.json>).

- r13: zone1/zone1 у ворот Посёлка (пилот X172–201/Y103–125), bz19g/bz19g,
  zone14/zone14:24 файла, все8 профилей на каждой карте. На всех24 диагностика0,
  треугольники в высотных границах; на снимках tiny/levitate_tiny «столбов» нет.
- Проверены: имена из embedded identity при переименовании файла; неверная
  карта; устаревшие algorithm7/container2; неизвестный алгоритм; усечённый файл;
  Base fallback; оба режима заливки с реальным изменением framebuffer.
- Старый AI-граф доступен на3 картах. Регресс227: zone7 + Autozone7letter ->
  zone6 -> zone7; дополнительный MOB корректно отсоединён. MPR/MOB байты сохранены.
- Настоящий r13 baker SHA256 перед запуском совпал:
  `85df0ef34432bfc3164cb83067aa9afd1153fdf96ae39e44b61b636ad6dc5ad7`.
  Через ту же startBake, что вызывает кнопка панели: bz19g, приватные
  game-root/output-root. Exit0 + BAKE_DONE maps1/completed1/missing0/errors0,
  918мс,8 файлов,578233 байта. Все8 PROFILE содержали height_outliers=0,
  поле отображено, все выходы повторно прочитаны и слой обновлён из результата.
- Реальные ошибки MAP_MISSING/exit5 и MAP_ERROR/exit4, неверный путь EXE,
  отмена, неполный журнал, ненулевой exit и ненулевой height_outliers проверены.
  Последние защитные сценарии и --all использовали тестовый процесс; всю
  кампанию заново не запекали. Штатные бэкапы:67 payload SHA совпали с метаданными.
  [Бэкапы](<C:/GOG Games/maped/ei-mapper-ataman-rebuild-x64/build-x64nav-r13/evidence/backup-receipt.json>),
  [выходы](<C:/GOG Games/maped/ei-mapper-ataman-rebuild-x64/build-x64nav-r13/evidence/baked-nav64-manifest.json>),
  [журнал](<C:/GOG Games/maped/ei-mapper-ataman-rebuild-x64/build-x64nav-r13/evidence/baker-log.txt>).

### Выбросы: до / после

| Карта | r12 tiny / levitate_tiny | r13 tiny / levitate_tiny |
|---|---:|---:|
| zone1 |9 /19|0 /0|
| bz19g |7 /10|0 /0|
| zone14 |12 /35|0 /0|

Остальные6 профилей на всех3 картах также имеют0. Ранее исходные r12-вершины
zone1 имели высоту3273.21167м; исходное подтверждение сохранено в истории и
[независимой квитанции r12](<C:/GOG Games/maped/ei-mapper-ataman-rebuild-x64/build-x64nav-final-r2/evidence/independent-geometry-receipt.txt>).
Маппер не маскирует дефект геометрии: нулевой результат получен на новых r13.

### Снимки

- Zone1: [заливка33% рядом со старым AI](<C:/GOG Games/maped/ei-mapper-ataman-rebuild-x64/build-x64nav-r13/evidence/screenshots/zone1-navmesh-ai.png>),
  [только контуры рядом с AI](<C:/GOG Games/maped/ei-mapper-ataman-rebuild-x64/build-x64nav-r13/evidence/screenshots/zone1-contours-ai.png>),
  [все8 профилей](<C:/GOG Games/maped/ei-mapper-ataman-rebuild-x64/build-x64nav-r13/evidence/screenshots/zone1-eight-profiles.png>).
- [bz19g / AI](<C:/GOG Games/maped/ei-mapper-ataman-rebuild-x64/build-x64nav-r13/evidence/screenshots/bz19g-navmesh-ai.png>),
  [все8 bz19g](<C:/GOG Games/maped/ei-mapper-ataman-rebuild-x64/build-x64nav-r13/evidence/screenshots/bz19g-eight-profiles.png>).
- [zone14 / AI](<C:/GOG Games/maped/ei-mapper-ataman-rebuild-x64/build-x64nav-r13/evidence/screenshots/zone14-navmesh-ai.png>),
  [все8 zone14](<C:/GOG Games/maped/ei-mapper-ataman-rebuild-x64/build-x64nav-r13/evidence/screenshots/zone14-eight-profiles.png>).

Zone1: CPU чтение+mesh+геометрия20–31мс; готовность в GUI33–1174мс с очередью
подготовки сцены. Кадр1100×820 с заливкой:64/168 тайлов,112865 vertices;
20 измерений после3 прогревочных, paintGL+GPU finish median8,3287мс,
p9510,5147мс. Это полный кадр; GL errors0, обещания постоянной latency нет.

## Release / передача

Свежий Release из git archive коммита88e74690e14be43d05f83ea8a971fbec6bedb18a,
Qt5.12.12/MinGW7.3 x64.2176 inputs побайтно совпали с Git, кроме генерируемых QM.
[Источник](<C:/GOG Games/maped/ei-mapper-ataman-rebuild-x64/build-x64nav-r13/source-identity.json>),
[inputs](<C:/GOG Games/maped/ei-mapper-ataman-rebuild-x64/build-x64nav-r13/archive-input-receipt.json>).

**Staging:** `C:\GOG Games\maped\ei-mapper-ataman-rebuild-x64\build-x64nav-r13\build-staging-227-x64nav2`.
Raw/staged --version: EI Mapper Endurance Edition 227+x64nav.2;
4 SHA EXE/MCP/runtime совпали. Финализатор PASS против установленной227:
**174 ->176 managed,0 потерь**; новые только docs/NAVMESH.md и licenses/Detour-zlib.txt.
cabinet.dll системная и не включена; игровой контент и запекатель не включены.
Manifest SHA256 `788BC1926C4480C1C8121F6389F552B160C04406B01508A0F47A87CBFDA2D9E6`.
[Состав](<C:/GOG Games/maped/ei-mapper-ataman-rebuild-x64/build-x64nav-r13/composition.json>),
[бинарники](<C:/GOG Games/maped/ei-mapper-ataman-rebuild-x64/build-x64nav-r13/staging-identity.json>),
[финализатор](<C:/GOG Games/maped/ei-mapper-ataman-rebuild-x64/build-x64nav-r13/finalize.log>).
После финализатора пакет не запускался. Установка/push/merge не выполнялись.

## Сохранность и границы

Тестовая копия с тяжёлыми read-only hardlinks удалена после PASS, сохранены
квитанции и7 PNG: [cleanup](<C:/GOG Games/maped/ei-mapper-ataman-rebuild-x64/build-x64nav-r13/cleanup-receipt.json>).
Живые Maps/Nav64/Cache64, установленная227, AppData/настройки/процессы владельца,
буфер и приоритеты не изменялись. Основной checkout остаётся main, его чужие
изменения сохранены. В EIRebuild64 записана только запрошенная копия REPORT.md.

Технический блокер r12 на проверенных данных закрыт. Игровой проход/позы/
физическая ширина ворот, интерактивная приёмка владельца, полная кампания49 карт
и авторинг .navsrc в этот прогон не входят. Актуальность по mtime приблизительна,
полный fingerprint не вычисляется. Следующий шаг — решение владельца о приёмке;
установка и дальнейшие этапы требуют его отдельного поручения.