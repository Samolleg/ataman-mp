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

SHORTCUT_RE = re.compile(
    r"^(?:[A-Z]|Del|Esc|F\d+|(?:(?:Ctrl|Alt|Shift|Meta)\+)+(?:[A-Z]|F\d+|Esc|Tab|Wheel)|(?:Ctrl|Alt|Shift|Meta) \+ Mouse Wheel)$")


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
