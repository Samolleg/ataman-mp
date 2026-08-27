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
    "Map ID": "ID объекта",
    "Player group": "Группа игрока",
    "Parent template": "Исходный шаблон",
    "Primary texture": "Основная текстура",
    "Secondary texture": "Дополнительная текстура",
    "Attached quest variable": "Привязанная переменная квеста",
    "Guard position": "Позиция охраны",
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
    "Lighting mode": "Режим освещения",
    "Unlit": "Без освещения",
    "Simple shading": "Простой",
    "Full environment": "Полный",
    "Sculpt terrain": "Правка рельефа",
    "Sculpt overlay surfaces": "Правка покрытий",
    "Raise": "Поднять",
    "Lower": "Опустить",
    "Smooth": "Сгладить",
    "Flatten": "Выровнять",
    "Sculpt overlay height": "Правка высоты покрытия",
    "Sculpt strength": "Сила кисти",
    "Sculpt radius: %1 vertices": "Радиус кисти: %1 вершин",
    "Objects": "Объекты",
    "Count": "Количество",
    "World objects": "Объекты мира",
    "Units": "Существа",
    "Torches": "Факелы",
    "Levers": "Рычаги",
    "Magic traps": "Магические ловушки",
    "Light sources": "Источники света",
    "Sound sources": "Источники звука",
    "Particle sources": "Источники частиц",
    "Minimize lighting mode": "Свернуть панель освещения",
    "Full environment (unavailable in ANGLE)": "Полный (недоступно в ANGLE)",
    "ANGLE compatibility renderer: Unlit and Simple shading are available.":
        "Режим совместимости ANGLE: доступны «Без освещения» и «Простой».",
    "Shadow maps are not initialized. Select Full environment, enable shadows, and render the failing view first.":
        "Карты теней не созданы. Выберите режим «Полный», включите тени и снова откройте этот вид.",
    "Flatten target": "Цель выравнивания",
    "Flatten whole map": "Выровнять всю карту",
    "Flatten target: %1 raw units": "Цель выравнивания: %1 исходных единиц",
    "Objects MOB resource": "Ресурс объектов MOB",
    "Map objects (*.mob);;Mob as JSON(*.json)":
        "Объекты карты (*.mob);;MOB как JSON (*.json)",
    "Left-drag sculpts; right-click samples the raw height as the Flatten target. Alt+Wheel changes radius; Shift+Wheel changes strength; Alt+Shift+Wheel changes raise/lower step or the Flatten target. Z/X/C/V select Raise/Lower/Smooth/Flatten. Wheel keeps camera zoom. Each drag is one undo step.":
        "ЛКМ правит рельеф; ПКМ выбирает высоту для выравнивания. Alt+колесо меняет радиус, Shift+колесо — силу, Alt+Shift+колесо — шаг или высоту выравнивания. Z/X/C/V: Поднять/Опустить/Сгладить/Выровнять. Колесо меняет масштаб камеры. Один мазок — одно действие отмены.",
    "Sculpt stroke created a steep local slope (%1 degrees, max raw change %2). Terrain remains seam-consistent, but inspect it for cliffs or exposed backfaces.":
        "Правка рельефа создала крутой склон (%1°, макс. исходное изменение %2). Швы сохранены, но проверьте обрывы и открытые обратные стороны.",
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
        "Размеры рельефа не позволяют создать игровой граф ИИ.",
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

# Follow-up review for compact controls and Russian-player terminology. Keep
# format/resource identifiers intact, but do not leave ordinary UI vocabulary
# in English or use infinitives where the control is a filter field.
CORRECTIONS.update({
    "English": "Английский",
    "Filter parts...": "Фильтр частей...",
    "Filter zones, types, islands, resources, comments...":
        "Фильтр: зоны, типы, острова, ресурсы, комментарии...",
    "Filter maps...": "Фильтр карт...",
    "Filter messages...": "Фильтр сообщений...",
    "Filter kind, match method, NID, name, prototype, or section path...":
        "Фильтр: тип, способ, NID, имя, прототип или раздел...",
    "Filter objects (name, ID, model, texture, database...)":
        "Фильтр объектов (имя, ID, модель, текстура, база...)",
    "Edit Endurance Collection": "Изменить коллекцию",
    "No map.txt mapping": "Нет привязки к map.txt",
    "map.txt mapping:": "Привязка к map.txt:",
    "Add MOB(s)...": "Добавить MOB...",
    "Ordered MOB files (main first, then Auto/supplemental):":
        "Файлы MOB по порядку (сначала основной, затем Auto и дополнительные):",
    "Evil Islands MPR (*.mpr)": "MPR «Проклятых земель» (*.mpr)",
    "Evil Islands MOB (*.mob)": "MOB «Проклятых земель» (*.mob)",
    "Evil Islands maps (*.mpr *.mob);;MPR (*.mpr);;MOB (*.mob)":
        "Карты «Проклятых земель» (*.mpr *.mob);;MPR (*.mpr);;MOB (*.mob)",
    "Evil Islands resource archive (*.res)":
        "Архив ресурсов «Проклятых земель» (*.res)",
    "Import hand-edited map.txt": "Импорт map.txt после ручной правки",
    "Endurance can verify basic map.txt structure and preserve the imported bytes, but it cannot prove that profile-specific tags, resource names, travel links, or script references are correct for the game. Import replaces the current in-memory catalog; no RES file is changed until you use Save Verified RES As.\n\nContinue?":
        "Endurance проверит основную структуру map.txt и сохранит импортированные байты, но не сможет проверить имена ресурсов, переходы и ссылки в скриптах. Импорт заменит каталог в памяти; RES изменится только после команды «Сохранить проверенный RES как».\n\nПродолжить?",
    "No .collection files were found. Create one from File > Collections.":
        "Файлы .collection не найдены. Создайте коллекцию: Файл → Коллекции.",
    "No passable spawn was found near that point. Pick another location.":
        "Рядом нет проходимой точки появления. Выберите другое место.",
    "Select a valid map.txt entrance before choosing a custom spawn.":
        "Сначала выберите допустимую точку входа из map.txt.",
    "Delete collection '%1'?\n\n%2\n\nMap files referenced by it will not be deleted.":
        "Удалить коллекцию «%1»?\n\n%2\n\nФайлы карт не будут удалены.",
    "map.txt primary": "Основной в map.txt",
    "Hide map.txt entrance areas and labels in the viewport.":
        "Скрыть области входа из map.txt и их метки в окне карты.",
    "Show map.txt entrance areas and labels in the viewport.":
        "Показать области входа из map.txt и их метки в окне карты.",
    "Parameter": "Параметр",
    "Value": "Значение",
    "Part": "Часть",
    "Enabled": "Включена",
    "Source": "Источник",
    "Visible": "Видимость",
    "Severity": "Уровень",
    "Source path": "Источник",
    "Kind": "Тип",
    "Match": "Сопоставление",
    "Confidence": "Надёжность",
    "Baseline": "Исходный",
    "Candidate": "Новый",
    "Baseline SHA-256": "SHA-256 исходного",
    "Candidate SHA-256": "SHA-256 нового",
    "AI navigation graph": "Навигация ИИ",
    "Minimize AI navigation graph": "Свернуть навигацию ИИ",
    "Expand AI navigation graph": "Развернуть навигацию ИИ",
    "No graph source loaded": "Граф не загружен",
    "Regenerate preview": "Обновить граф",
    "Diff loaded / regenerated": "Сравнить графы",
    "Graph: Loaded": "Граф: загруженный",
    "Graph: Generated": "Граф: созданный",
    "Graph: Embedded": "Граф: встроенный",
    "Graph: External": "Граф: внешний",
    "Bake / Export...": "Сохранить / экспорт...",
    "Bake / Export AI Graph...": "Сохранить / экспорт графа ИИ...",
    "Bake / Export AI Graph": "Сохранение графа ИИ",
    "AI graph difference": "Сравнение графов ИИ",
    "Loaded": "Загруженный",
    "Regenerated": "Созданный",
    "Layer": "Слой",
    "Total": "Итого",
    "Missing links": "Нет связей",
    "Extra links": "Лишние связи",
    "Changed weights": "Изменённые веса",
    "Changed positions": "Изменённые позиции",
    "Counts are directional link records. Component labels are intentionally ignored.":
        "Учитываются направленные связи; метки компонентов игнорируются.",
    "Flying / levitating": "Полёт / левитация",
    "Narrow / prone": "Узкий / лёжа",
    "Narrow / crawling": "Узкий / ползком",
    "Narrow / human-sized": "Узкий / в рост человека",
    "Narrow / tall": "Узкий / высокий",
    "Wide / low": "Широкий / низкий",
    "Wide / tall": "Широкий / высокий",
    "Very large": "Очень крупный",
    "Unknown": "Неизвестно",
    "Selected graph source: %1\n\nBake into Primary MOB uses exactly this selected graph. Bake and Switch MOB to External .grp save the primary MOB immediately, create a backup, establish a new clean baseline, and cannot be reversed through Edit > Undo.":
        "Источник графа: %1\n\n«Встроить в основной MOB» использует именно этот граф. Встраивание и переключение на внешний .grp сразу сохраняют основной MOB, создают резервную копию и не отменяются через «Правка → Отменить».",
    "Destination RES": "Целевой RES",
    "Hue": "Оттенок",
    "Large RES export": "Большой экспорт RES",
    "Orthographic game orientation: world +X is right and world +Y is down":
        "Ортографическая ориентация: +X вправо, +Y вниз",
    "Load the primary MOB before baking or exporting an AI graph.":
        "Загрузите основной MOB перед встраиванием или экспортом графа ИИ.",
    "Baked the selected graph into %1. It now has runtime priority over any sibling .grp.":
        "Выбранный граф встроен в %1. Теперь он имеет приоритет в игре над любым связанным .grp.",
    "The primary MOB currently has an embedded graph. Export Runtime .grp writes the selected graph. Switch MOB to External .grp never writes the existing .grp; it only validates that file and removes embedded ownership from the MOB.":
        "Основной MOB содержит встроенный граф. Экспорт игрового .grp записывает выбранный граф. Переключение MOB на внешний .grp не записывает существующий .grp; он только проверяет этот файл и удаляет встроенное владение из MOB.",
    "The embedded graph was left untouched because no valid existing runtime .grp is available. Export a graph first, then retry the ownership switch.":
        "Встроенный граф остался без изменений, поскольку действительный игровой .grp недоступен. Сначала экспортируйте граф, затем повторите переключение владельца.",
    "Existing runtime graph %1 was saved unchanged and the embedded graph was removed from %2. The active MPR now resolves this sibling .grp at runtime.":
        "Существующий игровой граф %1 сохранён без изменений, встроенный граф удалён из %2. Активный MPR теперь использует этот связанный .grp в игре.",
    "Existing runtime .grp was not modified, but the embedded graph could not be removed. The MOB still shadows the external file, so runtime ownership did not change.":
        "Существующий .grp не изменён, но встроенный граф не удалось удалить. MOB по-прежнему затеняет внешний файл, поэтому игровое владение не изменилось.",
    "Runtime priority starts with the primary MOB's embedded graph, then the exact sibling file of the active MPR's .grp.":
        "Приоритет в игре: сначала встроенный граф основного MOB, затем точный связанный файл .grp активного MPR.",
    "It is currently shadowed by the primary MOB's embedded graph.":
        "Сейчас игра использует встроенный граф основного MOB вместо этого файла.",
})

