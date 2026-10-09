# EI-GALLERY-THUMB-CACHE-128 — 2026-10-09

## Результат и границы

Версия128 реализована, Release собран, чистый staging финализирован и постоянная установка обновлена. Полные критерии остаются открытыми: full UI остановлен защитой буфера, scenario содержит два известных отказа песочницы. Нулевая задержка всего UI не доказана: перерисовка после перезапуска исчезла, но в скрытом реальном harness остаются225–268мс заполнения/обработки UI.

База main `53af1978c5137761684ac5565deb36b02813d118`,127→128. AGENTS SHA256 `F4B7FDD5EA82FCDF43D002D23D64EC36BE853650F054EDF70376527CD6432EC3`; CONTRIBUTING, PROJECT_CONTRACT, TEST_BUILD_HANDOFF и связанные102/104/120/88 прочитаны. G4: галерея не сканирует MOB и не сохраняет пользовательский JSON автоматически. G6/null nawa1/nawa2/unmoog не ремонтируется. Навыков evil-islands-modding/private-engine-reference нет; использован обычный код Ataman и разрешённое чтение ресурсов, приватный код не исследовался.

Изменены model_gallery.{cpp,h}, новый gallery_thumbnail_cache.{cpp,h}, model_gallery_check.cpp, два новых native test-файла, VERSION, CHANGELOG, указатели Readme/ROADMAP и отчёт. Основной .pro уже рекурсивно включает cpp/h; менять его не требуется. Другой код только читался. Коммита/сети, веток/worktree/подагентов и интерактивного редактора/игры не было.

## Потокобезопасность и UI

Registry provider под mutex, PreviewSources живёт через QSharedPointer. Инициализация RES catalog и операции двух QCache остаются под sources mutex. После публикации индекс неизменяем; providers/archive читают const-контейнеры. CResArchiveIndex readMember/sourceIsCurrent открывают локальные QFile, общего file handle нет.

MOD/FIG/BON parser, prepareStaticTexture, DXT/DirectDraw/PNT3 decoder, UV и CPU rasterizer используют локальные QByteArray/QDataStream/QImage/сцены. Изменяемого глобального состояния в этой цепочке не найдено; Qt const-таблицы/регулярные выражения не изменяются. Локальные копии переживают QCache eviction. OpenGL upload/getInstance/context/offscreen здесь не вызываются. Чтение отдельных RES, декодирование и рендер вынесены из mutex; другой resource manager/renderer не изменялся. Реальная проверка:32 результата равны последовательным побайтно, overlap4/limit4, включая холодный независимый provider registry.

Отдельный QThreadPool: qBound(2,idealThreadCount,4), максимум N заданий на диалог; dispatch только видимых элементов. GUI обход имеет8мс бюджет между плитками. m_thumbnailJobs сохраняет watcher/ключ до потребления finished; UserRole+2 сравнивается с текущей моделью/текстурой. Старый результат может заполнить свой корректный кэш, но не новую иконку. Workers не захватывают dialog/item/catalog. QObject disconnect прекращает delivery при закрытии; provider/shared state живут до окончания. Закрытие не ждёт future, bounded задачи завершаются самостоятельно. Проверены2–4 overlap, finished-before-notification, texture/filter/scroll/stale и закрытие при N заданиях.

Глобальная QtConcurrent очередь больше не используется для миниатюр. Это не доказывает закрытия full-UI timing120 без полного прогона.

## Дисковый кэш

Путь `<applicationDir>/cache/gallery-thumbnails` рядом с character-*. Используется их installation-relative visualCacheDirectoryForApplication; helper с побочным удалением deprecated character-cache не вызывается. Общий character-кэш/настройки не изменяются.

SHA256 ключ включает namespace ресурсов (упорядоченные absolute paths + размер + mtime всех RES), namespace профиля, case-folded модель/выбранную текстуру, формат1,192×132, ARGB32-LE, фиксированный фон RGB48/51/56 и идентичность framing/rotation/UV алгоритма. Theme не влияет на CPU pixels. Identity берётся при открытии по прежнему стилю: внешняя смена RES требует нового открытия/refresh; открытый snapshot не является live watcher. Намеренное сохранение и размера, и mtime не обнаруживается — граница существующего metadata-договора.

