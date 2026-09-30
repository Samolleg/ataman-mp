# Ataman 81 — EI-MODEL-GALLERY-81, 2026-09-30

База: ebdd8a7c5c5f1f40c3c9512c125d835bc75e47f6; ветка codex/model-gallery-v81.
Владелец подтвердил работу80 и запросил витрину, русские имена и постоянный JSON.

## Реализация и границы

- `model-catalog.json` рядом с EXE: schemaVersion1, модели с russianName,
  профили упорядоченных mapRoots с modelTextures и scanCache. Пользовательские
  записи/неизвестные поля сохраняются; JSON не входит в программный манифест.
- Первое чтение MOB асинхронно по одному файлу за событие; последующие открытия
  сверяют размер/время каждого источника, читают только изменённые. При смене
  профиля чужие пары не используются. Пользовательские списки текстур не
  заменяются результатом сканирования. Сохранение QSaveFile, внешнее изменение
  и повреждённый JSON вызывают отказ записи без затирания файла.
- Витрина: поиск по техническому имени и русской подписи, редактирование подписи,
  текстуры связанной модели перед общим списком, ручная привязка и выбор плитки.
  Русские подписи изначально пусты; они не меняют имена ресурсов или MOB-объектов.
- Превью используют независимые read-only индексы RES и CPU-парсер MOD/BON/FIG8;
  не используют GL-контекст/узлы сцены или singleton-кэш из worker. Один worker,
  только видимые плитки, кэш128, лимит64MiB на payload. При закрытии ожидается
  текущий worker. Незнакомый/неподдерживаемый ресурс показывает отсутствие превью.

## Проверки

- Fresh build-release-81-model-gallery через build_release.cmd, версия EXE81.
  Существующий dirty QM восстановлен byte-identical: SHA256
  EDA31955A912758ACB5A7BDE043BBF2EB5F8A2606FA32DC52AFA4156CA74ED9F.
- endurance_document_tests:187 PASS,0 FAIL,18 SKIP отсутствующих внешних fixtures.
  Новый случай проверяет имя/поиск, сохранение и перезапуск без чтения неизменного
  MOB, изменение файла, изоляцию профиля, ручной список текстур, unknown keys,
  отказ при внешней правке и повреждённом JSON. Старые тесты сочетаний сохранены.
- resource_browser_core_tests:24 PASS,0 FAIL,0 SKIP с read-only ресурсами
  DejaVu/Base. Синтетический textured renderer проверяет пиксели/границы/alpha;
  stbuho45/hadoganveranda00 даёт узнаваемое превью. Первый probe с одними
  mod-архивами FAIL: модель принадлежит Base; после добавления правильного
  Base provider тест проходит. Это не исправление или копирование игровых файлов.
- --model-gallery-check:PASS. Производственное окно с252 моделями проверяет
  видимую загрузку, поиск по обоим именам, редактирование и повторное чтение JSON,
  выбор технического ID и текстуры, Escape. --database-creation-check:PASS
  создание/повторное чтение технических объектов, IDs и undo/redo.
- --ui-widget-check:FAIL. В последнем прогоне восемь false:
  compatiblePastePreview/Commit, fieldPastePreview/Commit, fieldCopyAvailable,
  selectedComponentCopy, componentPlatePaste, componentContextPaste.
  EXE80 в той же среде даёт тот же отказ. Предыдущий прогон имел ещё два false
  nonTextPropertyPaste/numericComponentPaste; нестабильность clipboard не скрыта.
  Новый modelGalleryWorks=1. До windeployqt сырой81 не имел imageformat-плагина:
  select/move/rotate/scale icon null и compactToolbarWorks=0; после развёртывания
  complete Qt runtime compactToolbarWorks=1. Программные кнопки не менялись.
- Логи: build-tests-81-model-gallery/{document-result.txt,real-resource-result.txt,
  deployed-model-gallery-check.out,deployed-database-creation-check.out,
  deployed-ui-widget-check.out,v80-ui-same-cwd.out}. Не входят в Git/пакет.

## Оставшаяся проверка

Скрытые автоматические окна не заменяют проверку владельцем: открыть витрину
в обычном сеансе, дать название модели, перезапустить редактор, выбрать и
разместить модель/текстуру, проверить вид в игре. Игра и пользовательские карты
не запускались/не изменялись. Старые задачи патрулей, воды, кисти и сохранения,
общий отказ clipboard не закрыты этой функцией.

## Передача

- Проверенный коммит кода: `7ad43b055cf97fa92fcfc4367ccf49a86667f87e`.
  Сборка выполнена из этих исходников до локального коммита; после прогона
  изменялись только документы передачи. В VERSION81 и runtime EXE81.
- Постоянный путь: `C:\GOG Games\maped\ei-mapper-ataman\artifacts\EI-Mapper-Ataman-2-first-test`.
  EXE SHA256 `3D6B6957B1CD7BCC2D7EF68D86020C726B94808682BF75D6F1BA31588DC5ECB5`.
- Новая staging-папка build-staging-81-model-gallery создана с нуля, только
  по прежнему управляемому манифесту; свежие EXE/runtime/VERSION/текущие notes.
  windeployqt с matching MinGW compiler-runtime. EXE staging/tested hash равны,
  --version81 проверен до финализатора. Finalizer и independent validator PASS.
- При обновлении editor/runtime не работали.127 управляемых файлов проверены,
  ровно6 заменены после проверки backup hash, манифест заменён последним.
  Пользовательские файлы не копировались/не удалялись, settings SHA до/после равен
  349FFF5BEFA71C70E90434FD3CC83564E36CFE4F1213221A0CBAD4AC785AABA3.
  model-catalog.json пока отсутствовал: будет создан при первом входе в режим
  технических объектов; его нет в управляемом манифесте.
- Остался один rollback: artifacts/rollback-EI-Mapper-Ataman-v80-to-81-20260930.
  Его6 файлов повторно сверены с backup-манифестом. Прежний owned rollback79→80
  и временный staging81 удалены после успешной проверки установки.
  Редактор не открывался интерактивно вместо владельца. ZIP/другие установки
  не создавались. Несвязанные dirty report52 и QM не включены в коммит.
