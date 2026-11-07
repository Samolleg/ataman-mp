# EI-REBUILD64-OFFSCREEN-288 — итог, 2026-11-07

Завершение по прямому решению пользователя треда: без V4 и без дальнейшей доводки замечаний. Это частичный результат этапа, не утверждение полного закрытия исходного перечня. База кода 682c495bca6f5c290f3f78fb1155ad585087492c; поверх сохранён документационный коммит установки287 6d02bd4f6a1041d99b089290c2ada0f090e0618f. Номер288 занимает прежний плановый этап290; интеграция кистей получает следующий свободный номер, старые записи не перенумерованы. Приёмку и запуск следующего этапа выполняет пользователь этого треда. Установка только по слову владельца, здесь установки/push нет.

## Все18 строк прежнего этапа290

| Строка287 | Функция | Результат/граница проверки |
|---:|---|---|
| 158 | Сборки/группы/placement | CPU-путь сохранён; отдельный штатный сценарий сборки не завершён. |
| 159 | Галерея объектов | Native thumbnail через штатную галерею, V3 PASS. |
| 160 | Каталог сборок | CPU-каталог сохранён; отдельная визуальная проверка не завершена. |
| 161 | CPreview: модель, вращение, zoom | Native независимый вывод; две модели и Qt wheel, V3 PASS. Полная матрица жестов не снята. |
| 162 | Миниатюры/кеш | Native namespace отдельно от старого GL; V3 PASS. |
| 163 | Предметы/заклинания | Native предмет конструктора и базы/нового объекта PASS; 2D заклинания сохранены, отдельно не проверены. Appearance/stst8 — сообщение, следующий этап. |
| 166 | Миникарта/export/Test Map | Native regeneration, меню PNG и MMP в приватном RES с readback PASS. Игровой Test Map проверен только по общему пути, игра не запускалась. |
| 167 | Topdown/слои | Native 8 прежних PNG и manifest PASS. ID-растра в прежнем контракте нет; IDs находятся в manifest. Геометрическое наложение не доведено до числового критерия. |
| 168 | Viewport snapshot | Native принятый кадр, V3 PASS. |
| 169 | Браузер ресурсов | Исправление аудита: его QLabel/CPU model canvas не зависит от GL. Штатные texture/model просмотр и raw MMP/MOD export V3 PASS. |
| 178 | MCP export_map_topdown | Native пакет zone1 V3 / zone7 V2 PASS; протокол прежний. |
| 188 | MCP window_snapshot | Native снимок V3/V2 PASS; подготовка/отмена возвращают явную ошибку. |
| 1165 | aiGraphObjectPropertyInvalidatesPreview | Механическая зависимость сохранена; отдельный тест не повторялся. |
| 1166 | aiGraphUndoCommandInvalidatesPreview | Механическая зависимость сохранена; отдельный тест не повторялся. |
| 1812 | CPreview::attachView | Исправлено позднее подключение native-виджета; реальная форма базы V3 PASS. |
| 2089 | topdownTransparent | Native alpha/terrain-путь реализован; пакет PASS, мягкие края масок отличаются. |
| 2090 | topdownMask | Native coverage без новых шейдеров; пакет PASS. Маски бинарные, порог alpha 0,1. |
| 2277 | CModelGallery constructor | Native provider и отдельный кеш; штатная галерея V3 PASS. |

## Реализация и выходные файлы

Memory API ABI9: принятый кадр с/без служебного слоя, отдельный размер/камера, independent model preview, cartographic passes. Ответ содержит generation/map/neighbours/serial. Барьер тот же: итоговый документ, материалы и все принятые соседи. Отдельная камера не заменяет показанную; независимый preview использует отдельный device того же адаптера, основное окно продолжает Present. Очередь ограничена8. Изменение документа/закрытие проверяется перед публикацией. Для headless добавлен явный `--agent-native-output`: без него D3D не нужен и DLL не грузится; при native-запросе без DLL — ошибка без тихого GL.

Функциональные патчи встраивания020/021, отдельные блоки host/offscreen_output.inc, independent_preview.inc, cartographic_output.inc, output_live.inc. terrain.inc и шейдеры не менялись. Таблица638 шейдеров SHA256 `38fb2f9c8931f79080ca036d75f89388170852531627c31415b3ed3d99872f64`.

