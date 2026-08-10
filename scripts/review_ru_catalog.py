"""Apply the reviewed Endurance Russian terminology overrides to the Qt catalog."""

import re
import xml.etree.ElementTree as ET
from pathlib import Path


CATALOG = Path(__file__).resolve().parents[1] / "translations/ei_mapper_ru.ts"

CORRECTIONS = {
    "&File": "&Файл",
    "&Tools": "&Инструменты",
    "&Edit": "&Правка",
    "&Unit": "&Существо",
    "&About": "&О программе",
    "About": "О программе",
    "Settings": "Настройки",
    "Settings...": "Настройки...",
    "General": "Общие",
    "Render": "Отображение",
    "Resource": "Ресурсы",
    "Key Bindings": "Горячие клавиши",
    "Language": "Язык",
    "English": "English (английский)",
    "Russian": "Русский",
    "Select the editor interface language. Restart the editor to apply a language change.":
        "Выберите язык интерфейса. Чтобы применить изменение, перезапустите редактор.",
    "UI scale": "Масштаб интерфейса",
    "Apply": "Применить",
    "Save && Close": "Сохранить и закрыть",
    "Cancel": "Отмена",
    "OK": "ОК",
    "Save All": "Сохранить все",
    "&Yes": "&Да",
    "Yes to &All": "Да для &всех",
    "&No": "&Нет",
    "N&o to All": "Нет д&ля всех",
    "Abort": "Прервать",
    "Retry": "Повторить",
    "Ignore": "Игнорировать",
    "Discard": "Не сохранять",
    "Help": "Справка",
    "Restore Defaults": "Восстановить значения по умолчанию",
    "Close": "Закрыть",
    "Browse...": "Обзор...",
    "Clear": "Очистить",
    "File": "Файл",
    "Open": "Открыть",
    "Save": "Сохранить",
    "Exit": "Выход",
    "Import...": "Импорт...",
    "Export...": "Экспорт...",
    "Reset": "Сбросить",
    "Undo": "Отменить",
    "Redo": "Повторить",
    "Object Properties": "Свойства объекта",
    "Object Outliner": "Иерархия объектов",
    "Maps and Collections": "Карты и коллекции",
    "Maps && Collections": "Карты и коллекции",
    "Loaded Map": "Загруженная карта",
    "Loaded document": "Загруженный документ",
    "Message Log": "Журнал сообщений",
    "Landscape Tools": "Инструменты ландшафта",
    "MOB Family Validation": "Проверка семейства MOB",
    "Validation pending for edited MOB data. Open or refresh MOB Family Validation to run it.":
        "Изменённые данные MOB ещё не проверены. Откройте или обновите панель проверки семейства MOB.",
    "Game and mod setup": "Настройка игры и мода",
    "Game and Mod Setup...": "Настройка игры и мода...",
    "Game installation:": "Папка игры:",
    "Editing profile:": "Профиль редактирования:",
    "Detected configuration:": "Найденная конфигурация:",
    "Choose Evil Islands installation": "Выберите папку «Проклятых земель»",
    "Choose the Evil Islands installation folder. Endurance will find the archives and maps for the selected preset automatically.":
        "Выберите папку с игрой «Проклятые земли». Endurance автоматически найдёт архивы и карты для выбранного профиля.",
    "Select object": "Выбрать объект",
    "Create new object": "Создать объект",
    "Create": "Создать",
    "Preview": "Предпросмотр",
    "Initializing editor...": "Инициализация редактора...",
    "Opening main window...": "Открытие главного окна...",
    "Endurance Edition": "Редакция Endurance",
    "<choose object type>": "<выберите тип объекта>",
    "Undo history limit": "Размер истории отмены",
    "Sense X:": "Чувствительность X:",
    "Sense Y:": "Чувствительность Y:",
    "Always draw units logic": "Всегда показывать логику существ",
    "Free Camera": "Свободная камера",
    "Draw water": "Показывать воду",
    "Animate unit idle cycles": "Анимировать циклы ожидания существ",
    "Moving operations": "Перемещение",
    "Edit operations": "Редактирование",
    "Maximum shadow distance": "Максимальная дальность теней",
    "Unit category": "Категория существа",
    "Scale": "Масштаб",
    "Required key number": "Номер требуемого ключа",
    "Required Science skill": "Требуемый навык «Наука»",
    "Number of states": "Число состояний",
    "State count": "Число состояний",
    "Nearest unit only": "Только ближайшее существо",
    "Use MOB unit values": "Использовать значения существа из MOB",
    "Unit parameters": "Параметры существа",
    "Call-for-help radius": "Радиус зова на помощь",
    "The MOB could not be saved safely.": "Не удалось безопасно сохранить MOB.",
    "Open MPR or MOB...": "Открыть MPR или MOB...",
    "Open MOB(s)...": "Открыть MOB...",
    "Save All MOB's": "Сохранить все MOB",
    "Select &All": "Выбрать &все",
    "Create &New Object": "Создать &объект",
    "Show &History": "Показать &историю",
    "&Mob Parameters": "&Параметры MOB",
    "Un&do": "&Отменить",
    "Reset &Camera Position": "Сбросить положение &камеры",
    "Copy &IDs To Clipboard": "Копировать &ID в буфер обмена",
    "Open &Last Session": "Открыть &последний сеанс",
    "Reset &logic paths": "Сбросить пути &логики",
    "Rot": "Вра",
    "Sel": "Выб",
    "Mov": "Дви",
    "Sca": "Мас",
    "Bru": "Кис",
    "Searches for contiguous text by default. Combine terms with spaces. Use field=value for an exact match or prefix a value with ~ for fuzzy subsequence matching (for example id:123, id=123, or id:~123). Optional fields: name:, id:, type:, model:, texture:, db:, prototype:, mob:, validation:.":
        "По умолчанию ищется непрерывный текст. Разделяйте термины пробелами. Для точного совпадения используйте field=value, а для нечёткого поиска добавьте ~ перед значением (например, id:123, id=123 или id:~123). Поля: name:, id:, type:, model:, texture:, db:, prototype:, mob:, validation:.",
    "Armor stored directly in this MOB. Used when Use MOB unit values is on.": "Броня, хранящаяся непосредственно в MOB. Используется, когда включено «Использовать значения существа из MOB».",
    "Weapons stored directly in this MOB. Used when Use MOB unit values is on.": "Оружие, хранящееся непосредственно в MOB. Используется, когда включена опция значений существа из MOB.",
    "Spells stored directly in this MOB. Used when Use MOB unit values is on.": "Заклинания, хранящиеся непосредственно в MOB. Используются при включённой опции значений существа из MOB.",
    "Quick-use items stored directly in this MOB. Used when Use MOB unit values is on.": "Предметы быстрого доступа, хранящиеся в MOB. Используются при включённой опции значений существа из MOB.",
    "Quest inventory stored directly in this MOB. Used when Use MOB unit values is on.": "Квестовый инвентарь, хранящийся в MOB. Используется при включённой опции значений существа из MOB.",
    "Unit parameters stored directly in this MOB. Used when Use MOB unit values is on.": "Параметры существа, хранящиеся в MOB. Используются при включённой опции значений существа из MOB.",
}