# Catalog-wide native-Russian editorial pass. Every entry is keyed by the
# complete English source so regeneration remains deterministic and does not
# accidentally rewrite unrelated words in another context.
CORRECTIONS.update({
    "Children": "Дочерние точки",
    "Order / select": "Порядок / выбор",
    "Patrol route — Logic %1 — %2 patrols — %3 looks":
        "Маршрут — логика %1 — патрульных точек: %2 — точек обзора: %3",
    "Look %1": "Точка обзора %1",
    "Select in outliner and viewport": "Выбрать в иерархии и на карте",
    "Serialized": "Сохранено в MOB",
    "Serialized x%1": "Сохранено в MOB, повторов: %1",
    "Unknown serialized": "Неизвестное сохранённое значение",
    "Unknown serialized x%1": "Неизвестное сохранённое значение, повторов: %1",
    "World object": "Объект мира",
    "Magic trap": "Магическая ловушка",
    "Debug": "Отладка",
    "Endurance could not account for the complete MOB structure. The file was not opened.\n\n%1":
        "Endurance не удалось полностью разобрать структуру MOB. Файл не открыт.\n\n%1",
    "The MOB was opened, but its recovery snapshot could not be created.\n\n%1\n\nDo not edit or save this file until the backup location is writable.":
        "MOB открыт, но резервную копию для восстановления создать не удалось.\n\n%1\n\nНе редактируйте и не сохраняйте файл, пока папка резервных копий недоступна для записи.",
    "Baseline: %1\nCandidate: %2": "Исходный: %1\nСравниваемый: %2",
    "Select a script or document-section change to see its readable diff.":
        "Выберите изменение скрипта или раздела документа, чтобы увидеть различия.",
    "Could not write the report:\n%1": "Не удалось сохранить отчёт:\n%1",
    "is Quest Mob?": "Квестовый MOB",
    "Diplomacy table": "Таблица дипломатии",
    "No diplomacy table": "Таблица дипломатии отсутствует",
    "This MOB has no diplomacy table. Use the arrow menu to copy the intended table from a loaded source MOB, or create a neutral table.":
        "В этом MOB нет таблицы дипломатии. Скопируйте нужную таблицу из загруженного исходного MOB через меню со стрелкой или создайте нейтральную.",
    "Ambient": "Фоновый",
    "Edit or initialize this MOB's 32-player diplomacy table. Magic-trap hostility is derived from this table at runtime; the trap's stored diplomacy value is not sufficient.":
        "Изменить или создать таблицу дипломатии MOB для 32 групп. В игре цели магических ловушек определяются по этой таблице; сохранённого в ловушке значения недостаточно.",
    "Rows are the acting faction; columns are the faction being treated. Self-relations and one-way relations are supported, but validation warns so unusual values can be confirmed.":
        "Строка — действующая группа, столбец — группа, к которой она относится. Допустимы отношения к себе и односторонние отношения; проверка лишь предупреждает о необычных значениях.",
    "No other loaded MOB has a complete diplomacy table. Load the primary/source MOB, then try again.":
        "Ни в одном другом загруженном MOB нет полной таблицы дипломатии. Загрузите основной или исходный MOB и повторите.",
    "Param Type": "Параметр",
    "Randomize": "Задать случайно",
    "Randomize parameters": "Случайные параметры",
    "Intersects range: %1 - %2": "Пересекается с диапазоном %1–%2",
    "The selected installation and preset are not valid yet.":
        "Выбранная папка игры не подходит для этого профиля.",
    "Param 1": "Параметр 1",
    "Param 2": "Параметр 2",
    "Add to select": "Добавить к выделению",
    "Select": "Выбрать",
    "Start ID": "Начальный ID",
    "End ID": "Конечный ID",
    "Start group number": "Начальная группа",
    "End group number": "Конечная группа",
    "Endurance Edition": "Издание Endurance",
    "Magic trap points — %1 activation areas — %2 cast targets":
        "Точки ловушки — областей активации: %1 — целей заклинания: %2",
    "+ Cast target": "+ Цель заклинания",
    "Append a cast target to this trap.": "Добавить точку, в которую ловушка применяет заклинание.",
    "Cast targets": "Цели заклинания",
    "Cast targets (%1)": "Цели заклинания (%1)",
    "Cast target %1": "Цель заклинания %1",
    "Independent 2D destination points. The MOB stores no target rotation or height.":
        "Независимые точки назначения на плоскости. Поворот и высота цели в MOB не хранятся.",
    "Trap targets": "Цели ловушки",
    "Player group %1 attacks factions marked War in its diplomacy row.":
        "Группа игрока %1 атакует группы, для которых в её строке дипломатии указана «Война».",
    "Paste Logic slot at original world positions": "Вставить логику в исходных координатах",
    "Edit unit parameters": "Изменить параметры существа",
    "exact-subtree-hash": "точное совпадение структуры",
    "unique-type-prototype-name": "уникальные тип, прототип и имя",
    "The image is stretched to the complete terrain canvas. North is at the top. Thin lines are tiles; stronger lines are 16-tile SEC sectors.":
        "Изображение растягивается на всю карту. Север сверху. Тонкие линии обозначают тайлы, толстые — сектора SEC по 16 тайлов.",
    "%1 x %2 tiles (%3 x %4 sectors)": "%1 × %2 тайлов (%3 × %4 секторов)",
    "Raises the configured alarm when the unit sees an enemy.":
        "Поднимает заданную тревогу, когда существо видит врага.",
    "Raises the configured alarm when the unit is attacked.":
        "Поднимает заданную тревогу, когда существо атаковано.",
    "Raises the configured alarm when the unit becomes suspicious.":
        "Поднимает заданную тревогу, когда существо замечает подозрительное.",
    "Ally: the row faction treats the column faction as friendly.":
        "Союз: группа в строке считает группу в столбце дружественной.",
    "Neutral: the row faction treats the column faction as neither friendly nor hostile.":
        "Нейтралитет: группа в строке не считает группу в столбце ни дружественной, ни враждебной.",
    "War: the row faction treats the column faction as hostile.":
        "Война: группа в строке считает группу в столбце враждебной.",
    "Edit MPR surface materials and tile animations":
        "Изменить материалы поверхности MPR и анимацию тайлов",
    "Gold indicates the point currently selected in the viewport and outliner.":
        "Золотым цветом выделена точка, выбранная на карте и в иерархии.",
    "Point deleted": "Точка удалена",
    "Deleted range %1–%2": "Удалён диапазон %1–%2",
    "Changed range to %1–%2": "Диапазон изменён на %1–%2",
    "Added range %1–%2": "Добавлен диапазон %1–%2",
    "Paint tiles (%1)": "Рисование тайлов (%1)",
    "Raise/lower step": "Шаг подъёма/опускания",
    "Manual atlas tile mode (no blending)": "Ручной выбор тайла атласа (без смешивания)",
    "Allow land-family tiles on overlays": "Разрешить тайлы суши на поверхностях",
    "Show surface material slot numbers on tiles": "Показывать номера материалов поверхности на тайлах",
    "Terrain materials are loaded from the MPR namespace template.":
        "Материалы рельефа загружаются из шаблона ресурсов MPR.",
    "Terrain edits are undoable. Changed MPR saves use staged validation, backup, and atomic replacement.":
        "Правки рельефа можно отменить. Перед сохранением MPR проверяется, резервируется и заменяется целиком.",
    "Paint land tiles": "Рисовать тайлы суши",
    "Add / remove overlay surfaces": "Добавлять / удалять поверхности",
    "Paint existing overlays": "Рисовать по существующим поверхностям",
    "Radius (vertices)": "Радиус (в вершинах)",
    "Atlas texture material": "Материал текстуры атласа",
    "Brush size (tiles)": "Размер кисти (в тайлах)",
    "Inspect atlas / choose tile...": "Открыть атлас / выбрать тайл...",
    "Convert / review semantic atlas...": "Настроить материалы атласа...",
    "Export paint diagnostic...": "Экспортировать диагностику рисования...",
    "light": "свечение",
    "Namespace template ready: %1 material(s) for this layer.":
        "Шаблон ресурсов готов: материалов для этого слоя — %1.",
    "Review: %1 x %2 (%3 x %4 sectors)\nOutput: %5\nFraming: %6\nOrientation: %7":
        "Просмотр: %1 × %2 (%3 × %4 секторов)\nРезультат: %5\nКадрирование: %6\nОриентация: %7",
    "Embedded - no embedded minimap found": "Встроенная — текстура не найдена",
    "Embedded: %1": "Встроенная: %1",
    "%1 x %1 game texture - mouse wheel zooms; drag to pan":
        "Игровая текстура %1 × %1 — колесо меняет масштаб, перетаскивание сдвигает изображение",
    "Fit": "Вместить",
    "Linked views": "Связать виды",
    "Independent views": "Раздельные виды",
    "Regenerate": "Обновить",
    "Apply && Lock": "Применить и заблокировать",
    "Save review PNG...": "Сохранить PNG для проверки...",
    "Regenerated using current object visibility.": "Обновлено с учётом видимости объектов.",
    "This graded minimap is applied and locked. Use Unlock in the viewport minimap to permit regeneration.":
        "Настройки изображения применены, миникарта заблокирована. Чтобы обновлять её, нажмите «Разблокировать» на панели миникарты.",
    "Save full-resolution minimap review": "Сохранить полноразмерную миникарту для проверки",
    "Save minimap review": "Сохранить миникарту для проверки",
    "%1 is %2 MiB. The verified rebuild needs temporary disk space and may take time. Continue?":
        "%1 занимает %2 МиБ. Для безопасной пересборки понадобится дополнительное место и время. Продолжить?",
    "Inspecting destination RES": "Проверка целевого RES",
    "Could not inspect destination: %1": "Не удалось проверить целевой RES: %1",
    "%1 already exists in %2. Replace that member after creating a verified backup?":
        "%1 уже есть в %2. Создать резервную копию и заменить этот ресурс?",
    "Backing up, rebuilding, and validating the destination RES...":
        "Создание резервной копии, пересборка и проверка целевого RES...",
    "Approve and save": "Подтвердить и сохранить",
    "Review semantic terrain atlas": "Проверка материалов атласа рельефа",
    "Material families": "Семейства материалов",
    "Corner families (NW / NE / SE / SW)": "Материалы углов (СЗ / СВ / ЮВ / ЮЗ)",
    "Split selected slots...": "Отделить выбранные слоты...",
    "Merge selected families...": "Объединить выбранные семейства...",
    "Mark selected Reviewed": "Отметить выбранные как проверенные",
    "Mark selected Needs review": "Отметить выбранные для проверки",
    "Split material family": "Разделить семейство материалов",
    "Split family": "Разделить семейство",
    "Cannot split family": "Не удалось разделить семейство",
    "Merged family": "Объединённое семейство",
    "Cannot merge families": "Не удалось объединить семейства",
    "Select one or more slot rows first.": "Сначала выберите одну или несколько строк слотов.",
    "Atlas review incomplete": "Проверка атласа не завершена",
    "Atlas profile cannot be approved": "Профиль атласа нельзя подтвердить",
    "Tiles Parameters": "Параметры тайлов",
    "Tiles": "Тайлы",
    "Tile type": "Тип тайла",
    "Active liquid material id": "ID активного материала жидкости",
    "Quick access tiles table (hotkey 1-8)": "Тайлы быстрого доступа (клавиши 1–8)",
    "Available texture tiles table": "Доступные тайлы текстуры",
    "Start tile index": "Начальный индекс тайла",
    "MPR Atlas Tiles": "Тайлы атласа MPR",
    "MPR Atlas Tiles — %1": "Тайлы атласа MPR — %1",
    "Review and export the current generated minimap": "Проверить и экспортировать созданную миникарту",
    "Regenerate now using current object visibility": "Обновить с учётом видимости объектов",
    "Lock or unlock generated minimap regeneration": "Разрешить или запретить обновление миникарты",
    "Use a smaller minimap": "Уменьшить миникарту",
    "Use a larger minimap": "Увеличить миникарту",
    "Regenerate now": "Обновить сейчас",
    "Generated preview": "Созданная",
    "No minimap available": "Миникарта недоступна",
    "Generated (MPR-only: no MOB source is loaded)": "Созданная (только MPR, MOB не загружен)",
    "Open texts.res Copy...": "Открыть копию texts.res...",
    "Import map.txt...": "Импортировать map.txt...",
    "Export map.txt...": "Экспортировать map.txt...",
    "Allod / island ID": "ID аллода / острова",
    "Map sectors X": "Секторов по X",
    "Map sectors Y": "Секторов по Y",
    "Destination": "Зона назначения",
    "Dest exit": "Выход назначения",
    "Deploy X1": "Вход X1",
    "Remove X1": "Выход X1",
    "Pass time": "Время перехода",
    "Deploy angle": "Направление при входе",
    "Apply Raw Source": "Применить исходный текст",
    "Raw Source Span": "Исходный текст зоны",
    "Save Verified RES As...": "Сохранить проверенный RES как...",
    "Open a copied or mod-profile texts.res": "Открыть копию texts.res или архив из профиля мода",
    "Unsupported source value; choose game, brief, or edge before verified RES save.":
        "Недопустимое значение source: перед сохранением RES выберите game, brief или edge.",
    "New registration preview is created only after every required value is supplied. No weather, sky, camera, figure, cage, or LOD metadata will be invented.":
        "Предпросмотр новой зоны появится после заполнения обязательных полей. Редактор не создаёт автоматически данные погоды, неба, камеры, figure, cage или LOD.",
    "Imported %1 zones into memory; the RES archive is unchanged until verified save.":
        "Зон импортировано: %1. Архив RES не изменится до проверенного сохранения.",
    "Save verified texts.res": "Сохранить проверенный texts.res",
    "Destination already matches; no write was needed.": "Целевой файл уже совпадает; запись не потребовалась.",
    "Verified RES saved. Backup: %1": "RES сохранён и проверен. Резервная копия: %1",
    "Existing minimap — %1": "Встроенная миникарта — %1",
    "Minimap reference staged. Review it, then use Save As to publish the verified catalog update.":
        "Ссылка на миникарту подготовлена. Проверьте её и выберите «Сохранить как», чтобы записать изменения каталога.",
    "Controls detail written to the workflow log: 0 Off, 1 Fatal errors, 2 Warnings, 3 Information, 4 Debug. Use Information for normal testing and Debug when reproducing a problem.":
        "Подробность журнала: 0 — выкл., 1 — критические ошибки, 2 — предупреждения, 3 — информация, 4 — отладка. Для обычной проверки выберите «Информация», для воспроизведения проблемы — «Отладка».",
    "0 Off; 1 Fatal errors; 2 Warnings; 3 Information; 4 Debug. Information is recommended for testing; Debug produces the most diagnostic detail.":
        "0 — выкл.; 1 — критические ошибки; 2 — предупреждения; 3 — информация; 4 — отладка. Для обычной проверки рекомендуется «Информация», максимум сведений даёт «Отладка».",
    "Maximum number of editor commands retained for Undo and Redo, including selection changes. The default is 512. Applied after restarting the editor.":
        "Максимальное число действий в истории отмены и повтора, включая изменение выделения. По умолчанию — 512. Применяется после перезапуска редактора.",
    "Two-way diplomacy changes": "Изменять дипломатию в обе стороны",
    "Size of a newly added MOB object-ID range. The new range starts after the highest existing range and ends at start plus this value.":
        "Размер нового диапазона ID объектов MOB. Он начинается после последнего существующего диапазона и заканчивается на значении «начало + размер».",
    "New ID range size": "Размер нового диапазона ID",
    "Draw help radius in logic mode": "Показывать радиус помощи в режиме логики",
    "Frustum culling": "Не рисовать объекты за пределами камеры",
    "Vertical synchronization (restart required)": "Вертикальная синхронизация (требуется перезапуск)",
    "Limits viewport updates per second. Zero is unlimited; VSync may still limit presentation.":
        "Ограничивает частоту обновления окна карты. 0 — без ограничений; VSync всё равно может ограничивать частоту кадров.",
    "Scene point lights": "Точечные источники света",
    "Off": "Выкл.",
    "Maximum caster distance in world units (1-1024). Raising it expands shadow-map reach; 1 is useful for verifying the cutoff.":
        "Максимальная дальность объектов, отбрасывающих тень (1–1024). Большее значение увеличивает охват карт теней; 1 удобно для проверки границы.",
    "Open MOB script externally": "Открыть скрипт MOB во внешнем редакторе",
    "Shows or hides the Maps and Collections browser dock.": "Показывает или скрывает панель «Карты и коллекции».",
    "Adjust camera speed": "Изменение скорости камеры",
    "Open last session on startup": "Открывать последний сеанс при запуске",
    "Periodic recovery autosave": "Периодическое резервное автосохранение",
    "How often Endurance snapshots dirty documents. Identical document states are deduplicated.":
        "Как часто Endurance создаёт резервные копии изменённых документов. Одинаковые состояния повторно не сохраняются.",
    "Autosave Now": "Создать резервные копии сейчас",
    "Immediately create recovery generations for all dirty documents.":
        "Немедленно создать резервные копии всех изменённых документов.",
    "Character cache limit (MiB)": "Ограничение кэша персонажей (МиБ)",
    "Select external script editor": "Выбрать внешний редактор скриптов",
    "Open MOB(s)": "Открыть MOB",
    "Tile brush operations": "Работа кистью тайлов",
    "Draw|Hide land": "Показать / скрыть сушу",
    "Draw|Hide water": "Показать / скрыть воду",
    "Legacy figure folders (advanced)": "Дополнительные папки моделей (для совместимости)",
    "Legacy texture folders (advanced)": "Дополнительные папки текстур (для совместимости)",
    "This object belongs to a locked MOB. Activate its owner to edit it.":
        "Объект находится в неактивном MOB. Сделайте этот MOB активным, чтобы изменить объект.",
    "Logic 0 is the default setup and is always enabled. Its stored flag is preserved but not editable.":
        "Логика 0 используется по умолчанию и всегда включена. Сохранённый флаг остаётся в MOB, но не редактируется.",
    "Controls only whether the torch/campfire model casts its own silhouette. It does not disable shadows produced by the embedded point light; TORCH records have no separate shadow flag for that light.":
        "Определяет, отбрасывает ли тень модель факела или костра. Не влияет на тени от встроенного источника света: отдельного флага для него в MOB нет.",
    "Look-at position stored in the MOB. X and Y determine the facing direction; Z is stored but is not used when the look angle is calculated.":
        "Точка, куда смотрит существо. X и Y задают направление; Z сохраняется в MOB, но при расчёте поворота не используется.",
    "Preserved in the MOB, but no confirmed native magic-trap behavior or visualization uses this value.":
        "Значение сохраняется в MOB, но подтверждённого применения для магической ловушки нет.",
    "Effective trap targets are read-only and derived from Player group and MOB diplomacy.":
        "Цели ловушки нельзя изменить здесь: игра вычисляет их по группе игрока и таблице дипломатии MOB.",
    "Available to scripts": "Доступен для скриптов",
    "Scale": "Масштаб",
    "Ambient sound": "Фоновый звук",
    "Allowed interaction": "Способ открытия",
    "Repeatable": "Можно использовать повторно",
    "Door interaction": "Считать дверью",
    "Stored target mask (legacy)": "Сохранённая маска целей (устаревшая)",
    "Cast interval": "Пауза между срабатываниями",
    "Guard radius": "Радиус охраны",
    "Alarm condition": "Условие тревоги",
    "Alarm raised": "Поднимаемая тревога",
    "Ownership and diplomacy group used by this object.": "Группа объекта, определяющая принадлежность и дипломатию.",
    "Preserved object-template value. Normal units use Database prototype instead.":
        "Сохранённое значение шаблона. Обычные существа используют вместо него прототип из базы.",
    "For Logic 1-4, allow this setup to activate when its corresponding world alarm is raised. Logic 0 is the default enabled slot and ignores this setting.":
        "Для логики 1–4 разрешает включение при соответствующей мировой тревоге. Логика 0 всегда включена и не учитывает этот параметр.",
    "Distance within which allied units can respond for help.": "Расстояние, с которого союзные существа могут прийти на помощь.",
    "Keep this unit's AI running regardless of distance. Otherwise it activates within twice the player's maximum vision range plus 20 metres.":
        "Не отключать ИИ существа на расстоянии. Иначе ИИ работает в пределах двойной дальности зрения игрока плюс 20 метров.",
    "Stored and preserved in the MOB. The audited unit AI does not use this value.":
        "Значение сохраняется в MOB, но игра его не использует.",
    "Time spent looking toward this point. 15 ticks equal one second.":
        "Время взгляда в эту точку. 15 тактов равны одной секунде.",
    "How quickly the unit turns toward this point. 0 turns immediately.":
        "Скорость поворота к точке. 0 — повернуться мгновенно.",
    "0 uses normal behavior. 1 rests at the point.": "0 — обычное поведение; 1 — отдыхать в точке.",
    "Minimum Science skill required for manual interaction.": "Минимальный навык «Наука» для ручного открытия.",
    "Treat this lever as a door approached from its usable sides.": "Считать рычаг дверью, к которой подходят с доступных сторон.",
    "Update AI navigation when this object's state changes. Enable it when states change passability.":
        "Пересчитывать навигацию при смене состояния объекта. Включите, если состояния меняют проходимость.",
    "Serialized 32-bit player-group mask. Normal map loading replaces it using Player group and the MOB diplomacy table.":
        "Сохранённая 32-битная маска групп. При загрузке карты игра заменяет её значением, вычисленным по группе игрока и таблице дипломатии MOB.",
    "Groups marked War in this trap's Player group row of the containing MOB's diplomacy table. This is the target mask the game calculates when the map loads.":
        "Группы со значением «Война» в строке дипломатии MOB для группы игрока. Именно эти цели игра назначает ловушке при загрузке карты.",
    "Radius checked around this activation center.": "Радиус поиска целей вокруг центра области активации.",
    "Light color.": "Цвет света.",
    "Play this source repeatedly using its delay range.": "Повторять звук с задержкой в указанном диапазоне.",
    "Stored and preserved in the MOB. No confirmed runtime audio behavior uses this flag.":
        "Флаг сохраняется в MOB, но подтверждённого влияния на звук в игре нет.",
    "Landscape sculpt mode enabled.": "Включена правка рельефа.",
    "Overlay painting enabled for existing overlay cells only.": "Включено рисование по существующим поверхностям.",
    "Overlay-only sculpting enabled for active surfaces.": "Включена правка высоты активных поверхностей.",
    "Set the editor time of day. When changed, this time is also applied to JMV Test Current Map and hot reload.":
        "Установить время суток в редакторе. Оно также используется при тесте карты и горячей перезагрузке JMV.",
    "Build a non-saving diagnostic graph from current terrain and static-object collision proxies.":
        "Построить временный граф по текущему рельефу и границам неподвижных объектов, не сохраняя его.",
    "Compare loaded and regenerated connectivity, link weights, and node positions without changing either graph.":
        "Сравнить связи, их веса и положения узлов загруженного и созданного графов, не изменяя их.",
    "Switch between the graph loaded from the map and the generated graph. Test Current Map and Hot Reload stage the graph named on this button.":
        "Переключить показ между графом карты и созданным графом. При тесте и горячей перезагрузке используется граф, указанный на кнопке.",
    "Persist the selected loaded or regenerated graph as an exact runtime .grp, embed it into the primary MOB, or switch the primary MOB to external graph ownership.":
        "Сохранить выбранный граф во внешний .grp, встроить его в основной MOB или перевести MOB на уже существующий внешний .grp.",
    "Regeneration queued": "Повторное построение поставлено в очередь",
    "Regenerate again": "Построить ещё раз",
    "Showing the graph loaded from the map. Click to show the generated graph. Test Current Map and Hot Reload stage the graph named here.":
        "Показан граф карты. Нажмите, чтобы показать созданный граф. При тесте и горячей перезагрузке используется показанный здесь граф.",
    "Showing the generated graph. Click to show the graph loaded from the map. Test Current Map and Hot Reload stage the graph named here.":
        "Показан созданный граф. Нажмите, чтобы показать граф карты. При тесте и горячей перезагрузке используется показанный здесь граф.",
    "Freshly regenerated graph": "Новый созданный граф",
    "AI graph preview regeneration failed: %1": "Не удалось построить граф ИИ: %1",
    "The viewport renderer is not initialized.": "Окно карты ещё не готово к отрисовке.",
    "Excluded %1 dynamic unit(s).": "Исключено динамических существ: %1.",
    "MPR opened with pre-existing terrain warnings. Editing remains available; review the workflow log before saving.":
        "MPR открыт с предупреждениями о рельефе. Редактирование доступно; перед сохранением проверьте журнал работы.",
    "Preparing canonical terrain patches...": "Подготовка изменений рельефа...",
    "Staged MPR validation failed": "Подготовленный MPR не прошёл проверку",
    "Creating backup and committing atomically...": "Создание резервной копии и сохранение...",
    "Reopening and adopting validated terrain...": "Повторное открытие проверенного рельефа...",
    "%1 (%2 natural variant(s))": "%1 (вариантов: %2)",
    " An inference proposal is ready for review.": " Предложение по материалам готово к проверке.",
    "Paint diagnostic": "Диагностика рисования",
    "Could not write %1: %2": "Не удалось сохранить %1: %2",
    "Saving all MOBs": "Сохранение всех MOB",
    "Start atlas slot": "Начальный слот атласа",
    "Resolved range": "Диапазон слотов",
    "Disabled": "Отключено",
    "Invalid tile animation": "Недопустимая анимация тайлов",
    "Clamp to range": "Ограничить диапазоном",
    "Raw height delta": "Изменение исходной высоты",
    "Target raw height": "Целевая исходная высота",
    "Apply undoable operation": "Применить операцию",
    "Apply as one undoable change": "Применить одним отменяемым действием",
    "Scale terrain with vertical range": "Масштабировать рельеф вместе с высотой",
    "Allow vertices above the new range to clamp": "Ограничить вершины выше нового диапазона",
    "New maxZ": "Новое maxZ",
    "Policy": "Способ пересчёта",
    "Apply undoable scale migration": "Применить изменение масштаба",
    "Resize canvas and Save As...": "Изменить размер карты и сохранить как...",
    "Landscape (*.mpr)": "MPR (*.mpr)",
    "Open Atlas Browser...": "Открыть атлас...",
    "%1 x %2 sectors; %3 x %4 tiles; %5 x %6 vertices": "%1 × %2 секторов; %3 × %4 тайлов; %5 × %6 вершин",
    "This is a transition tile; sample a uniform tile to choose one material.":
        "Это переходный тайл. Возьмите образец однородного тайла, чтобы выбрать один материал.",
    "Paint manual atlas tiles": "Рисовать тайлами атласа вручную",
    "Paint terrain tiles": "Рисовать тайлы рельефа",
    "Prepare terrain materials": "Подготовить материалы рельефа",
    "disabled": "отключено",
    "Patrol point %1": "Патрульная точка %1",
    "Look point %1": "Точка обзора %1",
    "LMB: Select": "ЛКМ: выбрать",
    "MMB: Rotate camera": "СКМ: вращать камеру",
    "G: Move": "G: перемещение",
    "T: Scale": "T: масштаб",
    "R: Rotate": "R: вращение",
    "L: Add look to last patrol": "L: добавить точку обзора к последней патрульной точке",
    "L: Add look": "L: добавить точку обзора",
    "L: Add look after selected": "L: добавить точку обзора после выбранной",
    "L: Add cast target": "L: добавить цель заклинания",
    "Sculpt strength: %1": "Сила кисти: %1",
    "Select one or more units as Logic-slot destinations.": "Выберите одно или несколько существ, которым нужно вставить логику.",
    "&Unit": "&Существо",
    "Randomize Parameter": "Случайные параметры",
    "Logic 0 (default)": "Логика 0 (по умолчанию)",
    "The unit's normal behavior setup.": "Обычная настройка поведения существа.",
    "Select which of the unit's five behavior setups is shown and edited. The first setup is the default; the others activate from world alarms 1 to 4.":
        "Выберите одну из пяти настроек поведения существа. Первая используется по умолчанию, остальные включаются мировыми тревогами 1–4.",
    "Select owning object": "Выбрать объект-владелец",
    "Unload MOBs": "Выгрузить MOB",
    "Open Collection...": "Открыть коллекцию...",
    "Open Collection": "Открыть коллекцию",
    "Validate Active MOB Family": "Проверить семейство активного MOB",
    "Validate Current MOB Only": "Проверить только текущий MOB",
    "Show Selected Unit Final Texture...": "Показать итоговую текстуру выбранного существа...",
    "Capture Shadow Diagnostics...": "Собрать диагностику теней...",
    "Endurance backups": "Резервные копии Endurance",
    "Close Game": "Закрыть игру",
    "Leave Running": "Оставить запущенной",
    "Part UV projections": "UV-развёртки частей",
    "mixed": "разные значения",
    "Logic %1 (alarm %1; selection: %2)": "Логика %1 (тревога %1; выделение: %2)",
    "Logic %1 (alarm %1)": "Логика %1 (тревога %1)",
    "Responds to world alarm %1. Unit selection: %2.": "Реагирует на мировую тревогу %1. Выделение существ: %2.",
    "Final character texture — %1": "Итоговая текстура персонажа — %1",
    "none": "нет",
    "Entrance spawn": "Точка появления у входа",
    "Borrow map.txt identity": "Использовать данные другой зоны из map.txt",
    "Play from Here": "Играть отсюда",
    "Hot Reload Map": "Перезагрузить карту",
    "Godmode": "Режим бессмертия",
    "Base EI feasibility probe": "Проверка прямого запуска базовой ПЗ",
    "Choosing a safe map-table entrance...": "Выбор подходящего входа из map.txt...",
    "Generated: %1 static objects; %2 hidden; %3 units excluded\n%4":
        "Создано: статических объектов — %1; скрытых — %2; исключено существ — %3\n%4",
    "This experimental JMV-only test stages the exact current MPR/MOB pair, including unsaved in-memory edits, under a unique temporary name. After JMV completes its normal menu load, Endurance invokes the fingerprinted single-player New Game path on the game thread and supplies the staged pair. No console/server command or fixed delay is used.\n\nJMV initializes one normal player through map-table zone %1 and hand-placed entrance %2; %3.\n\nSource maps, JMV configuration, and live campaign saves are not changed. Complete payloads remain under Endurance AppData; the proven JMV loader also uses one randomly named, hash-owned MPR/MOB alias in the mod Maps directory and removes it on guarded cleanup. Every save/load path is redirected into a persistent profile-specific AppData folder shared by Endurance test runs. Intro movies are hidden only for the test process. Returning to the menu or travelling away may use original installed maps; use Stop Test when finished.\n\nStart the direct JMV map test?":
        "Этот экспериментальный тест JMV запускает точное текущее состояние MPR/MOB, включая несохранённые правки, под уникальным временным именем. После загрузки обычного меню Endurance запускает одиночную «Новую игру» и передаёт подготовленную карту. Консольные команды и фиксированные задержки не используются.\n\nJMV создаёт обычного игрока в зоне %1 у вручную заданного входа %2; %3.\n\nИсходные карты, настройки JMV и сохранения кампании не изменяются. Полные временные данные хранятся в AppData Endurance. Для загрузки также создаётся случайно названная временная пара MPR/MOB в папке Maps мода; она удаляется после завершения теста. Сохранения тестов направляются в отдельную постоянную папку AppData этого профиля. Вступительные ролики скрываются только для тестового процесса. После возврата в меню или перехода в другую зону игра может загрузить установленные карты; по завершении нажмите «Остановить тест».\n\nЗапустить прямой тест карты JMV?",
    "Bake / Export AI Graph": "Сохранение графа ИИ",
    "Endurance could not prepare a runtime-compatible AI graph:\n\n%1":
        "Endurance не удалось подготовить граф ИИ для игры:\n\n%1",
    "\n\nActive MPR: %1\nRuntime external graph: %2\nPrimary MOB: %3\n\nRuntime priority is the primary MOB's embedded graph first, then the exact active-MPR sibling .grp.":
        "\n\nАктивный MPR: %1\nВнешний .grp для игры: %2\nОсновной MOB: %3\n\nИгра сначала использует граф, встроенный в основной MOB. Если его нет — внешний .grp с именем активного MPR.",
    "\n\nThe primary MOB currently has an embedded graph. Export Runtime .grp writes the selected graph. Switch MOB to External .grp never writes the existing .grp; it only validates that file and removes embedded ownership from the MOB.":
        "\n\nВ основном MOB уже есть встроенный граф. «Экспортировать игровой .grp» записывает выбранный граф. «Переключить MOB на внешний .grp» не изменяет существующий .grp: команда проверяет файл и удаляет встроенный граф из MOB.",
    "\n\nThe primary MOB has unsaved edits. Bake or Switch will commit those edits together with the graph through the verified MOB save path.":
        "\n\nВ основном MOB есть несохранённые изменения. При встраивании или переключении они будут сохранены вместе с графом после проверки MOB.",
    "Choose how '%1' should be persisted.": "Выберите, как сохранить граф «%1».",
    "Export Runtime .grp": "Экспортировать игровой .grp",
    "Switch MOB to External .grp": "Переключить MOB на внешний .grp",
    "Staging and validating %1...": "Подготовка и проверка %1...",
    "The runtime .grp was not changed.\n\nPhase: %1\n%2": "Игровой .grp не изменён.\n\nЭтап: %1\n%2",
    "The exact runtime .grp already contained the selected graph:\n%1": "В игровом .grp уже находится выбранный граф:\n%1",
    "Exported and freshly validated the exact runtime .grp:\n%1": "Игровой .grp экспортирован и повторно проверен:\n%1",
    "The embedded graph was left untouched because no valid existing runtime .grp is available. Export a graph first, then retry the ownership switch.\n\n%1":
        "Встроенный граф не изменён: подходящий внешний .grp не найден или не прошёл проверку. Сначала экспортируйте граф, затем повторите переключение.\n\n%1",
    "The existing runtime .grp was not changed, but the embedded graph could not be removed. The MOB still shadows the external file, so runtime ownership has not changed.\n\n%1":
        "Внешний .grp не изменён, но удалить встроенный граф из MOB не удалось. Поэтому игра по-прежнему использует встроенный граф.\n\n%1",
    "Preserved the existing runtime graph %1 unchanged and removed the embedded graph from %2. The active MPR now resolves that sibling .grp at runtime.":
        "Внешний граф %1 оставлен без изменений, а встроенный граф удалён из %2. Теперь игра использует .grp с именем активного MPR.",
    "Validating runtime MOB family…": "Проверка игрового семейства MOB…",
    "Unit visual": "Внешний вид существа",
    "New family ID risk": "Риск ID в новом семействе",
    "Family members": "Файлы семейства",
    "SHADOWED": "НЕ ИСПОЛЬЗУЕТСЯ",
    "Not collision-checked; workspace source overrides it": "Не проверено: вместо этого источника используется файл из рабочей области",
    "Shadowed source for %1\n%2": "Неиспользуемый источник для %1\n%2",
    "Dismiss": "Закрыть",
    "NID repair": "Исправление NID",
    "Repair": "Исправить",
    "Ready": "Готово",
    "Replacement NID %1 is already used by the family or another selected repair.":
        "NID %1 уже используется в семействе или выбран для другого исправления.",
    "Collection profile": "Профиль коллекции не совпадает",
    "Loading landscape...": "Загрузка рельефа...",
    "MPR namespace": "Пространство имён MPR",
    "Width (16-tile sectors)": "Ширина (секторы по 16 тайлов)",
    "Height (16-tile sectors)": "Высота (секторы по 16 тайлов)",
    "Flat raw height": "Исходная высота плоскости",
    "Initial tile rotation": "Поворот начального тайла",
    "Edge taper width (tiles)": "Ширина сглаживания края (тайлы)",
    "Overlay atlas slot": "Слот атласа поверхности",
    "Overlay tile rotation": "Поворот тайла поверхности",
    "Liquid material index": "Индекс материала жидкости",
    "Overlay taper width (tiles)": "Ширина сглаживания поверхности (тайлы)",
    "Flat terrain preview: raw %1 = %2 world units. Choose a heightmap to preview its stretch and orientation.":
        "Плоский рельеф: исходное значение %1 = %2 единицам мира. Выберите карту высот, чтобы проверить растяжение и ориентацию.",
    "The validated MPR was created at:\n%1\n\nIt was not opened because the current document transition was cancelled.":
        "Проверенный MPR создан:\n%1\n\nОн не открыт, потому что смена текущего документа была отменена.",
    "Create an empty, runtime-valid MOB. Objects, embedded AI graph, VSS, and editor-only directory data are never copied. The normal script and selected map metadata can be copied explicitly.":
        "Создать пустой MOB, пригодный для игры. Объекты, встроенный граф ИИ, VSS и служебные данные редактора не копируются. Обычный скрипт и выбранные данные карты можно скопировать отдельно.",
    "Primary MOBs own world settings and normally anchor a map. Supplemental MOBs are the usual choice for AutoZones and optional map states.":
        "Основной MOB хранит настройки мира и обычно задаёт карту. Дополнительные MOB обычно используются для AutoZone и дополнительных состояний карты.",
    "Create a default 32-player diplomacy table": "Создать стандартную таблицу дипломатии для 32 групп",
    "Initial ambient-light scalar stored in WORLD_SET.": "Начальная яркость фонового света в WORLD_SET.",
    "Initial sun-light scalar stored in WORLD_SET.": "Начальная яркость солнечного света в WORLD_SET.",
    "Save New MOB": "Сохранить новый MOB",
    "MOB (*.mob)": "MOB (*.mob)",
    "The validated MOB was created at:\n%1\n\nIt could not be opened automatically.":
        "Проверенный MOB создан:\n%1\n\nОткрыть его автоматически не удалось.",
    "Open one or more MOB files": "Открыть один или несколько файлов MOB",
    "Save All MOBs": "Сохранить все MOB",
    "Source ambiguity": "Неоднозначный источник",
    "Incomplete family": "Неполное семейство",
    "Cross-family workspace range overlap": "Пересечение диапазонов разных семейств",
    "Cross-family workspace NID collision": "Конфликт NID между разными семействами",
    "Known static alias": "Известный общий ID неподвижных объектов",
    "NID collision": "Конфликт NID",
    "installed fallback": "резервный файл из установки",
    "explicit seed": "явно заданный источник",
    "loaded primary overlay": "загруженный основной файл вместо профильного",
    "Multiple opened MOBs use logical name %1; %2 is authoritative":
        "Несколько открытых MOB используют имя %1; основным считается %2",
    "MOB file \"%1\" was not found. Referenced by %2 script line %3: %4":
        "MOB-файл «%1» не найден. Ссылка в скрипте %2, строка %3: %4",
    "%1 has %2 directional diplomacy pair(s). One-way relations are supported and are not normalized, but confirm they are intentional: %3":
        "%1: односторонних отношений дипломатии — %2. Они допустимы и не исправляются автоматически; проверьте, что это сделано намеренно: %3",
    "%1 has %2 non-Ally self-relation(s). Self-relations are supported and are preserved, but are unusual; confirm they are intentional: %3":
        "%1: отношений группы к самой себе со значением не «Союз» — %2. Они допустимы и сохраняются, но необычны; проверьте их: %3",
    "%1 primary SC_OBJECT_DB_FILE MOBs are loaded; map ownership and Test Current Map are ambiguous":
        "Загружено основных MOB (SC_OBJECT_DB_FILE): %1. Нельзя однозначно определить основной MOB карты для теста",
    "NID %1 is owned by %2 objects across separately loaded map sets; they are not one %3 runtime family":
        "NID %1 используется объектами из разных загруженных карт: %2. Эти карты не входят в одно игровое семейство %3",
    "NID %1 is owned by %2 objects across the runtime family":
        "NID %1 используется объектами одного игрового семейства: %2",
    "Character '%1' doesn't have a valid tables.lua entry.": "Для персонажа «%1» нет правильной записи в tables.lua.",
})