Заметно меняется цвет/свет явно экспортированных composite.png, terrain.png, vegetation.png, structures.png и миникарт PNG/MMP: теперь Rebuild64. Остальные прежние слои: relief.png, water-mask.png, vegetation-mask.png, structures-mask.png. Имена, размеры и границы/ось +Y вверх сохранены. Потребители: MCP-клиент и cartographic previews/manifest, меню экспорта, миникарта редактора, явная запись выбранного MMP/RES и Test Map. Автоматическая запись миникарты при открытии не добавлена. Старый кеш GL сохранён; Native использует отдельный gallery-thumbnails-rebuild64 и fingerprint ресурсов/DLL. CPU-просмотры текстур/материалов/сборок не выдаются за D3D-перенос. Raw-экспорт байтов ресурсов прежний.

Ограничения изображения: utility object-color passes используют native interface lighting без полного world AO/shadow; маски coverage бинарные с alpha threshold0,1, края могут отличаться от GL. Lit/unlit в native minimap пока одна native-картинка: регулировка unlit shading заблокирована с сообщением, для полноценного unlit нужен отдельный shader pass — этот пункт остановлен согласно поручению. Остальные цветовые регуляторы сохранены; новое native начальное значение нейтральное100, старый GL grade не переписан. Appearance/многослойная внешность персонажа/stst8 честно отложены с сообщением.

## Уже завершённые проверки, без нового круга

Все результаты и снимки ниже — из собственной readback-цепочки скрытого настоящего main/MainWindow/Qt, RTX4070 SUPER. Перед native снимками барьер проверяет5/4 соседа, pending0 и SHA; это не снимки экрана владельца. Визуальная проверка настоящего окна остаётся владельцу. Финальный V4 не запускался по прямому указанию пользователя.

Рабочий корень evidence: `work/rebuild64-offscreen-288-20261107/`.

| Прогон | Рендер/соседи | Итог |
|---|---|---|
| case-v3-widgets-zone1 | Rebuild64/5; GL11 при штатном переключении | exit0, realClose; snapshot, memory, отдельный topdown, меню/миникарта PNG+MMP readback, модели, конструктор, база/новый объект, галерея, resource browser PASS |
| case-v2-outputs-zone7 | Rebuild64/4 | exit0; native snapshot/topdown/preview PASS |
| case-v2-headless-ordinary | GL headless; D3D отсутствует | exit0, open/read/Save без DLL PASS |
| case-v2-headless-native | Rebuild64/5 по явному запросу | exit0, snapshot и topdown PASS |
| case-v2-headless-no-dll | native requested / DLL отсутствует | exit0, понятная ошибка, тихого GL нет |
| case-v2-output-races | native5/4, затем function GL | закрытие/смена и GL fallback экспорта выполнены; device-loss FAIL из-за невооружённого тестового hook, не PASS |
| case-v3-output-races | Rebuild64/5→4→5 | Present одновременно с offscreen и close cancellation PASS; FAIL assertion «map switch cancels obsolete offscreen result». До проверки device loss не дошёл. Гонка не объявляется закрытой |

V3 тест требовал именно cancellation; если отдельный запрос успел завершиться во время синхронного открытия до смены generation, он мог законно вернуть прежний мир до смены. Журнал не различает это с полной точностью; дефект устаревшего кадра не доказан, строгий критерий не подтверждён. Дальнейшее исследование остановлено пользователем. Потеря устройства именно с новым offscreen остаётся непроверенной; принятые287 fallback-проверки не подменяют её.

V3 EXE `0e4d474c1fe94135be09c3aefe2fd3e066c9548ace5ab703e9f3d1ed4148f81e`, DLL `feb69972ef4e20f5c862505c0b1ec2887878797e3c3a02ffbe735c13b3c402f5`. V2 хэши зафиксированы в каждом process.json/exit.json; это промежуточный EXE/DLL, а не итоговый бинарный контроль всех функций. Полная Release-упаковка после решения о завершении повторно скомпоновала EXE из тех же исходников; хэш staging и отсутствие нового прогона приведены ниже. Бинарную идентичность с V3 не заявляю.

Просмотрены `closeout-contact.png`, V3 native-preview.png, constructor-item.png, database-item.png, gallery-thumbnail.png, gl-topdown.png/native-topdown.png и water-mask.png: правильные модели/предметы, нормальный мир без прежнего sky/checkerboard; различия света GL/native ожидаемы. Снимки ресурсов, меню/MMP, второй модели и presented stream оставлены. Наложение геометрии GL/native в числах не завершено; парные768×768 сохранены, визуальное совпадение русла/берега не заменяет критерий.

