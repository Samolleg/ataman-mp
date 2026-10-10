# EI-PUBLICATION-RES-READER-DRAIN — чтение и атомарная публикация RES

**Актуальный итог:** после расширения владения применён реальный `finishThumbnail`.
Reader-drain21/0, отдельный delivery3/0, full UI gate PASS (219 итоговых условий,
clipboard восстановлен), scenario50/0, dark70/0, save12229/0, gallery PASS.
Результат подготовлен к интеграции сопровождающим. Раздел «Финализация после
расширения владения» ниже имеет приоритет над промежуточными статусами.

Дата: 2026-10-10. Далее до раздела «Ожидание читателей» сохранены материалы первой
итерации, включая тогдашний незакрытый UI gate и известное несовпадение теста политики.

## База и границы

- Поручение: main `df91a69413b0c924e06bbfe2126b9e4d31c307c2`, VERSION131.
  AGENTS.md проверен: SHA256
  `F4B7FDD5EA82FCDF43D002D23D64EC36BE853650F054EDF70376527CD6432EC3`.
- Во время работы main продвинулся до
  `7515a3fb4d30f3601d700ea7ebbafbbcfec6d71c`: независимый terrain-recipe core
  и его ROADMAP/evidence. Эти файлы сохранены; пересечения владения нет.
  Финальная сборка содержит и этот самостоятельный модуль. VERSION остаётся131;
  `133-resdrain` — имя назначенного рабочего каталога, не новая версия продукта.
- Git использовался только для чтения. Коммита, изменения VERSION/CHANGELOG/
  ROADMAP/Readme/translations, сети, установки зависимостей и подагентов не было.
  Постоянная установка, Endurance и живые игровые данные не изменялись.
- Использован установленный Qt5.12.12/MinGW7.3 из toolchain Endurance только для
  чтения. qmake/make запускались в собственных build-каталогах; release-wrapper
  не применялся, поскольку он записывает translations/ei_mapper_ru.qm.
  Это raw build для проверок, не комплект для установки/распространения.
- Приватные engine-reference skills в сеансе отсутствуют; приватный код не
  исследовался. Изменения относятся к обычному коду Qt и синтетическим данным.

## Причина и исправление

Аудит77 подтвердил, что QFile(ReadOnly) другого потока препятствует замене через
QSaveFile в Windows. В resolver файл дополнительно оставался открыт при
декодировании/хешировании атласа и при вложенном resolver embedded-profile.
Конкретный держатель исторического zone18 отказа77 по-прежнему не доказан.

1. `terrain_atlas_archive_binding.cpp`: index/page/profile I/O берёт read lease;
   QFile и lease освобождаются после нужных байт, до decode/hash и вложенного
   exact resolver. Size/mtime/pixel binding сохранены; дополнительно проверяется
   поколение публикации после CPU-обработки.
2. `res_read_lease.h`: общий RAII gate по canonical path. Admission новых
   читателей при ожидающем/активном writer отказывает без ожидания GUI.
   Ранее допущенные вложенные reads и writer-owned validation разрешены;
   upgrade собственного reader в writer и рекурсивный writer отклоняются.
   Writer ждёт читателей через QWaitCondition максимум5000мс. Mutex защищает
   только состояние admission; canonical-path I/O и ожидание не держат его.
   Header-only реализация сохраняет совместимость существующих standalone
   qmake test targets без правок их списков sources.
3. `CResArchiveIndex`: lease охватывает индексирование, source check, range read
   и чтение transaction source. Index с прежним поколением требует переиндексации.
   Это общий I/O путь production provider галереи и индекса/preview браузера.
4. `resourceThumbnails`: каталог копируется перед отпусканием cache mutex;
   поколение проверяется до записи cache и выдачи картинки. Изменившееся
   поколение очищает scene/texture cache и переиндексирует sources при следующем
   запросе. Browser отклоняет запоздавшие результаты index/preview.
5. `commitMapSaveBatch`: publication leases берутся в одинаковом порядке до
   before-check и остаются до verification/rollback. Совокупный бюджет drain
   всех путей —5000мс. Leases отпускаются до checkpoint callback78.
   Missing/open/read/mismatch имеют отдельные причины; mismatch содержит длины
   и SHA256. В ошибке есть phase; результат содержит failedPath, committedPaths,
   число commit attempts и прежний rollbackComplete.
6. Все три RES coordinator paths берут destination publication lease до
   final race-check/backup/commit и держат до проверки. Результат отдельно
   сообщает `committed`, даже при неудачной post-commit verification.
   View выводит phase/errorString, а для такого случая — факт commit, требование
   reopen и backup, без утверждения «archive was not replaced».
7. После failed QSaveFile commit отдельный native CreateFileW probe проверяет
   DELETE-sharing. Это самостоятельный probe, не поздний GetLastError от Qt.
   Только коды32/33 разрешают максимум3 попытки с паузами50мс. Перед каждой
   повторной staging/commit снова сверяются before-байты; failed QSaveFile
   не используется повторно. Successful retry очищает предыдущую ошибку.
   Backup/recovery journal/byte-ownership rollback сохранены. Rollback существующих
   файлов использует тот же guarded retry. Direct-write fallback отключён;
   замена существующего файла не использует delete+rename.

Синхронные batch callers могут ждать drain до5с; общий GUI/cache mutex не
удерживается, release читателей не зависит от доставки GUI events. Уже
существующий coordinator publication в View выполняется через QtConcurrent.

## Проверки и точные границы результатов

Все UI/scenario/gallery/dark-theme/clipboard проверки выполнялись под
`build-locks/ui-tests.lock` с owner.txt и finally-release. Lock после проверок
отсутствует. Редактор/игра интерактивно не открывались.

