# Ataman 69 — вылет при привязке незавершённой текстуры

Дата: 2026-09-25. Задача `EI-RENDER-TEXTURE-CRASH-68`.

## Исходный случай и причина

Журнал установленной версии 68 `artifacts/EI-Mapper-Ataman-2-first-test/logs/workflow-20260925-220806.log` завершился исключением 0xc0000005 после записи MPR и RES. Минидамп `crash-20260925-220806-p15580-t20656.dmp` указывает на `QOpenGLTexture::bind(0)` из `source/render/render_queue.cpp:519`: внутренняя таблица OpenGL-функций Qt равна нулю. Тот же адрес сбоя встречался без сохранения, поэтому запись карты не установлена причиной.

Изолированное воспроизведение на Qt 5.12.12/MinGW: `QOpenGLTexture::setData()` без активного контекста выдаёт предупреждение и оставляет `isCreated()==false`; после `makeCurrent()` создаётся текстура с GL id. Код загрузки MOB создавал технические объекты с текстурами до явной активации контекста. `CMob::deserialize()` обновляет индикатор прогресса с обработкой событий. Загрузчик текстур добавлял такой объект в кэш без проверки `isCreated()`, а очередь позже вызывала `bind()`. Точную текстуру из минидампа установить невозможно; воспроизведён механизм неверного состояния и путь его попадания в очередь.

## Изменения

- При загрузке и обновлении MOB активируется контекст viewport; после обработки событий прогресса он запрашивается снова.
- `CTextureList` откладывает загрузку без контекста и не кэширует неудачно созданную текстуру; отсутствие контекста не становится отрицательным кэшированием имени.
- Очередь цветов и теней, выбор объектов и небо пропускают текстуры, которые Qt не создал.
- Регрессионный тест проверяет отказ очереди от `nullptr` и не созданного `QOpenGLTexture`.

## Проверки

- Свежая Release-сборка `build-release-69-render-texture/release/EIMapperEndurance.exe`: успешно; `--version` в staging: `EI Mapper Endurance Edition 69`. SHA-256 EXE `5B577C6D4643A5E2236D65992B833ACED667560A2E678980FCF035B7845549E6` совпал со сборкой.
- `render_queue_tests`: 15 PASS, 0 FAIL/SKIP.
- `--ui-widget-check`: exit 0, `PASS: widget interaction and secondary-window lifecycle checks` в обычном Windows-окружении. Sandbox-вариант имел недоступный clipboard и не засчитывался.
- `--scenario-layer-check`: exit 0, 50 PASS в обычном Windows-окружении. Sandbox-вариант не мог писать в Qt test AppData и не засчитывался.
- Скрытый `--workspace-visual-sequence` на исходных zone12dun2.mpr/MOB: exit 0; 260 объектов MOB и 20 персонажей, визуальная подготовка завершилась; `workflow.log` содержит `Workspace visual sequence complete stages:1` и `Shutdown live view deletion complete elapsedMs:343`. Игра и карты не изменялись.
- `--roundtrip-check --json` на zone12dun2.mpr: exit 0, `loaded=true`, `byteIdentical=true`; SHA-256 игрового MPR остался `F4EEC4CEB91FB29D90F11F6C1DE57CD2B70EBBA105909BF00EFD7AA0ED5C0498`.
- `packaging/finalize_package.ps1` и `validate_distribution.ps1`: PASS. Свежий пакет `C:\GOG Games\maped\ei-mapper-ataman\build-staging-69-render-texture`, 127 управляемых файлов. Хэш EXE до упаковки совпал со свежей Release-сборкой.
- Постоянная установка `C:\GOG Games\maped\ei-mapper-ataman\artifacts\EI-Mapper-Ataman-2-first-test` обновлена с 68 на 69 при закрытом редакторе. Проверены хэши 127 управляемых файлов, 13900 пользовательских файлов сохранены, 32 программных файла заменены. Откатная копия заменённых файлов: `artifacts/rollback-EI-Mapper-Ataman-v68-to-69-20260925`.

## Границы

Скрытый прогон проверил чтение, отрисовку и закрытие, но не интерактивное редактирование с сохранением во время долгой сессии и не игру. Предупреждения о старых несоответствиях профилей атласа при загрузке соседних карт остались, этот дефект не изменяет атласы. Пользовательская проверка исходного рабочего сценария остаётся открытой.