В V3: preview дома120,88мс, второго объекта123,37мс, предмета199,76мс, базы9,48мс; максимальные Qt gaps47,35/56,86/86,93/0,02мс соответственно. Полное открытие object-gallery81,53мс, базы104,80мс, конструктора1449,61мс: требование500мс для конструктора НЕ достигнуто. CPU каталоги уже вынесены в worker; оставшаяся причина отдельно не профилирована, синхронные rune icons — гипотеза, без нового исправления. Глобальный максимум V3 функционального сценария4,509с включает подготовку GL после переключения, не является временем native preview. Отдельный topdown2048 V3 занял143,25мс; сравнение frame-time до/во время не доведено до полноценного критерия, приведённые ниже наблюдения потока не заменяют его.

## Падения 18:58–20:53

Одна группа: 18:58 case-preview-zone1 (pid18820), 19:10 case-output020-zone1 (59280), 19:15 case-descriptor-zone1 (13260), 20:19 case-package-r2-zone1 (14044), 20:33 case-guard-zone1 (54360), 20:46 case-frame-values-zone1 (30948), 20:49 case-rebuilt-library-zone1 (48364), 20:53 case-interface-preview-zone1 (debug child68504): настоящий, НЕ намеренный use-after-free при закрытии CPreview после удаления CView. Во всех7 minidumps первая запись стека Qt5Core+0x212a25 (QObject::property), восьмой debug stack тот же. Исправлено QPointer<CView>/QPointer<CNode>; case-lifetime-zone1 и последующий V3 realClose exit0 подтверждают исправление. Пересборки рендера/проверки размеров сами этот дефект не исправляли; ранние гипотезы переполнения буфера не подтвердились. Evidence: logs/wer-events.json, logs/crash-stack-summary.json, case-interface-preview-zone1/process.log, V3 events.jsonl/exit.json. Неисправленного подтверждённого падения обычного сценария в завершённых V3 нет.

## Изоляция, сохранность, сборка

Общий замороженный набор295 файлов, 5 877 062 358 байт, включая все используемые RES, MPR/MOB и loose JSON/config. Live-файлы закрыты сразу после копирования; own PID handle audit и Qt read guard запрещают открытие живых PZ RES/MPR/MOB. Завершённые V2/V3 проверки: живых mod opens0, frozen hashes неизменны, запись только work. В начале финализации незавершённых собственных процессов не найдено; чужие процессы не завершались. Данные этого этапа удаляются после коммита, логи/кадры/manifest остаются; результат cleanup — cleanup-receipt.json либо cleanup-plan.json.

В коммит/архив входят только свои строки. В view.cpp свои17 добавлений/1 удаление,13 строк владельца остаются рабочими; neighbor_load/landscape не менялись нами. В индексе LF без CR. Рабочий QM владельца не менялся; staging получает свежий lrelease TS (3408 finished,0 unfinished). Удалённых функций нет. Старые GL-ветви сохранены, часть общих механизмов (QPointer и инвалидирование) исправлена также для них. Полную неизменность всех GL-эффектов не утверждаю.

Release из scoped git archive, 8.3 путь без subst/junction. build_release.ps1 наткнулся на старую ошибку пробелов x86-wrapper; оба x86-компонента успешно собраны теми же штатными cmd через8.3, журналы logs/release-wrapper.log и runtime-bridge/host.log. Установка не выполнялась. Следующий этап не начат.

## Готовый комплект

Staging: `build-staging-288-rebuild64-offscreen-20261107`. Финализатор PASS:1365 прежних managed путей →1365, потерь0, добавленных путей0; удалённого0. Свежий QM3408 сообщений,0 unfinished. EXE SHA256 `ce68b725865ac1e5ddba63d9330d44acc7fa5a178111a2e72efdbdd7644a6c64`; DLL SHA256 `feb69972ef4e20f5c862505c0b1ec2887878797e3c3a02ffbe735c13b3c402f5`. Полный Release повторно скомпоновал EXE; diff source trees V3 f3407f36 и финального архива содержит только CHANGELOG/ROADMAP/Readme/отчёт, C++/ABI/TS совпадают. После этого нового функционального запуска не было по прямому указанию завершить без V4.

Наблюдение настоящего потока V3 при отдельном2048 output: до запроса51 Present за1с, медиана интервала17,31мс, максимум47,64мс; во время143,25мс запроса2 Present, интервал129,39мс. Это заметная задержка показа во время отдельного вывода; требование без ухудшения frame-time не закрыто. Камера основного вида не подменяется (проверка сохранённых значений PASS), но полную визуальную матрицу отсутствия рывков не заявляю. Evidence: logs/closeout-audit.json и case-v3-output-races/presented.jsonl.

## R2 — первая попытка из staging, 2026-11-07 (FAIL изоляции; заменена прогоном копии)

