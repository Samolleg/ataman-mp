# Ataman266 — задержка загрузки MOB

Задача EI-MOB-LOAD-266-20261101. Жалоба владельца01.11: после исправлений аудита
любой MOB стал загружаться долго. База main
`8d23b2842834f9e44f7afee1e132833e8b1b17b4` / установленная265; зависимость —
переданные256–265. Работа в main, без push, запуска видимого редактора/игры
или записи исходных игровых данных. Чужие ROADMAP/QM сохранены.

## Причина и изменение

Последний пользовательский workflow относится к262: открытие zone3obr с семью
AddMob заняло39116мс, из них26060мс — MOB-фаза. Даже зависимые MOB с1–8 объектами
занимали1–2,4с. Между стадиями parser/projection progress вызывал processEvents,
а работающий viewport продолжал рисовать частично загруженную сцену, запускать
diagnostic readbacks, перестроение spatial/support данных и подготовку визуалов.
Это общий путь загрузки. Журнал не доказывает, что проблема появилась именно265.

Изменение программы ограничено двумя entry points в `source/render/view.cpp`:
`updateWindow` и `paintGL` не вызывают рендер, пока установлен существующий
workspace-load flag. Progress/UI event pump работает; после завершения или
ошибки загрузки существующий endWorkspaceLoad снимает flag и обновляет сцену.
Чтение, структурная валидация MOB, backup, AddMob traversal, сохранение данных,
outliner/texts.res и стабильная сортировка экспорта не изменялись.

Предыдущие проверки outliner/экспорта не измеряли эту работу viewport при
загрузке. Новый regression вызывает настоящий paintGL из event pump на логически
видимом native QWidget, скрытом через WA_DontShowOnScreen. Это не оконный FPS-тест
и не ручная приёмка пользователя.

## Воспроизведение и измерения

Приватная копия102 текущих входов: ресурсы профиля и zone3obr с зависимостями,
без writes в живую игру. Одинаковые source data/settings/viewport2214×1312,
настоящие DejaVu database/character/equipment providers. MPR editor initialization
завершена до замера MOB. Время первого кадра учитывается отдельно, чтобы перенос
паузы за закрытие progress не выдавался за её устранение.

| Финальные прогоны |265|266|
|---|---:|---:|
| MOB family load |15185мс|9667мс|
| Первый кадр после load |270мс|2214мс|
| Load + первый кадр |15455мс|11881мс|
| Progress event callbacks |33|33|
| Scene frames во время load |33|0|
| Чистые документы / объекты |8 /2019|8 /2019|

В более ранних прогонах с настоящими providers ready time был14483мс на265
и6203–7644мс на266. Нагрузка ПК менялась, поэтому это локальные измерения,
а не обещание такого же ускорения для любой карты. Детерминированный результат —
устранение33 промежуточных кадров при сохранении33 progress callbacks.

Evidence: `work/mob-load-266-20261101/baseline265-final/`, `fixed266-final/`,
`final-comparison.json`, `owner-workflow262.log`.

Ранние диагностические прогоны не подключали providers персонажей и затем
сравнивали модели до завершения asynchronous layer composites. Так получились
различия display Z у заглушек/ещё не готовых персонажей. Это не было принято
за готовый результат. Финальный стенд подключает настоящие providers и отдельно,
вне замера load/первого кадра, дожидается needsVisualResolution==false у всех
персонажей. После этого все2019 display-позиций совпали побайтно; все исходные
координаты также совпадают. Проверка не меняет режим работы установленной программы.

- Authored-position SHA256:
  `37ed7289f1c66de15d6f8a15da686583727c0bbbbdfbaffa901631edb1f81cf0`.
- Fully resolved display-position SHA256:
  `49fe863365a3f4f135a5eabb173b5148dd65815d87ab7792e34faf14984794ec`.

## Достаточный регресс

29 native PASS на266: реальная загрузка8 MOB, основной документ остаётся активным,
2019 объектов/clean state, завершение async composites, отсутствие промежуточных
кадров, первый готовый кадр, отказ отсутствующего файла с сохранением прежних
документов, возобновление viewport после ошибки, замена workspace и standalone
MOB без MPR со всеми8 объектами.

Ещё51 native PASS: полный openMapPair zone9, MCP/export, сохранение revision/context,
реальная geometry/source identity tree28 и отказ публикации при missing resource.
Все11 PNG побайтно равны ранее проверенной265. Evidence:
`work/zone9-export-255/mob-load266-regression-priority/`,
`work/mob-load-266-20261101/export-equality.json`.

Первый полный export-регресс **FAIL / EXPORT_TIMEOUT**:120014мс в resolving_resources.
В этот период CPU был100%; одновременно работали четыре внешних Blender и3ds Max,
память — около7,6ГиБ свободно из31ГиБ. Повтор проведён с AboveNormal только у
нового собственного hidden-test процесса. Timeout120с и все guards сохранены;
чужие процессы и приоритет установленного редактора не изменялись. Повтор PASS,
его время не используется как performance-замер. Исходный timeout не скрывается
и не переименовывается в PASS. Evidence: `host-load-evidence.json` и оба run directories.

## Передача

**Установлена266** в `artifacts/EI-Mapper-Ataman-2-first-test`.
Исходники сборки main: `902611ccfb2602505d3f6fa351296807ad9caefd`.

-Fresh source export/shadow Release266 и оба runtime собраны из исходников.
  Сверены532 tracked source files; raw/staged VERSION266 и четыре binary/runtime
  SHA совпали. Clean finalizer PASS:186→186 managed paths,0removed.
-Guarded installation PASS:186verified,8changed,0added/removed; сохранены
  метаданные13942 unmanaged файлов и SHA двух корневых JSON, user drift0.
-Installed manifest SHA256:
  `19801BD512ED98CEBAC0BB1F59056119E79F95DECD962381C270A8A0268EF637`.
-Installed EXE SHA256:
  `602F66D92DA632830654E46C0C02CC6D3EEB75378D97A5CDC1C0997AD8CA2911`.
-Verified rollback265: `artifacts/rollback265-to266-mob-load-20261101`.
  Прежний rollback262 удалён после проверки цепочки manifest, обоих backup,
  точного абсолютного пути, отсутствия reparse points и190 разрешённых файлов.
-Все102 приватных входа неизменны.101 live-вход совпал со снимком; live texts.res
  изменился извне за время работы. Эта задача его не записывала. Чужие QM и
  исходное содержимое ROADMAP сохранены.

Evidence: `work/mob-load-266-20261101/compiled-input-verification.json`,
`package.json`, `installation.json`, `rollback-retirement.json`,
`input-verification.json`. Ручная проверка обычного окна остаётся за владельцем.
