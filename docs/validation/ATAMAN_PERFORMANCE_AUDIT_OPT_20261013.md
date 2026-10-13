# EI-PERFORMANCE-AUDIT-OPT — Ataman167

Дата: 2026-10-13. Поручение: аудит производительности и оптимизация маппера.
База: main `5a8eaf4a8d9a5561e92037e7ae73c70fafa38aba`, Ataman166.

## Объём и состояние

Измеряются загрузка/готовность палитры, кисть, камера на полной активной MOB-сцене и обычное сохранение большого RES. Оптимизация сохраняет качество текстур, native128 воды, настройки теней, точные проверки исходника/назначения, резервирование, атомарную замену и повторное чтение.

Тесты выполняются на собственных копиях карты, RES и HD-библиотеки в `build-perf-167-20261013`. Оригиналы игры/библиотеки читаются при подготовке; редактор и игра интерактивно не запускаются. Каждое окно native-теста скрыто, настройки/backup/clipboard изолированы. Параллельные агенты и новые пользовательские чаты не создавались.

Работа завершена: финальная167 собрана из отдельного snapshot, сравнительные замеры и функциональные проверки выполнены, программа установлена в постоянную папку. Полные результаты и границы приведены ниже.

## Реализованная оптимизация

1. `source/render/terrain_atlas_archive_binding.cpp`: ограниченный общий кэш128 доказанных pixel SHA. Ключ включает все сжатые MMP-байты, число страниц и физических слотов; namespace-алиасы одного содержимого получают тот же результат. Изменение payload инвалидирует proof даже при прежних размере/времени файла. Пиксели не удерживаются; запрос самих изображений по-прежнему декодирует их. Проверки generation/метаданных и полного набора членов сохранены.
2. `source/documents/res_save_coordinator.cpp`: после доказанного сравнения disk==baseline исходные байты разделяются с неизменным baseline. Для source==destination используется та же начальная доказанная копия. Завершённый staged document и его reopened bytes освобождаются перед backup/post-commit parse; поздние временные source buffers освобождаются после проверок. Все поздние точные проверки исходника/назначения, backup SHA, atomic commit, свежий parse и сравнение published members остаются.

Добавлен диагностический режим `performanceScene` штатного native check: полный MOB и60 измеряемых camera frames после20 прогревочных, с GPU finish и подтверждением object submissions. Тест использует копию настроек владельца; свет/тени/качество для этой сцены не отключаются. Добавлены четыре регресса источников/кэша и response-file поддержка длинного Windows пути для двух native test targets.

Гипотеза лишних полных GPU-загрузок при отпускании кисти была опровергнута сверкой событий: соответствующие записи относились к проверке Undo/Redo. Промежуточный model-only prototype прошёл native37/0, но убедительного выигрыша не дал и полностью удалён из финальной реализации. `view.cpp`/`view.h` восстановлены до принятых166 blobs; исправление166 первого штампа/круглой рамки сохранено.

## Идентичность входов

[identity.json](../../build-perf-167-20261013/identity.json): копия installed166 EXE SHA256 `CCAAF01207A0DD6B4E99DAD86645A78A5E01493DE5343A53A54AF68A076EA659`.

- zone1 MPR SHA256 `A0D4CAC240925BD9008B200C609C2E398D14734F911DF1DE849C271E61DF111F`.
- zone1 MOB SHA256 `AE38BA7983B20B0828007E6A0D76B48C4A6994E0D2FA243C1F2011AE26C08FFF`.
- HDPack_Terrain.res1519477675 bytes, SHA256 `3C512EE7A9A49A7C2A6C0278CB449331993011CB921F1D6B262F634016546277`.

Перед первым изменяющим Save исходные MPR/RES дополнительно сохранены в собственном `inputs`. Для сравнительного Save используются те же private destination paths после восстановления этих входов. Библиотека полностью скопирована; её индексы/варианты не пишутся в пользовательскую установку.

Baseline full-scene executable собран из166 archive с добавлением только диагностического `performanceScene` в check; производственные renderer/save/binding modules166 не менялись. [Финальная идентичность исходников](../../build-perf-167-20261013/final-source.json) содержит SHA всех собственных изменений поверх166.

## Замеры

Это ограниченные сравнительные прогоны на данном компьютере, а не статистическая гарантия для любой карты/диска. OS cache не сбрасывался. Throughput-тесты и camera/brush measurements выполняются без параллельной компиляции.

