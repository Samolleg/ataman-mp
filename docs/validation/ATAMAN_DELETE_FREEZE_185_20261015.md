# Ataman185 — пакетное удаление объектов

2026-10-15, EI-OBJECT-DELETE-FREEZE-185. База main5d730671d56972ba2120c25676a6e681100ec40e/184. Поручение: исправить задержку удаления, сохранить проверки ID, качество миникарты и Undo/Redo, проверить связанные сценарии и установить. Работа лично в main; чужие изменения и игровые оригиналы сохраняются. Диагностика: ATAMAN_DELETE_FREEZE_AUDIT_184_20261015.md.

## Реализация

- Delete применяет прежние CDeleteNodeCommand/CDeleteLogicPoint в прежнем порядке внутри CObjectEditBatchCommand. Каждая команда захватывает restore index непосредственно перед своим первым удалением, как раньше: заранее захватить все индексы и восстановить их в обратном порядке было бы неверно. Initial redo root-команды пропускается ровно один раз, поскольку дочерние изменения уже применены.
- collectObjectTreeData откладывает публикацию на конец вложенного object-edit batch. Тот же root группирует Undo и Redo. Проверка script references остаётся до открытия batch и до мутации. Перестроение логических индексов между дочерними командами сохранено: оно требуется последующим hash lookups.
- Обработчик indexChanged не сериализует документы повторно, если весь пройденный диапазон состоит из CObjectEditBatchCommand: они уже обновляют свои owning MOB. Для остальных команд остаётся прежний общий путь. Это также сохраняет владение при переключении активной карты и не помечает посторонние MOB изменёнными.
- Найденный связанным Save/Undo-тестом пробел: после сохранения удаления старый ID отсутствовал в disk snapshot и occupiedMapIds, хотя узел удерживался для Undo. occupiedMapIds теперь включает exact deleted nodes. Такие ID консервативно заняты до закрытия MOB, как session allocations; прочие проверки диапазонов, auxiliary IDs и семейных коллизий сохранены.
- Рендер/разрешение/пиксели/параметры обработки миникарты не изменены. Проверены invalidation после Delete и точные изображения после Undo/Redo, включая grading/MMP и защиту worker от устаревшей публикации.

## Сравнение184/185

Расширен tests/object_edit_performance_check.cpp. Native entrypoint работает с настоящими скрытыми MainWindow/CView, production object files и собственным MOB2000 объектов/1502140 bytes. Удаляются24 разнесённых по исходному порядку объекта. Старая184 использует сохранённые headers184, чтобы не смешивать ABI с185. Настройки/clipboard-файл/карты изолированы; системный clipboard этим тестом не изменяется. Структура donor MPR только читается, тестовая плоская карта и8 цветных alpha-cutout фигур создаются отдельно. Игровые данные и сгенерированные изображения не входят в Git/пакет.

| Операция |184, мс|185, мс|
|---|---:|---:|
|Delete24|4795|69|
|Undo Delete24|1269|65|
|Redo Delete24|5018|74|
|Публикации MOB/tree на Delete, Undo, Redo|24/24 на действие|1/1 на действие|

Это последовательные измерения одного собственного набора, не p95 и не гарантия времени на любой карте. Окончательная185: copy121ms, paste preview324ms, move36ms/Undo36ms/Redo35ms. Minimap full capture138ms, GUI portion automatic35ms; heartbeat24 pulses/max gap6ms во время CPU worker.

Побайтно совпали между184/185:

- Deleted MOB projection: `b6ae92463e9e3e6f8e68b690c8c3a64c26d8581d374106e816093bf7afebfbd9`.
- Clipboard JSON: `ec7947747de37704093f544a6a86f34fbd0b3bb9b72952005176a254781470bd`.
- MOB после move Undo/Redo: `6faaf50ea21c291cea16c788607d614c8698d52b403a2cf6d6b07cb10e83ba84`.
- Terrain hits: `0a7e141fc3c24802595f785d471fcd0592c22617cb07baaff0fdd114a6c0a255`.
- Minimap lit: `f44a3517a89038d076298a057eeb95838e16ef45726095dfa9c410554b11bfeb`; unlit: `2945de3c84d2c122eddb2793439846cd71f7d6ef11038dda0f8ded2713f6461d`.