| Проверка | Результат | Evidence в build-tests-133-resdrain |
|---|---|---|
| Финальный raw release build | PASS, VERSION131 | ../build-release-133-resdrain/build.log |
| Новый RES drain target | **15 PASS,0 FAIL,0 SKIP**, exit0 | results-drain.txt |
| Собственный читатель + checkpoint78 | wait191мс;1 commit;1 callback после release | results-drain.txt |
| Production concurrency | **4/4 cold thumbnail consumers +1/1 embedded-profile hydration**, drain185мс | results-drain.txt |
| RES/checkpoint regressions | **27 PASS,1 FAIL,0 SKIP**, exit1 | results-res-78-122.txt |
| Контроль прежней batch реализации | **2 PASS,1 FAIL,0 SKIP** на том же policy assertion799 | results-baseline.txt |
| Resource-index/transaction regressions | **22 PASS,0 FAIL,2 SKIP**, exit0 | results-resource-core.txt |
| Scenario | **50 PASS,0 FAIL**, exit0 | scenario-layer-check.stdout.log |
| Gallery | suite PASS,exit0;497 models/7165 textures; close during600мс job=1мс | model-gallery-check.stdout.log |
| Dark theme | **70 PASS,0 FAIL**, exit0 | dark-theme-check.stdout.log |
| Production save122 | **29 PASS,0 FAIL**, exit0 | terrain-height-res-save-check.stdout.log |
| Полный native --ui-widget-check | **ABORT,exit2;не PASS** до synthetic writes | ui-widget-check.stderr.log |
| Whitespace diff | PASS | source-receipt.json |

Новый target проверяет physical QFile holder, bounded timeout40мс, admission
другого потока и независимый архив, внешний QFile holder с3 неудачными commits
без потери before/backup, native exclusive open, native byte-range read failure,
missing/mismatch без retry, transient2-attempt commit с fresh staging,
before-mismatch на повторе, rollback первого файла при отказе второго, повтор
после rollback, отказ rollback при чужих байтах с retained recovery journal,
поколение старого index и настоящий RES publication coordinator.

Concurrency test намеренно удерживает5 admitted readers при старте сохранения,
затем вызывает настоящий `resourceThumbnails` для4 разных cold model/texture
пар и настоящий `findEmbeddedArchive`/nested `findExactArchive`. Это воспроизведение
коллизии на синтетическом архиве, не измерение частоты её появления на живых
картах. Warm provider, cold consumers и rebuild после publication проверены.

Прежний FAIL: `tests/res_archive_document_tests.cpp:799` ожидает PublishModernAtlas
для persistedWorkingDraft=true,saveAs=false. Неизменённая политика
`source/core/terrain_save_flow_policy.h` базы131 возвращает PlainMpr. Этот assertion
расположен после всех byte/rollback/published-member checks, которые прошли.
Тот же FAIL воспроизведён с batch/coordinator cpp из принятой базы: для линковки
baseline batch cpp использует текущий result header с дополнительными полями,
без изменения baseline алгоритма. Первоначальная попытка baseline harness с
несогласованным ABI аварийно завершилась; она не считается PASS.
Исправление чужого теста/политики не применялось. Сопровождающему нужно согласовать
expectation799 с принятой политикой; строка800 для saveAs=true не меняется этим выводом.

2 SKIP resource-core — отсутствуют настройки внешних read-only model probe и
representative archives; они не засчитаны в PASS. Synthetic transaction/index
coverage прошла. Первые сбои собственного harness были исправлены: QtConcurrent
мог выполнить ожидаемую задачу в caller thread; задан неверный момент release
retry holder; native process/file logging требовало корректного quoting.
Последний новый target полностью проходит, условия проверок не ослаблялись.

UI gate: native clipboard содержит CF_BITMAP (format2), который встроенная
защита не может сохранить/восстановить: `Native clipboard format 2 has no
preservable synthesis source; check aborted before writes.` Буфер не очищался,
защита не обходилась. Запрошено сообщение владельца после копирования обычного
текста; без изменения предпосылки повтор gate не имеет смысла. Полный UI-regress
и ручная приёмка исторической проблемы77/78 остаются открытыми.

## Выявленный побочный эффект штатных проверок

Штатные regressions создают16 backup payloads синтетических файлов в стандартных
`C:/Users/samol/AppData/Local/EI Mapper Endurance/backups` и
`C:/Users/samol/AppData/Local/qttest/EI Mapper Endurance/backups`, вне назначенного
каталога. Это ошибка изоляции запуска. Источники — собственные temporary fixtures;
существующие пользовательские backup/карты не заменялись. Точные созданные пути
и журналы происхождения: `build-tests-133-resdrain/external-test-backups.json`.
При итоговом readback остаются14 payloads и14 metadata JSON;2 payloads уже
отсутствуют, причина этого отдельно не исследовалась. Receipt существования:
`build-tests-133-resdrain/external-backup-receipt.json`. Агент вне владения
эти файлы не удалял. Новый RES-drain target передаёт явный backup
root и держит все fixtures/backups внутри своего дерева. Для строгой изоляции
будущих штатных native checks нужен test backup-root override в общей save/test
infrastructure; соответствующие файлы вне владения, правка не применена.

## Артефакты и интеграция

- EXE: `build-release-133-resdrain/release/EIMapperEndurance.exe`, SHA256
  `8916C49926C0F403B297CD1668BD9034ABB517E3FB9EFC3ABC9C733BDCB2DB83`.
- Новый target: `tests/res_reader_drain_tests.pro` линкует точные production
  release objects без main.o; PRE_TARGETDEPS не допускает stale relink.
  Сначала нужен production release, затем qmake/make этого target.
  Регистрация в main.cpp не нужна; main.cpp не изменялся.
- Повтор UI gate: собственный `run-ui.ps1 -Only ui-widget-check` после смены
  clipboard prerequisite; он захватывает общий lock. Для save122 runner передаёт
  EI_TEST_TERRAIN_FIXTURE на собственную копию distributable
  `tests/fixtures/runtime/particle-flat-7x5.mpr` (source/copy SHA256 совпадают).
- Живые архивы, карты и установки не использованы для записи; art/game acceptance
  этим отчётом не подтверждается. Lease охватывает назначенные background paths,
  не чужие процессы и не все legacy scanners/прочие writers программы.
  Если потребуется распространить gate на resourcemanager/assembly_inventory
  или отдельные View rollback paths, это изменение вне текущего владения.
- Сопровождающий назначает версию и коммит; отдельно добавляет переводы новых
  View/Browser `tr()` сообщений о committed-but-unverified и stale index/preview.

### ТОЧНЫЙ список файлов для коммита (15)

```text
source/documents/map_save_batch.cpp
source/documents/map_save_batch.h
source/documents/res_save_coordinator.cpp
source/documents/res_save_coordinator.h
source/render/terrain_atlas_archive_binding.cpp
source/render/view.cpp
source/resources/res_archive_index.cpp
source/resources/res_archive_index.h
source/resources/res_read_lease.h
source/ui/model_gallery.cpp
source/ui/resource_browser.cpp
source/ui/resource_browser.h
tests/res_reader_drain_tests.cpp
tests/res_reader_drain_tests.pro
docs/validation/ATAMAN_RES_READER_DRAIN_20261010.md
```