# The same English source can require different Russian wording in different
# widgets. Apply these after all catalog-wide cleanup so context wins.
CORRECTIONS.update({
    "Stages unique temporary map copies and launches the validated JMV runtime. This capability is experimental.":
        "Создаёт уникальные временные копии карты и запускает их в проверенной версии JMV. Возможность экспериментальная.",
    "Crop/expand is a structural Save As operation. Position the original map directly on the resulting tile grid; right-drag the map in the preview or enter its left/top tile offsets. Sector-aligned retained SEC payloads stay byte-identical; other offsets are re-tiled and freshly validated. Expansion projects the nearest land boundary and does not invent overlays. Closed edges are explicitly lossy and deactivate overlays in the taper band. The preview uses generated MPR terrain only; embedded minimaps and MOB geometry are excluded. A companion MOB is always a new file; its objects are shifted with the map, optionally clipped by object origin, and its stale embedded AI graph is omitted. The 32 x 32 ceiling corresponds to the runtime's 256 x 256 navigation-grid limit.":
        "Обрезка и расширение всегда сохраняют новый MPR. Расположите исходную карту на сетке тайлов: перетащите её ПКМ в предпросмотре или задайте смещения слева и сверху. При смещении на целые сектора данные SEC сохраняются без изменений; в остальных случаях тайлы перестраиваются и проверяются заново. Расширение продолжает ближайший край суши, но не создаёт новые поверхности. Сглаживание внешнего края меняет данные и отключает поверхности в этой полосе. Предпросмотр показывает только рельеф MPR, без встроенной миникарты и объектов MOB. Связанный MOB всегда создаётся как новый файл: объекты сдвигаются вместе с картой, при необходимости удаляются за её границами, а устаревший встроенный граф ИИ не копируется. Максимум 32 × 32 сектора соответствует игровому пределу навигационной сетки 256 × 256.",
    "Stages unique temporary MPR/MOB copies and launches the active validated runtime profile. JMV uses hash-owned temporary aliases in the mod Maps directory; source maps are never overwritten.":
        "Создаёт уникальные временные копии MPR/MOB и запускает их с проверенным игровым профилем. Для JMV временная пара получает случайное имя в папке Maps мода; исходные карты не перезаписываются.",
    "Borrowed map identity: %1. Runtime metadata and scripts belong to that registered zone.":
        "Используются данные зоны %1. Игровые метаданные и скрипты принадлежат этой зарегистрированной зоне.",
    "Selects the map.txt entrance used to initialize the runtime session.":
        "Выбирает вход из map.txt, с которого начнётся тест в игре.",
    "Endurance is waiting for an interrupted test game to finish exiting and for hash-owned cleanup to complete.":
        "Endurance ждёт завершения прерванного теста и удаления принадлежащих ему временных файлов.",
    "The active installation does not match an exact supported or experimental runtime manifest. Run Game and Mod Setup again and review its diagnostics.":
        "Установленная версия игры не совпадает ни с одним поддерживаемым или экспериментальным профилем. Повторно откройте настройку игры и мода и проверьте диагностику.",
    "The active installation does not match an exact supported or experimental runtime manifest.\n\n%1\n\nRun Game and Mod Setup again if this is not the intended installation.":
        "Установленная версия игры не совпадает ни с одним поддерживаемым или экспериментальным профилем.\n\n%1\n\nЕсли выбрана не та папка игры, повторно откройте настройку игры и мода.",
    "\n\nIt is currently shadowed by the primary MOB's embedded graph.":
        "\n\nСейчас игра использует встроенный граф основного MOB вместо этого файла.",
    "No repairable reserved object ID or critical NID collision is present in the active runtime family.":
        "В активном игровом семействе нет исправляемого зарезервированного ID объекта или критического конфликта NID.",
})

CORRECTIONS.update({
    "Automatic mode always covers the brush core, uses an exact or constrained nearest authored seam when available, and otherwise leaves a hard edge. Manual mode paints the selected existing atlas tile and rotation without blending. Size 1 paints one core tile. Right-click samples from the map. Alt+Wheel changes brush size. Shift+Wheel selects the automatic material or rotates the manual tile; Wheel keeps camera zoom. Overlay surface mode adds/removes coverage. Its Terrain material selects the atlas texture; its Surface material independently controls tint, opacity, illumination, waves, and texture warp. Overlay painting only changes the atlas texture on cells that already exist.":
        "Авто: кисть подбирает подходящие тайлы и швы; если перехода нет, остаётся резкая граница. Вручную рисуется выбранный тайл без смешивания. ПКМ берёт образец с карты. Alt+колесо — размер кисти; Shift+колесо — материал или поворот; обычное колесо — масштаб камеры. Режим поверхности добавляет или удаляет покрытие. Материал рельефа выбирает текстуру атласа, а материал поверхности задаёт цвет, прозрачность, свечение, волны и искажение.",
    "Land tile painting enabled. Choose an atlas tile in Landscape Tools.":
        "Включено рисование тайлов суши. Выберите тайл атласа в инструментах ландшафта.",
    "Show material slots on overlay tiles": "Показывать материалы на тайлах поверхности",
    "Hide material slots on overlay tiles": "Скрыть материалы на тайлах поверхности",
    " tiles": " тайлов",
    "Original map offset: X %1, Y %2 tiles. Retain %3 x %4 (%5) source tile(s). World bounds become %6 x %7 units.%8 Output is a new MPR; the source is not overwritten.":
        "Смещение исходной карты: X %1, Y %2 тайлов. Сохраняется область %3 × %4 (%5 тайлов). Размер мира станет %6 × %7 единиц.%8 Результат записывается в новый MPR; исходный файл не изменяется.",
    " The outer %1 tile(s) will be tapered to raw 0.": " Внешних тайлов, сглаживаемых до исходной высоты 0: %1.",
    "Choose an atlas tile before painting in manual mode.": "Перед рисованием вручную выберите тайл атласа.",
    "This map's atlas does not yet have an approved semantic material profile. Automatic terrain and overlay painting will remain unavailable; manual atlas-tile painting still works.\n\nGenerate the review proposal now? The review process only describes existing atlas tiles and does not modify the MPR or texture resources.":
        "Для атласа этой карты ещё не подтверждено назначение материалов. Автоматическое рисование рельефа и поверхностей пока недоступно, но тайлы атласа можно выбирать вручную.\n\nПодготовить список материалов для проверки? Он только описывает существующие тайлы и не изменяет MPR или текстуры.",
    "Painted %1 atlas tile(s) manually.": "Нарисовано тайлов атласа вручную: %1.",
    "Painted %1 atlas tile(s), including bounded seam updates.": "Нарисовано тайлов атласа с обновлением соседних швов: %1.",
    "Show Terrain Tile Grid": "Показать сетку тайлов рельефа",
    "Draw the 2 x 2 terrain-tile boundaries over the landscape.": "Показывать на рельефе границы тайлов 2 × 2.",
    "A proven MPR whose atlas namespace, tile metadata, liquid materials, animations, and other MP settings will be copied. The template itself is never modified.":
        "Проверенный MPR, из которого будут скопированы имя ресурсов атласа, данные тайлов, материалы жидкостей, анимации и другие настройки MP. Сам шаблон не изменяется.",
    "Creates an active overlay tile over the entire new map. Leave disabled for terrain-only maps.":
        "Создаёт активный тайл поверхности на всей новой карте. Оставьте выключенным для карт без воды и других поверхностей.",
    "Existing liquid/overlay material from the template MP. This controls material properties; the atlas slot controls the visible tile texture.":
        "Материал жидкости или поверхности из шаблона MP. Он задаёт свойства материала, а слот атласа — видимую текстуру тайла.",
    "Atlas tile animation records": "Анимации тайлов атласа",
    "%1 atlas slots; %2 surface material record(s); %3 tile animation record(s)":
        "Слотов атласа: %1; материалов поверхности: %2; анимаций тайлов: %3",
    "Adding an overlay requires an existing liquid material and atlas tile family.":
        "Для добавления поверхности нужен существующий материал жидкости и семейство тайлов атласа.",
    "Terrain stroke: %1 stamp(s), %2 changed tile(s), %3 no-op, %4 refused.":
        "Мазок рельефа: шагов — %1; тайлов изменено — %2; без изменений — %3; отклонено — %4.",
    "The companion operation never overwrites an existing MOB:\n%1\n\nChoose another resized MPR name. No file was changed.":
        "Связанный MOB никогда не перезаписывает существующий файл:\n%1\n\nВыберите другое имя для изменённого MPR. Файлы не изменены.",
    "Applies the game's heightfield-based hill and cliff self-shadowing to terrain. This is separate from object and point-light shadows, updates after Apply or Save/Close, and is unavailable on the ANGLE compatibility renderer.":
        "Добавляет рельефу игровое самозатенение холмов и обрывов по карте высот. Оно не связано с тенями объектов и точечных источников, обновляется после «Применить» или «Сохранить и закрыть» и недоступно в режиме совместимости ANGLE.",
})