Пользователь треда принял288 с прежними замечаниями. renderer-diagnostics.patch перенесён вручную поверх288: requested/latched/interrupted, причины selection/compatibility/fallback и постоянный индикатор Rebuild64/OpenGL. Условия выбора не менялись; ветви отмены offscreen и outputOnly fallback288 сохранены. Русская подсказка внесена в TS.

Свежий `build-staging-288-rebuild64-offscreen-r2-20261107`: финализатор PASS,1365→1365 managed путей, потерь0/добавлений0, QM3409 finished/0 unfinished. EXE SHA256 `cbc09ff557b9c79a5e91cd1b3d55043cc432047c470797716ee5e2ab02b51ea3`; DLL `feb69972ef4e20f5c862505c0b1ec2887878797e3c3a02ffbe735c13b3c402f5`. После разрешения на однократное копирование сборка не менялась и не пересобиралась. Для изоляции агент временно добавил frozen resource-profile JSON в собственный staging; после процесса файл удалён, EXE/DLL не изменены.

Один запуск именно staging EXE/DLL:27,233с, exit1, без аварийного падения. Настоящие Qt-события: startup Rebuild64 + постоянный индикатор PASS; браузер карт и double-click zone1 PASS; native barrier5 принятых соседей/pending0; MCP snapshot и принятый кадр PASS; один CPreview дома показан и закрыт, native image819,34мс, maxQt gap76,77мс. Снимки r2-viewport.png/r2-model.png просмотрены: нормальный мир/дом. Но финальная проверка изоляции FAIL: portable character caches вычисляют путь от applicationDirPath (`source/runtime/persistent_visual_cache.cpp:399`), то есть `staging/cache/character-{atlases,composites,plans,animations}`. Драйвер разрешает запись только в work, поэтому попытки записи cache-manifest.json заблокированы. Это несовместимость настройки строгой изоляции теста с portable cache, не краш и не дефект выбора вида; фактическая запись в живой мод/установку не произошла. Исправлений и повторов не было.

Из-за FAIL до штатного MainWindow close сценарий не дошёл, realClose не подтверждён. Второй запуск НЕ выполнялся. В журнале первого есть точная строка `Renderer startup requested:Rebuild64 latched:0 interrupted:0`; повторный выбор не заявляется проверенным. После завершения первого процесса `known-folders/10/rebuild64` пуст, startup-*.json и manual-retry-required отсутствуют; это не подмена проверки штатного закрытия.

Заморожено292 файла (5 874 376 462 байт), один раз из живых источников по явному разрешению; тест читает только копии. Собственные handle/read audits:0 живых mod handles/opens, хэши копий после процесса неизменны. Evidence: work/rebuild64-offscreen-288-20261107/r2/{fixture-manifest.json,failed-result.json,cache-after-first-close.json,outputs.png}, case-r2-accept288/{process.json,exit.json,events.jsonl,process.log,logs/workflow.log}. Копии удалены; итог в r2/cleanup-receipt.json. Логи и PNG оставлены.

По прямому условию пользователя при FAIL **коммит не сделан**, новые круги/матрицы не запускались. HEAD остаётся b1a07c2ddd0a1c03f5f1e2470a386a3ebacca37c. Патч/документы остаются своими незакоммиченными изменениями; чужие правки сохранены. Установки/push нет. Следующий шаг — решение пользователя по незакрытому сценарию; автоматическая доводка не назначается.

### R2: полный сценарий на идентичной копии — PASS

По следующему поручению пользователя staging скопирован в `work/rebuild64-offscreen-288-20261107/r2/runtime-copy`; EXE/DLL SHA256 совпадают со staging. Запись portable character cache в эту рабочую копию разрешена; это обычное поведение программы, не дефект288. Сам staging во время этого сценария не менялся. Архивы/карты/config заново заморожены однократным чтением:292 файла,5 874 376 462 байт. Тесты открывали только копии, frozen hashes неизменны; live archive/map handles и denied reads0.

Ровно два запуска EXE, без пересборки и без дополнительных матриц. Первый23,610с, exit0: запуск Rebuild64, постоянный индикатор, настоящий maps browser с Qt double-click, zone1/5 принятых соседей/pending0, native snapshot, один просмотр модели и его закрытие, штатное MainWindow close (`realClose:true`). Сразу после закрытия перечислен cache/rebuild64: `known-folders/10/rebuild64: []`; startup-*.json/manual-retry-required отсутствуют. Второй с теми же настройками/кешем5,798с, exit0, снова Rebuild64, штатный close; журнал второго запуска содержит ровно требуемую строку `Renderer startup requested:Rebuild64 latched:0 interrupted:0`. Падений нет, видимых окон и неожиданных диалогов0, выход за разрешённую область записи0. Результат первого не подменяется предыдущим неуспешным запуском прямо из staging.