Запись101482 байта: magic, полный ключ, SHA256 payload,192×132 raw pixels. Проверяются точная длина/ключ/checksum до ограниченной аллокации. Повреждение/старая identity → miss → provider. QSaveFile без direct-write fallback делает temp+atomic commit; отказ записи возвращает рендер галерее. Null/Preview unavailable на диск не пишется; прежний session-memory cache сохраняется под полным ключом.

Лимит128 МиБ (около1322 записей). Самые старые по write/успешному read mtime вытесняются только среди hash-named *.thumb своего плоского каталога. Recency best effort: read-only кэш остаётся читаемым. Нет рекурсивной очистки или удаления соседних character-*. Не принадлежащие формату файлы сохраняются. Учёт сканируется при первой записи, превышении лимита и каждых64 записях; внешние параллельные writers могут дать краткий overshoot до пересчёта.

## Измерения на реальном профиле

Одинаковые native hidden harness/геометрия, реальные RES и приватная копия catalog, без записи оригиналов/MOB-сканирования. Эквиваленты доступного профиля:36 моделей unmo* и71 модель stbu*, по8 видимых. Точные вкладки владельца «персонажи/Хадаган» в этом catalog не размечены; его интерактивный жест/видео не воспроизводился. fill_ms — после show до готовых ключей всех видимых плиток; total включает ctor/snapshot identity/первое создание скрытого Qt окна. Точечные замеры, не медианы.

| Эквивалент |127 cold fill / total мс|128 cold fill / total мс|128 disk warm, новый процесс fill / total мс|128 повтор в cold-сессии fill / total мс|
|---|---:|---:|---:|---:|
| unmo,8/8 |350.440 /681.187|298.870 /593.344|267.648 /566.280|225.976 /238.796|
| stbu,8/8 |312.734 /327.416|253.198 /265.027|258.026 /271.390|225.065 /236.268|

Cold128:8 provider calls на вкладку,16 записей. Disk warm в новом процессе:0/0 calls,16 disk reads. Повтор в сессии:0/0. Baseline127 повтор:246.082/247.070мс fill и0/0 calls. Maximum UI gap в128 real harness225–263мс, baseline cold250–273мс. Причина оставшейся задержки не установлена; её устранение и «мгновенность» всего UI не заявляются. Холодное улучшение умеренное; постоянство подтверждено отсутствием перерисовки после рестарта.

Доказательства: build-tests-128-gallery/baseline-retry-visible-metric, measure-retry-slice-cold, measure-retry-slice-warm-process. Ранние baseline/1/2 завершилисьexit3 из-за отсутствующих выбранных разделов/FIG-only models в старом MOD-only списке. Это сбои harness, не PASS; окончательный stbu-эквивалент существует, данные до/после одинаковы.

## Проверки

| Проверка | PASS | FAIL | Граница |
|---|---:|---:|---|
| Native cache QtTest |7|0|5 методов + init/cleanup,0 SKIP; identity/format, corrupt, atomic/concurrent16, failed write/null, LRU/изоляция|
| model_gallery_check |1|0|функциональная группа, прежний300мс; selection/Shift/multi-job/stale/close|
| assembly-catalog-check |91|0|143/1451, Esc/commit/save/Undo/Redo/IDs, один tree refresh|
| dark-theme конечного EXE |70|0|hidden native widgets|
| Real provider exact pixels |1 группа/32 картинки|0|overlap4/limit4; provider implementation после этого не менялся|
| Real RES copy/stored identity |4 группы|0|size+mtime, same-size content/mtime, stable format, profile/texture|
| Corrupt record, новый процесс |1|0|ровно1 provider call, exact original record восстановлен|
| --version128 |1|0|VERSION/raw/staged совпадают|
| scenario конечного EXE |48|2|test backup LOCALAPPDATA/qttest; повтор обязателен|
| Full UI/image fixture |0 выполнено|0 assertions выполнено|guard exit2 до child: native clipboard49470; BLOCKED, не PASS|

Gallery:497 моделей/7165 текстур, open7ms/select3ms, Shift143=2ms/entries0ms, close при600ms worker1ms. Batch: preview953ms/1 tree; commit18ms/cancel29ms/Undo after save110ms/Redo73ms; family-aware143 preview1142ms. Дедлайн300мс не увеличен и passed isolated-gallery. Full UI timing120 открыт. Guard не подтверждает image-regress/восстановление картинки в этом запуске, но остановился до любых записей. Пользователю предложено безопасно подготовить clipboard; ответа о готовности не получено.