| Сценарий | 166 | 167 | Граница вывода |
|---|---:|---:|---|
| Повторная точная сверка8-page атласа | 512–517ms | 70–76ms | Одинаковый pixel SHA; примерно7 раз быстрее повторно |
| Первая сверка того же атласа | 516ms | 653ms | Холодный поиск не ускорен; добавлено хеширование полного сжатого содержимого |
| Палитра при paint | ready 9.33 s + thumbnails 42.70 s | ready 5.36 s + thumbnails 26.44 s | OS/library cache прогрет; эффект одной правки из этого замера не следует |
| Release двух одинаковых мазков | 885 ms / 230 ms | 877 ms / 216 ms | Существенное ускорение release не подтверждено; GPU prototype не принят |
| Полная активная сцена 1699 объектов | camera median 14.45 ms / p95 17.15 ms | median 14.31 ms / p95 15.63 ms | 60 frames; visited 29048 / submitted 7162 в обоих; убедительного ускорения камеры не установлено |
| Обычный Save 1.52 GB RES, сравнительная пара | 62.65 s | 53.02 s | Около 15% быстрее в этой паре; включает backup, guards, publication, adoption |
| Пик private memory в том же процессе Save | 18.53 GB | 10.95 GB | Сэмплирование раз в секунду; снижение примерно 41% |
| Peak working set в том же процессе Save | 16.86 GB | 9.19 GB | Счётчик Windows процесса, а не всего компьютера |

Численные источники: [atlas before](../../build-perf-167-20261013/res-index-probe/atlas-before.json), [atlas after](../../build-perf-167-20261013/res-index-probe/atlas-after.json); pixel digest в обоих `a8b2299c4e8738702f496be75c2d0ff29cb462d8d6040d0a8ed83173500b087e`. Первое измерение чтения одного2.80MB member дало около2.1ms; само чтение индекса не было основным расходом, его отдельная оптимизация не добавлена.

Baseline logs: [paint 37/0](../../build-perf-167-20261013/baseline-paint.stdout.log), [scene 8/0](../../build-perf-167-20261013/baseline-scene.stdout.log), [save 30/0](../../build-perf-167-20261013/baseline-save-guarded.stdout.log). В Save worker-фазы: source read 421 ms, destination read 429 ms, build/member validation 6.65 s, stage write 2.14 s, staged reopen 7.60 s, backup 13.21 s, atomic commit 1.47 s, post-commit reopen 6.69 s. Полная 83.30 s включает также подготовку и renderer/catalog adoption.

Final logs: [paint 37/0](../../build-perf-167-20261013/final-paint.stdout.log), [scene 8/0](../../build-perf-167-20261013/final-scene.stdout.log), [Save 30/0](../../build-perf-167-20261013/final-save.stdout.log). Final scene сохранил private output из baseline config: ошибка присваивания несуществующего `outputDir` в runner не затронула входы; stdout/metrics и рабочий каталог отдельные. Фактически использованный config сохранён в `final-scene.actual.json`; этот завершённый прогон не повторяется ради пути картинок.

Первый baseline Save не измерял peak memory, поэтому выполнен один дополнительный [baseline 30/0 с памятью](../../build-perf-167-20261013/baseline-save-memory.stdout.log) с теми же восстановленными входами и новым счётчиком. В нём Save занял 62.652 s вместо начальных 83.302 s; это демонстрирует влияние прогрева, поэтому сравнивается 62.652 → 53.017 s, а выигрыш 36% относительно первого запуска не заявляется. Восстановленные input hashes совпали: [baseline inputs](../../build-perf-167-20261013/baseline-save-memory.inputs.json), [final inputs](../../build-perf-167-20261013/final-save.inputs.json). Peak private: 18534658048 → 10948915200 bytes; peak working set: 16855367680 → 9188691968 bytes. [Baseline metrics](../../build-perf-167-20261013/baseline-save-memory.metrics.json), [final metrics](../../build-perf-167-20261013/final-save.metrics.json). GB в таблице десятичные.

## Проверки

Изолированный кандидат167: **42 PASS /0 FAIL /0 SKIP**:

- binding cache6/0: разные ожидаемые hashes/алиасы, изменённый действительный BC1-block при прежних size/mtime, copy-on-write возвращённых pixels, native256/native128 и разные физические counts;
- HD memory7/0: bounded cache, точные pixels, native-sized/mapped/COW ownership;
- RES29/0: staging, untouched members, backup/source conflicts, atomic failure/rollback, sharing failure/retry и cohort checks. Optional large benchmark не запускался этим общим target: нормальные функции перечислены в `selected-tests.txt`, большой Save измерен отдельно выше.

[Unit summary](../../build-perf-167-20261013/unit-summary.json). Ошибки собственных runner (qmake argument array, Windows link-command limit, сбор строки Totals) устранены; первоначальный BC1 mutation затрагивал неиспользуемый endpoint и не менял pixels, поэтому fixture заменена на заведомо другой валидный блок с независимым hash oracle. Эти промежуточные ошибки не являются PASS. Первый Save run остановлен guard имени fixture до записи; повторный разрешённый private-root run30/0 сохранён отдельно.