Build-каталоги, логи, snapshot источников, receipt и игровые/синтетические
бинарные данные в коммит не включать. Чужой terrain-recipe core не входит в список.

### Текст ROADMAP (вносит сопровождающий)

```markdown
## EI-PUBLICATION-RES-READER-DRAIN — осушение читателей перед публикацией RES
- Поручение 2026-10-10: реализация по аудиту77, база df91a69413b0c924e06bbfe2126b9e4d31c307c2/VERSION131; только назначенное владение, без Git-записи/установки/изменения живых данных. Сделаны короткий QFile scope, общий per-path reader/publication lease с drain5000мс, guarded retry3× с fresh staging/before-check, точная I/O phase diagnostics и committed-but-unverified UI; checkpoint/backup/recovery/ownership rollback78 сохранены. Независимый main7515a3fb4d30f3601d700ea7ebbafbbcfec6d71c сохранён.
- Проверки: новый target15 PASS/0 FAIL/0 SKIP (4 thumbnails+1 hydration, wait191/185мс), scenario50 PASS, save12229 PASS, dark-theme70 PASS, gallery PASS; resource-core22 PASS/2 SKIP, RES27 PASS/1 прежний policy FAIL799, подтверждён baseline. Полный native UI gate ABORT до writes из-за непереносимого clipboard CF_BITMAP. Ошибка изоляции штатных checks:16 синтетических backup payloads в стандартном AppData, пути в evidence. Реализация подготовлена, полный gate не закрыт. Evidence: docs/validation/ATAMAN_RES_READER_DRAIN_20261010.md. Следующий шаг: повтор native UI после смены clipboard prerequisite, согласование expectation799, test backup-root isolation вне владения, назначение версии/переводы/scoped-коммит сопровождающим; ручная приёмка77/78 остаётся открытой.
```

### Текст CHANGELOG (версию назначает сопровождающий)

```markdown
- EI-PUBLICATION-RES-READER-DRAIN (2026-10-10): RES publication ждёт собственных фоновых читателей конкретного архива, закрывает admission новым reads и освобождает atlas QFile до decode/hash. Sharing retry ограничен3 попытками с новой QSaveFile staging и повторным before-check; backup/recovery/checkpoint78 и guarded rollback сохранены. Ошибки различают missing/open/read/mismatch/commit, UI сообщает committed-but-unverified. Новый target15 PASS; scenario50, save12229, dark-theme70 и gallery PASS. Полный UI gate пока ABORT из-за CF_BITMAP; прежний RES policy assertion799 и test backup-root isolation переданы сопровождающему. Evidence: docs/validation/ATAMAN_RES_READER_DRAIN_20261010.md.
```

## Ожидание читателей

Продолжение 2026-10-10 по замечанию сопровождающего. Причина замечания подтверждена
кодом: первая итерация немедленно отклоняла нового читателя при publication/waiting
writer. Resolver отвечал `continue`, а gallery provider мог построить отсутствующую
сцену/текстуру из пустого payload; `finishThumbnail` кэшировал даже null в двух
кэшах памяти и помечал item завершённым. Штатный disk writer уже отказывается
сохранять null, однако это не устраняло кэширование неуспешного результата в памяти.

### Применено в назначенном владении

- `ReadLease` ждёт фонового читателя через `QWaitCondition` с общей верхней
  границей 10000 мс; mutex реестра освобождается на время ожидания. При таймауте
  вызывающий получает `reader-admission timeout`, путь и длительность. Приоритет
  ожидающего писателя сохранён; его drain остаётся 5000 мс. Вложенные чтения уже
  допущенного потока и verification текущего писателя не ждут.
- Новый `ReadLeases` допускает snapshot нескольких архивов в порядке canonical
  path с общим admission budget 10000 мс. Gallery берёт эти leases **до** cache
  mutex, после ожидания фиксирует актуальную generation, перестраивает index и
  читает payload. Ошибки I/O передаются через `resIo::ReadFailure` (`QException`),
  пустая/неуспешная сцена и null texture в provider-кэши не записываются. Перед
  финальной CPU rasterization leases освобождаются; изменение generation во время
  render даёт явную ошибку повторного запроса, а не null.
- Оба atlas resolver возвращают ошибку допуска вызывающему, не пропуская архив.
  Вложенная ошибка exact binding также возвращается embedded resolver напрямую.
  QFile по-прежнему закрывается перед decode/hash.
- Browser indexing получает admission перед capture generation/build. Preview
  ждёт, проверяет private snapshot и при изменении архива пересчитывает индекс и
  находит выбранный member **по имени**, не использует старые offsets. Смена
  companion archive также вызывает reindex. GUI delivery slots проверяют
  `isFinished()`: queued сигнал предыдущего future не заставит ждать новую задачу.

### Потоки и отсутствие взаимной блокировки

`view_map_save.cpp::commitMapCohortMpr` / `saveMapCompanions` и
`view_world_terrain.cpp` вызывают `commitMapSaveBatch` синхронно из View/GUI.
Публикация через `view.cpp` около `commitStagedResPublishedMembers` идёт в
`QtConcurrent::run` с отдельным ожиданием события в GUI. Поэтому новое чтение
из потока `QCoreApplication::instance()->thread()` при чужой публикации или
ожидающем писателе **никогда не ждёт**: получает
`reader-admission: GUI read cannot wait for publication`. Writer-owned GUI
verification остаётся разрешённой. Фоновые hydration/provider/browser работают
в workers. Общего I/O mutex или ожидания GUI со стороны background jobs нет.
Источник этих thread-paths изучен только для чтения; перечисленные файлы View вне
владения не изменялись. Новая синтетика проверяет и GUI отказ, и ожидание workers
при GUI-owned publication.

### История согласования участка (закрыто при финализации)

Первоначальное поручение ограничивало `source/ui/model_gallery.cpp` словами
«только provider resourceThumbnails». Для полноценного результата нужна также
минимальная правка `finishThumbnail` в этом же файле. На промежуточном этапе она
не применялась к основному исходнику; последним поручением сопровождающего
владение расширено, diff применён по смыслу и проверен на реальном файле.