CORRECTIONS.update({
    "Infer a namespace-scoped semantic profile, review every ambiguity, and explicitly approve it. This does not edit atlas images or MPR resources.":
        "Определить назначение материалов для этого набора ресурсов атласа, проверить спорные места и подтвердить результат. Изображения атласа и MPR при этом не изменяются.",
    "Resource names are archive namespaces, normally written without .mpr/.mob extensions. Values are never generated automatically.":
        "Имена ресурсов задаются так, как они записаны в архивах, обычно без расширений .mpr/.mob. Редактор не заполняет их автоматически.",
    "Namespace: %1   Slots: %2   Decoded atlas SHA-256: %3":
        "Набор ресурсов: %1   Слотов: %2   SHA-256 распакованного атласа: %3",
    "Export semantic atlas profile copy": "Экспортировать копию профиля материалов атласа",
    "Semantic atlas profiles (*.semantic-atlas.json);;JSON files (*.json)":
        "Профили материалов атласа (*.semantic-atlas.json);;Файлы JSON (*.json)",
    "Could not export semantic atlas profile": "Не удалось экспортировать профиль материалов атласа",
    "Semantic atlas profile exported": "Профиль материалов атласа экспортирован",
    "No approved semantic profile is available for namespace '%1'. Automatic painting is disabled; manual atlas tile mode remains available.":
        "Для набора ресурсов «%1» нет подтверждённого профиля материалов. Автоматическое рисование отключено, но тайлы атласа можно выбирать вручную.",
    "Approved semantic profile loaded: %1 land and %2 overlay materials.":
        "Загружен подтверждённый профиль материалов: суша — %1, поверхности — %2.",
    " A saved auto-inferred profile is obsolete or failed its namespace/hash/slot binding; review the new proposal before use.":
        " Сохранённый автоматически созданный профиль устарел или не совпадает с набором ресурсов, хешем либо слотами. Перед использованием проверьте новое предложение.",
    "This atlas layout is not registered; semantic terrain painting is disabled.":
        "Для этого атласа не зарегистрировано назначение материалов; автоматическое рисование рельефа отключено.",
    "Load an MPR and its complete terrain atlas before reviewing semantic conversion.":
        "Перед настройкой материалов загрузите MPR и полный атлас рельефа.",
    "Could not save semantic atlas profile": "Не удалось сохранить профиль материалов атласа",
    "The reviewed semantic atlas profile is approved and automatic painting is enabled. Saved to %1":
        "Профиль материалов атласа подтверждён, автоматическое рисование включено. Сохранено: %1",
    "Semantic atlas draft saved to %1. Automatic painting remains disabled until approval.":
        "Черновик профиля материалов сохранён: %1. Автоматическое рисование останется отключённым до подтверждения.",
    "semantic profile approved": "профиль материалов подтверждён",
    "authored semantic template active": "используется готовый шаблон материалов",
    "semantic profile draft — automatic paint disabled": "черновик профиля материалов — автоматическое рисование отключено",
    "Embedded namespace": "Имя ресурсов атласа",
    "No authored slot template is registered for this atlas namespace.":
        "Для этого набора ресурсов атласа не зарегистрирован готовый шаблон слотов.",
    "A new landscape reuses the exact atlas, material, and animation metadata from a proven template MPR. Endurance creates fresh MP/SEC members and does not invent a MOB or map.txt registration. If you change the namespace, matching atlas textures must already exist in the active resource profile.":
        "Новый рельеф копирует данные атласа, материалов и анимации из проверенного шаблона MPR. Endurance создаёт новые ресурсы MP/SEC, но не создаёт MOB и запись в map.txt. При смене имени ресурсов соответствующие текстуры атласа уже должны находиться в активном профиле.",
    "Internal MP/SEC resource-name stem for the new archive. Keep the template namespace unless matching atlas textures already exist under a different namespace.":
        "Общая основа имён внутренних ресурсов MP/SEC в новом архиве. Оставьте имя из шаблона, если подходящие текстуры атласа не существуют под другим именем.",
    "Raw height samples remain unchanged; world heights scale with maxZ and derived SEC normals are rebuilt.":
        "Исходные значения высоты не меняются; высота в мире масштабируется по maxZ, нормали SEC пересчитываются.",
    "Could not map NID %1 at %2 onto a loaded raw object.":
        "Не удалось сопоставить NID %1 по адресу %2 с загруженной исходной записью объекта.",
    "Sampled %1 raw height %2 as the Flatten target.":
        "Для выравнивания выбрана исходная высота %2 (%1).",
    "Raw MPR height for black (0-65535):": "Исходная высота MPR для чёрного (0–65535):",
    "Raw MPR height for white (0-65535):": "Исходная высота MPR для белого (0–65535):",
    "%1 | Raw: %2": "%1 | Исходное: %2",
    "The selected object's raw structure could not be converted safely. See the workflow log.":
        "Исходную структуру выбранного объекта нельзя безопасно преобразовать. Подробности — в журнале работы.",
    "Initial terrain height when no heightmap is selected. World height = raw height / 65535 x maxZ. A selected heightmap replaces this value across the terrain.":
        "Начальная высота рельефа без карты высот. Высота в мире = исходная высота / 65535 × maxZ. При выборе карты высот это значение заменяется по всему рельефу.",
    "Raw terrain height assigned to black pixels. It may be higher than the white endpoint to invert the heightmap.":
        "Исходная высота для чёрных пикселей. Она может быть выше значения для белых, чтобы инвертировать карту высот.",
    "Raw terrain height assigned to white pixels. It may be lower than the black endpoint to invert the heightmap.":
        "Исходная высота для белых пикселей. Она может быть ниже значения для чёрных, чтобы инвертировать карту высот.",
    "Heightmap black raw height": "Исходная высота чёрного на карте высот",
    "Heightmap white raw height": "Исходная высота белого на карте высот",
    "Constant raw height of the overlay plane. World height = raw height / 65535 x maxZ. This plane does not use the terrain heightmap.":
        "Постоянная исходная высота поверхности. Высота в мире = исходная высота / 65535 × maxZ. Карта высот рельефа на эту плоскость не влияет.",
    "Plane raw height": "Исходная высота поверхности",
    "Validating staged MPR candidate...": "Проверка подготовленного MPR...",
    "The temporary candidate failed validation. No destination was changed.\n\n%1":
        "Временный MPR не прошёл проверку. Целевой файл не изменён.\n\n%1",
    "Building resized MPR candidate...": "Создание MPR нового размера...",
    "Endurance could not represent the complete terrain edit as provenance-checked sector patches. No destination was changed.\n\n%1":
        "Endurance не удалось безопасно представить все правки рельефа как изменения секторов. Целевой файл не изменён.\n\n%1",
    "Endurance could not verify the selected object's lossless raw provenance.":
        "Endurance не удалось подтвердить, что исходную запись выбранного объекта можно сохранить без потерь.",
    "Object NID %1 is not selectable: its provenance is ambiguous or no longer present.":
        "Нельзя выбрать объект NID %1: его исходная запись неоднозначна или уже отсутствует.",
    "Exact stored scalar self-illumination. Emitted RGB is clamp(tint RGB x illumination, 0-1). It is a material property, not a lava-texture flag.":
        "Сохранённая яркость самосвечения. Итоговый RGB ограничивается диапазоном 0–1 после умножения оттенка на яркость. Это свойство материала, а не признак текстуры лавы.",
    "Exact stored scalar multiplier for wind-driven water displacement. Effective geometric amplitude is world Wind strength x 0.3 x this value, so Wind strength 0 produces no geometric motion. Wind direction and strength come from the loaded MOB's world environment. This also scales apparent wave-phase speed and does not control atlas-frame animation.":
        "Сохранённый множитель колебания воды от ветра. Амплитуда = сила ветра в мире × 0,3 × это значение; при силе 0 геометрия не движется. Направление и сила ветра берутся из настроек мира загруженного MOB. Параметр также меняет видимую скорость волны, но не анимацию кадров атласа.",
    "Exact stored scalar warp-speed value. The confirmed gameplay path ignores this authored value and uses a fixed 0.05 lateral-warp rate. Liquids can still appear to move at different speeds because Wave multiplier also scales their wave phase.":
        "Сохранённая скорость искажения. Игра не использует это значение и применяет постоянную боковую скорость 0,05. Жидкости всё же могут двигаться с разной скоростью, потому что множитель волны влияет и на её фазу.",
    "Scale terrain keeps every raw sample and changes world-space heights. Preserve world heights requantizes raw samples and may introduce rounding. Clamping is never accepted silently.":
        "«Масштабировать рельеф» сохраняет исходные значения и меняет высоту в мире. «Сохранить высоты в мире» пересчитывает исходные значения с возможным округлением. Выход за новый диапазон всегда требует явного разрешения.",
    "The requested migration is invalid.": "Такое изменение масштаба недопустимо.",
    "The requested operation would exceed the current raw range. It is refused until you choose an explicit clamp or maxZ migration policy.":
        "Операция выйдет за текущий диапазон исходных высот. Выберите ограничение значений или способ изменения maxZ.",
    "Apply vertical-scale migration": "Применить изменение вертикального масштаба",
    "3 - Byte-identical staged redirect": "3 — временная копия без изменения данных",
    "Applying the staged map generation in the game...": "Применение подготовленной карты в игре...",
    "No generated AI graph output buffer was provided.": "Не удалось получить данные созданного графа ИИ.",
    "Generated AI graph failed fresh validation: %1": "Созданный граф ИИ не прошёл проверку: %1",
    "Regenerated: %1/%2 authored collision parts, %3 bridge cells, %4 water samples":
        "Граф создан: частей столкновений — %1/%2; ячеек мостов — %3; образцов воды — %4",
    "Regenerating diagnostic graph...": "Построение временного графа...",
    "Generated minimap preview will be staged automatically for Test Current Map.":
        "Созданная миникарта будет автоматически использована при тесте карты.",
    "The run-local MOB copy could not be prepared for the generated AI graph:\n\n%1":
        "Не удалось подготовить временную копию MOB для созданного графа ИИ:\n\n%1",
    "Generated minimap locked; automatic regeneration is paused.":
        "Созданная миникарта заблокирована; автоматическое обновление приостановлено.",
    "Generated minimap unlocked; automatic regeneration is enabled.":
        "Созданная миникарта разблокирована; автоматическое обновление включено.",
    "Applied and locked generated texture": "Созданная текстура применена и заблокирована",
    "Generated minimap applied and locked.": "Созданная миникарта применена и заблокирована.",
    "The generated MPR was not committed.\n\n%1": "Созданный MPR не сохранён.\n\n%1",
    "The generated MOB failed its fresh validation and was not written.\n\n%1":
        "Созданный MOB не прошёл проверку и не был записан.\n\n%1",
    "The generated MOB was not committed.\n\n%1": "Созданный MOB не сохранён.\n\n%1",
})

CORRECTIONS.update({
    "%1%2%3\n\nCreate the separately named output now? Retained data will be staged, freshly reopened, backed up if the MPR destination exists, and replaced atomically.":
        "%1%2%3\n\nСоздать новый файл с отдельным именем? Данные будут подготовлены и повторно проверены; если целевой MPR уже существует, будет создана резервная копия, после чего файл безопасно заменится.",
    "Select candidate MOB": "Выберите сравниваемый MOB",
    "Remove objects whose origin lies outside the new canvas":
        "Удалить объекты, опорная точка которых находится за границами новой карты",
    "Structured metadata was copied from %1. The new Zone ID is intentionally blank. Unknown tags, comments, and raw source ownership were not copied; review every resource and entrance before registering.":
        "Структурированные данные скопированы из %1. ID новой зоны намеренно оставлен пустым. Неизвестные теги, комментарии и исходный текст не копировались; перед регистрацией проверьте все ресурсы и входы.",
    "maxZ maps the complete 0-65535 raw range into world height. Changing it either scales the landscape or requantizes every height. Use Vertical scale to choose the migration policy explicitly.":
        "maxZ переводит исходный диапазон 0–65535 в высоту мира. При его изменении можно либо масштабировать рельеф, либо пересчитать все исходные значения, сохранив высоту в мире. Выберите нужный способ в разделе «Вертикальный масштаб».",
    "Normalize / remap range": "Растянуть значения на новый диапазон",
    "Behavior setup %1: %2; default enabled; %3 patrol points. Logic 0 ignores its stored enabled setting.":
        "Настройка поведения %1: %2; всегда включена; патрульных точек — %3. Логика 0 не учитывает сохранённый флаг включения.",
    "The normal behavior and the reset target for alarm 0. Logic 0 is enabled by default and ignores the Logic enabled setting.":
        "Обычное поведение и исходное состояние для тревоги 0. Логика 0 всегда включена и не учитывает параметр «Логика включена».",
    "Shows unit patrol routes and other unit logic even when their owning unit is not selected.":
        "Показывает маршруты патрулирования и другую логику существ, даже если соответствующее существо не выбрано.",
    "Change maxZ from %1 to %2?\n\n%3\n\nThis is one undoable action and will be saved through staged MPR validation.":
        "Изменить maxZ с %1 на %2?\n\n%3\n\nЭто одно отменяемое действие. При сохранении MPR будет проверен.",
    "Generated": "Созданная",
    "Keep generated and embedded minimaps at the same zoom and position":
        "Сохранять одинаковый масштаб и положение созданной и встроенной миникарт",
    "The selected loaded AI graph is structurally incompatible with this map. Regenerate the preview or choose another graph: %1":
        "Загруженный граф ИИ не подходит к этой карте. Постройте новый граф или выберите другой: %1",
    "Locked: click to enable regeneration": "Заблокировано: нажмите, чтобы разрешить обновление",
    "Applied and locked; unlock before regenerating.": "Применено и заблокировано; перед обновлением разблокируйте.",
})

CORRECTIONS.update({
    "Edits are staged into map.txt only. Unknown tags and all other RES members are preserved. Save creates a verified backup before replacing an existing destination.":
        "Изменения готовятся только для map.txt. Неизвестные теги и все остальные ресурсы RES сохраняются. Перед заменой существующего файла создаётся проверенная резервная копия.",
    "Archive contains more than one case-insensitive map.txt member.":
        "В архиве найдено несколько ресурсов с именем map.txt, отличающихся только регистром.",
    "Zone '%1' was not found in the loaded map.txt.": "Зона «%1» не найдена в загруженном map.txt.",
    "Import map.txt": "Импортировать map.txt",
    "Profile source: %1\nFormat: %2\nDimensions: %3 × %4\nAuthored mip levels: %5\nSHA-256: %6":
        "Источник профиля: %1\nФормат: %2\nРазмер: %3 × %4\nУровней детализации: %5\nSHA-256: %6",
    "The MOB is structurally valid, but the current editor projection could not interpret it completely. No editable document was opened; details are in logs/workflow.log.":
        "Структура MOB допустима, но редактор не смог полностью представить все данные. Документ не открыт для редактирования; подробности — в logs/workflow.log.",
    "Convert object ID %1 (%2) using the proven Base/JMV lever layout. The original common and opaque object records remain byte-identical. The conversion is one undoable edit.":
        "Преобразовать объект ID %1 (%2) в рычаг по проверенной структуре базовой ПЗ/JMV. Общие и неизвестные записи объекта останутся без изменений. Преобразование можно отменить одним действием.",
    "Choose map.txt identity...": "Выбрать зону из map.txt...",
    "No valid #deploy entrances": "Нет подходящих входов #deploy",
    "Centers the editor camera on the selected map.txt deploy area.":
        "Перемещает камеру редактора к выбранной области входа из map.txt.",
    "This loose MPR/MOB pair has no map.txt identity. Place Play from Here, or explicitly borrow a registered map identity.":
        "Эта отдельная пара MPR/MOB не привязана к зоне из map.txt. Задайте точку «Играть отсюда» или явно выберите данные зарегистрированной зоны.",
    "The active profile has no valid gameplay map.txt entrance that can initialize a temporary player session.":
        "В активном профиле нет подходящего игрового входа из map.txt для временного игрока.",
    "Runs the exact-hash Base EI direct-launch feasibility probe (Ctrl+F5).":
        "Запускает проверку прямого старта базовой «Проклятой земли» для точно совпадающей версии игры (Ctrl+F5).",
    "This exact-hash Base EI feasibility probe uses an editor-owned 32-bit host. It creates Base game.exe suspended, loads the run-local bridge only into that owned process, calls its explicit initializer outside DllMain, and resumes only after fingerprint and byte guards pass.\n\nRun the probe levels in order. Source maps and the Base game installation are not modified.\n\nContinue to probe-level selection?":
        "Эта экспериментальная проверка прямого запуска работает только с точно распознанной версией базовой игры. Endurance запускает собственный 32-битный процесс game.exe в приостановленном состоянии, проверяет версию и ожидаемые данные, подключает временный модуль только к этому процессу и лишь затем продолжает запуск.\n\nПроходите уровни проверки по порядку. Исходные карты и установленная базовая игра не изменяются.\n\nПерейти к выбору уровня?",
    "LOOSE MAP WARNING: this MPR/MOB pair has no map.txt entry. Endurance will create a process-local synthetic zone with the loaded MPR dimensions and the Play from Here point as its only entrance. It has no inherited minimap, travel zones, or registered quest identity. The installed map table and source files are not changed.\n\n":
        "ПРЕДУПРЕЖДЕНИЕ: эта пара MPR/MOB не зарегистрирована в map.txt. Только для тестового процесса Endurance создаст временную зону размером с загруженный MPR и единственным входом в точке «Играть отсюда». У неё не будет миникарты, переходов и зарегистрированных квестовых данных. Установленный map.txt и исходные файлы не изменяются.\n\n",
    "%1 — %2; RGB %3/%4/%5, opacity %6%, light %7, wave %8, warp %9":
        "%1 — %2; RGB %3/%4/%5, непрозрачность %6%, свечение %7, волна %8, искажение %9",
    "2 - Pass-through hooks (start Base New Game manually)":
        "2 — проверить перехват без перенаправления (запустить «Новую игру» вручную)",
    "Baked the selected graph into %1. It now has runtime priority over any sibling .grp.":
        "Выбранный граф встроен в %1. Теперь игра использует его вместо внешнего .grp с именем MPR.",
    "Maximum combined size of persistent character plans, decoded atlases, composites, and animations beside the editor. Oldest entries are removed first. The default is 4096 MiB; enter 0 for no limit.":
        "Максимальный общий размер кэша моделей персонажей, распакованных атласов, готовых текстур и анимаций. Сначала удаляются самые старые записи. По умолчанию — 4096 МиБ; 0 снимает ограничение.",
    "Model: %1\nSize: %2 × %3\nVisible layers: %4 / %5\nUV projection: renderer-effective QImage upload, Repeat wrapping":
        "Модель: %1\nРазмер: %2 × %3\nВидимых слоёв: %4 / %5\nUV-развёртка: итоговая текстура с повторением по краям",
})

CORRECTIONS.update({
    "Validated MPR saved: %1": "Проверенный MPR сохранён: %1",
    "Validated changed MPR copy written: %1": "Проверенная копия изменённого MPR сохранена: %1",
    "Emitted RGB: %1, %2, %3. Stored warp speed: %4 (not used by the confirmed gameplay path; lateral warp uses 0.05). Wave multiplier also changes apparent wave-phase speed.":
        "Итоговый RGB: %1, %2, %3. Сохранённая скорость искажения: %4 (игра её не использует; боковая скорость равна 0,05). Множитель волны также меняет видимую скорость её фазы.",
    "Endurance found a verified game process left by an interrupted map-test session.\n\n%1\n\nClose it now? Any unsaved progress in that game process will be lost. If you leave it running, Endurance will monitor it and clean up automatically after it exits.":
        "Endurance обнаружил игровой процесс, оставшийся после прерванного теста карты.\n\n%1\n\nЗакрыть его сейчас? Несохранённый прогресс в игре будет потерян. Если оставить процесс запущенным, Endurance дождётся его завершения и автоматически удалит временные файлы.",
    "Stretch %1 x %2 samples across %3 x %4 terrain vertices?\nOrientation: %5\nRaw range: %6 to %7\n\nThis is undoable and can be committed through the validated MPR save path.":
        "Растянуть изображение %1 × %2 на %3 × %4 вершины рельефа?\nОриентация: %5\nИсходный диапазон: %6–%7\n\nИзменение можно отменить; при сохранении MPR будет проверен.",
    "No deploy rectangle was found in the selected zone's map-table entrances. Endurance will not invent a player entrance.\n\n%1":
        "У выбранной зоны в map.txt нет области входа #deploy. Endurance не будет создавать вход игрока автоматически.\n\n%1",
    "Embedded minimap selected; Test Current Map will use the active profile texture.":
        "Выбрана встроенная миникарта; при тесте карты будет использована текстура активного профиля.",
    "Play from Here, Godmode, and time override are unavailable for this loaded MOB set. Test Current Map and Hot Reload remain available.":
        "Для этого набора MOB недоступны «Играть отсюда», режим бессмертия и изменение времени. Тест карты и горячая перезагрузка остаются доступными.",
    "Every available ID in %1's effective range is also declared by another runtime-family MOB. Endurance selected unused NID %2 and will continue, but the family ranges should eventually be reviewed.":
        "Весь рабочий диапазон ID %1 также объявлен другим MOB игрового семейства. Endurance выбрал свободный NID %2 и продолжит работу, но диапазоны семейства следует проверить.",
    "Nothing is changed automatically. Select the newly added/edited owners to renumber. Endurance preserves the established collision owner by default and refuses automatic repair when a script or typed ParentID reference is ambiguous. Ordinary save remains available for legacy data, but Test Current Map is blocked while an authored NID 0 remains.":
        "Автоматически ничего не меняется. Выберите новые или изменённые объекты, которым нужно назначить другие ID. По умолчанию Endurance сохраняет ID за существующим объектом и не выполняет автоматическое исправление при неоднозначной ссылке в скрипте или ParentID. Старые данные можно сохранить как обычно, но тест карты заблокирован, пока в ней остаётся сохранённый NID 0.",
    "%1 is not reviewable: %2": "Не удалось открыть %1 для проверки: %2",
})