# Concise, idiomatic UI wording. These overrides intentionally favor the
# vocabulary a Russian player sees in an editor over literal engineering
# translations produced by the initial catalog seed.
CORRECTIONS.update({
    "Restart required": "Требуется перезапуск",
    "Restart EI Mapper to apply the new interface language.":
        "Перезапустите EI Mapper, чтобы сменить язык интерфейса.",
    "Diff loaded / regenerated": "Сравнение загружено / обновлено",
    "Perspective MOB Diff - Details": "Сравнение MOB — подробности",
    "Regenerate preview": "Обновить предпросмотр",
    "Generated preview": "Предпросмотр",
    "Preview graph: Loaded": "Граф предпросмотра: загружен",
    "Save review PNG...": "Сохранить PNG...",
    "Maximize viewport": "Развернуть окно карты",
    "MOB Script: %1": "Скрипт MOB: %1",
    "MOB Script: %1 [unapplied local text retained]":
        "Скрипт MOB: %1 [несохранённый текст сохранён]",
    "Profile: %1": "Профиль: %1",
    "Mod: %1": "Мод: %1",
    "RES archives: %1": "Архивы RES: %1",
    "\nResource priority (highest first):": "\nПриоритет ресурсов:",
    "\nMap locations:": "\nПапки карт:",
    "\nNotices:": "\nПримечания:",
    "\nReady to apply.": "\nМожно применить.",
    "\nSelect a valid installation or another preset.":
        "\nВыберите папку с игрой или другой профиль.",
    "The selected folder does not contain game.exe.":
        "В выбранной папке нет game.exe.",
    "%1 was not found below the selected game installation.":
        "%1 не найден в папке игры.",
    "Missing resource directory: %1": "Не найдена папка ресурсов: %1",
    "No RES archives found in: %1": "В папке нет архивов RES: %1",
    "Optional map directory not found: %1": "Не найдена дополнительная папка карт: %1",
    "Missing runtime component: %1": "Не найден компонент запуска: %1",
    "Runtime hash mismatch [%1]: %2 (SHA-256 %3)":
        "Не совпадает хеш компонента [%1]: %2 (SHA-256 %3)",
    "Unavailable — invalid Player group %1": "Недоступно — неверная группа игрока %1",
    "Unavailable — no complete diplomacy row for Player group %1":
        "Недоступно — нет полной строки дипломатии для группы %1",
    "none (no groups marked War)": "нет (ни одна группа не враждебна)",
    "Player group %1 → %2": "Группа игрока %1 → %2",
    "added": "добавлено",
    "removed": "удалено",
    "modified": "изменено",
    "moved": "перемещено",
    "ambiguous": "неоднозначно",
    "unchanged": "без изменений",
    "exact-subtree-hash": "точное совпадение",
    "unique-family-nid": "уникальный NID в семействе",
    "unique-type-prototype-name": "тип, прототип и имя",
    "non-unique-type-prototype-name": "неуникальные тип, прототип и имя",
    "unmatched": "нет пары",
    "script": "скрипт",
    "old-script": "старый скрипт",
    "document-section": "раздел документа",
    "Delete object ID: %1": "Удалить объект %1",
    "Created object ID: %1": "Создать объект %1",
    "Convert world object ID %1 to lever": "Преобразовать объект %1 в рычаг",
    "Change value to %1": "Изменить значение: %1",
    "Edit MOB parameters": "Изменить параметры MOB",
    "Create first patrol point and enable Path behavior":
        "Создать первую точку патруля и включить маршрут",
    "Create patrol point": "Создать точку патруля",
    "Paste look point": "Вставить точку обзора",
    "Paste patrol point": "Вставить точку патруля",
    "Paste %1 %2": "Вставить: %1 (%2)",
    "Reorder look point": "Переместить точку обзора",
    "Reorder patrol point": "Переместить точку патруля",
    "Reorder cast target": "Переместить цель заклинания",
    "Reorder activation area": "Переместить область активации",
    "Paste Logic slot": "Вставить слот логики",
    "Paste Logic slot to %1 units": "Вставить слот логики: существ %1",
    "Point deleted": "Удалить точку",
    "Switch MOB to %1": "Переключиться на MOB %1",
    "Quest MOB to Base": "Квестовый MOB → обычный",
    "Base MOB to Quest": "Обычный MOB → квестовый",
    "Deleted range %1–%2": "Удалить диапазон %1–%2",
    "Changed range to %1–%2": "Изменить диапазон: %1–%2",
    "Added range %1–%2": "Добавить диапазон %1–%2",
    "Diplomacy group (%1, %2) changed to %3": "Дипломатия (%1, %2): %3",
    "Change MOB to: %1": "Сменить MOB: %1",
    "Reset IDs for %1 objects": "Сбросить ID объектов: %1",
    "Paint tiles (%1)": "Покрасить плитки (%1)",
    "Randomize %1 objects": "Случайные параметры: объектов %1",
    "Change %1 objects": "Изменить объекты: %1",
    "Paste %1 properties": "Вставить свойства: %1",
    "Paste objects: %1": "Вставить объекты: %1",
    "Select pasted objects": "Выбрать вставленные объекты",
    "%1 objects: %2": "%1: объектов %2",
    "Delete %1 objects": "Удалить объекты: %1",
    "Create and place logic points on %1 objects": "Создать точки логики: объектов %1",
    "Create and place logic point": "Создать точку логики",
    "Transform": "Преобразовать",
    "Move": "Переместить",
    "Rotate": "Повернуть",
    "Scale": "Масштабировать",
    "Unit": "Существо",
    "Unit definition used by the game.": "Прототип существа из базы игры.",
    "Serialized unit category.": "Категория существа в MOB.",
    "Controls how the unit moves and guards while this logic is active.":
        "Движение и охрана существа при активной логике.",
    "Controls how the unit reacts to enemies.": "Реакция существа на врагов.",
    "For unit-targeted spells, affect only the nearest eligible unit. Off affects all eligible units.":
        "Только ближайшая подходящая цель. Если выключено — все подходящие цели.",
    "L: Add look to last patrol": "L: точка обзора в последний патруль",
    "L: Add look after selected": "L: точка обзора после выбранной",
    "%1 look(s)": "Точек обзора: %1",
    "Open MOB(s)...": "Открыть MOB...",
    "Save all MOBs": "Сохранить все MOB",
    "Unload %1 MOBs": "Выгрузить MOB: %1",
    "%1 MOB(s)": "MOB: %1",
    "%1 selected object(s)": "Выбрано объектов: %1",
    "%1 objects selected": "Выбрано объектов: %1",
    "%1 — %2 units selected": "%1 — выбрано существ: %2",
    "%1 matching objects out of %2": "Найдено объектов: %1 из %2",
    "%1 vertex/vertices will be requantized across the affected SEC members.\n%2 value(s) round; %3 value(s) clamp. Maximum world-height error: %4.":
        "Будет пересчитано вершин: %1 в затронутых SEC.\nОкруглено: %2; ограничено: %3. Макс. ошибка высоты: %4.",
    "Added overlay surface; %1 vertex/vertices were clamped to MPR maxZ and may overlap the land until the terrain or maxZ is adjusted.":
        "Добавлена поверхность. Вершин ограничено по MPR maxZ: %1; до правки рельефа они могут пересекать землю.",
    "Vertical range changed to %1; %2 vertex/vertices were requantized and navigation data may need review.":
        "Диапазон высот: %1. Пересчитано вершин: %2; проверьте навигацию.",
    "Bake into Primary MOB": "Встроить в основной MOB",
    "Center of random roaming for Guard area. Alarm guard ignores this value and uses the active alarm coordinates.":
        "Центр случайного движения в режиме «Охрана области». «Охрана по тревоге» использует координаты тревоги.",
    "On uses the equipment, inventory, spells, and parameters stored in this MOB. Off uses the Database prototype.":
        "Вкл.: данные из MOB. Выкл.: данные прототипа из базы.",
    "On returns from the last patrol point to the first. Off follows the route back in reverse.":
        "Вкл.: после последней точки — к первой. Выкл.: возврат по маршруту в обратном порядке.",
    "Smooth has no height-step control.": "Для сглаживания шаг высоты не используется.",
    "Generated preview is automatically staged for Test Current Map. Embedded uses the active profile texture without staging an override.":
        "Предпросмотр автоматически используется для теста карты. «Встроенная» использует текстуру активного профиля.",
    "Atlas slots in selected family": "Слоты атласа выбранного семейства",
    "Review atlas slots": "Проверить слоты атласа",
    "Atlas slot categories are MPR metadata and are not edited by the painting brush.":
        "Категории слотов атласа хранятся в MPR и не меняются кистью.",
    "New MPR from Terrain Template...": "Новый MPR по шаблону рельефа...",
    "\nCompanion MOB: %1 (%2 translated, %3 removed; %4 on the closed-edge band; stale embedded graph omitted: %5)":
        "\nСвязанный MOB: %1 (перемещено: %2; удалено: %3; на закрытом крае: %4; старый граф исключён: %5)",
    "Companion MOB write failed": "Не удалось записать связанный MOB",
    "Raw height samples remain unchanged; packed normals are rebuilt in every SEC.\nWorld height range: %1-%2 -> %3-%4.\nThe MP maxZ field and derived SEC normals change; terrain and navigation scale vertically.":
        "Исходные высоты не меняются; нормали пересчитываются во всех SEC.\nВысота мира: %1-%2 → %3-%4.\nМеняются maxZ в MP и нормали SEC; рельеф и навигация масштабируются по вертикали.",
    "Primary map MOB (SC_OBJECT_DB_FILE)": "Основной MOB карты (SC_OBJECT_DB_FILE)",
    "Supplemental / AutoZone MOB (PR_OBJECT_DB_FILE)":
        "Дополнительный MOB / AutoZone (PR_OBJECT_DB_FILE)",
    "No texts.res was found in the active resource profile.":
        "В активном профиле не найден texts.res.",
    "The selected texts.res does not contain map.txt.":
        "В выбранном texts.res нет map.txt.",
    "map.txt did not contain any zones with an MPR/MOB resource pair.":
        "В map.txt нет зон с парой ресурсов MPR/MOB.",
    "Could not read %1: %2": "Не удалось прочитать %1: %2",
    "%1: not found: %2": "%1: не найдено: %2",
    "Open MOB(s)": "Открыть MOB",
    "%1 family ambiguity flag(s), %2 slot ambiguity flag(s), %3 missing same-layer family transition(s). Missing transitions are diagnostic: automatic painting keeps a hard edge instead of inventing a tile.":
        "Неоднозначность семейств: %1; слотов: %2; нет переходов в слое: %3. Если перехода нет, автопокраска оставляет резкую границу.",
    "%1 atlas slots; %2 surface material record(s); %3 tile animation record(s)":
        "Слотов атласа: %1; материалов поверхности: %2; анимаций плиток: %3",
    "Terrain stroke: %1 stamp(s), %2 changed tile(s), %3 no-op, %4 refused.":
        "Мазок рельефа: шагов %1; плиток изменено %2; без изменений %3; отклонено %4.",
    "Pasted Logic %1 to %2 unit(s), relative to each unit.":
        "Логика %1 вставлена существам: %2 (относительно каждого).",
    "Overlay sculpt stroke: %1 stamp(s), %2 sector update(s); liquid navigation may need review.":
        "Правка поверхности: шагов %1; секторов обновлено %2. Проверьте навигацию по воде.",
    "Sculpt stroke: %1 stamp(s), %2 vertex updates; MOB placement and navigation may need review.":
        "Правка рельефа: шагов %1; вершин обновлено %2. Проверьте объекты MOB и навигацию.",
    "%1 selected MOB(s) have unsaved changes.":
        "Есть несохранённые изменения в выбранных MOB: %1.",
    "\n%1 selected object(s)": "\nВыбрано объектов: %1",
    "Family %1: %2 MOB(s), %3 critical, %4 warning, %5 incomplete":
        "Семейство %1 — MOB: %2; критических: %3; предупреждений: %4; неполных: %5",
    "Plays provider-resolved default unit idle cycles. Distant units use reduced pose update rates.":
        "Показывает стандартную анимацию ожидания. Для далёких существ частота обновления снижена.",
    "Model parts stored for this object. WARNING: An empty list enables every model part; incompatible combinations can crash the game, especially on units.":
        "Части модели объекта. ВНИМАНИЕ: пустой список включает все части; несовместимое сочетание может вызвать сбой игры, особенно у существ.",
    "Maximum roaming distance for Guard area and Alarm guard. The unit moves to random points inside this radius.":
        "Радиус движения для охраны области и охраны по тревоге. Существо выбирает случайные точки внутри него.",
    "Pasted Logic %1 to %2 unit(s) at original world positions.":
        "Логика %1 вставлена существам: %2 (в исходных координатах).",
    "Select one or more units (or their patrol points) to append patrols.":
        "Выберите существ или их точки, чтобы добавить патруль.",
    "Event that makes this unit raise its configured alarm.":
        "Событие, при котором существо поднимает тревогу.",
    "World alarm this unit raises when its alarm condition occurs. 0 raises no alarm.":
        "Номер тревоги. 0 — не поднимать тревогу.",
    "Raises the configured alarm when the unit becomes suspicious.":
        "Поднимает тревогу, когда существо замечает подозрительное.",
    "%1 vertices will be requantized; %2 will clamp; maximum world-height error is %3.":
        "Будет пересчитано вершин: %1; ограничено значений: %2; макс. ошибка высоты: %3.",
    "Direct atlas-category and liquid-material mutation is disabled until it uses the canonical staged MPR transaction path.":
        "Изменение категорий атласа и материалов жидкости отключено, пока для него не используется безопасное сохранение MPR.",
    "These operations retain the current sector dimensions and vertex count. They are applied as one undoable canonical terrain command and saved through the validated MPR transaction.":
        "Размер секторов и число вершин не меняются. Все правки выполняются одним отменяемым действием и сохраняются после проверки MPR.",
    "The active profile has no Endurance direct-map-test provider.":
        "Активный профиль не поддерживает прямой тест карты Endurance.",
    "The active resource profile could not resolve %1. A texture exported to an editor-local RES remains unavailable here until that archive is added to the active profile; Test Current Map can instead use the generated viewport preview through its run-owned staging path.":
        "Активный профиль не нашёл %1. Текстура из локального RES станет доступна после добавления архива в профиль; для теста карты можно временно использовать предпросмотр окна карты.",
    "JMV direct map test is waiting for the normal menu lifecycle, then will load the staged map automatically.":
        "Тест JMV ждёт загрузки главного меню, затем автоматически откроет временную карту.",
    "No loaded MOB is available for staging.": "Нет загруженного MOB для тестовой копии.",
    "No map.txt mapping": "Нет привязки map.txt",
    "JMV direct map test finished (exit code %1). Temporary files were cleaned up.":
        "Прямой тест карты JMV завершён (код %1). Временные файлы удалены.",
    "An unmapped test requires exactly one loaded MPR and one loaded MOB. Use a collection or map.txt mapping for supplemental MOB sets.":
        "Для теста без привязки нужны один MPR и один MOB. Для дополнительных MOB используйте коллекцию или привязку map.txt.",
    "Complete MPR + MOB capture. Terrain, overlays, and %1 static objects are included; %2 dynamic units were excluded.":
        "Полный снимок MPR + MOB: рельеф, поверхности и статические объекты (%1). Динамические существа исключены (%2).",
    "Incomplete MPR-only capture. Terrain and overlays are included, but no MOB source is loaded. Load a MOB for a production minimap.":
        "Неполный снимок только из MPR: рельеф и поверхности без объектов MOB. Загрузите MOB для готовой миникарты.",
    "Incomplete MPR-only capture. Terrain and overlays are included, but no MOB source is loaded.":
        "Неполный снимок только из MPR: рельеф и поверхности без объектов MOB.",
    "Load an MPR with canonical terrain data first.":
        "Сначала загрузите MPR с полными данными рельефа.",
    "The canonical terrain dimensions cannot produce a runtime AI graph.":
        "Размеры рельефа не позволяют создать игровой граф AI.",
    "Canonical terrain ownership is unavailable.": "Полные данные рельефа недоступны.",
    "Canonical terrain ownership is unavailable for this map. No destination was changed.":
        "Для этой карты недоступны полные данные рельефа. Файлы не изменены.",
    "Preparing canonical terrain patches...": "Подготовка изменений рельефа...",
    "Save canonical landscape": "Сохранить рельеф",
    "Save canonical landscape copy": "Сохранить копию рельефа",
    "The validated MPR was committed, but its canonical terrain baseline could not be adopted. The backup is intact. Reopen the MPR before making further edits.\n\n%1":
        "Проверенный MPR сохранён, но редактор не смог принять новое состояние рельефа. Резервная копия цела. Перед дальнейшей работой откройте MPR заново.\n\n%1",
    "Ctrl + Mouse Wheel": "Ctrl + колесо мыши",
    "Alt + Mouse Wheel": "Alt + колесо мыши",
    "Shift + Mouse Wheel": "Shift + колесо мыши",
    "Meta + Mouse Wheel": "Win + колесо мыши",
    "Automatic mode always covers the brush core, uses an exact or constrained nearest authored seam when available, and otherwise leaves a hard edge. Manual mode paints the selected existing atlas tile and rotation without blending. Size 1 paints one core tile. Right-click samples from the map. Alt+Wheel changes brush size. Shift+Wheel selects the automatic material or rotates the manual tile; Wheel keeps camera zoom. Overlay surface mode adds/removes coverage. Its Terrain material selects the atlas texture; its Surface material independently controls tint, opacity, illumination, waves, and texture warp. Overlay painting only changes the atlas texture on cells that already exist.":
        "Авто: кисть подбирает подходящие плитки и швы; если перехода нет, остаётся резкая граница. Вручную: рисуется выбранная плитка без смешивания. ПКМ берёт образец с карты. Alt+колесо — размер кисти; Shift+колесо — материал или поворот; обычное колесо — масштаб камеры. Режим поверхности добавляет или удаляет покрытие. Материал рельефа выбирает текстуру атласа, а материал поверхности задаёт цвет, прозрачность, свет, волны и искажение.",
    "World-space height represented by raw height 65535. Raw 0 is world height 0; raw 32768 is approximately half of maxZ. Increase maxZ for taller elevation differences without changing the 0-65535 raw precision.":
        "Высота мира при исходном значении 65535. При 0 высота равна 0, при 32768 — примерно половине maxZ. Увеличьте maxZ для более высокого рельефа без потери точности диапазона 0–65535.",
})