Подготовлен конкретный diff:
`build-tests-133-resdrain/finish-thumbnail-review.patch`.
Он ловит `ReadFailure`/`QException`, освобождает watcher/job, показывает причину
в tooltip/status, не вставляет null в локальный/общий cache, не ставит завершённый
item key при временной ошибке и повторяет запрос при следующем resize/visibility/
selection request. Существующий worker после exception не достигает disk write;
сам disk writer null не сохраняет. Успешный результат кэшируется штатно.

Первичная проверка diff выполнена **на собственной копии**
`build-tests-133-resdrain/model_gallery_delivery_review.cpp`, без изменения
основного `finishThumbnail`. Копия отличается только этим методом. Review target
подключался через `RESDRAIN_REVIEW_DELIVERY=1`. После расширения владения этот
режим удалён из .pro, а тест включён в обычный target с реальным production
`model_gallery.o`; необработанная exception в GUI delivery устранена.
Регистрация в main.cpp не нужна: проверки используют отдельный target.

### Проверки продолжения

Все UI/scenario/gallery/dark-theme/save проверки захватывали общий
`build-locks/ui-tests.lock`, записывали owner.txt и освобождали lock в finally.
На завершении lock отсутствует. Полный `--ui-widget-check` **не запускался** по
прямому указанию владельца из-за текущего скриншота в clipboard и параллельной
задачи guard. Результаты параллельной задачи не выдаются за наши проверки.

| Проверка | Результат | Что подтверждено |
|---|---|---|
| reader-drain, production objects | 20 PASS / 0 FAIL / 0 SKIP | Прежние 15 checks, новые wait/timeout/GUI/resolver/browser cases |
| Свой уже открытый читатель | ожидание 181 мс, 1 commit, 1 checkpoint | Закрытие reader перед заменой и освобождение lease перед checkpoint |
| Уже допущенные реальные consumers | 4/4 thumbnail + 1/1 hydration, drain 196 мс | Вложенные reads не взаимно блокируются с ожидающим writer |
| Новые consumers после начала batch publication | 5/5 ждут; 4/4 thumbnail + 1/1 hydration; 5/5 читают новые bytes | Сохранение не теряет результат фоновых запросов |
| Реальный provider admission timeout | 10012 мс; явная exception; тот же provider при повторе успешен | Ограниченный timeout 10000 мс и восстановление чтения |
| Подготовленный gallery delivery diff | 3 PASS / 0 FAIL / 0 SKIP, 305 мс | 1 функциональный widget test + init/cleanup; ошибка явная, memory/disk cache пусты, следующий запрос и успешный cache работают |
| scenario | 50 PASS / 0 FAIL | Связанный document/UI regression |
| gallery | PASS | 497 models / 7165 textures; bounded jobs, stale results, close during worker |
| dark-theme | 70 PASS / 0 FAIL | Контраст текста |
| save122 | 29 PASS / 0 FAIL | Реальные paths сохранения, height-only/mixed painting/reopen/stale baseline |
| resource-core | 22 PASS / 0 FAIL / 2 SKIP | Synthetic index/read/transaction; external probe и representative archives не настроены |

Результаты: `build-tests-133-resdrain/results-drain.txt`,
`results-delivery-review.txt`, `results-resource-core.txt`,
`scenario-layer-check.stdout.log`, `model-gallery-check.stdout.log`,
`dark-theme-check.stdout.log`, `terrain-height-res-save-check.stdout.log`.
В этой итерации одна проверка с некорректной длиной test hash исправлена; один
browser run прерван (-1), а не PASS: stack показал `QMessageBox::question` из
штатного archive watcher. Тест теперь использует `beginInternalPublication` перед
своей записью. Последующий полный target прошёл. Критерии проверки не ослаблены.

Для save122 создан собственный launcher `save122-isolated`: все реальные app
objects, включая `main.o`, используются из release; только
`backup_manager.o` заменён копией, у которой **только** `defaultRootPath()`
возвращает `EI_RESDRAIN_BACKUP_ROOT` и запрещает запуск без этой переменной.
Алгоритмы backup/save/rollback, native main и сам check неизменны. Root:
`build-tests-133-resdrain/isolated-save122-backups`; TEMP/TMP, fixture copy,
settings и cache также внутри собственных build-каталогов. Это точная граница
test-only подмены; полный запуск установленного приложения не утверждается.
Проблема первой итерации с AppData backups описана выше; чужие/внешние backup
файлы не удалялись. Прежний RES policy assertion799 остаётся отдельным известным
ограничением исходной базы; в продолжении не исправлялся.

### База, артефакты и файлы

AGENTS SHA256 неизменен:
`F4B7FDD5EA82FCDF43D002D23D64EC36BE853650F054EDF70376527CD6432EC3`.
В ходе продолжения сопровождающий/параллельная задача продвинули main до
`6bd5e44e382fd201f322fc8ec3e3b96dff282151` (clipboard132), VERSION132. Эти файлы,
номер, ROADMAP/CHANGELOG не изменялись нами. Финальная проверка выполнена после
пересборки на этой базе. Самостоятельного коммита, Git-записи, сети, установки,
делегирования или интерактивного запуска редактора/игры нет.

Raw release: `build-release-133-resdrain/release/EIMapperEndurance.exe`,
SHA256 `A1B9B64D9C93CB42AD0DD6DC3BA2237D3DDB8CB1CD26D0B2B3E3FCD842B74C6A`.
save122 test launcher SHA256:
`B751BB26743AC4CDC8848B14A34C43667CF5489CDF58D5E4794070850B779C39`.
Постоянная установка не обновлялась. Эти exe не являются готовым пакетом поставки.

Точный список 15 файлов для будущего scoped-коммита остаётся приведённым выше;
из них в продолжении изменены: `res_read_lease.h`,
`terrain_atlas_archive_binding.cpp`, `model_gallery.cpp` (provider),
`resource_browser.cpp`, `res_reader_drain_tests.cpp/.pro`, этот отчёт.
Review copies/patch/logs/build dirs в коммит не включать. `verified_clipboard.*`
и его tests принадлежат параллельной задаче и не входят в список.

### Текст ROADMAP на промежуточной стадии (история)