Native185:55 PASS/0 FAIL. В том числе одиночный/групповой/пустой Delete, exact pointers/order/bytes/clean state, отмена и подтверждение script warning, другие MOB и Undo после переключения owner, все четыре вида logic points, mixed owner+child, validated save candidate/reopen, настоящее atomic MOB save, Undo/Redo после Save и семейное выделение нового ID12024 вместо удерживаемого10000. Full review3072², lit/unlit/game1024², удаление видимой геометрии и точные Undo/Redo pixels/graded MMP PASS; async generation/context/explicit/embedded/lock/gesture guards PASS.

Связанный регресс: assembly catalog/ID/atomic family placement91 PASS, group rotation26, model connection42, pointer modes56, object hierarchy31, mixed/empty property selection26 — всего327 адресных утверждений с native185. После последней правки occupiedMapIds повторены именно затронутые ID-suite91 и native55; остальные успешные проверки неизменных путей переиспользованы. Properties26 собраны с финальными production objects185.

Штатный UI-widget-check: exit1, старый порог выбора объекта с7164 textures/493 models: median346.541ms при лимите200ms. Gallery на этом прогоне прошла. Функциональные flags свойств/истории/мини-карты прошли; это не полный UI PASS. Проверка выполнена до финального дополнения occupiedMapIds; относящийся UI-код после неё не менялся.

Промежуточные native-прогоны не считались PASS: тест сначала использовал неподдерживаемую форму ссылки RemoveObject(10000) и не очищал историю выделения перед проверкой количества команд. После корректного GetObject и очистки history baseline23/0. Дополнительные Delete/Undo запускали реальный автоматический worker миникарты до замера следующей генерации; harness теперь дожидается завершения уже инвалидированного worker, затем сравнивает свежую генерацию. Save/ID-проверка выявила настоящий пробел occupiedMapIds, устранённый в185. Финальный набор55/0, без отключения проверок качества.

## Сборка и передача

Fresh Release: build-release-185-delete. Папка доказательств: build-delete-185-20261015, comparison.json, build-receipt.json, final-probe-receipt.json. Исходный SHA и hashes финальных EXE/runtime/source files фиксируются перед staging. Новая staging: build-staging-185-delete, передача в постоянную artifacts/EI-Mapper-Ataman-2-first-test по docs/TEST_BUILD_HANDOFF.md. Итог guarded install и сохранности owner files записывается после выполнения.

Ручная приёмка владельцем удаления на привычных картах остаётся отдельной. Интерактивно редактор не запускается; новые ядра переходов не возвращены.

## Итог установки185

Source main7a984ce13f2848e838a45e2e9025380bb9b8bf2a. Установленный редактор был закрыт; fresh process guard, backup, atomic replacement/readback PASS. Fresh stage/finalizer PASS, установлен каждый из129 managed files;6 заменены/1 добавлен/1 прежний managed report удалён (счётчик changed7 включает удаление). Все17067 пользовательских файлов вне manifest побайтно сохранены; owner digest `E27CFCAF385413E6727E8D45337F2A6E0B1C7F2FC08864C7E64C69D53F16D5BF`.

EXE `4d163a8035b0fccc857920338487739909e127c13d0a02275c53abdc956c4a46`, manifest `e38b53ce79a0f77a76b845dc9688911e7f096a748b6f47bcd03c857bc0606adf`. Runtime bridge `bec44c86cd2d4cd4cb9c3376804bbfd3d90e4d480ec588be733df7618df72342`, host `3ec0413e27dcda1d984bdfb06c4e8f1ac7fb83e4d082bfea264f3ead7ca72289`.

Постоянная папка: artifacts/EI-Mapper-Ataman-2-first-test. Резерв184 artifacts/rollback184-to185-delete-20261015 проверен по полному прежнему manifest; прежний точно опознанный резерв183 artifacts/rollback183-to184-edit-20261015 удалён по договору (19579633 bytes). Остальные исторические каталоги сохранены. Receipts: build-delete-185-20261015/{staging-receipt,install-receipt,rollback-retirement}.json.

Финализатор на пользовательской установке не запускался; редактор интерактивно не открывался. После finalization staging не запускалась и не менялась; этот addendum добавлен только в исходный отчёт. GitHub185 не обновлялся. Ручная приёмка владельцем остаётся открытой, общий UI timing limit выше не выдан за PASS.