SHORTCUT_RE = re.compile(
    r"^(?:[A-Z]|Del|Esc|F\d+|(?:(?:Ctrl|Alt|Shift|Meta)\+)+(?:[A-Z]|F\d+|Esc|Tab|Wheel))$")


def main():
    tree = ET.parse(CATALOG)
    changed = 0
    for message in tree.getroot().findall(".//message"):
        source = message.findtext("source", default="")
        translation = message.find("translation")
        if SHORTCUT_RE.fullmatch(source) and translation is not None:
            translation.attrib.pop("type", None)
            translation.text = source
            changed += 1
        elif source in CORRECTIONS and translation is not None:
            translation.attrib.pop("type", None)
            translation.text = CORRECTIONS[source]
            changed += 1
        elif translation is not None:
            text = "".join(translation.itertext())
            text = text.replace("МОБ", "MOB").replace("МПР", "MPR")
            text = text.replace("МНР", "MPR").replace("ДЖМВ", "JMV")
            text = text.replace("НИД", "NID").replace("*.mоб", "*.mob")
            text = text.replace("Выносливость", "Endurance")
            text = text.replace("выносливости", "Endurance")
            text = text.replace("выносливость", "Endurance")
            text = text.replace("предварительного просмотра", "предпросмотра")
            text = text.replace("предварительном просмотре", "предпросмотре")
            text = text.replace("предварительный просмотр", "предпросмотр")
            text = text.replace("Предварительный просмотр", "Предпросмотр")
            text = text.replace("Полный предпросмотр заказа", "Порядок обновляется")
            text = text.replace("полный предпросмотр заказа", "порядок обновляется")
            text = text.replace("окне просмотра", "окне карты")
            text = text.replace("область просмотра", "окно карты")
            text = text.replace("контуре", "списке объектов")
            text = text.replace("слот Логика", "слот логики")
            text = text.replace("невыполнимое действие", "действие можно отменить")
            text = text.replace("одним невыполнимым действием",
                                "одним действием с возможностью отмены")
            text = text.replace("необратимое редактирование",
                                "изменение с возможностью отмены")
            text = text.replace("полезные нагрузки", "данные")
            text = text.replace("полезной нагрузки", "данных")
            text = text.replace("сопутствующего MOB", "связанного MOB")
            text = text.replace("сопутствующий MOB", "связанный MOB")
            text = text.replace("MOB-компаньон", "связанный MOB")
            text = text.replace("Компаньон MOB", "Связанный MOB")
            text = text.replace("компаньона MOB", "связанного MOB")
            text = text.replace("Companion MOB", "Связанный MOB")
            text = text.replace("Primary MOB", "основной MOB")
            text = text.replace("Test Current Map", "Тест карты")
            text = text.replace("Apply Raw Source", "Применить исходный текст")
            text = text.replace("Save Verified RES As", "Сохранить проверенный RES как")
            text = text.replace("Export Runtime .grp", "Экспорт игрового .grp")
            text = text.replace("Switch MOB to External .grp",
                                "Переключить MOB на внешний .grp")
            text = text.replace("Bake or Switch", "Встроить или переключить")
            text = text.replace("Stop Test", "Остановить тест")
            text = text.replace("Reload Now", "Перезагрузить")
            text = text.replace("Keep Working", "Продолжить работу")
            text = text.replace("New Game", "Новая игра")
            text = text.replace("World:", "Высота мира:")
            text = text.replace("MOBе", "MOB").replace("MOBа", "MOB")
            text = text.replace("был зафиксирован", "был сохранён")
            text = text.replace("зафиксированного файла", "сохранённого файла")
            text = text.replace("зафиксировать", "сохранить")
            text = text.replace("зафиксирует", "сохранит")
            text = text.replace("каноническими", "штатными")
            text = text.replace("канонических", "штатных")
            text = text.replace("каноническую", "штатную")
            text = text.replace("каноническое", "штатное")
            text = text.replace("каноническая", "штатная")
            text = text.replace("канонический", "штатный")
            text = text.replace("канонические", "штатные")
            text = text.replace("промежуточную пару", "временную пару")
            text = text.replace("промежуточную карту", "временную карту")
            text = text.replace("промежуточного просмотра", "подготовки тестовой копии")
            text = text.replace("во время выполнения", "в игре")
            text = text.replace("среда выполнения", "игровая среда")
            text = text.replace("профиль времени выполнения", "профиль запуска")
            text = text.replace("авторские", "исходные")
            text = text.replace("авторский", "исходный")
            text = text.replace("авторского", "исходного")
            text = text.replace("авторских", "исходных")
            text = text.replace("необработанными", "исходными")
            text = text.replace("необработанных", "исходных")
            text = text.replace("необработанного", "исходного")
            text = text.replace("необработанную", "исходную")
            text = text.replace("необработанной", "исходной")
            text = text.replace("необработанном", "исходном")
            text = text.replace("необработанные", "исходные")
            text = text.replace("необработанный", "исходный")
            text = text.replace("необработанное", "исходное")
            text = text.replace("необработанной", "исходной")
            text = text.replace("исходного исходного", "исходного")
            text = text.replace("исходный исходный", "исходный")
            text = text.replace("неизвестный сохранившийся поплавок",
                                "неизвестное сохранённое число")
            text = text.replace("Неизвестный сохранившийся поплавок",
                                "Неизвестное сохранённое число")
            text = text.replace("Поколение миникарт", "Создание миникарты")
            text = text.replace("Gold указывает", "Золотым выделена")
            text = text.replace("Золото указывает", "Золотым выделена")
            text = text.replace("Золото обозначает", "Золотым выделена")
            text = re.sub(r"\bмоб(?:а|у|ом|е|ы|ов|ами|ах)?\b", "MOB",
                          text, flags=re.IGNORECASE)
            if "graph" in source.lower():
                text = re.sub(r"график(а|у|ом|е|и|ов)?", r"граф\1",
                              text, flags=re.IGNORECASE)
            translation.text = text
    ET.indent(tree, space="    ")
    tree.write(CATALOG, encoding="utf-8", xml_declaration=True)
    print(f"applied {changed} reviewed terminology entries")


if __name__ == "__main__":
    main()