```markdown
- EI-PUBLICATION-RES-READER-DRAIN: продолжение ревью 2026-10-10. Workers теперь ждут admission до10000мс, writer drain5000мс сохранён; GUI read не ждёт чужую publication, resolver возвращает явную ошибку, provider/browser перестраивают snapshot после ожидания. Production reader target20 PASS; scenario50, dark70, save12229 и gallery PASS; resource-core22 PASS/2 SKIP. Full UI по указанию владельца не запускался. Проверенный finishThumbnail diff (3 PASS на собственной копии: no failure memory/disk cache + retry) ожидает расширения владения за пределы provider; до применения задача НЕ готова к интеграции. База main6bd5e44e382fd201f322fc8ec3e3b96dff282151/VERSION132 сохранена. Evidence: docs/validation/ATAMAN_RES_READER_DRAIN_20261010.md. Далее: применить согласованный delivery diff, проверить его на production objects, назначить версию/переводы/scoped-коммит сопровождающим.
```

### Текст CHANGELOG на промежуточной стадии (история)

```markdown
- EI-PUBLICATION-RES-READER-DRAIN: фоновые RES readers ждут завершения publication с ограниченным timeout; GUI reader не блокирует GUI publisher. Atlas binding возвращает ошибку admission, gallery/browser обновляют index после ожидания; подготовлена проверенная обработка ошибки миниатюры без failure cache и с повторной попыткой. Reader target20 PASS, scenario50/dark70/save12229/gallery PASS, resource-core22 PASS/2 SKIP; full UI не запускался по указанию владельца. Delivery diff пока только в тестовой копии (3 PASS); запись для завершённого релиза требует применения diff. Evidence: docs/validation/ATAMAN_RES_READER_DRAIN_20261010.md.
```

## Финализация после расширения владения

Последнее поручение сопровождающего разрешило менять `CModelGallery::finishThumbnail`
и при необходимости header. Diff из собственной review copy применён по смыслу
**к реальному** `source/ui/model_gallery.cpp`. Изменение header не понадобилось.
Параллельных файлов или установок не изменяли.

### Поведение и причина

Раннее admission-отклонение и молчаливый пропуск архива устранены предыдущим этапом.
Теперь устранён и последний участок: `watcher->result()` ловит `ReadFailure` и
прочие `QException`, удаляет job/watcher, показывает причину в tooltip/status.
Null и ошибка не попадают ни в локальный, ни в общий thumbnail cache; временная
ошибка не фиксирует completed item key. Следующий запрос видимости/selection/resize
повторяет provider. Worker при exception не доходит до disk write; прежний disk
writer отказывается от null. Successful preview сохраняется как прежде.
Ошибка не запускает немедленный цикл автоматических повторов.

`tests/res_reader_drain_tests.pro` больше не подменяет `model_gallery.o` review
копией. Widget-тест обработчика включён без conditional define в обычный target.
В Makefile нет `model_gallery_delivery_review`; все production .o кроме main.o
берутся из текущей release сборки. Старые review copies сохранены только как
исторические артефакты в собственном build-каталоге и не использовались в этих runs.

### Финальные проверки на рабочем каталоге

База `main` / HEAD `6bd5e44e382fd201f322fc8ec3e3b96dff282151`, VERSION132,
плюс все текущие изменения задачи. AGENTS SHA256 неизменен. qmake/make заново
собрали рабочий release, reader target и save122 launcher в своих каталогах.
Зависимости не устанавливались, translations/документы сопровождающего не писались.
Перед передачей финализатор132 продвинул HEAD до
`99802fec8cb944098829ca2115f9b6d2cebb87f0`. Read-only diff от проверенной базы
содержит только ROADMAP.md и clipboard132 validation report; код и VERSION
не изменились, повторять проверки тех же бинарников не требуется. Эти чужие
документальные изменения сохранены, Git-запись нами не выполнялась.

Все семь runs ниже выполнены одним `run-ui.ps1` под атомарно захваченным
`build-locks/ui-tests.lock` с owner.txt. Lock освобождён в finally; на завершении
его нет. Последнее разрешение на полный UI gate заменяет промежуточный запрет.
Проверка использовала текущий нативный clipboard владельца, никаких просьб очистить
или заменить буфер не было.

| Проверка | Итог | Время |
|---|---|---|
| reader-drain, actual production objects | **21 PASS / 0 FAIL / 0 SKIP** | 12.574 с process; QtTest12177мс |
| отдельный handler на реальном файле | **3 PASS / 0 FAIL / 0 SKIP** | 0.555 с process; QtTest323мс |
| scenario | **50 PASS / 0 FAIL**, exit0 | 1.437 с |
| полный `--ui-widget-check` | **PASS**, exit0; 219 итоговых conjunctive условий | 9.700 с |
| gallery | **PASS**, exit0 | 1.679 с |
| dark-theme | **70 PASS / 0 FAIL**, exit0 | 1.112 с |
| save122 | **29 PASS / 0 FAIL**, exit0 | 2.891 с |

Отдельные 3 QtTest PASS — это один функциональный тест
`galleryDeliveryErrorIsNotCachedAndRetries` плюс init/cleanup, как в предыдущем
review run; этот же функциональный тест входит в 21 PASS полного reader target.
Результаты не суммируются как независимые 24 проверки. Widget-тест подтверждает
явный timeout tooltip, пустой item key, отсутствие disk entry, следующий запрос
без отрицательного local/shared cache hit и появление успешной disk entry.

Reader evidence: удерживаемый свой читатель —187мс, 1 commit/1 checkpoint;
4/4 thumbnails+1/1 hydration —197мс drain. Новые 5/5 workers, стартовавшие при
ожидающей publication, дождались и прочитали новые bytes. Production provider
timeout —10021мс; явная ошибка и успешный повтор тем же provider. Writer drain
5000мс, retry3×, before recheck, backup/recovery/guarded rollback не изменялись
при этой финализации.

Full UI выполнялся **raw production exe** рабочего каталога, не на снимке git
и не на review copy. Число219 — число conjunctive условий в итоговом
`const bool passed = clipboardWritesVerified && clipboardRestored ...` текущего
main.cpp; исходник main не изменялся. В raw stdout:
`PASS: native clipboard write preconditions and verified restoration clipboardWritesVerified=1 clipboardRestored=1`.
Завершение `PASS: widget interaction and secondary-window lifecycle checks`.
Clipboard guard132 действительно пропустил prerequisite и подтвердил restoration;
clipboard source/guard не изменялся нашей задачей.

