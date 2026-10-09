# EI-PAINT-DOCS-SYNC-20261010 — синхронизация документов

Дата: 2026-10-10. Ветка **main**, база на старте:
**33d5c29f6cbca25483dcc589a0efa43ad3db42a7**, VERSION **128**.
AGENTS.md прочитан и повторно сверён перед передачей:
SHA256 **F4B7FDD5EA82FCDF43D002D23D64EC36BE853650F054EDF70376527CD6432EC3**.
Поручение владельца через сопровождающего Claude; только четыре разрешённых
документа. Сборка, установка, UI, игра, сеть и запись в .git не выполнялись.

## Результат и причина расхождений

Обновлены MVP и обзор атласа: совместные Ground + Overlay, Save MPR на месте,
16 384 слота с предупреждением выше 4096, граница исторического runtime,
HDPack_Terrain.res с Ataman 52, семантика MPR с 122 и режимы рисования.
Дополнительно исправлены schema 7 → текущая 10, описание математических путей
генерации и устаревшее утверждение об автоматическом пересчёте AI.
Исторические acceptance/reference и договор оформления опросников сохранены
с явным указанием границ, без выдачи старых тестов за новые.

Причина — документы сохраняли ранний MVP-контракт после развития программы.
Для R151 уточнена область: упаковка исходных тайлов не объединяет повёрнутые
эквиваленты; генератор продолжает канонизировать сигнатуры и кэшировать их.
Метка R151 взята из задания/истории, а текущее поведение сверено напрямую.
Пакетный BUILD_INFO R151 содержит SHA другой исторической линии, отсутствующий
в этом checkout; он не использован как база или доказательство текущего кода.

Восстановлен самостоятельный UTF-8 гайд по стыкам: шесть полей контракта,
ограничения, система координат, JSON, пример пары стен, соглашение +Z/+Y,
порядок настройки, состав передачи и границы проверки. Исходный пропавший
файл не найден/не скопирован: новый текст восстановлен по текущему коду.
ROADMAP:474–479 подтверждает отсутствие прежнего файла; git ls-files также
не находит этот путь среди отслеживаемых файлов.

## Матрица утверждений: текущий код и границы

Ссылки указывают начало проверенного участка; последующие строки функции
изучены для вывода. Исторические версии 52/122 дополнительно сверены по
CHANGELOG, но действующий контракт подтверждён кодом.