# Current-main delta: every entry below was reviewed against its concrete UI
# context after the 0.5 integration merge.  Keep these exact-source mappings
# separate from the older editorial pass so future lupdate deltas remain easy
# to audit.
CORRECTIONS.update({
    "Materials and assigned tiles": "Материалы и назначенные тайлы",
    "+ Add patrol": "+ Добавить патрульную точку",
    "Remove": "Удалить",
    "Open prototype in Database Editor":
        "Открыть прототип в редакторе базы данных",
    "Right-click to open this prototype in the Database Editor.":
        "Щёлкните правой кнопкой, чтобы открыть прототип в редакторе базы данных.",
    "The fresh Test Current Map process could not be started after stopping the previous process.\n\n%1":
        "После остановки предыдущего процесса не удалось запустить новый тест текущей карты.\n\n%1",
    "The 32-bit Evil Islands runtime paths do not match the selected Endurance game. Launching this mixed-root configuration can load incompatible resources and report missing prototypes.\n\nSelected game root:\n%1\n\n32-bit registry paths:\nWork path: %2\nResources path: %3\nCD-ROM Path: %4\n\nRestore the runtime paths for the selected installation, then retry Test Current Map.":
        "Пути 32-разрядной игры не соответствуют выбранной установке Endurance. При таком смешении папок могут загрузиться несовместимые ресурсы, а нужные прототипы окажутся недоступны.\n\nВыбранная папка игры:\n%1\n\nПути 32-разрядной игры в реестре:\nРабочая папка: %2\nПапка ресурсов: %3\nПуть к CD-ROM: %4\n\nВосстановите пути выбранной установки и повторите тест текущей карты.",
    "Pending %1 changes require a fresh Test Current Map process because database and JMV Lua state are initialized once at process startup.":
        "Изменения %1 требуют перезапуска теста текущей карты: база данных и Lua-среда JMV загружаются только при запуске процесса.",
    "A fresh Test Current Map process is already being prepared.":
        "Новый процесс теста текущей карты уже подготавливается.",
    "The active editor profile differs from the running test profile.":
        "Активный профиль редактора не совпадает с профилем запущенного теста.",
    "Controls both the campfire particle scale and its light radius (power x 10). Use a positive value; negative values can disable the effect and do not create darkness.":
        "Одновременно задаёт размер эффекта огня и радиус света (сила × 10). Используйте положительное значение: отрицательное может отключить эффект, но не создаёт затемнение.",
    "Distance affected by the light. Negative values do not darken the scene: the game uses the absolute value and marks the light unshadowed. In bright daylight, a weaker local light may have no visible effect.":
        "Радиус действия света. Отрицательное значение не затемняет сцену: игра использует его модуль и отключает тени от источника. При ярком дневном свете слабый местный свет может быть незаметен.",
    "JMV Lua [%1] — %2": "JMV Lua [%1] — %2",
    "JMV Lua [%1] — Unit stat: %2": "JMV Lua [%1] — параметр существа: %2",
    "%1.%2 contains invalid item or spell code '%3': %4":
        "%1.%2 содержит недопустимый код предмета или заклинания «%3»: %4",
    "%1.%2 contains '%3': item requires material type %4, but %5 is %6.":
        "%1.%2 содержит «%3»: предмет требует материал типа %4, но %5 имеет тип %6.",
    "%1.%2 contains '%3': spell prototype '%4' is unknown.":
        "%1.%2 содержит «%3»: прототип заклинания «%4» неизвестен.",
    "%1.%2 contains '%3': %4": "%1.%2 содержит «%3»: %4",
    "%1.%2 contains '%3': spell energy %4 exceeds item energy %5.":
        "%1.%2 содержит «%3»: энергия заклинания %4 превышает энергию предмета %5.",
    "%1.%2 contains '%3': spell difficulty %4 exceeds item maximum difficulty %5.":
        "%1.%2 содержит «%3»: сложность заклинания %4 превышает допустимую для предмета сложность %5.",
    "Fire": "Огонь",
    "Lightning": "Молния",
    "Acid": "Кислота",
    "Domination": "Подчинение",
    "Unclassified": "Без категории",
    "Spell energy %1 exceeds the constructed item's energy %2.":
        "Энергия заклинания %1 превышает энергию создаваемого предмета %2.",
    "Spell difficulty %1 exceeds the constructed item's maximum difficulty %2.":
        "Сложность заклинания %1 превышает допустимую для создаваемого предмета сложность %2.",
    "Unknown rune code '%1'.": "Неизвестный код руны «%1».",
    "shown": "показано",
    "hidden": "скрыто",
    "WRONG MATERIAL / FAMILY": "НЕВЕРНЫЙ МАТЕРИАЛ ИЛИ СЕМЕЙСТВО",
    "WRONG ROTATION": "НЕВЕРНЫЙ ПОВОРОТ",
    "NO / WRONG BLEND": "НЕТ ПЕРЕХОДА ИЛИ ОН НЕВЕРЕН",
    "Add overlay surface and resolve terrain tiles":
        "Добавить покрытие и подобрать тайлы рельефа",
    "Next Save will create a new content-hash identity.":
        "При следующем сохранении будет создан новый идентификатор по хешу содержимого.",
    "Valid identity. Existing consumers may also resolve these pages.":
        "Идентификатор действителен. Эти страницы могут использовать и другие карты.",
    "Create a visible backup before replacement (%1 additional space)\n%2":
        "Создать резервную копию перед заменой (дополнительно %1)\n%2",
    "target does not exist": "целевой файл не существует",
    "Stable physical row number. Search for #%1 to filter this row.":
        "Постоянный физический номер строки. Чтобы оставить в списке эту строку, введите #%1.",
    "Read-only raw payload. It remains preserved byte-for-byte; editing is disabled until this field's type and serializer are verified.":
        "Исходные данные только для чтения. Они сохраняются побайтово; редактирование недоступно, пока не проверены тип поля и способ его записи.",
    "Choose a referenced row from the list. Right-click and select Open Referenced Row to follow the link.":
        "Выберите связанную строку из списка. Для перехода щёлкните правой кнопкой и выберите «Открыть связанную строку».",
    "This value names a game resource. Right-click and select Open in Resource Browser to locate its active provider.":
        "Здесь указано имя игрового ресурса. Чтобы найти используемый файл, щёлкните правой кнопкой и выберите «Открыть в просмотре ресурсов».",
    "Preview limited to %1 of %2 characters. Double-click or press Enter/F2 to edit the complete value.":
        "Показаны первые %1 из %2 символов. Для редактирования полного значения дважды щёлкните ячейку или нажмите Enter/F2.",
    "Field is unsupported or read-only.": "Поле не поддерживается или доступно только для чтения.",
    "This optional field is absent from the source row; safe field insertion is not yet proven.":
        "В исходной строке нет этого необязательного поля; безопасное добавление пока не подтверждено.",
    "Item / Spell Constructor": "Конструктор предметов и заклинаний",
    "The active profile database cannot be opened.\n\n%1":
        "Не удалось открыть базу данных активного профиля.\n\n%1",
    "Database Editor": "Редактор базы данных",
    "The active profile database cannot be opened for authoring.\n\n%1":
        "Не удалось открыть базу данных активного профиля для редактирования.\n\n%1",
    "Resource not found": "Ресурс не найден",
    "No active-profile resource matches '%1'.":
        "В активном профиле нет ресурса «%1».",
    "Prototype not found": "Прототип не найден",
    "The active profile database does not contain monster prototype '%1'.":
        "В базе данных активного профиля нет прототипа существа «%1».",
    "No active profile database is available. Configure the game/mod profile first.":
        "База данных активного профиля недоступна. Сначала настройте профиль игры или мода.",
    "External database archive": "Сторонний архив базы данных",
    "The integrated Database Editor authors the active profile's database.res bundle. This archive is not that active database.\n\nSwitch the active game/mod profile to this database, or open Database Editor and use Import > Import database.res to bring it into the active document.":
        "Встроенный редактор изменяет database.res активного профиля. Этот архив не является активной базой данных.\n\nПереключите профиль игры или мода на эту базу либо откройте редактор базы данных и выберите «Импорт → Импорт database.res», чтобы загрузить её в текущий документ.",
    "Resource Browser": "Просмотр ресурсов",
    "Reloaded resources after publishing %1":
        "Ресурсы обновлены после публикации %1",
    "Database Editor...": "Редактор базы данных...",
    "Item / Spell Constructor...": "Конструктор предметов и заклинаний...",
    "Test Current is using game resources":
        "Тест текущей карты использует ресурсы игры",
    "The game process launched by Endurance is still running and may hold the selected resource archive open. Windows cannot atomically replace an archive while the game is using it.\n\n%1\n\nStop Test Current before publishing?":
        "Запущенная из Endurance игра всё ещё работает и может удерживать выбранный архив ресурсов открытым. Windows не может безопасно заменить архив, пока он используется игрой.\n\n%1\n\nОстановить тест текущей карты перед публикацией?",
    "Stop Test Current and continue": "Остановить тест и продолжить",
    "Stopping Test Current and releasing game resources...":
        "Остановка теста и освобождение ресурсов игры...",
    "Preparing terrain publication": "Подготовка рельефа к публикации",
    "Terrain publication paused": "Публикация рельефа приостановлена",
    "The Endurance-owned game process did not release its resources in time. Publication was not started. Stop the test process and retry.":
        "Запущенная из Endurance игра не освободила ресурсы вовремя. Публикация не началась. Остановите тест и повторите попытку.",
    "The owned JMV test process restarted with the changed terrain atlas and is loading the staged map.":
        "Тест JMV перезапущен с изменённым атласом рельефа и загружает подготовленную карту.",
    "Terrain atlas changed; restarting the owned JMV test process with the new atlas...":
        "Атлас рельефа изменён; тест JMV перезапускается с новым атласом...",
    "Restarting Test...": "Перезапуск теста...",
    "Stopping the previous Endurance-owned game process before launching the current database, JMV Lua, and map state.":
        "Предыдущий процесс игры останавливается перед запуском текущей базы данных, Lua JMV и состояния карты.",
    "The map-specific terrain atlas package could not be read or matched to the staged MPR. No game process was started.":
        "Не удалось прочитать пакет атласа рельефа карты или сопоставить его с подготовленным MPR. Игра не запущена.",
    "The active profile database cannot be resolved for runtime staging.":
        "Не удалось определить базу данных активного профиля для подготовки теста.",
    "The active database could not be staged safely.\n\n%1":
        "Не удалось безопасно подготовить активную базу данных.\n\n%1",
    "The active JMV tables.lua could not be staged safely.\n\n%1":
        "Не удалось безопасно подготовить активный tables.lua JMV.\n\n%1",
    "Restart Test Current Map": "Перезапустить тест текущей карты",
    "The running game cannot safely apply pending %1 changes during a map hot reload. The database and JMV Lua state are initialized once when the game process starts.\n\nRestart the Endurance-owned test process and stage the current map, database, and Lua state together?":
        "Запущенная игра не может безопасно применить изменения %1 при обновлении карты. База данных и Lua-среда JMV загружаются только при запуске процесса.\n\nПерезапустить тест и одновременно подготовить текущую карту, базу данных и Lua?",
    " and ": " и ",
    "Restart Test": "Перезапустить тест",
    "Stopping the current test before a fresh launch...":
        "Остановка текущего теста перед новым запуском...",
    "Stopping the current test; a fresh process will launch automatically with the current database and JMV Lua state.":
        "Текущий тест останавливается; новый процесс автоматически запустится с текущей базой данных и Lua-средой JMV.",
    "Restarting the owned game with the changed terrain atlas...":
        "Перезапуск игры с изменённым атласом рельефа...",
    "Terrain atlas changed; the owned JMV test process is restarting safely.":
        "Атлас рельефа изменён; тест JMV безопасно перезапускается.",
    "%1Database: %2 finding(s).": "%1База данных: замечаний — %2.",
    "Database": "База данных",
    "%1 finding(s)": "Замечаний: %1",
    "Double-click to open this row in the Database Editor.":
        "Дважды щёлкните, чтобы открыть строку в редакторе базы данных.",
    "The database finding's row is no longer available.":
        "Строка, к которой относится замечание, больше недоступна.",
    "Save MPR Draft": "Сохранить черновик MPR",
    "Finalize Draft MPR As...": "Завершить черновик MPR как...",
    "Finalize and Save MPR": "Завершить и сохранить MPR",
    "Finalize MPR As...": "Завершить MPR как...",
    "Unapplied database changes": "Неприменённые изменения базы данных",
    "The active database has unapplied changes. Apply them before deciding whether to save maps, so saved maps cannot reference database rows that were never published.":
        "В активной базе данных есть неприменённые изменения. Примените их до сохранения карт, чтобы карты не ссылались на строки, которые не были опубликованы.",
    "Apply Database": "Применить базу данных",
    "Exit Without Applying": "Выйти без применения",
    "Database apply failed": "Не удалось применить базу данных",
    "Database Editor — %1": "Редактор базы данных — %1",
    "Profile: %1  •  Provider: %2\nSource: %3":
        "Профиль: %1  •  Источник данных: %2\nФайл: %3",
    "Filter rows across all fields or enter #row…":
        "Фильтр по всем полям или #номер строки…",
    "Item / Spell Constructor…": "Конструктор предметов и заклинаний…",
    "Clone Row…": "Клонировать строку…",
    "Delete Row": "Удалить строку",
    "Review Changes…": "Просмотреть изменения…",
    "Export XLSX…": "Экспортировать XLSX…",
    "Import XLSX…": "Импортировать XLSX…",
    "Save Database As…": "Сохранить базу данных как…",
    "Import…": "Импортировать…",
    "Import database.res…": "Импортировать database.res…",
    "Import tables.lua…": "Импортировать tables.lua…",
    "Import database.res + tables.lua…":
        "Импортировать database.res и tables.lua…",
    "Edit Full Value…": "Изменить полное значение…",
    "Open Referenced Row": "Открыть связанную строку",
    "Lock columns through “%1”": "Закрепить столбцы до «%1»",
    "Unlock columns": "Открепить столбцы",
    "Show all": "Показать всё",
    "Hide all": "Скрыть всё",
    "Show unknown/raw columns": "Показать неизвестные и исходные столбцы",
    "Hide unknown/raw columns": "Скрыть неизвестные и исходные столбцы",
    "database.res": "database.res",
    "tables.lua (JMV)": "tables.lua (JMV)",
    "Editor columns": "Столбцы редактора",
    "This complete/raw table is visible for preservation review. Structural editing is disabled until its typed serializer is verified.":
        "Полная исходная таблица показана для проверки сохранности данных. Изменение структуры недоступно, пока не проверен способ записи её типов.",
    "Edit rejected": "Изменение отклонено",
    "Edit %1": "Изменить %1",
    "%1 — %2": "%1 — %2",
    "One list entry per line. The brackets and quotation marks used internally are intentionally hidden.":
        "По одной записи на строку. Служебные скобки и кавычки намеренно скрыты.",
    "This is the complete stored value. Long lines wrap visually.":
        "Это полное сохранённое значение. Длинные строки переносятся только на экране.",
    "%1 item(s) — one per line": "Записей: %1 — по одной на строку",
    "%1 character(s) — complete value": "Символов: %1 — полное значение",
    "Copy Row ID": "Копировать ID строки",
    "Reference not found": "Связанная строка не найдена",
    "The referenced row '%1' is not present in %2.":
        "Связанная строка «%1» отсутствует в %2.",
    "Open in Resource Browser": "Открыть в просмотре ресурсов",
    "Duplicate / Clone Row…": "Создать копию строки…",
    "Clone row": "Клонирование строки",
    "New stable name / ID:": "Новое постоянное имя или ID:",
    "The ID cannot be empty.": "ID не может быть пустым.",
    "Enter a new ID; the original ID is already in use.":
        "Введите новый ID: исходный уже используется.",
    "That ID is already used by another row.":
        "Этот ID уже используется другой строкой.",
    "The new ID is available.": "Новый ID свободен.",
    "Clone rejected": "Клонирование отклонено",
    "Delete row": "Удаление строки",
    "Mark '%1' for deletion? References will be validated before publication.":
        "Пометить «%1» для удаления? Перед публикацией редактор проверит ссылки.",
    "Delete rejected": "Удаление отклонено",
    "Database refresh failed": "Не удалось обновить базу данных",
    "Verified:": "Проверено:",
    "Opaque:": "Нераспознано:",
    "No pending changes.": "Нет неприменённых изменений.",
    "Pending Database Changes": "Неприменённые изменения базы данных",
    "Database applied": "База данных применена",
    "The active database and linked JMV Lua data were updated as one recoverable transaction.\n\nBackups:\n%1":
        "Активная база данных и связанные данные Lua JMV обновлены одной операцией с возможностью восстановления.\n\nРезервные копии:\n%1",
    "Save Database As": "Сохранить базу данных как",
    "Evil Islands database (*.res)": "База данных «Проклятых земель» (*.res)",
    "Choose a staging path": "Выберите отдельный путь",
    "Saving over the opened database is blocked. Choose a separate staging path so the source remains recoverable.":
        "Нельзя сохранять поверх открытой базы данных. Выберите отдельный путь, чтобы исходный файл можно было восстановить.",
    "Save linked JMV tables.lua As": "Сохранить связанный tables.lua JMV как",
    "Lua table (*.lua)": "Таблица Lua (*.lua)",
    "Saving over the opened tables.lua is blocked. Choose a separate staging path so the source remains recoverable.":
        "Нельзя сохранять поверх открытого tables.lua. Выберите отдельный путь, чтобы исходный файл можно было восстановить.",
    "Database save failed": "Не удалось сохранить базу данных",
    "Database saved": "База данных сохранена",
    "The database bundle was staged successfully at:\n%1\n\nThe source files were not modified.":
        "Копия базы данных успешно подготовлена по адресу:\n%1\n\nИсходные файлы не изменены.",
    "Export Database XLSX": "Экспорт базы данных в XLSX",
    "Excel workbook (*.xlsx)": "Книга Excel (*.xlsx)",
    "XLSX export failed": "Не удалось экспортировать XLSX",
    "JMV Lua companion export failed": "Не удалось экспортировать связанные данные Lua JMV",
    "The XLSX was exported, but its editable Lua companion could not be written.\n\n%1":
        "XLSX экспортирован, но записать редактируемый связанный файл Lua не удалось.\n\n%1",
    "The import was applied as one undoable change. Existing source-data findings are retained for review; only newly introduced critical issues cancel import.":
        "Импорт применён как одно отменяемое изменение. Замечания к исходным данным сохранены для проверки; импорт отменяется только при новых критических ошибках.",
    "Import Database XLSX": "Импорт базы данных из XLSX",
    "XLSX import cancelled": "Импорт XLSX отменён",
    "No database changes were applied.\n\n%1":
        "Изменения базы данных не применены.\n\n%1",
    "Imported %1": "Импортирован %1",
    "XLSX import completed with warnings": "Импорт XLSX завершён с предупреждениями",
    "Native database import cancelled": "Импорт базы данных отменён",
    "No database or Lua changes were applied.\n\n%1":
        "Изменения базы данных и Lua не применены.\n\n%1",
    "Native import completed with warnings": "Импорт завершён с предупреждениями",
    "Import database.res": "Импортировать database.res",
    "Import tables.lua": "Импортировать tables.lua",
    "Import linked tables.lua": "Импортировать связанный tables.lua",
    "The selected database/Lua pair could not be read.":
        "Не удалось прочитать выбранную пару базы данных и Lua.",
    "Imported %1 + %2": "Импортированы %1 и %2",
    "%1 pending change(s) • Use Review or the Validation panel for current findings • Full schema authoring: 26 database tables. Descriptor-unknown fields remain visible as hexadecimal and serialize without touching unrelated data.":
        "Неприменённых изменений: %1 • Текущие замечания доступны в обзоре изменений и на панели проверки • Полное редактирование схемы: 26 таблиц базы данных. Поля неизвестного типа показаны в шестнадцатеричном виде и сохраняются без изменения остальных данных.",
    "Active Profile": "Активный профиль",
    "Open Archive": "Открыть архив",
    "Open .res...": "Открыть .res...",
    "Filter by member or archive...": "Фильтр по имени файла или архива...",
    "All Types": "Все типы",
    "Image": "Изображение",
    "Text": "Текст",
    "3D / animation": "3D и анимация",
    "Audio": "Звук",
    "Archive": "Архив",
    "Binary": "Двоичные данные",
    "All Statuses": "Все состояния",
    "Winning": "Используется",
    "Overridden": "Перекрыт",
    "Members": "Файлы",
    "Size": "Размер",
    "Name": "Имя",
    "Type": "Тип",
    "Status": "Состояние",
    "Provider": "Источник",
    "100%": "100%",
    "Left drag pans; middle drag rotates; right drag orbits; wheel zooms":
        "Левая кнопка перемещает вид; средняя вращает модель; правая вращает камеру; колёсико меняет масштаб",
    "Pause": "Пауза",
    "Animation": "Анимация",
    "Filter model parts...": "Фильтр частей модели...",
    "Normal": "Обычный",
    "All": "Все",
    "None": "Нет",
    "Reset view": "Сбросить вид",
    "Play": "Воспроизвести",
    "Volume": "Громкость",
    "Select a resource to preview it.": "Выберите ресурс для предпросмотра.",
    "Copy Name": "Копировать имя",
    "Open Location": "Открыть расположение",
    "Open in Database Editor": "Открыть в редакторе базы данных",
    "Replace...": "Заменить...",
    "Add...": "Добавить...",
    "Rename...": "Переименовать...",
    "Delete...": "Удалить...",
    "No resource catalog loaded.": "Каталог ресурсов не загружен.",
    "The deployed multimedia backend cannot decode this audio stream. Export remains available.\n\n%1":
        "Установленный модуль мультимедиа не может декодировать этот звуковой поток. Файл по-прежнему можно экспортировать.\n\n%1",
    "Indexing %1 archive(s) for %2...": "Индексируются архивы (%1) для %2...",
    "RES archive changed": "Архив RES изменён",
    "A loaded archive changed on disk:\n%1\n\nRefresh the Resource Browser and live resource lists now?":
        "Загруженный архив изменился на диске:\n%1\n\nОбновить просмотр ресурсов и открытые списки ресурсов?",
    "All archives": "Все архивы",
    "%1 members from %2 archive(s)%3": "Файлов: %1; архивов: %2%3",
    "; %1 archive(s) rejected": "; отклонено архивов: %1",
    "Loading preview...": "Загрузка предпросмотра...",
    "Active:": "Используется:",
    "Overridden:": "Перекрыт:",
    "Neutral assembly pose": "Нейтральная поза сборки",
    "Could not stage this audio preview as a local file. Export remains available.":
        "Не удалось подготовить локальный файл для прослушивания. Файл по-прежнему можно экспортировать.",
    "Audio preview ready (%1). WAV uses the deployed Windows audio backend; MP3 also requires an installed system decoder.":
        "Звуковой предпросмотр готов (%1). WAV воспроизводится средствами Windows; для MP3 также нужен установленный системный декодер.",
    "Open RES archive": "Открыть архив RES",
    "Evil Islands archives (*.res)": "Архивы «Проклятых земель» (*.res)",
    "Export resource": "Экспортировать ресурс",
    "Protected database archive": "Защищённый архив базы данных",
    "database.res cannot be changed by the generic resource transaction. Use the dedicated Database Editor integration.":
        "database.res нельзя изменить через общую операцию с ресурсами. Используйте редактор базы данных.",
    "Apply RES changes": "Применить изменения RES",
    "The target is staged, semantically reopened, race-checked, and atomically replaced. The live resource index refreshes only after validation succeeds.":
        "Новый архив подготавливается отдельно, повторно открывается и проверяется, а затем заменяет старый одной безопасной операцией. Индекс ресурсов обновляется только после успешной проверки.",
    "This archive belongs to the active base installation and is read-only here. Choose a mod-owned or copied archive as the target.":
        "Этот архив принадлежит активной базовой установке и доступен здесь только для чтения. Выберите архив мода или его отдельную копию.",
    "Target archive:": "Целевой архив:",
    "Apply changes": "Применить изменения",
    "Choose RES transaction target": "Выберите целевой архив RES",
    "Protected active archive": "Защищённый активный архив",
    "Base-installation archives are read-only. Choose a mod-owned, copied, or new override archive.":
        "Архивы базовой установки доступны только для чтения. Выберите архив мода, отдельную копию или новый архив переопределений.",
    "Protected database destination": "Защищённая база данных",
    "Generic RES transactions cannot publish to database.res or databaseJmv.res.":
        "Общие операции с RES не могут записывать в database.res или databaseJmv.res.",
    "RES transaction backup": "Резервная копия RES",
    "RES transaction failed": "Не удалось изменить RES",
    "RES transaction complete": "Изменение RES завершено",
    "The validated archive was written to:\n%1\n\nNo backup was created.":
        "Проверенный архив записан по адресу:\n%1\n\nРезервная копия не создавалась.",
    "The validated archive was written to:\n%1\n\nBackup:\n%2":
        "Проверенный архив записан по адресу:\n%1\n\nРезервная копия:\n%2",
    "Select replacement payload": "Выберите новый файл ресурса",
    "All files (*)": "Все файлы (*)",
    "Replace resource": "Замена ресурса",
    "Replace %1 with %2 (%3 -> %4).": "Заменить %1 файлом %2 (%3 → %4).",
    "Select member payload": "Выберите добавляемый файл",
    "Add RES member": "Добавить файл в RES",
    "Archive member name:": "Имя файла в архиве:",
    "Add resource": "Добавление ресурса",
    "Add %1 from %2. CP1251 and case-insensitive collision checks passed.":
        "Добавить %1 из %2. Проверки CP1251 и совпадений имён без учёта регистра пройдены.",
    "Rename RES member": "Переименовать файл в RES",
    "New member name:": "Новое имя файла:",
    "Rename resource": "Переименование ресурса",
    "Rename %1 to %2.\n\nReference coverage is incomplete: structured MOB/script and opaque binary references may still use the old name and must be reviewed manually.":
        "Переименовать %1 в %2.\n\nПроверка ссылок неполна: старое имя может остаться в структурах MOB, скриптах и нераспознанных двоичных данных. Их нужно проверить вручную.",
    "Delete resource": "Удаление ресурса",
    "No lower-priority provider is currently indexed.":
        "Другой источник этого ресурса с меньшим приоритетом не найден.",
    "Deleting the winning member exposes: %1":
        "После удаления будет использоваться: %1",
    "Delete %1.\n\n%2\n\nReference coverage is incomplete: structured and opaque references must be reviewed manually.":
        "Удалить %1.\n\n%2\n\nПроверка ссылок неполна: структурированные и нераспознанные ссылки нужно проверить вручную.",
    "Item or spell code:": "Код предмета или заклинания:",
    "Paste an item code such as armor.material[spell{rune;rune}]":
        "Вставьте код предмета, например armor.material[spell{rune;rune}]",
    "Import": "Импортировать",
    "Copy": "Копировать",
    "Filter name or code…": "Фильтр по имени или коду…",
    "All categories": "Все категории",
    "Weapons": "Оружие",
    "Armor": "Доспехи",
    "Quick items": "Быстрые предметы",
    "All item types": "Все типы предметов",
    "All armor layers": "Все слои доспехов",
    "Upper / heavy armor": "Верхний слой / тяжёлые доспехи",
    "Lower / light armor": "Нижний слой / лёгкие доспехи",
    "Optional spell code": "Необязательный код заклинания",
    "Use spell from Spell tab": "Взять заклинание со вкладки «Заклинания»",
    "Open in Spell tab": "Открыть на вкладке «Заклинания»",
    "Compatible material:": "Совместимый материал:",
    "Inserted spell:": "Добавленное заклинание:",
    "Reset camera": "Сбросить камеру",
    "Items & Quick Items": "Предметы и быстрые предметы",
    "Filter spell name or code…": "Фильтр по имени или коду заклинания…",
    "All magic schools": "Все школы магии",
    "Available runes": "Доступные руны",
    "Filter rune name or code…": "Фильтр по имени или коду руны…",
    "All rune types": "Все типы рун",
    "Add →": "Добавить →",
    "↑": "↑",
    "↓": "↓",
    "Runes in spell (maximum 8)": "Руны заклинания (не более 8)",
    "Spells": "Заклинания",
    "Armor layers do not apply to this item category.":
        "Для этой категории предметов слои доспехов не применяются.",
    "%1 • code: %2 • type: %3": "%1 • код: %2 • тип: %3",
    "code: %1 • school: %2 • subtype: %3":
        "код: %1 • школа: %2 • подтип: %3",
    "%1 • %2": "%1 • %2",
    "No material": "Без материала",
    "%1 (%2, authored compatibility)": "%1 (%2, заданная совместимость)",
    "%1 (%2, code %3)": "%1 (%2, код %3)",
    "No supported %1 material exists": "Нет поддерживаемого материала %1",
    "Unknown spell prototype '%1'.": "Неизвестный прототип заклинания «%1».",
    "Spell icon %1 was not found in the active profile.":
        "Значок заклинания %1 не найден в активном профиле.",
    "%1 - %2 (%3)": "%1 — %2 (%3)",
    "%1 + %2 = %3": "%1 + %2 = %3",
    "%1\nCode: %2\nSchool: %3 | %4\nEnergy: %5 (%6 per second) | difficulty: %7\nEffect: %8 | duration: %9 s (%10 segments)\nRange: %11 m | area: %12 m² (radius %13 m) | targets: %14\n\nRunes:\n%15":
        "%1\nКод: %2\nШкола: %3 | %4\nЭнергия: %5 (%6 в секунду) | сложность: %7\nЭффект: %8 | длительность: %9 с (%10 отрезков)\nДальность: %11 м | область: %12 м² (радиус %13 м) | цели: %14\n\nРуны:\n%15",
    "%1 | %2 | database code: %3": "%1 | %2 | код в базе данных: %3",
    "Material family: %1 | base energy: %2 | maximum spell difficulty: %3 | crafting material required: %4 components":
        "Семейство материала: %1 | базовая энергия: %2 | предельная сложность заклинания: %3 | для изготовления требуется материала: %4 ед.",
    "Preview: standalone inventory figure with its material texture, matching the game's item preview path.":
        "Предпросмотр: отдельная модель предмета с текстурой материала, как при просмотре предмета в игре.",
    "This item requires a supported %1 material.":
        "Для этого предмета требуется поддерживаемый материал %1.",
    "Constructed item: energy %1 | maximum spell difficulty %2 | energy regeneration %3 per second":
        "Созданный предмет: энергия %1 | предельная сложность заклинания %2 | восстановление энергии %3 в секунду",
    "Unknown spell prototype code '%1'.":
        "Неизвестный код прототипа заклинания «%1».",
    "Inserted spell: energy %1 (%2 per second) | difficulty %3":
        "Добавленное заклинание: энергия %1 (%2 в секунду) | сложность %3",
    "Code is structurally valid. Compatibility findings are advisory.":
        "Структура кода верна. Замечания о совместимости носят рекомендательный характер.",
    "Advisory validation:\n• %1": "Рекомендации по совместимости:\n• %1",
    "Unknown rune": "Неизвестная руна",
    "Unknown item prototype '%1'.": "Неизвестный прототип предмета «%1».",
    "Material '%1' is not compatible with %2 (%3 required).":
        "Материал «%1» несовместим с %2 (требуется %3).",
    "Code could not be imported": "Не удалось импортировать код",
    "Confirm material": "Подтвердить материал",
    "Use selected tile as thumbnail": "Использовать выбранный тайл как миниатюру",
    "Apply + Close": "Применить и закрыть",
    "Material atlas": "Атлас материалов",
    "The editor has already made every safe assignment it could. Painting can use those materials now. Unusual tiles stay in the unassigned pool and never disable the valid palette. Drag a tile onto a material to assign it; for transition tiles, select the tile and drag material rows onto its four corners. Missing transitions only produce hard paint edges.":
        "Редактор уже выполнил все безопасные назначения. Эти материалы готовы к рисованию. Необычные тайлы остаются без назначения и не отключают рабочую палитру. Чтобы назначить материал, перетащите на него тайл. Для переходного тайла выберите его и перетащите материалы на четыре угла. Если нужного перехода нет, край останется резким.",
    "Materials — drag a material row onto a tile corner":
        "Материалы — перетащите строку материала на угол тайла",
    "New": "Создать",
    "Rename": "Переименовать",
    "Delete empty": "Удалить пустые",
    "Ground": "Земля",
    "Underwater only": "Только под водой",
    "Ground + Overlay": "Земля и покрытия",
    "Ground (surface + underwater)": "Земля (на поверхности и под водой)",
    "NW": "СЗ",
    "NE": "СВ",
    "SE": "ЮВ",
    "SW": "ЮЗ",
    "All physical atlas slots": "Все физические слоты атласа",
    "Choose an atlas page to inspect every physical slot, including repeated slots in expanded atlases. Select a slot for its full preview and exact corner identities. Drag an unassigned slot to a material to classify it.":
        "Выберите страницу атласа, чтобы просмотреть каждый физический слот, включая повторы в расширенных атласах. Выберите слот для полного предпросмотра и точных материалов его углов. Чтобы назначить материал, перетащите на него свободный слот.",
    "Page %1 — slots %2–%3": "Страница %1 — слоты %2–%3",
    "Reset to default": "Восстановить исходные назначения",
    "Load profile...": "Загрузить профиль...",
    "Save copy...": "Сохранить копию...",
    "This custom atlas has no exact built-in default.":
        "Для этого нестандартного атласа нет точного встроенного профиля.",
    "Remove the managed local override and restore the shipped atlas assignments.":
        "Удалить локальные изменения и восстановить поставляемые с редактором назначения атласа.",
    "Unknown material %1": "Неизвестный материал %1",
    "  (transition support)": "  (для переходов)",
    "  • confirm": "  • подтвердить",
    "%1": "%1",
    "%1 atlas slots assigned. Allowed section: %2. Drag this row onto a selected tile corner.":
        "Назначено слотов атласа: %1. Допустимый раздел: %2. Перетащите эту строку на угол выбранного тайла.",
    "Transition support: this component is required by exact tile corners, but the atlas has no uniform or one-on-three tile that can form a coherent brush. It remains available for corner editing.":
        "Материал нужен для точных углов переходных тайлов, но в атласе нет равномерного тайла или перехода 1 к 3, из которых можно составить полноценную кисть. Материал по-прежнему доступен для ручного назначения углов.",
    "Ⓣ  Tile %1": "Ⓣ  Тайл %1",
    "Tile %1": "Тайл %1",
    "NW %1 / NE %2 / SE %3 / SW %4": "СЗ %1 / СВ %2 / ЮВ %3 / ЮЗ %4",
    "Thumbnail tile for %1. %2": "Тайл-миниатюра материала %1. %2",
    "unassigned": "не назначен",
    "assigned": "назначен",
    "Slot %1  (local %2) — %3": "Слот %1  (на странице %2) — %3",
    "Page %1, local slot %2. NW %3 / NE %4 / SE %5 / SW %6":
        "Страница %1, слот %2. СЗ %3 / СВ %4 / ЮВ %5 / ЮЗ %6",
    "Page %1, local slot %2. No semantic definition is available.":
        "Страница %1, слот %2. Описание материалов отсутствует.",
    "Confirmation needed: the parser could not prove this material's identity or classification. Check its name, section, thumbnail, and assigned tiles, then confirm once.":
        "Требуется подтверждение: редактор не смог надёжно определить материал или его раздел. Проверьте имя, раздел, миниатюру и назначенные тайлы, затем подтвердите материал.",
    "This material is resolved. Moving or editing one of its tiles updates all dependent membership automatically.":
        "Материал подтверждён. При перемещении или изменении его тайла все связанные назначения обновляются автоматически.",
    "Select a material to rename, classify, confirm, or choose its thumbnail.":
        "Выберите материал, чтобы переименовать или подтвердить его, изменить раздел либо выбрать миниатюру.",
    "%1 — palette thumbnail": "%1 — миниатюра палитры",
    "Select a tile": "Выберите тайл",
    "Tile preview": "Предпросмотр тайла",
    "A transition tile may reference two, three, or four materials. It appears under every material it references, but counts once in atlas coverage.":
        "Переходный тайл может содержать два, три или четыре материала. Он показан у каждого из них, но в заполнении атласа учитывается один раз.",
    "—": "—",
    "Tile %1%2": "Тайл %1%2",
    " — unassigned": " — не назначен",
    "Preview unavailable": "Предпросмотр недоступен",
    "Drag a material row onto a corner. Ground/underwater and overlay materials can never be mixed in one tile.":
        "Перетащите строку материала на угол. В одном тайле нельзя смешивать материалы земли или дна с материалами покрытия.",
    "This proposal is not used by painting. First drag the tile onto a material to choose its layer and base assignment; then refine individual corners.":
        "Предложенное распознавание не используется при рисовании. Сначала перетащите тайл на материал, чтобы выбрать слой и основное назначение, затем уточните отдельные углы.",
    "Tile was not moved": "Тайл не перемещён",
    "Tile was not unassigned": "Тайл уже назначен",
    "Corner assignment is not valid": "Материал нельзя назначить этому углу",
    "Assign tile corner": "Назначить материал углу тайла",
    "Material": "Материал",
    "Rename material": "Переименовать материал",
    "Material name (also shown in the paint palette)":
        "Имя материала (также показано в палитре рисования)",
    "New material": "Новый материал",
    "Material name": "Имя материала",
    "Material is still referenced": "Материал всё ещё используется",
    "Move or resolve every tile that references this material before deleting it.":
        "Перед удалением переместите или исправьте все тайлы, использующие этот материал.",
    "Material section was not changed": "Раздел материала не изменён",
    "Thumbnail was not changed": "Миниатюра не изменена",
    "%1 / %2 atlas slots assigned (%3 unassigned). %4 active material confirmation(s) remain. Unassigned tiles are a debug warning, not a painting blocker; they will use hard-edge fallback when no valid transition exists.":
        "Назначено слотов атласа: %1 из %2; без назначения: %3. Осталось подтверждений материалов: %4. Свободные тайлы дают предупреждение, но не мешают рисованию: если подходящего перехода нет, используется резкий край.",
    "Save material atlas profile copy": "Сохранить копию профиля атласа материалов",
    "Material atlas profiles (*.semantic-atlas.json);;JSON files (*.json)":
        "Профили атласа материалов (*.semantic-atlas.json);;Файлы JSON (*.json)",
    "Could not save material atlas profile": "Не удалось сохранить профиль атласа материалов",
    "Profile copy saved": "Копия профиля сохранена",
    "Saved to:\n%1\n\nUse Load profile in this window to reopen it.":
        "Сохранено по адресу:\n%1\n\nЧтобы снова открыть копию, нажмите «Загрузить профиль» в этом окне.",
    "Load material atlas profile": "Загрузить профиль атласа материалов",
    "Material atlas profiles (*.semantic-atlas.json *.json)":
        "Профили атласа материалов (*.semantic-atlas.json *.json)",
    "Profile does not match this atlas": "Профиль не соответствует этому атласу",
    "No built-in default is available": "Встроенный профиль отсутствует",
    "Reset Material atlas to default": "Восстановить исходный атлас материалов",
    "Discard the managed local override for this atlas and restore the shipped default?\n\nProfile copies saved elsewhere will not be deleted.":
        "Удалить локальные изменения этого атласа и восстановить поставляемый с редактором профиль?\n\nКопии профиля, сохранённые в других папках, не будут удалены.",
    "Source: Local override — %1": "Источник: локальные изменения — %1",
    "Source: Built-in default (edited). Apply + Close will create a local override at %1.":
        "Источник: встроенный профиль (изменён). После применения будет создан локальный профиль: %1.",
    "Source: Built-in default. No local override is loaded. Editing and applying will create one at %1.":
        "Источник: встроенный профиль. Локальные изменения не загружены. После редактирования и применения будет создан профиль: %1.",
    "Source: Generated draft for this atlas. Apply + Close will create a local override at %1.":
        "Источник: созданный для этого атласа черновик. После применения будет создан локальный профиль: %1.",
    "0 / 0 assigned": "Назначено: 0 из 0",
    "Added exact tiles: 0": "Добавлено точных тайлов: 0",
    "Noise": "Шум",
    "Distributes proven uniform physical variants and deterministic rotations across each stroke interior. Exact blend rotations remain fixed by their corner identities.":
        "Распределяет проверенные равномерные варианты тайлов и предсказуемые повороты внутри каждого мазка. Повороты точных переходов определяются материалами их углов и не меняются.",
    "Safe: exact authored blends only":
        "Безопасный режим: только точные готовые переходы",
    "Auto-resolve blends": "Автоматически подбирать переходы",
    "Vary uniform texture rotation across strokes":
        "Менять поворот равномерной текстуры внутри мазков",
    "Left-drag sculpts; right-click samples the raw height as the Flatten target. Alt+Wheel changes radius; Shift+Wheel changes strength; Alt+Shift+Wheel changes raise/lower step, Noise range, or the Flatten target. Z/X/C/V select Raise/Lower/Smooth/Flatten; Noise is available in the Tool list. Wheel keeps camera zoom. Each drag is one undo step.":
        "Перетаскивание левой кнопкой правит рельеф; правая кнопка берёт исходную высоту для выравнивания. Alt+колёсико меняет радиус, Shift+колёсико — силу, Alt+Shift+колёсико — шаг подъёма и опускания, диапазон шума или высоту выравнивания. Z/X/C/V выбирают «Поднять», «Опустить», «Сгладить» и «Выровнять»; «Шум» доступен в списке инструментов. Обычное колёсико меняет масштаб камеры. Каждый мазок отменяется целиком.",
    "Underwater": "Под водой",
    "Overlay textures": "Текстуры покрытий",
    "Overlay": "Покрытие",
    "Existing ground designations": "Существующие назначения земли",
    "Existing underwater designations": "Существующие назначения дна",
    "Existing overlay designations": "Существующие назначения покрытий",
    "When enabled, Modern strokes realize exact transition tiles around the painted uniform cells. Disable it to stamp only uniform tiles without creating or selecting blend tiles.":
        "В современном режиме создаёт точные переходные тайлы вокруг закрашенных равномерных ячеек. Отключите, чтобы ставить только равномерные тайлы без создания и выбора переходов.",
    "Apply every exact authored connection that is available and leave unsupported boundary cells untouched. Disable this only when a local uniform hard edge is preferable to an unchanged gap.":
        "Применяет все доступные точные готовые переходы и не меняет границы, для которых перехода нет. Отключайте только там, где резкий край лучше неизменённого разрыва.",
    "Filter materials or atlas slots...": "Фильтр материалов или слотов атласа...",
    "Modern (Z)": "Современный (Z)",
    "Classic Auto (X)": "Классический (X)",
    "Manual (C)": "Ручной (C)",
    "Modern reuses exact authored junctions and generates an exact blend only when one is missing. Hold right-click and move to sample the nearest material vertex; Shift+Wheel changes material.":
        "Современный режим повторно использует точные готовые стыки и создаёт переход, только если нужного ещё нет. Удерживайте правую кнопку и двигайте мышь, чтобы взять материал ближайшей вершины; Shift+колёсико меняет материал.",
    "Classic Auto uses only exact tiles already present in this atlas and never changes the atlas. Safe mode leaves unsupported boundaries untouched. Hold right-click and move to sample a material.":
        "Классический режим использует только точные тайлы, уже имеющиеся в атласе, и никогда не меняет сам атлас. Безопасный режим не трогает границы без подходящего перехода. Удерживайте правую кнопку и двигайте мышь, чтобы взять материал.",
    "Manual paints the selected physical atlas tile and rotation directly, without semantic blending. Hold right-click and move to sample a tile; Shift+Wheel rotates it.":
        "Ручной режим напрямую рисует выбранным физическим тайлом атласа с заданным поворотом, без автоматического подбора переходов. Удерживайте правую кнопку и двигайте мышь, чтобы взять тайл; Shift+колёсико поворачивает его.",
    "Circular brush": "Круглая кисть",
    "Square brush": "Квадратная кисть",
    "Tile": "Тайл",
    "Vertex": "Вершина",
    "MPR Materials...": "Материалы MPR...",
    "Open the MPR surface-material and flipbook records used by overlays.":
        "Открыть записи MPR с материалами поверхности и покадровой анимацией покрытий.",
    "Brush size (Alt+Wheel)": "Размер кисти (Alt+колёсико)",
    "Paint target": "Что закрашивать",
    "Resolve missing blends... (Shift+R)": "Исправить недостающие переходы... (Shift+R)",
    "Scan the complete map and rebuild missing or inconsistent physical blend tiles from its shared-corner material field. The map repair is committed as one undo step. Shortcut: Shift+R.":
        "Проверить всю карту и восстановить отсутствующие или неверные физические переходные тайлы по общим материалам углов. Все исправления карты отменяются одним действием. Горячая клавиша: Shift+R.",
    "Material atlas...": "Атлас материалов...",
    "Inspect material thumbnails and visually assign unusual atlas tiles. This changes only the editor profile, not atlas images or MPR resources.":
        "Просмотреть миниатюры материалов и вручную назначить необычные тайлы атласа. Изменяется только профиль редактора, а не изображения атласа или ресурсы MPR.",
    "No / wrong blend": "Нет перехода или он неверен",
    "Wrong material / family": "Неверный материал или семейство",
    "Wrong rotation": "Неверный поворот",
    "Mark bad tiles on map": "Помечать проблемные тайлы на карте",
    "Choose an issue type, enable marking, then drag across every visibly bad tile. Painting is disabled while marking. Each drag appends exact tile, corner, rotation, atlas-binding, and stroke information to a portable diagnostic JSON.":
        "Выберите тип проблемы, включите пометку и проведите по всем видимым проблемным тайлам. Во время пометки рисование отключено. Каждый мазок добавляет в переносимый диагностический JSON точные сведения о тайле, углах, повороте, привязке атласа и мазке.",
    "Save MPR as modern draft...": "Сохранить MPR как современный черновик...",
    "Save a portable MPR plus adjacent map-local atlas, even when the MPR itself has no unsaved changes. Normal Save MPR and Save MPR As publish game-ready atlas pages into textures.res instead.":
        "Сохранить переносимый MPR вместе с соседним локальным атласом карты, даже если сам MPR не изменён. Обычные команды сохранения MPR вместо этого публикуют готовые для игры страницы атласа в textures.res.",
    "Noise range": "Диапазон шума",
    "%1 original + %2 added exact = %3 active slots":
        "%1 исходных + %2 добавленных точных = %3 активных слотов",
    "Original slots remain byte-identical. Added exact tiles include reused stock JMV blends and newly generated combinations cached for this map session.":
        "Исходные слоты остаются побайтово неизменными. Среди добавленных точных тайлов есть повторно используемые штатные переходы JMV и новые сочетания, созданные для текущего сеанса карты.",
    "shipped-size range": "штатный диапазон",
    "expanded range": "расширенный диапазон",
    "high-memory range": "диапазон с повышенным расходом памяти",
    "unvalidated / potentially unstable range": "непроверенный и потенциально нестабильный диапазон",
    "%1 / 16384 slots - %2": "%1 из 16384 слотов — %2",
    "Potential instability: this atlas is above 4096 slots. Staging remains available, but runtime memory limits depend on the rest of the map.":
        "Возможна нестабильность: в атласе больше 4096 слотов. Подготовка к игре доступна, но предел памяти зависит и от остального содержимого карты.",
    "MARKING: %1": "ПОМЕТКА: %1",
    "Circular tile brush; the viewport outlines affected tiles":
        "Круглая кисть; затрагиваемые тайлы обведены на карте",
    "Square tile brush; the viewport outlines affected tiles":
        "Квадратная кисть; затрагиваемые тайлы обведены на карте",
    "%1 / %2 assigned%3": "Назначено: %1 из %2%3",
    " (%1 unassigned)": " (без назначения: %1)",
    "No atlas loaded": "Атлас не загружен",
    "Local override:\n%1": "Локальные изменения:\n%1",
    "Slot %1 - runtime type %2": "Слот %1 — игровой тип %2",
    "Slot %1 - %2 - runtime type %3": "Слот %1 — %2 — игровой тип %3",
    "%1 - exact authored one-corner stamp. Vertex target only. %2":
        "%1 — точный готовый штамп одного угла. Только для вершин. %2",
    "Safe mode applies it only when every incident authored tile is exact.":
        "Безопасный режим применяет его, только если все соседние готовые тайлы точны.",
    "It is restricted to its reviewed support material.":
        "Разрешён только для проверенного опорного материала.",
    "%1 - %2 authored uniform source(s). %3":
        "%1 — готовых равномерных вариантов: %2. %3",
    "Currently used on this map.": "Сейчас используется на этой карте.",
    "Unused on this map; Classic Auto preserves available authored blends. Safe mode leaves a boundary unchanged when its exact blend is absent.":
        "На этой карте не используется. Классический режим сохраняет доступные готовые переходы; безопасный режим не меняет границу, если точного перехода нет.",
    "Unused on this map; Modern painting adds this material and generates exact blends as needed.":
        "На этой карте не используется. Современный режим добавит материал и при необходимости создаст точные переходы.",
    "Exact reviewed-HD preview is loading.\n": "Загружается проверенный HD-предпросмотр.\n",
    "No exact reviewed-HD preview is available.\n": "Точный проверенный HD-предпросмотр недоступен.\n",
    "Used on this map": "Используются на карте",
    "Unused materials": "Неиспользуемые материалы",
    "No existing automatic material designation is available for this layer.":
        "Для этого слоя нет готовых автоматических назначений материалов.",
    "%1 existing material designation(s). Safe Classic Auto applies only complete exact authored placements; tiles or vertices with unsupported boundaries stay unchanged.":
        "Готовых назначений материалов: %1. Безопасный классический режим применяет только полные точные готовые сочетания; тайлы и вершины на границах без поддержки остаются неизменными.",
    "%1 existing material designation(s). Classic Auto never changes the atlas; unresolved boundaries may use a local uniform hard edge.":
        "Готовых назначений материалов: %1. Классический режим никогда не меняет атлас; на границах без перехода может использоваться локальный резкий край равномерного материала.",
    "No physical atlas tiles are available.": "В атласе нет доступных физических тайлов.",
    "%1 physical atlas tile(s). Manual painting writes the selected slot and rotation directly.":
        "Физических тайлов атласа: %1. Ручной режим напрямую записывает выбранный слот и поворот.",
    "No materials are available for this layer.": "Для этого слоя нет доступных материалов.",
    "%1 material(s) ready for this layer.": "Для этого слоя готово материалов: %1.",
    "%1 (%2)": "%1 (%2)",
    "%1\n%2": "%1\n%2",
    "Paint issue marking mode: %1. Drag across every bad tile; normal painting is disabled until you choose a material brush.":
        "Режим пометки проблем: %1. Проведите по каждому неверному тайлу; обычное рисование отключено, пока не выбрана кисть материала.",
    "Visuals": "Отображение",
    "Expand visuals": "Развернуть панель отображения",
    "Show every currently available viewport visual channel.":
        "Показать все доступные элементы карты.",
    "Hide every currently available viewport visual channel.":
        "Скрыть все доступные элементы карты.",
    "Entrances": "Входы",
    "Object helpers": "Вспомогательные модели объектов",
    "Show editor helper models for light, particle, and sound objects. This does not hide their actual effects.":
        "Показывать вспомогательные модели источников света, частиц и звука. Их настоящие эффекты при этом не скрываются.",
    "Static objects": "Статические объекты",
    "Show ordinary static MOB geometry such as scenery, levers, torches, and trap locators.":
        "Показывать обычные статические объекты MOB: декорации, рычаги, факелы и указатели ловушек.",
    "Dynamic objects": "Динамические объекты",
    "Show units and their animated equipment.": "Показывать существ и их анимированное снаряжение.",
    "Terrain": "Рельеф",
    "Show the main terrain. This stays synchronized with Landscape Tools and the M shortcut.":
        "Показывать основной рельеф. Настройка связана с инструментами ландшафта и клавишей M.",
    "Overlays": "Покрытия",
    "Show overlay surfaces such as water and lava. This stays synchronized with Landscape Tools and the O shortcut.":
        "Показывать покрытия, например воду и лаву. Настройка связана с инструментами ландшафта и клавишей O.",
    "Fog": "Туман",
    "Show authored distance fog in Full environment lighting mode.":
        "Показывать заданный картой туман вдали в полном режиме освещения.",
    "Scene lights": "Источники света",
    "Show illumination from authored MOB point lights and torch lights. This mirrors Scene point lights in Render settings.":
        "Показывать свет от точечных источников и факелов MOB. Настройка соответствует пункту «Точечные источники карты» в настройках отображения.",
    "Terrain shadows": "Тени рельефа",
    "Show authored terrain shadowing when a landscape, Full environment, and desktop shadow renderer are available.":
        "Показывать сохранённые тени рельефа, когда загружен ландшафт, включено полное освещение и доступно настольное отображение теней.",
    "Object shadows": "Тени объектов",
    "Show real-time shadows cast by static and dynamic objects in Full environment mode.":
        "Показывать динамические тени от статических и движущихся объектов в полном режиме освещения.",
    "Skybox": "Небо",
    "Show the profile skybox in Full environment mode on outdoor maps.":
        "Показывать небо из активного профиля на открытых картах в полном режиме освещения.",
    "Particle visuals": "Эффекты частиц",
    "Show rendered particle effects. Particle object helper models are controlled separately.":
        "Показывать эффекты частиц. Вспомогательные модели их источников включаются отдельно.",
    "Freeze animations": "Заморозить анимации",
    "Hold animated units at their current idle-cycle frame. Available only when Animate unit idle cycles is enabled in Render settings.":
        "Остановить анимированных существ на текущем кадре анимации ожидания. Доступно, только если в настройках отображения включены анимации ожидания существ.",
    "Minimize visuals": "Свернуть панель отображения",
    "Unavailable: switch Lighting mode to Full environment.":
        "Недоступно: выберите полный режим освещения.",
    "Unavailable: Full environment requires the desktop renderer.":
        "Недоступно: для полного освещения требуется настольный рендерер.",
    "Unavailable: load terrain and enable shadow quality in Render settings.":
        "Недоступно: загрузите рельеф и включите качество теней в настройках отображения.",
    "Unavailable: enable shadow quality in Render settings.":
        "Недоступно: включите качество теней в настройках отображения.",
    "Unavailable: cave environments do not render a skybox.":
        "Недоступно: на пещерных картах небо не отображается.",
    "Unavailable: enable Animate unit idle cycles in Render settings.":
        "Недоступно: включите анимации ожидания существ в настройках отображения.",
    "Could not bind the offscreen minimap capture target.":
        "Не удалось подключить внеэкранную цель для снимка миникарты.",
    "Could not rebind the offscreen minimap capture target.":
        "Не удалось повторно подключить внеэкранную цель для снимка миникарты.",
    "Restore exact terrain atlas": "Восстановить точный атлас рельефа",
    "This MPR has an exact editor recovery snapshot, but its matching self-describing RES is not configured.\n\nRestore a map-local atlas now?\n%1\n\nOnly the exact saved pages and semantic profile will be restored; no atlas identity will be inferred.":
        "Для этого MPR сохранена точная копия атласа для восстановления, но соответствующий RES с описанием материалов не настроен.\n\nВосстановить локальный атлас карты?\n%1\n\nБудут восстановлены только точно сохранённые страницы и профиль материалов; имя атласа не будет определяться предположительно.",
    "Terrain atlas recovery failed": "Не удалось восстановить атлас рельефа",
    "The exact recovery snapshot could not be rebuilt. The MPR was not opened.\n\n%1":
        "Не удалось восстановить точную сохранённую копию. MPR не открыт.\n\n%1",
    "The existing map-local RES could not be backed up. The MPR was not opened.":
        "Не удалось создать резервную копию существующего локального RES карты. MPR не открыт.",
    "The exact map-local RES could not be committed. The MPR was not opened.\n\nPhase: %1\n%2":
        "Не удалось записать точный локальный RES карты. MPR не открыт.\n\nЭтап: %1\n%2",
    "The rebuilt RES failed exact semantic binding and was rolled back: %1. The MPR was not opened.\n\n%2":
        "Восстановленный RES не прошёл точную проверку привязки материалов и был возвращён в прежнее состояние: %1. MPR не открыт.\n\n%2",
    "complete": "завершено",
    "failed": "ошибка",
    "Terrain atlas restored": "Атлас рельефа восстановлен",
    "The exact map-local terrain atlas was restored and will be used to open this MPR.\n\n%1":
        "Точный локальный атлас рельефа восстановлен и будет использован для открытия этого MPR.\n\n%1",
    "This finalized MPR references an exact published terrain atlas, but no single configured RES archive matches it. The map was not opened, so the editor cannot mix old and new texture pages.\n\nResource identity: %1\nExpected atlas hash: %2\n\n%3":
        "Завершённый MPR ссылается на точно опубликованный атлас рельефа, но среди настроенных архивов RES нет полного соответствия. Карта не открыта: редактор не будет смешивать старые и новые страницы текстур.\n\nИдентификатор ресурса: %1\nОжидаемый хеш атласа: %2\n\n%3",
    "Finalized terrain RES unavailable": "RES завершённого рельефа недоступен",
    "Modern Draft pair required": "Требуется пара файлов современного черновика",
    "This map has unsaved working-atlas tiles. They cannot be stored in an MPR alone: Save As must write a paired MPR plus adjacent RES Modern Draft.":
        "В рабочем атласе этой карты есть несохранённые тайлы. Одного MPR недостаточно: команда «Сохранить как» должна записать MPR вместе с соседним RES современного черновика.",
    "Save as Modern Draft keeps the editable working atlas beside the selected MPR. Finalize and Save As publishes a clean runtime atlas. Cancel leaves the current map and destination unchanged.":
        "«Сохранить как современный черновик» оставляет редактируемый рабочий атлас рядом с выбранным MPR. «Завершить и сохранить как» публикует чистый игровой атлас. При отмене текущая карта и выбранное место не изменяются.",
    "Save as Modern Draft": "Сохранить как современный черновик",
    "Finalize and Save As": "Завершить и сохранить как",
    "The current terrain uses an unsaved working atlas, but this operation cannot preserve its paired RES resource.":
        "Текущий рельеф использует несохранённый рабочий атлас, а эта операция не может сохранить связанный с ним RES.",
    "The edited MPR could not be reopened before staging its working atlas.":
        "Не удалось повторно открыть изменённый MPR перед подготовкой его рабочего атласа.",
    "The authored terrain page '%1' could not be read for isolated test staging.":
        "Не удалось прочитать исходную страницу рельефа «%1» для отдельной подготовки теста.",
    "Modern MPR publication required": "Требуется публикация современного MPR",
    "This map contains modern terrain atlas edits. Use Save MPR or Save MPR As to publish them, or use Save MPR as modern draft when you intentionally need a portable map-local atlas.":
        "На карте изменён современный атлас рельефа. Для публикации используйте «Сохранить MPR» или «Сохранить MPR как». Если нужен переносимый локальный атлас рядом с картой, сохраните MPR как современный черновик.",
    "Encoding the map-local working atlas...": "Кодирование локального рабочего атласа карты...",
    "Modern draft save refused": "Сохранение современного черновика отклонено",
    "The edited MPR could not be reopened before pairing it with the working atlas. No destination was changed.\n\n%1":
        "Не удалось повторно открыть изменённый MPR перед связыванием с рабочим атласом. Целевые файлы не изменены.\n\n%1",
    "The authored terrain page '%1' could not be read. No destination was changed.":
        "Не удалось прочитать исходную страницу рельефа «%1». Целевые файлы не изменены.",
    "The editor could not preserve the working material assignments for reopen. No destination was changed.\n\n%1":
        "Редактор не смог сохранить рабочие назначения материалов для повторного открытия. Целевые файлы не изменены.\n\n%1",
    "The exact shared-corner material field could not be bound to the draft MPR. No destination was changed.\n\n%1":
        "Не удалось привязать к черновику MPR точное поле материалов общих углов. Целевые файлы не изменены.\n\n%1",
    "The exact material profile could not be embedded in the map-local RES. No destination was changed.\n\n%1":
        "Не удалось встроить точный профиль материалов в локальный RES карты. Целевые файлы не изменены.\n\n%1",
    "The paired MPR could not be validated for exact atlas recovery. No destination was changed.\n\n%1":
        "Не удалось подтвердить, что связанный MPR допускает точное восстановление атласа. Целевые файлы не изменены.\n\n%1",
    "The exact semantic atlas recovery snapshot could not be created, so no destination was changed.\n\n%1":
        "Не удалось создать точную копию профиля материалов для восстановления атласа, поэтому целевые файлы не изменены.\n\n%1",
    "Staging and verifying the map-local atlas...": "Подготовка и проверка локального атласа карты...",
    "Modern draft save failed": "Не удалось сохранить современный черновик",
    "The map-local atlas was not committed.\n\nPhase: %1\n%2":
        "Локальный атлас карты не записан.\n\nЭтап: %1\n%2",
    "Committing and reopening the paired MPR...": "Запись и повторное открытие связанного MPR...",
    "The MPR was not committed. Atlas rollback: %1.\n\nPhase: %2\n%3":
        "MPR не записан. Возврат атласа: %1.\n\nЭтап: %2\n%3",
    "Modern draft saved; reopen required": "Современный черновик сохранён; требуется открыть его заново",
    "The paired MPR and map-local atlas were saved, but the current session could not adopt them. Reopen this MPR before editing further:\n%1":
        "Связанный MPR и локальный атлас карты сохранены, но текущий сеанс не смог перейти на них. Перед дальнейшим редактированием откройте MPR заново:\n%1",
    "Modern draft save complete.": "Современный черновик сохранён.",
    "Modern draft saved with map-local atlas: %1":
        "Современный черновик сохранён с локальным атласом карты: %1",
    "MPR save refused": "Сохранение MPR отклонено",
    "The exact shared-corner material state could not be prepared. No destination was changed.\n\n%1":
        "Не удалось подготовить точное состояние материалов общих углов. Целевые файлы не изменены.\n\n%1",
    "The active terrain RES could not be read before storing the map's shared-corner material state. No destination was changed.\n\n%1":
        "Не удалось прочитать активный RES рельефа перед сохранением материалов общих углов карты. Целевые файлы не изменены.\n\n%1",
    "The active terrain RES could not be validated before storing the map's shared-corner material state. No destination was changed.\n\n%1":
        "Не удалось проверить активный RES рельефа перед сохранением материалов общих углов карты. Целевые файлы не изменены.\n\n%1",
    "The map's shared-corner material state could not be stored in the active terrain RES. The MPR was not changed.\n\nPhase: %1\n%2":
        "Не удалось сохранить материалы общих углов карты в активном RES рельефа. MPR не изменён.\n\nЭтап: %1\n%2",
    "The changed MPR save did not complete.\n\nPhase: %1\n%2\n\nBackup: %3\nSemantic RES rollback: %4":
        "Сохранение изменённого MPR не завершено.\n\nЭтап: %1\n%2\n\nРезервная копия: %3\nВозврат RES с профилем материалов: %4",
    "%1 — on %2": "%1 — включено для %2",
    "material %1": "материал %1",
    "No structurally valid material is available for namespace '%1'. Open Material atlas to assign one; manual atlas tile mode remains available.":
        "Для набора «%1» нет материала с допустимой структурой. Откройте атлас материалов и назначьте его; ручной выбор тайла атласа по-прежнему доступен.",
    " The obsolete shared profile was ignored and a working Material field was rebuilt from this MPR and its complete sibling atlas; modern painting remains available.":
        " Устаревший общий профиль пропущен. Рабочее поле материалов восстановлено из этого MPR и полного связанного атласа; современное рисование доступно.",
    " The obsolete shared profile was ignored. This atlas could not be inferred safely, so only manual tile painting is available.":
        " Устаревший общий профиль пропущен. Надёжно определить материалы этого атласа не удалось, поэтому доступно только ручное рисование тайлами.",
    "Game-wide Material families loaded: %1 ground and %2 overlay families.":
        "Загружены общие семейства материалов игры: для земли — %1, для покрытий — %2.",
    "Local Material atlas override loaded: %1 land and %2 overlay materials.":
        "Загружен локальный профиль атласа материалов: материалы земли — %1, покрытий — %2.",
    "Generated material atlas ready: %1 land and %2 overlay materials; %3 of %4 atlas slots assigned.":
        "Созданный профиль атласа готов: материалы земли — %1, покрытий — %2; назначено слотов — %3 из %4.",
    "Saved material atlas loaded: %1 land and %2 overlay materials.":
        "Загружен сохранённый профиль атласа: материалы земли — %1, покрытий — %2.",
    " No safe uniform material could be identified; open Material atlas to assign one.":
        " Не удалось надёжно определить равномерный материал; откройте атлас материалов и назначьте его.",
    "Default atlas": "Исходный атлас",
    "Local override: %1": "Локальные изменения: %1",
    "Generated draft": "Созданный черновик",
    "%1 (stamp on %2)": "%1 (штамп для %2)",
    "NW %1 / NE %2 / SW %3 / SE %4": "СЗ %1 / СВ %2 / ЮЗ %3 / ЮВ %4",
    "Material atlas is in use": "Атлас материалов используется",
    "Save or undo the current semantic terrain edits before changing the Material atlas profile. This keeps every generated working tile and undo reference bound to the atlas that created it.":
        "Перед сменой профиля атласа материалов сохраните или отмените текущие изменения материалов рельефа. Тогда каждый созданный рабочий тайл и действие отмены останутся связаны с атласом, в котором они созданы.",
    "Could not reset Material atlas": "Не удалось восстановить атлас материалов",
    "Local Material atlas override removed; the built-in default is active.":
        "Локальные изменения атласа материалов удалены; используется встроенный профиль.",
    "Built-in Material atlas remains active; no local override was created.":
        "Встроенный атлас материалов остаётся активным; локальный профиль не создавался.",
    "Material atlas applied and saved to %1.": "Атлас материалов применён и сохранён в %1.",
    "Material atlas applied and saved to %1. Unassigned or unconfirmed items remain excluded from painting.":
        "Атлас материалов применён и сохранён в %1. Неназначенные или неподтверждённые элементы по-прежнему недоступны для рисования.",
    "ground": "земля",
    "Terrain atlas artwork is missing": "Изображение атласа рельефа отсутствует",
    "Modern paint was not started. The %1 map cell %2,%3 references physical atlas slot %4, but the loaded atlas has no image for that slot. Restore the matching RES atlas before painting; Material Atlas cannot assign artwork that is not loaded.":
        "Современное рисование не начато. Ячейка %2,%3 слоя %1 ссылается на физический слот атласа %4, но в загруженном атласе нет его изображения. Перед рисованием восстановите соответствующий атлас RES: атлас материалов не может назначить незагруженное изображение.",
    "Terrain tile needs a material assignment": "Тайлу рельефа нужно назначить материал",
    "Modern paint was not started. The %1 map uses %2 physical artwork slot(s) without semantic assignments; the first is slot %3 at cell %4,%5. Undo the unassigned Manual paint or finish the active Modern atlas session, then assign the slot in Material Atlas.":
        "Современное рисование не начато. В слое %1 используется физических слотов без назначенного материала: %2; первый — слот %3 в ячейке %4,%5. Отмените ручное рисование неназначенным тайлом или завершите текущий сеанс современного атласа, затем назначьте материал слоту в атласе материалов.",
    "Modern paint was not started. The %1 map uses %2 physical artwork slot(s) without semantic assignments; the first is slot %3 at cell %4,%5. Material Atlas will open on that slot so its material or four corners can be assigned.":
        "Современное рисование не начато. В слое %1 используется физических слотов без назначенного материала: %2; первый — слот %3 в ячейке %4,%5. Атлас материалов откроется на этом слоте, чтобы назначить ему материал или материалы четырёх углов.",
    "Unassigned": "Не назначено",
    "Assigned": "Назначено",
    "material atlas approved": "атлас материалов подтверждён",
    "material atlas available": "атлас материалов доступен",
    "no safe material assignments": "нет надёжных назначений материалов",
    "Modern MPR save refused": "Сохранение современного MPR отклонено",
    "Save MPR or Save MPR As must select the destination before modern terrain publication begins. No file was changed.":
        "Перед публикацией современного рельефа команда сохранения MPR должна определить целевой файл. Файлы не изменены.",
    "Modern MPR publication refused": "Публикация современного MPR отклонена",
    "The selected destination is the currently open editable draft. Publishing over it would replace the draft MPR's atlas layout while its adjacent draft RES still owns the working layout.\n\nUse Save MPR to update the editable draft, or use Save MPR As and choose the final game MPR destination. No file was changed.":
        "Выбран текущий открытый редактируемый черновик. При публикации поверх него схема атласа в MPR изменится, а соседний черновой RES останется владельцем рабочей схемы.\n\nЧтобы обновить редактируемый черновик, используйте «Сохранить MPR». Для публикации выберите «Сохранить MPR как» и укажите итоговый игровой MPR. Файлы не изменены.",
    "Nothing to publish": "Нечего публиковать",
    "Paint at least one semantic terrain material before saving a modern MPR.":
        "Перед сохранением современного MPR нанесите хотя бы один материал рельефа.",
    "Preparing map-specific terrain atlas...": "Подготовка атласа рельефа для карты...",
    "Saving modern MPR": "Сохранение современного MPR",
    "This MPR cannot be converted to an exact shared-corner material field without changing terrain meaning. No identity or file was changed.\n\n%1":
        "Этот MPR нельзя преобразовать в точное поле материалов общих углов без изменения вида рельефа. Идентификатор и файлы не изменены.\n\n%1",
    "The virtual material field is incomplete; no package was written.":
        "Виртуальное поле материалов неполно; пакет не записан.",
    "A required terrain blend has not been realized in the live atlas. Run Resolve Missing Blends and retry; no file was changed.\n\n%1":
        "В рабочем атласе отсутствует необходимый переход рельефа. Выполните «Исправить недостающие переходы» и повторите попытку; файлы не изменены.\n\n%1",
    "Auditing preserved terrain references and unused slots...":
        "Проверка сохранённых ссылок рельефа и неиспользуемых слотов...",
    "Loading exact stock transitions used by this map...":
        "Загрузка точных штатных переходов, используемых картой...",
    "An authored stock transition used by this map could not be imported into the isolated final atlas.\n\n%1":
        "Не удалось перенести штатный переход, используемый картой, в отдельный итоговый атлас.\n\n%1",
    "Resolving, evicting unused tiles, and rebuilding physical pages...":
        "Подбор переходов, удаление неиспользуемых тайлов и перестроение физических страниц...",
    "The working atlas provenance could not be restored after compaction. No package was written.\n\n%1":
        "После уплотнения не удалось восстановить происхождение тайлов рабочего атласа. Пакет не записан.\n\n%1",
    "Review modern MPR save": "Проверка сохранения современного MPR",
    "Published atlas: %1 slots (%2 pages)  |  Layers: %7\nRetained original-map tiles: %3  |  Evicted/merged original-map tiles: %4\nIncluded stock JMV blends: %5  |  Included generated exact blends: %6":
        "Публикуемый атлас: %1 слотов (%2 страниц)  |  Слои: %7\nСохранено исходных тайлов карты: %3  |  Удалено или объединено исходных тайлов: %4\nДобавлено штатных переходов JMV: %5  |  Добавлено созданных точных переходов: %6",
    "Save result: the selected MPR and textures.res will be updated together. The MPR includes every current landscape edit, including sculpted heights and the latest paint stroke. Resource identity: %1 (%2). Editor-side JSON review metadata stays beside the MPR and is never put inside the runtime RES archive.":
        "Результат сохранения: выбранные MPR и textures.res будут обновлены вместе. В MPR войдут все текущие изменения ландшафта, включая правку высот и последний мазок. Идентификатор ресурса: %1 (%2). Служебные данные проверки JSON останутся рядом с MPR и не попадут в игровой архив RES.",
    "choose after this review": "выбрать после проверки",
    "use / replace": "использовать или заменить",
    "new content": "новое содержимое",
    "potentially unstable": "возможна нестабильность",
    "Potential instability: this atlas exceeds 4,096 slots. Runtime memory headroom depends on the rest of the map. The package may still be created and staged for controlled testing.":
        "Возможна нестабильность: в атласе больше 4096 слотов. Запас памяти игры зависит от остального содержимого карты. Пакет всё равно можно создать и подготовить для контролируемого теста.",
    "Non-original tiles written to this isolated atlas (stock reuse first, generated only when missing):":
        "Неисходные тайлы, записываемые в отдельный атлас (сначала используются штатные; новые создаются только при отсутствии):",
    "Blue frame: existing stock JMV blend added to this map.  Orange frame: newly generated exact blend.":
        "Синяя рамка: существующий штатный переход JMV, добавленный на карту.  Оранжевая рамка: новый созданный точный переход.",
    "Slot %1 - %2": "Слот %1 — %2",
    "stock JMV": "штатный JMV",
    "generated": "созданный",
    "Used for: %1\nCorners: %2 | %3 | %4 | %5\nPixel SHA-256: %6\nBlend source: %7":
        "Используется для: %1\nУглы: %2 | %3 | %4 | %5\nSHA-256 пикселей: %6\nИсточник перехода: %7",
    "original map atlas": "исходный атлас карты",
    "No non-original tiles are needed; the isolated atlas contains only compacted artwork from the original map.":
        "Неисходные тайлы не нужны; отдельный атлас содержит только уплотнённые изображения исходной карты.",
    "Save modern MPR": "Сохранить современный MPR",
    "Choose terrain resource identity": "Выберите идентификатор ресурса рельефа",
    "Create a new content identity (recommended)":
        "Создать новый идентификатор содержимого (рекомендуется)",
    "Use / replace an identity I specify":
        "Использовать или заменить указанный мной идентификатор",
    "Creating a new content identity is the safest export option because it avoids conflicts with existing map atlases. New content defaults to <map>_<12-character content hash>. Using an existing identity can affect every MPR that resolves the same MMP page names after the resulting textures.res is installed.":
        "Новый идентификатор содержимого — самый безопасный вариант экспорта: он исключает конфликты с существующими атласами карт. По умолчанию используется <карта>_<12 символов хеша содержимого>. Если использовать существующий идентификатор, после установки нового textures.res могут измениться все MPR с теми же именами страниц MMP.",
    "Invalid terrain resource identity": "Недопустимый идентификатор ресурса рельефа",
    "Atlas package validation failed": "Пакет атласа не прошёл проверку",
    "Choose textures.res to update in place": "Выберите textures.res для обновления",
    "Evil Islands resources (*.res)": "Ресурсы «Проклятых земель» (*.res)",
    "Save will publish every current landscape edit in one operation.\n\nMPR: %1\ntextures.res: %2\nResource identity: %3 (%4)\nLayers: %5\n\nEditor-side JSON review metadata stays beside the MPR and is never put inside the runtime RES archive.":
        "При сохранении все текущие изменения ландшафта будут опубликованы одной операцией.\n\nMPR: %1\ntextures.res: %2\nИдентификатор ресурса: %3 (%4)\nСлои: %5\n\nСлужебные данные проверки JSON останутся рядом с MPR и не попадут в игровой архив RES.",
    "No active resource-profile folder is configured. This RES is still allowed, but the editor and game will not resolve it until that location is installed or configured.":
        "В активном профиле не настроена папка ресурсов. Этот RES можно сохранить, но редактор и игра не найдут его, пока папка не будет установлена или настроена.",
    "The selected RES is outside the active resource profile. It is still allowed, but the editor and game will not resolve it until that location is installed or configured.":
        "Выбранный RES находится вне активного профиля ресурсов. Его можно сохранить, но редактор и игра не найдут его, пока эта папка не будет установлена или настроена.",
    "textures.res publication refused": "Публикация textures.res отклонена",
    "The selected resource archive could not be read safely.\n\n%1":
        "Не удалось безопасно прочитать выбранный архив ресурсов.\n\n%1",
    "Replace textures.res in place": "Заменить textures.res",
    "Endurance will stage and reopen the complete updated archive before atomically replacing:\n%1\n\nCreate a backup first?":
        "Endurance отдельно подготовит и повторно откроет полный обновлённый архив, прежде чем безопасно заменить:\n%1\n\nСначала создать резервную копию?",
    "Replace with backup": "Заменить с резервной копией",
    "Replace without backup": "Заменить без резервной копии",
    "Atlas package not written": "Пакет атласа не записан",
    "The isolated physical atlas could not be decoded.\n\n%1":
        "Не удалось декодировать отдельный физический атлас.\n\n%1",
    "The isolated material profile does not describe every logical atlas tile exactly. No RES or MPR was changed.\n\n%1":
        "Отдельный профиль материалов не содержит точного описания каждого логического тайла атласа. RES и MPR не изменены.\n\n%1",
    "The exact shared-corner material field could not be bound to the published MPR. No RES or MPR was changed.\n\n%1":
        "Не удалось привязать точное поле материалов общих углов к публикуемому MPR. RES и MPR не изменены.\n\n%1",
    "Atlas publication refused": "Публикация атласа отклонена",
    "The selected MPR destination could not be read before replacement. No RES or MPR was changed.":
        "Перед заменой не удалось прочитать выбранный целевой MPR. RES и MPR не изменены.",
    "The complete MPR candidate failed its fresh reopen. No output was written.\n\n%1":
        "Полный новый MPR не прошёл повторное открытие. Результат не записан.\n\n%1",
    "The MPR could not be bound to an exact recovery snapshot. No output was written.\n\n%1":
        "Не удалось связать MPR с точной копией для восстановления. Результат не записан.\n\n%1",
    "The exact semantic atlas recovery snapshot could not be created, so no RES or MPR was changed.\n\n%1":
        "Не удалось создать точную копию профиля материалов атласа для восстановления, поэтому RES и MPR не изменены.\n\n%1",
    "Building and reopening the complete textures.res candidate...":
        "Создание и повторное открытие полного нового textures.res...",
    "The complete textures.res candidate could not be built. No output was written.\n\n%1":
        "Не удалось создать полный новый textures.res. Результат не записан.\n\n%1",
    "The complete staged textures.res failed reopen. No output was written.\n\n%1":
        "Подготовленный полный textures.res не прошёл повторное открытие. Результат не записан.\n\n%1",
    "The staged textures.res did not reopen with its exact embedded material profile. No output was written.\n\n%1":
        "Подготовленный textures.res не открылся с точным встроенным профилем материалов. Результат не записан.\n\n%1",
    "Backing up and atomically replacing textures.res...":
        "Создание резервной копии и безопасная замена textures.res...",
    "Atomically replacing textures.res...": "Безопасная замена textures.res...",
    "textures.res publication failed": "Не удалось опубликовать textures.res",
    "The selected archive was not replaced.\n\nPhase: %1\n%2":
        "Выбранный архив не заменён.\n\nЭтап: %1\n%2",
    "Saving modern MPR...": "Сохранение современного MPR...",
    "MPR publication failed": "Не удалось опубликовать MPR",
    "Publication did not complete. textures.res rollback: %1; MPR rollback: %2.\n\nPhase: %3\n%4":
        "Публикация не завершена. Возврат textures.res: %1; возврат MPR: %2.\n\nЭтап: %3\n%4",
    "manual recovery required": "требуется ручное восстановление",
    "Adopting the saved map in this session...": "Переход текущего сеанса на сохранённую карту...",
    "Modern MPR saved; reopen required": "Современный MPR сохранён; требуется открыть его заново",
    "The validated textures.res and MPR are saved, but this session could not adopt them. Reopen the MPR before editing further.":
        "Проверенные textures.res и MPR сохранены, но текущий сеанс не смог перейти на них. Перед дальнейшим редактированием откройте MPR заново.",
    "Modern terrain atlas published in place: %1":
        "Современный атлас рельефа опубликован с заменой: %1",
    "Modern MPR saved": "Современный MPR сохранён",
    "textures.res was updated in place and the saved MPR is now the open editing baseline. No reload is required.\n\nMPR: %1\ntextures.res: %2\nIdentity: %3\nUsed tiles: %4 / %5\nTexture pages: %6\n\nUndo history was cleared because publication changed external resource identity and physical atlas ownership.":
        "textures.res обновлён, а сохранённый MPR теперь служит открытой исходной версией для редактирования. Повторная загрузка не требуется.\n\nMPR: %1\ntextures.res: %2\nИдентификатор: %3\nИспользовано тайлов: %4 из %5\nСтраниц текстур: %6\n\nИстория отмены очищена, поскольку публикация изменила внешний идентификатор ресурса и физический атлас.",
    "Terrain resource identity": "Идентификатор ресурса рельефа",
    "Animation repack refused": "Перенос анимации отклонён",
    "The runtime requires every flipbook to occupy one contiguous typed uniform atlas range. The working atlas could not preserve this animation without guessing or replacing a reference.\n\n%1":
        "Игра требует, чтобы каждая покадровая анимация занимала один непрерывный равномерный диапазон атласа одного типа. Рабочий атлас не может сохранить эту анимацию без предположений или замены ссылки.\n\n%1",
    "The repacked animation's complete runtime terrain-type table could not be installed. No atlas slots or map cells were changed.":
        "Не удалось установить полную игровую таблицу типов рельефа для перенесённой анимации. Слоты атласа и ячейки карты не изменены.",
    "zone2 or zone2_<content hash>": "zone2 или zone2_<хеш содержимого>",
    "Current identity": "Текущий идентификатор",
    "Identity to use / replace": "Использовать или заменить идентификатор",
    "The identity determines the MMP member names used by this MPR. Individual page names are derived automatically and are intentionally hidden. Edit the value above to replace that identity on the next Save MPR or Save MPR As. Clear it to let Save create a new content-hash identity.":
        "Идентификатор определяет имена файлов MMP, используемых этим MPR. Имена отдельных страниц создаются автоматически и намеренно скрыты. Измените значение выше, чтобы заменить идентификатор при следующем сохранении MPR. Очистите поле, чтобы при сохранении создать новый идентификатор по хешу содержимого.",
    "Reset to current identity": "Вернуть текущий идентификатор",
    "Resource identity": "Идентификатор ресурса",
    "Sampled nearest terrain vertex material %1.":
        "Взят материал ближайшей вершины рельефа: %1.",
    "The nearest vertex material is contextual and has no brush in this palette.":
        "Материал ближайшей вершины зависит от окружения, поэтому в этой палитре для него нет кисти.",
    "The semantic Add Overlay preview was refused; no edit was kept.":
        "Не удалось подготовить предпросмотр добавления покрытия; изменения отменены.",
    "The overlay atlas and surface could not be published together; no edit was kept.":
        "Не удалось одновременно применить атлас покрытия и поверхность; изменения отменены.",
    "Preparing exact reviewed-HD terrain artwork...":
        "Подготовка точных проверенных HD-изображений рельефа...",
    "Preparing terrain material": "Подготовка материала рельефа",
    "Finish the current Classic Auto stroke before changing material or layer.":
        "Завершите текущий мазок классического режима перед сменой материала или слоя.",
    "The terrain atlas and painted cells could not be published together; no edit was kept.":
        "Не удалось одновременно применить атлас рельефа и закрашенные ячейки; изменения отменены.",
    "Endurance identified usable materials in this map's atlas. Painting is already available for the resolved tiles.":
        "Endurance определил пригодные материалы в атласе карты. Для распознанных тайлов уже доступно рисование.",
    "Endurance could not prove a safe uniform material in this atlas. This usually indicates an unusual custom tileset or a parser gap; manual tile painting remains available.":
        "Endurance не смог надёжно определить равномерный материал в этом атласе. Обычно это означает необычный набор тайлов или неподдерживаемую структуру; ручное рисование тайлами остаётся доступным.",
    "Review generated Material atlas": "Проверить созданный атлас материалов",
    "%1\n\n%2 of %3 atlas tiles are assigned; %4 unusual tile(s) remain unassigned. Open Material atlas now to review names, thumbnails, and assignments? This does not modify the MPR or texture resources.":
        "%1\n\nНазначено тайлов атласа: %2 из %3; необычных тайлов без назначения: %4. Открыть атлас материалов, чтобы проверить имена, миниатюры и назначения? MPR и ресурсы текстур при этом не изменятся.",
    "Resolving exact terrain blends...": "Подбор точных переходов рельефа...",
    "Finishing terrain stroke": "Завершение мазка рельефа",
    "Classic Auto refused the whole stroke because %1 exact blend tuple(s) are not already present in this atlas. Use Modern mode to add them.":
        "Классический режим отклонил весь мазок: в атласе отсутствуют точные сочетания переходов (%1). Чтобы добавить их, используйте современный режим.",
    "Resolving exact terrain blends (%1 / %2)...":
        "Подбор точных переходов рельефа (%1 из %2)...",
    "Preparing the resolved terrain transaction...":
        "Подготовка полного изменения рельефа...",
    "Terrain stroke refused without substituting material identities:\n%1":
        "Мазок рельефа отклонён без подмены материалов:\n%1",
    " Classic Auto Safe preserved %1 unsupported vertex/vertices and %2 boundary cell(s) unchanged.":
        " Безопасный классический режим оставил без изменений неподдерживаемые вершины (%1) и граничные ячейки (%2).",
    "Terrain stroke: %1 stamp(s), %2 changed tile(s), %3 no-op, %4 refused.%5%6":
        "Мазок рельефа: штампов — %1, изменённых тайлов — %2, без изменений — %3, отклонено — %4.%5%6",
    " Preview cache checkpointed; earlier undo history was cleared.":
        " Состояние предпросмотра сохранено; предыдущая история отмены очищена.",
    "Cancelled unfinished terrain stroke.": "Незавершённый мазок рельефа отменён.",
    "Resolve missing blends": "Исправить недостающие переходы",
    "Open an editable MPR with a modern terrain atlas before running the map-wide resolver.":
        "Перед проверкой всей карты откройте редактируемый MPR с современным атласом рельефа.",
    "Scan Ground and Overlay and replace every unambiguous missing or inconsistent physical blend with the exact Modern result?\n\nDisputed shared corners are preserved exactly; this repair will not choose a side or move an existing material border. The MPR changes will be one undo step. Newly required atlas tiles remain in the working atlas.":
        "Проверить землю и покрытия и заменить каждый однозначно отсутствующий или неверный физический переход точным результатом современного режима?\n\nСпорные общие углы останутся неизменными: исправление не будет выбирать сторону или перемещать существующую границу материала. Все изменения MPR отменяются одним действием. Новые необходимые тайлы останутся в рабочем атласе.",
    "Don't show this confirmation again": "Больше не показывать это подтверждение",
    "Scanning terrain material connections...": "Проверка соединений материалов рельефа...",
    "Resolve missing terrain blends": "Исправить недостающие переходы рельефа",
    "Resolve missing blends refused": "Исправление недостающих переходов отклонено",
    "No MPR cell was changed.\n\n%1": "Ячейки MPR не изменены.\n\n%1",
    "No terrain cell needed repair.%1": "Исправление ячеек рельефа не требуется.%1",
    "The repaired physical tile command could not be applied. No MPR, atlas, preview, or undo state was changed.\n\n%1":
        "Не удалось применить исправленные физические тайлы. MPR, атлас, предпросмотр и история отмены не изменены.\n\n%1",
    "No MPR, atlas, preview, or undo state was changed.\n\n%1":
        "MPR, атлас, предпросмотр и история отмены не изменены.\n\n%1",
    "Resolved %1 terrain cell(s) in one undo step; %2 exact atlas tuple(s) were added.":
        "Исправлено ячеек рельефа: %1; добавлено точных сочетаний атласа: %2. Все изменения отменяются одним действием.",
    "Resolving full character visuals": "Подготовка полного вида персонажей",
    "Cell %1,%2 has no %3 terrain tile.": "В ячейке %1,%2 нет тайла слоя «%3».",
    "land": "земля",
    "unavailable": "недоступно",
    "Cell %1,%2 — %3 slot %4, rotation %5":
        "Ячейка %1,%2 — слой %3, слот %4, поворот %5",
    "Runtime terrain / ambience / traversal: %1 (%2)":
        "Игровой рельеф, окружение и проходимость: %1 (%2)",
    "Uniform brush: %1": "Равномерная кисть: %1",
    "Blend: %1": "Переход: %1",
    "Corners NW / NE / SE / SW: %1 / %2 / %3 / %4":
        "Углы СЗ / СВ / ЮВ / ЮЗ: %1 / %2 / %3 / %4",
    "Semantic material identity: unrecognized": "Материал не распознан",
    "Atlas origin: generated exact working-cache tile":
        "Источник в атласе: созданный точный тайл рабочего кэша",
    "Atlas origin: authored stock JMV tile":
        "Источник в атласе: штатный тайл JMV",
    "Could not create the terrain-paint diagnostic folder.":
        "Не удалось создать папку диагностики рисования рельефа.",
    "Could not write paint issue diagnostic: %1 (%2)":
        "Не удалось записать диагностику проблемы рисования: %1 (%2)",
    "Paint issue marked. Send this file: %1":
        "Проблема рисования отмечена. Отправьте этот файл: %1",
    "MARK: %1": "ПОМЕТКА: %1",
    "Noise range: %1 raw units": "Диапазон шума: %1 исходных единиц",
})