Ошибки постобработки обвязки: после первого успешного EXE inventory первоначально прочитан из старой папки; после второго ожидалось накопление двух строк в workflow.log, хотя он перезаписывается при запуске. Перечень взят из настоящей папки первого процесса, журнал второго проверен отдельно. Дополнительных запусков EXE из-за этих ошибок не было. Evidence: `r2/two-start-result.json`, `r2/cache-after-first-close.json`, `case-r2-copy-accept288/runs/run-001/{process.json,exit.json,events.jsonl}`, `case-r2-copy-accept288/{process.json,exit.json,events.jsonl,logs/workflow.log,r2-viewport.png,r2-model.png}`. Предыдущие неуспешные журналы оставлены с границами проверки.

Итоговый staging `build-staging-288-rebuild64-offscreen-r2-20261107`:1365 путей, потерь0/добавлений0, финализатор PASS, свежий QM3409. EXE SHA256 `cbc09ff557b9c79a5e91cd1b3d55043cc432047c470797716ee5e2ab02b51ea3`; DLL SHA256 `feb69972ef4e20f5c862505c0b1ec2887878797e3c3a02ffbe735c13b3c402f5`. Рендер/шейдеры и выбор вида не менялись; собственные shared-строки коммитятся отдельно, чужие правки сохранены. Тестовые данные и runtime-копия удаляются после коммита; логи/кадры/manifest сохраняются, квитанция `r2/cleanup-copy-receipt.json`. Установки/push нет.


## Установка 288 R2 — 2026-11-07

По прямому поручению владельца «ставь 288» установлен неизменённый `build-staging-288-rebuild64-offscreen-r2-20261107` в `artifacts/EI-Mapper-Ataman-2-first-test`. HEAD/main перед установкой точно `281b02489bac88aadfa0b3a27ec5eb900e5fade9`; diff принятого SHA против рабочего `packaging/*` пуст. Использован только репозиторный `packaging/install_program.ps1`. Пересборки, финализации установки, запуска маппера/игры, загрузки DLL и computer-use не было.

Установщик: `INSTALLED verified=1365 changed=11 added=0 removed=0`. Все 1365 управляемых файлов независимо сверены со staging, включая EXE, ataman_rebuild64.dll, runtime/EIMapperEnduranceJmvBridge.plg, runtime/EIMapperEnduranceRuntimeHost.exe и translations/ei_mapper_ru.qm. Контрольные EXE `cbc09ff557b9c79a5e91cd1b3d55043cc432047c470797716ee5e2ab02b51ea3`, DLL `feb69972ef4e20f5c862505c0b1ec2887878797e3c3a02ffbe735c13b3c402f5` совпали до установки и после. `EIMapperMcp.exe --version`: `EIMapperMcp 288`. Полный файловый SHA-снимок staging после установки неизменен.

Откат: `artifacts/rollback287-to288-rebuild64-offscreen-20261107`; собственный `rollback.ps1 -VerifyOnly` вернул `ROLLBACK_VERIFIED`. Прежние откаты сохранены. 15956 пользовательских файлов вне managed/служебного retired-учёта сохранили SHA; настройки, выбор вида и соседних карт не изменялись. Регистрация мостов не менялась.

PID мостов до замены: 17412, 49824, 55140, 74000, 60108, 74448, 40888, 54716, 11956, 43988, 53412, 19832. Все 12 живы после замены; процессы не завершались. Маппер/runtime из установки и игра перед заменой и после отсутствовали; чужой тестовый маппер из work/rebuild64-units-289-* сохранён. Занят только заменяемый `EIMapperMcp.exe`, отодвинут в `EIMapperMcp.exe.old-ataman-4edd8b4072ae4ae18dfdef42068fa6ae`. Два прежних .old-ataman файла сохранены: `EIMapperMcp.exe.old-ataman-0faf7fe49f064623b59d728f72a63a89`, `EIMapperMcp.exe.old-ataman-7afec64e64ae4104bb1ce9512c6815cb`; штатная очистка: removed=0 retained=2. `subst` в конце пуст.

Статус этапа **PARTIAL**: сохраняются все замечания выше, включая время конструктора/Present, appearance/stst8, unlit minimap, числовое сравнение геометрии, гонку смены карты/device-loss и неполную матрицу просмотров. Установка не закрывает эти ограничения; видимая приёмка за владельцем. VERSION/код и чужие hunks не менялись, push не выполнялся.