Gallery:497 models/7165 textures, open7мс/select3мс; Shift143 —3мс; закрытие при
worker600мс —1мс; bounded parallel/stale results/all-N close PASS. Dark70 и save29
подсчитаны по PASS records, у save122 `failures=0`.

Логи: `build-tests-133-resdrain/results-drain.txt`, `results-delivery.txt`,
`targeted-results.json`, `ui-widget-check.stdout.log` / `.stderr.log`,
`scenario-layer-check.stdout.log`, `model-gallery-check.stdout.log`,
`dark-theme-check.stdout.log`, `terrain-height-res-save-check.stdout.log`.
Resource-core не менялся при применении delivery diff: последний результат этой
задачи **22 PASS / 0 FAIL / 2 SKIP** остаётся актуальным; SKIP внешних probes не PASS.

### Артефакты, ограничения и интеграция

Final raw release `build-release-133-resdrain/release/EIMapperEndurance.exe`,
SHA256 `8EA878A8A3092990D3FDCABBA37FEC63D98238EE6281B062637D4BF444BC4586`.
Reader exe SHA256 `62450FDC98938384D6E93789FF12E288560C91CEE71F583625D0FF0ED73F6E86`.
save122 isolated exe SHA256 `A8F1C60449C43A533994A273A1C34A126F028A8D360D135D379423BBDCE52D20`.
Save122 test-only backup root override и все его остальные production objects
описаны выше. Копия backup_manager обратно преобразована заменой только тела
defaultRootPath и совпала с текущим production исходником целиком. Read-only
проверка двух стандартных AppData backup roots с времени начала финальной сборки
не нашла новых metadata этой задачи (`133-resdrain`):0. Receipt:
`build-tests-133-resdrain/final-backup-isolation.json`. Backup source/алгоритмы
сохранения в живой программе не изменены.

Интерактивного редактора/игры, установки, Git-записи, сети и подагентов не было.
VERSION остаётся132; `133-resdrain` — только назначенный build prefix. Номер новой
версии, переводы и scoped-коммит выполняет сопровождающий. Новые runtime сообщения
header lease даны как явная техническая причина; UI показывает их в status/tooltip.
Ручная игровая приёмка и произвольные внешние архивы не утверждаются. Старый RES
policy FAIL799 из первого этапа остаётся известным отдельным несоответствием базы
вне владения; его baseline evidence сохранено, это не FAIL финальных runs.

**Финальный точный список файлов для scoped-коммита (15):**

```text
source/documents/map_save_batch.cpp
source/documents/map_save_batch.h
source/documents/res_save_coordinator.cpp
source/documents/res_save_coordinator.h
source/render/terrain_atlas_archive_binding.cpp
source/render/view.cpp
source/resources/res_archive_index.cpp
source/resources/res_archive_index.h
source/resources/res_read_lease.h
source/ui/model_gallery.cpp
source/ui/resource_browser.cpp
source/ui/resource_browser.h
tests/res_reader_drain_tests.cpp
tests/res_reader_drain_tests.pro
docs/validation/ATAMAN_RES_READER_DRAIN_20261010.md
```

Build trees, .o/.exe, review copy/diff, fixtures, settings/cache/backups/logs и
clipboard132 files в этот коммит не включать. `model_gallery.h` не менялся.
Итог задачи по текущему поручению: **реализация и назначенный регресс завершены,
подготовлено к интеграции сопровождающим**.

### Финальный текст ROADMAP

```markdown
- EI-PUBLICATION-RES-READER-DRAIN — реализация и регресс завершены. По аудиту77 сокращён QFile scope, RES publisher осушает своих readers до5000мс; новые workers ждут admission до10000мс, GUI reader не ждёт чужую publication. Resolver возвращает явную ошибку, gallery/browser обновляют snapshot после ожидания; реальный finishThumbnail ловит ошибку, не кэширует null/timeout и допускает повтор. Retry3× с fresh QSaveFile/before-check и backup/recovery/guarded rollback78 сохранены. База main6bd5e44e382fd201f322fc8ec3e3b96dff282151/VERSION132; номер/коммит назначает сопровождающий. Проверки: reader21/0/0, handler3/0/0 на реальном файле; full UI PASS (219 итоговых условий, clipboard restored); scenario50/0, dark70/0, save12229/0, gallery PASS; resource-core22/0/2 SKIP. Известный прежний RES-policy FAIL799 вне владения сохранён в evidence. Отчёт: docs/validation/ATAMAN_RES_READER_DRAIN_20261010.md. Далее: назначение версии/переводы/scoped-коммит сопровождающим; ручная игровая приёмка отдельно.
```

### Финальный текст CHANGELOG (номер назначает сопровождающий)

```markdown
- EI-PUBLICATION-RES-READER-DRAIN: надёжная атомарная публикация RES — короткое удержание QFile, осушение собственных readers, ограниченное ожидание новых фоновых запросов без блокировки GUI, точные I/O diagnostics и committed-but-unverified сообщение. Миниатюры и hydration не теряют результат при сохранении; timeout миниатюры не кэшируется, повтор читает архив заново. Sharing retry3× повторяет before-check и сохраняет backup/recovery/rollback78. Проверено: reader21 PASS, handler3 PASS на production objects, full UI PASS с verified clipboard restoration, scenario50/dark70/save12229/gallery PASS; resource-core22 PASS/2 SKIP. Evidence: docs/validation/ATAMAN_RES_READER_DRAIN_20261010.md.
```

## Передача 133

**EI-FINALIZE-133, 2026-10-10: передача ОСТАНОВЛЕНА по заданному порогу
скорости; 133 НЕ установлена.** Этот раздел определяет состояние финальной
передачи и имеет приоритет над прежними записями о готовности к интеграции.
Холодное заполнение восьми видимых миниатюр замедлилось во всех пяти повторах:
unmo +336,595 мс / +438,53%, stbu +332,822 мс / +1110% по медианам.
Постоянная установка остаётся на 132. Новый staging, finalizer, manifest133,
rollback132 и обновление установки не выполнялись в силу прямого условия
«НЕ устанавливай, остановись»; старые rollback сохранены. TRAVEL_EDITOR.md
пока не возвращён в постоянную папку. Это незавершённая передача, не PASS.

### База, сборка и регресс