CONTEXT_CORRECTIONS = {
    ("CMessageLogPanel", "Debug"): "Отладка",
    ("CMobParameters", "Ambient"): "Фоновый свет",
    ("CTableManager", "Ambient"): "Фоновый звук",
    ("MainWindow", "Ambient"): "Фоновый свет",
    ("CTableManager", "Scale"): "Масштаб",
    ("CTableManager", "Guard radius"): "Радиус охраны",
    ("CTableManager", "Available to scripts"): "Доступен для скриптов",
    ("CTableManager", "Repeatable"): "Можно использовать повторно",
    ("CTableManager", "Door interaction"): "Считать дверью",
    ("CLandscapeTools", "light"): "свечение",
    ("CView", "disabled"): "отключено",
    ("MainWindow", "disabled"): "отключено",
    ("MainWindow", "Select"): "Выбрать",
    ("MainWindow", "Open Collection"): "Открыть коллекцию",
    ("CSelectForm", "Select"): "Выбрать",
}

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
            text = re.sub(r"\bAI\b", "ИИ", text)
            text = text.replace("English (английский)", "Английский")
            text = text.replace("Островов Зла", "Проклятых земель")
            text = text.replace("Evil Islands", "«Проклятые земли»")
            text = text.replace("Map.txt", "map.txt")
            text = text.replace("Map файлы", "Файлы карт")
            text = text.replace("вьюпорте", "окне карты")
            text = text.replace("Вьюпорте", "Окне карты")
            text = re.sub(r"\bмоб(?:а|у|ом|е|ы|ов|ами|ах)?\b", "MOB",
                          text, flags=re.IGNORECASE)
            if "graph" in source.lower():
                text = re.sub(r"график(а|у|ом|е|и|ов)?", r"граф\1",
                              text, flags=re.IGNORECASE)
            translation.text = text
    # The English source "Smooth" is intentionally shared by the terrain
    # action and the brush falloff. Russian needs an imperative for the action
    # and an adjective for the falloff, so keep this override context-specific.
    for context in tree.getroot().findall("context"):
        context_name = context.findtext("name", default="")
        for message in context.findall("message"):
            source = message.findtext("source", default="")
            replacement = CONTEXT_CORRECTIONS.get((context_name, source))
            if replacement is None:
                continue
            translation = message.find("translation")
            if translation is not None:
                translation.attrib.pop("type", None)
                translation.text = replacement

        if context.findtext("name", default="") != "LandscapeFalloff":
            continue
        for message in context.findall("message"):
            if message.findtext("source", default="") != "Smooth":
                continue
            translation = message.find("translation")
            if translation is not None:
                translation.attrib.pop("type", None)
                translation.text = "Гладкий"
    ET.indent(tree, space="    ")
    tree.write(CATALOG, encoding="utf-8", xml_declaration=True)
    print(f"applied {changed} reviewed terminology entries")


if __name__ == "__main__":
    main()