| Утверждение | Источник |
|---|---|
| Три enum-режима Modern/ClassicAuto/Manual; Smart не отдельный enum | [source/ui/landscape_tools.h:49](../../source/ui/landscape_tools.h#L49), [source/ui/landscape_tools.cpp:733](../../source/ui/landscape_tools.cpp#L733) |
| Modern automatic transitions по умолчанию true; отключение оставляет uniform-покраску | [source/ui/landscape_tools.cpp:510](../../source/ui/landscape_tools.cpp#L510) |
| Classic Safe по умолчанию true; при отключении возможен local uniform hard edge | [source/ui/landscape_tools.cpp:522](../../source/ui/landscape_tools.cpp#L522), [source/ui/landscape_tools.cpp:1983](../../source/ui/landscape_tools.cpp#L1983) |
| Manual пишет физический слот/поворот | [source/ui/landscape_tools.cpp:1991](../../source/ui/landscape_tools.cpp#L1991), [source/render/view.cpp:21117](../../source/render/view.cpp#L21117) |
| Палитры Ground/Underwater/Overlay, thumbnails и contextual cards | [source/ui/landscape_tools.cpp:1700](../../source/ui/landscape_tools.cpp#L1700), [source/ui/landscape_tools.cpp:1819](../../source/ui/landscape_tools.cpp#L1819) |
| Накопление/разрешение stroke и итоговая отменяемая транзакция | [source/render/view.cpp:20031](../../source/render/view.cpp#L20031), [source/render/view.cpp:20251](../../source/render/view.cpp#L20251), [source/render/view.cpp:20981](../../source/render/view.cpp#L20981) |
| Current authored → stock library → generation | [source/documents/terrain_virtual_atlas.cpp:3120](../../source/documents/terrain_virtual_atlas.cpp#L3120) |
| Uniform source обязателен; provenance и композиция нескольких материалов | [source/documents/terrain_virtual_atlas.cpp:2665](../../source/documents/terrain_virtual_atlas.cpp#L2665) |
| Reviewed masks предпочтительны; существуют canonical mathematical paths | [source/documents/terrain_virtual_atlas.cpp:1344](../../source/documents/terrain_virtual_atlas.cpp#L1344) |
| В нулевых промежутках multi-family используется corner bilinear partition | [source/documents/terrain_virtual_atlas.cpp:2770](../../source/documents/terrain_virtual_atlas.cpp#L2770) |
| Generated signatures по-прежнему канонизируются через четверть оборота | [source/documents/terrain_virtual_atlas.cpp:2192](../../source/documents/terrain_virtual_atlas.cpp#L2192), [source/documents/terrain_virtual_atlas.cpp:3194](../../source/documents/terrain_virtual_atlas.cpp#L3194) |
| Generated typed pixel key: pixels + class + corners + route owner | [source/documents/terrain_virtual_atlas.cpp:41](../../source/documents/terrain_virtual_atlas.cpp#L41), [source/documents/terrain_virtual_atlas.cpp:3244](../../source/documents/terrain_virtual_atlas.cpp#L3244) |
| Authored упаковка сравнивает исходные pixels без перебора поворотов; semantics/class/owner должны совпадать, animation phases не объединяются | [source/documents/terrain_virtual_atlas.cpp:3444](../../source/documents/terrain_virtual_atlas.cpp#L3444), [source/documents/terrain_virtual_atlas.cpp:3483](../../source/documents/terrain_virtual_atlas.cpp#L3483) |
| Runtime class: corner majority, tie по preferred/painted type, unknown отказ | [source/documents/terrain_virtual_atlas.cpp:3046](../../source/documents/terrain_virtual_atlas.cpp#L3046) |
| Live animation blocks и opposite-layer references сохраняются/remap | [source/documents/terrain_virtual_atlas.cpp:3539](../../source/documents/terrain_virtual_atlas.cpp#L3539), [source/documents/terrain_virtual_atlas.cpp:3767](../../source/documents/terrain_virtual_atlas.cpp#L3767) |
| DXT1 binary alpha, отказ graded alpha, premultiplied mipmaps | [source/resources/mmp_codec.cpp:139](../../source/resources/mmp_codec.cpp#L139), [source/resources/mmp_codec.cpp:821](../../source/resources/mmp_codec.cpp#L821) |
| Legacy validated constant 1024 ≠ actual editor limit 16384 | [source/documents/terrain_virtual_atlas.h:21](../../source/documents/terrain_virtual_atlas.h#L21), [source/render/view.cpp:14806](../../source/render/view.cpp#L14806) |
| 64 слота на страницу, 256 страниц при 16384 | [source/documents/terrain_virtual_atlas.h:25](../../source/documents/terrain_virtual_atlas.h#L25), [source/documents/terrain_virtual_atlas.cpp:3850](../../source/documents/terrain_virtual_atlas.cpp#L3850) |
| Warning >4096, экспорт допускается для controlled testing | [source/render/view.cpp:14905](../../source/render/view.cpp#L14905), [source/render/view.cpp:14924](../../source/render/view.cpp#L14924) |
| Runtime исторически PASS до 4096; corrected 8192 FAIL; 16384 не запускался в ordered rerun | [docs/validation/EXPANDED_TERRAIN_ATLAS_RUNTIME.md:129](EXPANDED_TERRAIN_ATLAS_RUNTIME.md#L129) — свидетельство, не код и не новая проверка |
| Ground + Overlay вместе; sparse overlay и pin/remap | [source/render/view.cpp:14612](../../source/render/view.cpp#L14612), [source/render/view.cpp:14661](../../source/render/view.cpp#L14661), [source/render/view.cpp:14713](../../source/render/view.cpp#L14713) |
| Необходимая декодировка/нереализованные изменённые tuples отказывают публикации | [source/render/view.cpp:14667](../../source/render/view.cpp#L14667), [source/render/view.cpp:14700](../../source/render/view.cpp#L14700) |
| Save MPR передаёт текущий путь, новая MPR не обязательна | [source/render/view.cpp:9793](../../source/render/view.cpp#L9793), [source/render/view.cpp:15491](../../source/render/view.cpp#L15491) |
| Семантическое поле само по себе не новая artwork; ordinary route не публикует RES | [source/core/terrain_save_flow_policy.h:37](../../source/core/terrain_save_flow_policy.h#L37), [source/render/view.cpp:10509](../../source/render/view.cpp#L10509) |
| HDPack target, предупреждение о влиянии общей identity, staged validation/backup/rollback | [source/render/view.cpp:15108](../../source/render/view.cpp#L15108), [source/render/view.cpp:15185](../../source/render/view.cpp#L15185), [source/render/view.cpp:15350](../../source/render/view.cpp#L15350), [source/render/view.cpp:15502](../../source/render/view.cpp#L15502) |
| История: HDPack с 52, embedded semantic state с 122 | [CHANGELOG.md:466](../../CHANGELOG.md#L466), [CHANGELOG.md:39](../../CHANGELOG.md#L39); текущие пути см. две следующие строки |
| ei_mapper_terrain_semantics.json, embedded MPR и Ground/Overlay/bindings | [source/documents/terrain_semantic_field_store.cpp:223](../../source/documents/terrain_semantic_field_store.cpp#L223), [source/render/view.cpp:13857](../../source/render/view.cpp#L13857) |
| Embedded MPR читается первым, RES fallback при отсутствии | [source/render/view.cpp:13916](../../source/render/view.cpp#L13916) |
| Tile binding исключает heights/maxZ, включает MP/tiles/overlay materials | [source/documents/terrain_semantic_field_store.cpp:193](../../source/documents/terrain_semantic_field_store.cpp#L193) |
| AI edit invalidates preview, не запускает вычисление | [source/render/view.cpp:4399](../../source/render/view.cpp#L4399) |
| Exact-bound presets и внутренние transition-support компоненты | [source/render/terrain_atlas_presets.cpp:642](../../source/render/terrain_atlas_presets.cpp#L642), [source/render/terrain_atlas_profile.cpp:1150](../../source/render/terrain_atlas_profile.cpp#L1150) |
| Visual corners NW/NE над SW/SE; JSON NW,NE,SE,SW и migration | [source/render/terrain_atlas_profile.cpp:261](../../source/render/terrain_atlas_profile.cpp#L261), [source/render/terrain_atlas_profile.cpp:510](../../source/render/terrain_atlas_profile.cpp#L510), [source/ui/terrain_atlas_review_dialog.cpp:713](../../source/ui/terrain_atlas_review_dialog.cpp#L713) |
| Schema writer 10: allowedLayers, tileType, textureCount, routeOwner | [source/render/terrain_atlas_profile.cpp:261](../../source/render/terrain_atlas_profile.cpp#L261), [source/render/terrain_atlas_profile.cpp:434](../../source/render/terrain_atlas_profile.cpp#L434) |
| Четыре угла требуют общий допустимый слой, family может позволять оба | [source/render/terrain_atlas_profile.cpp:776](../../source/render/terrain_atlas_profile.cpp#L776), [source/render/terrain_atlas_profile.cpp:848](../../source/render/terrain_atlas_profile.cpp#L848) |
| Review drag/drop/corner edits/thumbnail/confirm, сохранение и reset | [source/ui/terrain_atlas_review_dialog.cpp:700](../../source/ui/terrain_atlas_review_dialog.cpp#L700), [source/ui/terrain_atlas_review_dialog.cpp:1002](../../source/ui/terrain_atlas_review_dialog.cpp#L1002), [source/ui/terrain_atlas_review_dialog.cpp:1038](../../source/ui/terrain_atlas_review_dialog.cpp#L1038), [source/ui/terrain_atlas_review_dialog.cpp:1096](../../source/ui/terrain_atlas_review_dialog.cpp#L1096) |
| Portable profile paths и выбор explicit override | [source/render/terrain_atlas_profile.cpp:1210](../../source/render/terrain_atlas_profile.cpp#L1210), [source/render/view.cpp:11795](../../source/render/view.cpp#L11795) |
| connectors_ui.cpp — только status/buttons/icon helpers | [source/layout_components/connectors_ui.cpp:22](../../source/layout_components/connectors_ui.cpp#L22), [source/layout_components/connectors_ui.cpp:56](../../source/layout_components/connectors_ui.cpp#L56) |
| Поля, finite/norm/cross thresholds, ≤128 и уникальные id | [source/documents/model_connections.cpp:6](../../source/documents/model_connections.cpp#L6), [source/documents/model_connections.cpp:21](../../source/documents/model_connections.cpp#L21), [source/documents/model_connections.cpp:26](../../source/documents/model_connections.cpp#L26) |
| +Z normal/+Y up/+X up×normal — кадр, не автоматический импорт Empty | [source/documents/model_connections.cpp:14](../../source/documents/model_connections.cpp#L14) |
| Type equality, opposing normals, aligned up, формула position/rotation | [source/documents/model_connections.cpp:46](../../source/documents/model_connections.cpp#L46) |
| Catalog рядом database, canonical model key и connections; unknown fields сохраняются | [source/documents/technical_model_catalog.cpp:52](../../source/documents/technical_model_catalog.cpp#L52), [source/documents/technical_model_catalog.cpp:84](../../source/documents/technical_model_catalog.cpp#L84), [source/documents/technical_model_catalog.cpp:183](../../source/documents/technical_model_catalog.cpp#L183), [source/documents/model_connections.cpp:40](../../source/documents/model_connections.cpp#L40) |
| schema1/32MiB, invalid JSON/external modification guard, atomic save | [source/documents/technical_model_catalog.cpp:98](../../source/documents/technical_model_catalog.cpp#L98), [source/documents/technical_model_catalog.cpp:139](../../source/documents/technical_model_catalog.cpp#L139) |
| UI: free type, численные векторы, UUID и явное Save/Cancel | [source/ui/model_connection_editor.cpp:190](../../source/ui/model_connection_editor.cpp#L190), [source/ui/model_connection_editor.cpp:211](../../source/ui/model_connection_editor.cpp#L211) |
| UI: направление мышью, up вместе, Esc gesture, click surface | [source/ui/model_connection_editor.cpp:71](../../source/ui/model_connection_editor.cpp#L71), [source/ui/model_connection_editor.cpp:139](../../source/ui/model_connection_editor.cpp#L139), [source/ui/model_connection_editor.cpp:198](../../source/ui/model_connection_editor.cpp#L198) |
| Один объект, scale position, видимые соседи, Tab и применение pose | [source/render/view_model_connections.cpp:27](../../source/render/view_model_connections.cpp#L27), [source/render/view_model_connections.cpp:48](../../source/render/view_model_connections.cpp#L48), [source/render/view_model_connections.cpp:109](../../source/render/view_model_connections.cpp#L109) |

## Проверки

- Статическая сверка утверждений с указанными функциями; сборки и продуктовых
  тестов **0**, UI-блокировка не требовалась.
- Python-пробник JSON/геометрии: **30 PASS / 0 FAIL** до исправления кодировки;
  после исправления выполнен повтор с дополнительными проверками кириллицы
  и отсутствия замены текста вопросительными знаками: **37 PASS / 0 FAIL**.
  Все **95** относительных ссылок/строк в четырёх документах проверены.
- JSON: schema1, 2 стыка, 6 трёхкомпонентных векторов, уникальные ID, пределы,
  единичность/ортогональность и одинаковые type.
- Формула кадра из model_connections.cpp рассчитана отдельно: соединение
  left второй стены с right первой даёт identity rotation, translation [4,0,0],
  совпадение [2,0,1], противоположные normal и согласованные up.
  Это численная проверка примера, не запуск Qt/магнита.
- UTF-8 и readback всех четырёх файлов, проверка относительных ссылок и
  допустимых номеров строк источников; scoped git diff --check.
- Запись документов: временный файл в той же папке, flush/fsync и os.replace
  целого файла. Во время записи другим читателям доступны целые версии.

Первый PowerShell pipe использовал ASCII и заменил кириллицу/типографику.
Это выявлено чтением готового файла; поток переведён в UTF-8, файл восстановлен
атомарно, проверка усилена. Первую проверку не считать подтверждением кириллицы.

## Ограничения и отложенные находки

Сеть, зависимости, сборки, MPR/MOB/RES и постоянная установка не затронуты.
Runtime 4096 — историческое доказательство конкретного workload, не свежая
приёмка текущей версии. Новая UI/игровая приёмка не заявляется.
Экспортёр Blender, реальные модели и импорт Empty не проверялись; +Z/+Y
обозначено предложенным соглашением, а не реализованным импортом.
Специализированные навыки evil-islands-modding и private-engine-reference
не доступны; приватные источники не читались, обычного кода проекта достаточно.

Вне владения: terrain_virtual_atlas.h:21–24 хранит старый комментарий/constant
validated=1024; класс на :200–202 имеет обобщённый комментарий pixel dedup.
Это не фактический editor ceiling. При отдельном разрешении сопровождающий
может уточнить комментарии и смысл legacy constant; в этой задаче код не менялся.
Исторический договор оформления опросников сохранён как договор, без новой
сверки внешних исследовательских генераторов. Существующие чужие изменения
не перечисляются как результат и не включаются в список коммита.

## Точный список для коммита сопровождающего

Изменены:

- docs/MODERN_TERRAIN_EDITOR_MVP.md
- docs/TERRAIN_ATLAS_REVIEW.md

Созданы:

- docs/MODEL_CONNECTORS_FOR_MODEL_AUTHOR.txt
- docs/validation/ATAMAN_PAINT_DOCS_SYNC_20261010.md

## Предлагаемый текст ROADMAP

### EI-PAINT-DOCS-SYNC-20261010 — синхронизация terrain и гайд стыков

- Поручение владельца через Claude 2026-10-10, main,
  база 33d5c29f6cbca25483dcc589a0efa43ad3db42a7, VERSION 128.
  Только документация; программа, общие файлы, Git, сборка и установка исключены.
- Результат: MVP/обзор соответствуют текущим Ground+Overlay, сохранению на месте,
  ceiling16384/warning4096, HDPack52/embedded semantics122, режимам рисования
  и разграничению authored dedup/generated canonicalization; восстановлен
  самостоятельный UTF-8 MODEL_CONNECTORS_FOR_MODEL_AUTHOR.txt по коду.
- Статус: документация подготовлена и проверена; evidence
  docs/validation/ATAMAN_PAINT_DOCS_SYNC_20261010.md. VERSION остаётся128.
  Следующий шаг сопровождающего: scoped-коммит четырёх документов;
  по EI-MODEL-CONNECTOR-HANDOFF-20261002 — передача гайда автору и проверка
  асимметричного экспортированного образца. Родительские приёмки не закрыты.

Для старого EI-MODEL-CONNECTOR-HANDOFF-20261002: заменить фразу об отсутствии
файла на «Восстановлен 2026-10-10 по коду128; см. EI-PAINT-DOCS-SYNC-20261010».
ROADMAP здесь не редактировался.

## Предлагаемая запись CHANGELOG (документы, без новой версии)

- EI-PAINT-DOCS-SYNC-20261010: синхронизированы Modern terrain MVP и обзор
  атласа с Ataman128: Ground+Overlay, Save MPR на месте, 16384/warning4096,
  HDPack и семантика MPR; уточнены режимы, schema10 и область дедупликации.
  Восстановлен UTF-8 гайд стыков для автора моделей с JSON и локальными осями.
  Проверки/ограничения: docs/validation/ATAMAN_PAINT_DOCS_SYNC_20261010.md.
  Только документы, VERSION128, без сборки/установки.