- Поручение владельца через сопровождающего Claude от 2026-10-10. Ветка main,
  чистая исходная рабочая копия, HEAD
  `50eba3aaa02ce50037fa427de879960c41a6c5d6`, VERSION133.
  AGENTS.md прочитан и проверен: SHA256
  `F4B7FDD5EA82FCDF43D002D23D64EC36BE853650F054EDF70376527CD6432EC3`.
  Договор docs/TEST_BUILD_HANDOFF.md применён; требование владельца сохранить
  старые rollback учитывается. Параллельных задач/подагентов нет.
- Свежий `git archive` указанного SHA распакован в
  `build-release-133-final/src`; shadow build —
  `build-release-133-final/release`, EXE — `release/release/EIMapperEndurance.exe`.
  1809 tracked blobs сверены с Git: 0 отсутствующих/лишних/несовпадающих;
  1598 архивных CRLF-файлов приведены к точным LF-байтам blobs перед сборкой.
  После штатного build_release.ps1 снова 0 blob-несовпадений, включая перевод.
  Qt5.12.12/MinGW7.3 взяты из установленного toolchain только для чтения.
  Wrapper выполнил lrelease, Release и обе x86 runtime-сборки; exit0,
  119,06 с. Сеть, установка зависимостей и изменения Endurance отсутствуют.
- `--version`: `EI Mapper Endurance Edition 133`, exit0.
  Свежий EXE SHA256:
  `D94052AF103B05EC81D279E23AC44E0FD2BA8A1BE8917DEB3B9EAD9570ED3A9A`.
- Все UI/native/scenario/dark/gallery/drain и benchmark-процессы выполнялись
  под атомарным mkdir `build-locks/ui-tests.lock`, с owner.txt и finally-release.
  Lock освобождён. Редактор и игра интерактивно не открывались. Буфер не очищался
  вне guard; native suite захватила исходное содержимое до тестовых записей.

| Проверка свежего результата | Результат | Evidence в build-tests-133-final |
|---|---|---|
| Полный --ui-widget-check на текущем OLE-скриншоте | 169 PASS + 4 PASS независимого parent, 0 FAIL, 0 SKIP, exit0; 8,38 с | ui-live/{0.out,0.err,result.json} |
| Восстановление исходного буфера | Побайтно сохранены все 6 публичных форматов; PNG/CF_BITMAP доступны; parent восстановил original native clipboard | ui-live/0.out |
| --scenario-layer-check | 50 PASS / 0 FAIL / 0 SKIP, exit0; 1,33 с | scenario/ |
| --dark-theme-check | 70 PASS / 0 FAIL / 0 SKIP, exit0; 1,16 с | dark-theme/ |
| --model-gallery-check | PASS, exit0; 1,77 с; 497 моделей/7165 текстур, open6мс/select3мс, Shift2мс, close0мс; queue/cache/stale/parallel/all-N подтверждены | gallery/0.out |
| tests/res_reader_drain_tests из точных production objects133 | 21 PASS / 0 FAIL / 0 SKIP, exit0; 12,50 с | drain/results.txt |
| Native clipboard tests из снимка133 | **25 PASS / 1 FAIL / 0 SKIP, exit1**, не PASS; 5,17 с | native/results.txt |

Reader-drain: собственный reader180мс, 1 commit/1 checkpoint; concurrency
4/4 thumbnails +1/1 hydration, drain198мс; admission timeout10020мс возвращает
явную ошибку, повтор того же provider успешен. Его синтетические RES и backups
ограничены собственным test tree; request.backupRootOverride задан в тесте.

Native FAIL: `restoreAllFormatsAndFlushNativeData`, assertion guard.captured(),
`Native clipboard format 49968 cannot be read completely within clipboard
limits; check aborted before writes.` Read-only GetClipboardFormatNameW
определил49968 как `image/icns`. В том же suite есть сообщения transient
OleSetClipboard busy; причинная связь с этим FAIL не доказана. Исходный буфер
после suite восстановлен; последующий полный UI подтвердил точные bytes и
PNG/CF_BITMAP. Прогон не скрыт и не заменён PASS; после speed-stop повторов
не выполнялось. Дальше нужен отдельный контроль synthetic image/icns fixture
и guard capture, если сопровождающий поручит продолжение.

### Скорость на реальном профиле

Один и тот же скрытый native harness из
`build-tests-129-thumbs/real-benchmark.cpp` связан отдельно с точными Release
objects132 и133, исключён только main.o. Использованы те же разрешённые
DejaVu/Base RES и private model-catalog/models/textures из отчёта129: unmo36,
stbu71, по8 видимых плиток. Это профиль замера129, а не активный JamaisVu135
из пользовательских настроек. Живые RES/карты/каталог/кэши установки не менялись.
Подготовленные harness находятся в собственных build-tests; приватные fixtures
и бинарники не включаются в Git или передачу.

Пять повторов для каждой версии; порядок132/133 чередуется между повторами.
Для каждого повтора — новая app-папка с отсутствующим производным cache:
первый процесс измеряет cold и повтор в памяти; второй процесс той же app-папки
измеряет warm disk и ещё один повтор в памяти. Холодный режим означает новый
процесс и miss thumbnail memory/disk, не принудительную очистку файлового кэша
ОС. Ни OS cache, ни чужие кэши не очищались. На каждом cold provider8 calls,
на каждом disk/session0 calls. Все80 измерений завершились: visible8/ready8,
unavailable0, complete1, без timeout/crash. Все20 процессов exit0.
Fill начинается после show; total включает ctor/show, как в отчёте129.

| Fill, мс; n=5 на каждую ячейку | 132 min / median / max | 133 min / median / max | Δ median, мс / % |
|---|---:|---:|---:|
| unmo cold | 73,062 / 76,755 / 98,384 | 405,497 / 413,350 / 436,267 | +336,595 / +438,53% |
| stbu cold | 28,252 / 29,984 / 30,548 | 351,698 / 362,806 / 377,640 | +332,822 / +1110% |
| unmo warm disk | 28,356 / 28,966 / 31,267 | 27,653 / 29,152 / 34,595 | +0,186 / +0,64% |
| stbu warm disk | 9,281 / 9,659 / 10,032 | 9,307 / 9,650 / 10,662 | −0,009 / −0,09% |
| unmo повтор в памяти после cold | 5,045 / 5,274 / 5,673 | 4,950 / 5,065 / 5,377 | −0,209 / −3,96% |
| stbu повтор в памяти после cold | 5,500 / 5,696 / 6,426 | 5,441 / 6,203 / 6,364 | +0,507 / +8,90% |
| unmo повтор после warm disk | 4,888 / 4,952 / 5,201 | 4,816 / 5,073 / 5,135 | +0,121 / +2,44% |
| stbu повтор после warm disk | 5,547 / 5,633 / 6,242 | 5,488 / 5,717 / 6,482 | +0,084 / +1,49% |