Native initial6/1 выявил отсутствие recency-update через ReadOnly Windows handle; исправлен ReadWrite timestamp touch, итог7/0. Два ранних gallery-прогона упали на reopening/resource-generation. Первый выявил необходимость дождаться доставки bounded scroll jobs при измерении нового окна. Причина следующего deadline не доказана: убраны full directory walks на каждой записи, добавлена диагностика; финальный gallery проходит без ослабления300мс. Промежуточные FAIL сохранены.

Evidence directories: native-retry-accounting, gallery-retry-slice, batch, dark-theme-retry-final-artifact, provider, invalidation-retry-mtime, measure-retry-corruption + corruption-result.json, scenario-retry-final-artifact, ui, version-retry-final-artifact. Они находятся в build-tests-128-gallery.

Runner build-tests-128-gallery/run-check.py имеет native/ui/scenario/dark-theme/version и gallery/batch/provider/invalidation/measure. UI использует guard127: сначала сохраняет пользовательский буфер, затем делает CF_DIB/CF_BITMAP fixture, в конце проверяет восстановление. Refusal не пишет в буфер. Для повторов -retry-<name>; результаты не перезаписываются. Timeout180s записываетсяexit124/FAIL.

## Сборка и установка

Fresh shadow build-release-128-gallery; retained Qt5.12.12/MinGW7.3 и свежие x86 bridge/host собраны wrapper. Зависимости не устанавливались. Fresh build-staging-128-gallery-handoff создан из127 verified managed files предыдущего clean-stage с текущими EXE/runtime/VERSION/Readme/changelist. Raw/staged EXE и оба runtime совпали, staged --version128 проверен до finalizer. Finalizer/internal validator PASS,127 файлов; finalized stage больше не запускался.

Постоянная установка artifacts/EI-Mapper-Ataman-2-first-test, VERSION128. No-owner-process guards, old hashes и backup verified;6 программных замен атомарны, manifest последним. Все21211 unlisted user files byte-identical, включая cache/settings/maps. Новый кэш установщик не создавал. Verified rollback artifacts/rollback-EI-Mapper-Ataman-v127-to-128-20261009 сохранён (7 файлов); старый owned v126-to-127 идентифицирован provenance/manifest/7 hashes и удалён только после успешной проверки128. Другие установки/игра/мод/Endurance не менялись.

EXE SHA256: `A1EF2A218918197D2079CB3EFCE8EB389760087BF6FF722DA79789E9C84B7B95`.
Manifest SHA256: `ADC34F993558E57A20742AD43881C3BDCDAC06401B9E247E64C722187A66E29C`.
Чужой QM после каждой сборки восстановлен: `AB06B0CC2247832C97FC5DCD654E32854C2712ADD23EC17378E451F743C7F1E3`. Чужой текст ROADMAP сохранён точно относительно ROADMAP-before.bin. Evidence: install-result/plan, user-files-before/after, staging-result, finalize.log, retirement-result, release-build.json/out/err.

## Открытая передача

Scoped-патч build-tests-128-gallery/scoped-review.patch:12 своих путей, только свои строки ROADMAP относительно HEAD; .git/индекс не записывались. Коммит делает сопровождающий в main. Сети/ZIP не было.

Следующие gates: full UI при безопасно сохранённой image fixture; scenario вне ограничения LOCALAPPDATA на этом EXE; scoped-коммит main сопровождающим; затем владелец проверяет реальные вкладки/restart/texture/scroll/close/143 на своей карте. До них128 и родительские102/104/120 не объявляются полностью закрытыми.

## Дополнение сопровождающего (Claude), 2026-10-09

- Вне песочницы Codex тем же runner на конечном EXE SHA256
  `A1EF2A218918197D2079CB3EFCE8EB389760087BF6FF722DA79789E9C84B7B95`:
  full `--ui-widget-check` под guard изображения **171 PASS / 0 FAIL**, буфер
  владельца восстановлен; `--scenario-layer-check` **50 PASS / 0 FAIL**.
  Каталоги `build-tests-128-gallery/ui-retry-claude/`, `scenario-retry-claude/`.
- Ревью: CResArchiveIndex::readRange открывает собственный QFile, mutable
  состояния нет; общие QCache и инициализация индекса под mutex; устаревшие
  результаты применяются только при совпадении текущего ключа элемента.
- Установленный EXE совпадает с конечным; QM владельца побайтно сохранён.
- Scoped-коммит main выполнен сопровождающим; чужие изменения не включены.
