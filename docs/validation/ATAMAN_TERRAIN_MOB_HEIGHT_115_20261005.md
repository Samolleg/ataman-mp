# Ataman115 — положение MOB-объектов при правке рельефа

Задача:EI-TERRAIN-MOB-HEIGHT-115,2026-10-05. База main
bf93683a16ad595746a792eb794c5e64ced52b78, VERSION114→115.
Поручение: добавить выбор между движением объектов вслед за рельефом и
сохранением их высоты. Игровые данные не изменялись; игровые runtime skills
для приватных источников не использовались, работа выполнена на коде проекта.

## Поведение

В обычной «Правке рельефа» находится русская галочка «Перемещать объекты с
рельефом». По умолчанию включена. Выбор сохраняется в QSettings
ui/landscape/moveMobObjects. В режиме поверхностей/покраски галочка скрыта.
Подсказка объясняет, что без галочки нужно сохранить **MPR и MOB**.

Объекты мира, рычаги, факелы и ловушки хранят Z как смещение от земли:
worldZ=terrainZ+mobZ (`map_object_placement_policy.h`). Просто пропускать
проекцию нельзя: после открытия результат исчезнет. При выключенной галочке
снимок фиксирует исходное смещение и высоту земли до первого затрагивающего
stamp. По завершении: afterOffset=beforeOffset+(beforeTerrain-afterTerrain).
Незатронутые объекты сохраняют точный исходный Z; скрытые включаются.

Компенсация входит в ту же историю, что рельеф: локальный QUndoCommand или
companion-состояние общего world stroke. Undo/Redo устанавливают точные
before/after значения, независимо от последующего переключения галочки.
Настоящая MOB dirty проекция, свойства и тени обновляются; выделение и активный
MOB не переключаются. Идентичность основана на QPointer<CNode>, не MapID.
Состояние world history отказывается частично воспроизводиться, если затронутый
MOB/узел выгружен или другой MPR активен. История MPR не продвигается при отказе.
Индекс принадлежности строится один раз за применение, без повторного полного
поиска MOB для каждого объекта; компенсация выполняется один раз за штрих.

Существа сохраняют штатную отдельную привязку к земле. Источники света, звука
и частиц уже используют абсолютную высоту. Read-only фоновые MOB соседних карт
не редактируются. Галочка относится к основной земле, не к подъёму покрытий.

## Проверки

- Fresh shadow build115 и raw --version115:PASS. При первой компиляции исправлены
  ошибки нового диагностического harness (Windows near macro, сигнатуры и friend
  доступа); production feature затем собрана и проверена.
- Native --terrain-mob-height-check:19 PASS,0 FAIL. Собственный flat canvas из
  distributable particle-flat-7x5.mpr; свои MOB-объекты, без installed game data.
  Подъём с галочкой/без неё, четыре типа и скрытый объект, свет/вне кисти,
  dirty/clean MOB, точные Undo/Redo после смены галочки; binary MOB reopen и
  canonical MPR candidate reopen вместе сохраняют мировую высоту. Два stamp
  понижения в одной локальной команде и общий world stroke: точное Undo/Redo;
  удаление объекта безопасно блокирует частичное воспроизведение world history.
  Русская подпись загружена из production QM; снимок landscape-options.png
  просмотрен: чекбокс помещается в панели380px.
- landscape_tools_widget_tests:26 PASS,0 FAIL,0 SKIP; новая проверка default,
  сохранения и видимости галочки. Исправлена отсутствовавшая link dependency
  ui_theme.cpp в этом тестовом .pro; приложение её уже собирало.
- world_terrain_sculpt_tests:34 PASS,0 FAIL,5 SKIP. Пропуски — необязательные
  read-only installed-map cases zone2/11/12/13/6/4/triple; переменные ресурсов
  для них не задавались. Синтетическая общая кисть проверена.
- Native --ui-widget-check:exit1, прежние10 clipboard FAIL114, новых отличий
  flags нет:compatiblePasteCommit,compatiblePastePreview,componentContextPaste,
  componentPlatePaste,fieldCopyAvailable,fieldPasteCommit,fieldPastePreview,
  nonTextPropertyPaste,numericComponentPaste,selectedComponentCopy.
  Сравнение сохранено в ui-regression-comparison.json; это не общий UI PASS.
- Fresh staging/version/finalizer и отдельный validate_distribution:PASS;
 127 managed payloads. EXE71F07FDD5F3481DE70D96E5A228F2409EBD4ED544F5D93DD7239AE3435509E4A.
  После финализации package не запускался. Чужой source QM восстановлен
  byte-identical EDA31955A912758ACB5A7BDE043BBF2EB5F8A2606FA32DC52AFA4156CA74ED9F;
  новый перевод находится в проверенном EXE.

Доказательства:build-tests-115-terrain-mob-height/ (build-final.log,
terrain-mob-height-check-native/check.out,landscape-options.png,
landscape_tools_widget_tests/result.txt,world_terrain_sculpt_tests/result.txt,
ui-widget-check-native/check.out,ui-regression-comparison.json,
stage-version.out,finalize.log,validate-package.log).

Реализация в main:e95f52fa209693f89fe57046f43b59a2f59e8f57.
Постоянная установка:artifacts/EI-Mapper-Ataman-2-first-test,114→115 обновлена.
Редактор не работал во время замены.127 managed payloads и manifest verified;
6 программных замен с проверенным откатом114 в
artifacts/rollback-EI-Mapper-Ataman-v114-to-115-20261005.17890 неуправляемых
пользовательских файлов до/после имеют одинаковые пути и SHA256. Старый
проверенный owned rollback113 удалён; единственный rollback114 сохранён.
Raw/staged/installed EXE hash одинаковый; runtime PLG/host raw/staged/installed
также сверены отдельно. Отчёт install.log,user-files-before.json,
user-files-after.json,retire-rollback.log. Название stage114 в единственной
строке stage.log унаследовано из шаблона; реальный stage-version.out и VERSION
однозначно115. Это не новая версия114 и не другой комплект.

Ручная приёмка на пользовательской карте остаётся открытой: снять галочку,
поднять/опустить землю под деревом, Undo/Redo, сохранить MPR+MOB и открыть
повторно; затем включить галочку и проверить следование земле. Аудит выделения114
остаётся отдельным; его alpha-дефект этим изменением не исправлялся.