Оба cold-сценария превышают оба запретительных порога (>20% или >50мс).
Тёплый диск и память почти не изменились: регресс связан с путём production
provider на miss, не подтверждает конкретную внутреннюю причину.
Сырые samples, total/open/show/GUI-gap и итог:
[benchmark-raw.json](../../build-tests-133-final/benchmark-raw.json),
[benchmark-summary.json](../../build-tests-133-final/benchmark-summary.json),
`bench-{132,133}-{01..05}/{cold,disk-warm}.{out,err}`.

Предложение исправления, **гипотеза, без изменения кода**: профилировать
provider отдельно по admission/generation, каталогу, чтению, decode/render;
хранить immutable shared catalog snapshot на поколение и заменять его при
publication, передавая worker общий const snapshot. При этом QVector в
CResourceCatalog использует implicit sharing: глубокое копирование всего
каталога пока не доказано. `resIo::generation(path)` и ReadLease повторно
вызывают pathKey с QFileInfo::canonicalFilePath; проверить стоимость повторной
канонизации всех source paths и возможность переиспользовать canonical keys/
registry handles на протяжении snapshot без ослабления read-drain и проверки
поколения. После изменения обязательны cold/warm/repeat132-vs-new и concurrency
регресс; повышать дедлайн либо обходить защиту публикации не предлагается.

### Состав и байты пакета: аудит, передача отложена

Сверены все payload hashes и manifest исторических staging130/131. В каждом
127 managed files; между130 и131 менялись только EXE, два runtime-компонента,
VERSION, Readme.md и CHANGELOG.md. TRAVEL_EDITOR.md, LICENSE, NOTICE и
heightmaps/README.md оставались побайтно одинаковыми.

Процедуры `build-tests-130-final/stage.py` и
`build-tests-131-stream/stage.py` копировали каждый hash-проверенный файл
предыдущего manifest (129→130→131), затем заменяли шесть текущих файлов.
TRAVEL_EDITOR.md наследовался этой цепочкой; его содержимое совпадает с
`docs/TRAVEL_EDITOR.md` снимка133 после CRLF-нормализации. LICENSE/NOTICE
наследовались из предыдущего staging; Readme брался из working checkout,
где `git ls-files --eol` показывает i/lf, w/crlf, attr/text=auto. Это источник
CRLF-различия с точными LF-байтами Git-снимка132. Текущий tracked build wrapper/
finalizer не добавляет TRAVEL_EDITOR автоматически: финализация не заменяет
полную подготовку состава. Сравнение и источники:
[package-audit.json](../../build-tests-133-final/package-audit.json).

В staging132 осталось126 файлов, TRAVEL_EDITOR отсутствует; кроме версионных
шести файлов отличаются LICENSE.txt, NOTICE.md и heightmaps/README.md только
окончаниями строк. Для следующей передачи выбрана явная политика: UTF-8 с
CRLF для текстов пакета (не менять Git blobs/исходники; не добавлять BOM).
Она сохраняет точные bytes неизменённых документов130/131 и устраняет случайную
зависимость от checkout/archive. VERSION и текущий CHANGELOG тоже записывать
CRLF; это уже версионные файлы. В данном поручении политика установлена в
отчёте, но к пакету ещё не применена: staging133 не создан из-за speed-stop.

| Неизменяемый документ из снимка133, после CRLF | SHA256, совпадает с130/131 |
|---|---|
| LICENSE.txt | B25CBCA477B14288BB1294C969392C11B86500CD47567188040885151F2A9DFF |
| NOTICE.md | F59B2224AFDB75D7A006BE11C626A28A0078123B08253FB3AD622EC704DD88F5 |
| docs/TRAVEL_EDITOR.md → TRAVEL_EDITOR.md | 945D3E7B5905944D8735D07E518063DBC04BCDA15887CFD5B119F463EF1ACC83 |
| heightmaps/README.md | 051A5079ED12BDF76F9DACFDB6D4BF37DC89E0C668C4646C3B010616A5887ACE |

Планируемый набор следующего staging должен восстановить127 managed payloads;
допустимые отличия от131 — только EXE, два заново собранных runtime-файла,
VERSION, Readme и текущий CHANGELOG, с объяснением результатов сборки/версии.
Фактического сравнения staging133, успешного finalizer или manifest133 нет,
и их PASS не заявляется. Скрипты упаковки в Git не менялись.

### Состояние установки и scoped-коммит

Read-only после остановки:
`artifacts/EI-Mapper-Ataman-2-first-test`, VERSION132,
126/126 managed hashes совпали, ошибок0; TRAVEL_EDITOR отсутствует.
Установленный EXE SHA256
`DF44E7E6BD840FAD308525A0C1E08B367805846A8F049086CC9E9CEC6A1FEA06`;
manifest SHA256
`8ACFF8F7C3A1145671A77625521E982A53EF99F22A0CF2D5E50FAC52F0766660`.
Никаких операций записи/backup/замены/очистки постоянной установки нет;
пользовательские файлы/кэши, старые rollback, игра, мод и живые карты сохранены.
Receipt: [stop-receipt.json](../../build-tests-133-final/stop-receipt.json).

Для scoped-коммита разрешены и изменены только этот отчёт и два статуса в
ROADMAP.md. Версия остаётся133; реализации и tracked packaging scripts не
менялись. Engineering scripts/logs/binaries/fixtures из build-tests/build-release
не входят в Git. Коммит выполняется в main с process-local переменными
GIT_AUTHOR_NAME/GIT_COMMITTER_NAME=Codex,
GIT_AUTHOR_EMAIL/GIT_COMMITTER_EMAIL=codex@localhost и trailer
`Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
Push/remote/network и изменения глобального Git config запрещены и не выполнялись.
Ручная визуальная/игровая приёмка не проводилась; следующий шаг — поручение на
устранение speed-регресса и native image/icns FAIL, затем полная передача пакета.