Финальная сборка: paint 37/0, scene 8/0, Save 30/0, [water 68/0](../../build-perf-167-20261013/final-water.stdout.log). Water включает изменённые hover/press/release coordinates, native framebuffer Undo/Redo, ordinary Save и fresh reopen; water pixels/rotation совпали, размер 128×128 сохранён. [UI widget interaction/lifecycle](../../build-perf-167-20261013/final-ui.stdout.log) PASS, exit0. Native attempts к системному clipboard равны0 во всех финальных проверках; в water external clipboard metadata изменилась, что отдельно помечено журналом и не приписывается тесту.

Native картинки первого мазка до/после просмотрены: участок изменился на выбранный Yellow lichen, исходные текстуры вокруг сохранены; изображение не пустое. Также просмотрены `final-water/sampled-circle-preview.png` и `sampled-water-after.png`: native круг виден, физический рисунок воды перенесён. Это просмотр фактического рендера, отдельно от ручной приёмки владельца. Проверки завершены: 42 unit +143 final native PASS, без FAIL/SKIP; UI PASS отдельно. Успешные module tests из кандидата повторно не запускаются: их production binding/save module hashes совпадают с финальными, удалённый GPU prototype в этих случаях не исполнялся.

## Передача

Свежий EXE, оба staging и установленный EXE имеют один SHA256 `BBCAD30F149E5DA13E1FE5B45B3E9BDFDB78E79057837FED6B158EECF2CB3793`. Raw и staged `--version` подтвердили `EI Mapper Endurance Edition 167`; собственные VERSION/source module hashes совпали с `final-source.json`. Чистая Release сборка включает свежие x86 runtime components. [Build log](../../build-perf-167-20261013/build-final.log), [version](../../build-perf-167-20261013/final-version.stdout.log).

Первый собственный staging167 прошёл финализатор, но при сверке после установки выявлен пропущенный README: указатель оставался166. Код программы не менялся. Создан новый `build-staging-167-performance-final` из проверенных managed files, заменён только README, повторно подтверждены тот же EXE/167 и [финализатор](../../build-perf-167-20261013/finalizer-final.log). Первоначальный finalized stage не изменялся. Финальная SHA256SUMS SHA256 `B0D47B209BE54E1AA52B24E9C29BF924090F2D7ACC5BED09CDE51A5A956FEABA`.

Перед обновлением host-level guard обнаружил установленный editor PID18704; ни его процесс, ни загруженные файлы не менялись. Владелец подтвердил закрытие и установку167; повторный guard перед записью прошёл. Обновлена одна постоянная папка [artifacts/EI-Mapper-Ataman-2-first-test](../../artifacts/EI-Mapper-Ataman-2-first-test):147 managed files,6 замен/1 добавление/0 удалений, включая отдельную коррекцию README/manifest. Нормализованный package README и CLI указывают167. Backup166 сохранён до записи; copy hashes, atomic replacement и итоговые managed hashes проверены. Финализатор на установке не запускался.

Все22127 unlisted пользовательских файлов сохранены побайтно. Digest до/после обеих фаз обновления `9A8AF5088F0BE1A876FE7D776CD06F82AEA24E7D85E73A0A861833515FA0502F`. [Первый receipt](../../build-perf-167-20261013/install-receipt.json), [итоговый receipt](../../build-perf-167-20261013/final-handoff-receipt.json); единственный retained rollback — `artifacts/rollback166-EI-PERFORMANCE-167-20261013`.

Автоматическое review сначала отклонило необязательную очистку диагностических копий без явного поручения. После отдельного подтверждения владельца выполнен точный guarded список: удалены13152889801 bytes собственных game-data copies, baseline/candidate sources/builds, старого staging и подтверждённого rollback165. Новый rollback166 побайтно сверён перед удалением старого. [Cleanup receipt](../../build-perf-167-20261013/cleanup-receipt.json). Final source/build/staging167, logs, input hashes, JSON receipts, unit results и native PNG сохранены. Оригиналы игры, текущая установка, Endurance checkout и чужая документация из очистки исключены.

Исходники и эта запись фиксируются scoped-коммитом в main; полного SHA собственного коммита внутри самого документа нет, он записывается в `build-perf-167-20261013/source-commit.txt` и итоговом сообщении. Исходная166 база и fingerprints перечислены выше. GitHub не обновлялся.

## Ограничения

RES по-прежнему переписывается/резервируется целиком. Удаление архивных данных, отключение качества/теней/проверок и переработка сохранения в streaming protocol не выполнялись. Три находки предыдущего аудита остаются отдельными пунктами; производственная логика восстановления прерванного batch этим изменением не исправлена.

Native measurements не подтверждают все user camera angles/neighbor scenes, длительный интерактивный сеанс и работу в игре. Ручная приёмка владельцем остаётся отдельно.
