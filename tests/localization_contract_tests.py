import json
import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def placeholders(text):
    return sorted(re.findall(r"%(?:L?\d+|[A-Za-z])", text or ""))


def tags(text):
    return sorted(re.findall(
        r"</?(?:b|span|i|br|p|ul|li|strong|em)\b[^>]*>", text or "",
        flags=re.IGNORECASE))


def main():
    project = (ROOT / "ei_maper.pro").read_text(encoding="utf-8")
    startup = (ROOT / "source/app/main.cpp").read_text(encoding="utf-8")
    settings = (ROOT / "source/app/settings.cpp").read_text(encoding="utf-8")
    properties = (ROOT / "source/layout_components/tablemanager.cpp").read_text(
        encoding="utf-8")
    main_window = (ROOT / "source/app/main_window.cpp").read_text(encoding="utf-8")
    script_window = (ROOT / "source/ui/mob_script_window.cpp").read_text(encoding="utf-8")
    diff_dialog = (ROOT / "source/ui/mob_diff_dialog.cpp").read_text(encoding="utf-8")
    setup_dialog = (ROOT / "source/ui/resource_setup_dialog.cpp").read_text(encoding="utf-8")
    diplomacy = (ROOT / "source/core/trap_diplomacy.cpp").read_text(encoding="utf-8")
    maps_browser = (ROOT / "source/ui/maps_browser.cpp").read_text(encoding="utf-8")
    body_parts = (ROOT / "source/ui/bodypartedit_form.cpp").read_text(encoding="utf-8")
    unit_stats = (ROOT / "source/ui/unitstat_form.cpp").read_text(encoding="utf-8")
    view = (ROOT / "source/render/view.cpp").read_text(encoding="utf-8")
    tree_view = (ROOT / "source/layout_components/tree_view.cpp").read_text(
        encoding="utf-8")
    landscape_tools = (ROOT / "source/ui/landscape_tools.cpp").read_text(
        encoding="utf-8")
    world_terrain = (ROOT / "source/render/view_world_terrain.cpp").read_text(
        encoding="utf-8")
    create_object = (ROOT / "source/ui/createobjectform.cpp").read_text(
        encoding="utf-8")
    build = (ROOT / "scripts/build_release.ps1").read_text(encoding="utf-8")
    catalog_path = ROOT / "translations/ei_mapper_ru.ts"

    require("TRANSLATIONS" in project and "ei_mapper_ru.ts" in project,
            "qmake must own the Russian catalog")
    require("installApplicationTranslator" in startup,
            "startup must install the translator before constructing UI")
    require('new COptString("language", "ru")' in settings,
            "Russian must be the persisted default/fallback language")
    require(catalog_path.is_file(), "Russian TS catalog is missing")
    require("lrelease.exe" in build and "ei_mapper_ru.qm" in build,
            "release builds must compile the catalog")
    require("translations.qrc" in project,
            "compiled catalog must be deployed as an application resource")
    require('QT_TRANSLATE_NOOP("QPlatformTheme", "Cancel")' in
            (ROOT / "source/app/localization.cpp").read_text(encoding="utf-8"),
            "Qt standard dialog buttons must use the application catalog")
    require(startup.index("runHeadlessCommand(app)") <
            startup.index("installApplicationTranslator(app)"),
            "headless diagnostics must remain English")
    require(not re.search(r"m_aRow(?:Name|ToolTip)\[[^]]+\]\s*=\s*\"", properties),
            "dynamic property metadata must use translation lookups")
    require("QT_TRANSLATE_NOOP(\"MobFamilyValidation\"" in main_window and
            "localizedMobFamilyText(finding.message)" in main_window,
            "validation findings must be localized only at presentation")
    require("Restart required" in settings and "languageChanged" in settings,
            "saving a changed language must show a restart-required prompt")
    require('setWindowTitle(QString("MOB Script:' not in script_window,
            "MOB script window titles must use translation lookup")
    require("localizedDiffTerm" in diff_dialog,
            "MOB comparison enum values must be translated at presentation")
    require('lines.append(QString("Profile:' not in setup_dialog,
            "setup summary labels must use translation lookup")
    require('result.display = QString("' not in diplomacy,
            "diplomacy display text must use translation lookup")
    require("localizedMapCatalogMessages(result.messages)" in maps_browser,
            "map catalog diagnostics must be translated at presentation")
    require('header.append("Part")' not in body_parts and
            'header.append("Parameter")' not in unit_stats and
            'header.append("Parameter")' not in properties,
            "object/property table column titles must use translation lookup")
    require("localizedAiGraphLayerLabel" in view and
            "<th>Missing links</th>" not in view,
            "AI graph layer names and comparison columns must be translated")
    require('labels << tr("Objects") << "ID" << tr("Count")' in view and
            'eWorldObject, tr("World objects")' in tree_view,
            "object hierarchy headings and categories must use translation lookup")
    require('QCoreApplication::translate("LandscapeFalloff", "Smooth")' in
            landscape_tools,
            "the Smooth tool and smooth falloff must have separate translations")
    require('text != "<choose object type>"' not in create_object,
            "Create Object must not compare localized display text to an English literal")
    require("\ufffd" not in world_terrain,
            "world-terrain source must preserve valid UTF-8 labels")
    require("if(!m_objType.values().contains(object))" in create_object,
            "Create Object must reject placeholder or unknown translated selections")
    require("if(m_objType.values().contains(text))" in create_object,
            "Create Object showEvent must reopen only a structurally valid type")
    update_table = create_object[
        create_object.index("void CCreateObjectForm::updateTable()"):
        create_object.index("CPreview *CCreateObjectForm::viewWidget()")]
    require("if(!m_pNode)" in update_table,
            "Create Object table refresh must tolerate an absent template node")

    technical_literals = {"Codex", "MCP", "X", "Y", "Z", "JSON (*.json)",
                          "MPR (*.mpr)", "MOB (*.mob)", "EIRebuild64NavBake.exe (*.exe)"}
    visible_literal_patterns = (
        re.compile(r"set(?:Text|ToolTip|StatusTip|WindowTitle|PlaceholderText)\(\s*\"([^\"]*)\""),
        re.compile(r"Q(?:Label|PushButton|CheckBox|GroupBox|Action)\(\s*\"([^\"]*)\""),
        re.compile(r"QFileDialog::\w+\([^\n]*,\s*\"([^\"]*)\""),
        re.compile(r"QMessageBox::\w+\([^\n]*,\s*\"([^\"]*)\""),
    )
    for folder in ("source/app", "source/ui", "source/mob", "source/layout_components"):
        for source_path in (ROOT / folder).glob("*.cpp"):
            if source_path.name == "main.cpp" or source_path.name.endswith("_check.cpp"):
                continue  # Headless fixtures contain literal test input, not application labels.
            for line_number, line in enumerate(
                    source_path.read_text(encoding="utf-8").splitlines(), 1):
                if line.lstrip().startswith("//"):
                    continue
                for pattern in visible_literal_patterns:
                    match = pattern.search(line)
                    if (match and match.group(1) and
                            match.group(1) not in technical_literals and
                            not re.fullmatch(r"[+-]?[\d.]+", match.group(1))):
                        raise AssertionError(
                            f"raw user-visible literal at {source_path.relative_to(ROOT)}:"
                            f"{line_number}: {match.group(1)!r}")

    root = ET.parse(catalog_path).getroot()
    require(root.attrib.get("language") == "ru_RU", "catalog language must be ru_RU")
    translations = {}
    unchanged_allowlist = {
        "EI Mapper", "RGB", "ICO", "Esc", "Ctrl+R", "Ctrl+Tab", "Ctrl+V",
        "Ctrl+Z", "Ctrl+Y", "Ctrl+X", "Ctrl+D", "Ctrl+H", "Ctrl+F5",
        "Ctrl+S", "Shift+Esc", "MPR:", "MOB:", "MOB", "MPR", "MOB %1", "MPR %1",
        "MPR (*.mpr)", "MOB (*.mob)", "NID %1 (%2): %3", "NID %1 — %2", "NID %1: %2",
        "JMV Lua [%1] — %2", "database.res", "tables.lua (JMV)",
    }
    technical_tokens = ("MOB", "MPR", "JMV", "NID")
    shortcut_re = re.compile(
        r"^(?:[A-Z]|Del|Esc|F\d+|(?:(?:Ctrl|Alt|Shift|Meta)\+)+(?:[A-Z]|F\d+|Esc|Tab|Wheel)|(?:Ctrl|Alt|Shift|Meta) \+ Mouse Wheel)$")
    message_count = 0
    for message in root.findall(".//message"):
        source = message.findtext("source", default="")
        translation = message.find("translation")
        require(translation is not None, f"translation node missing: {source!r}")
        require(translation.attrib.get("type") != "unfinished",
                f"unfinished translation: {source!r}")
        translated = "".join(translation.itertext())
        require(translated != "", f"empty translation: {source!r}")
        require(placeholders(source) == placeholders(translated),
                f"placeholder mismatch: {source!r} -> {translated!r}")
        require(tags(source) == tags(translated),
                f"rich-text tag mismatch: {source!r} -> {translated!r}")
        require(source.count("\n") == translated.count("\n"),
                f"line-break mismatch: {source!r} -> {translated!r}")
        for token in technical_tokens:
            require(token not in source or token in translated,
                    f"technical token changed: {token} in {source!r}")
        if source == translated and re.search(r"[A-Za-z]{3}", source):
            require(source in unchanged_allowlist or shortcut_re.fullmatch(source),
                    f"untranslated user-visible source: {source!r}")
        translations[source] = translated
        message_count += 1

    forbidden_machine_phrases = {
        "Дифференциал", "Выносливость", "невыполнимое действие",
        "слот Логика", "предварительный просмотр заказа",
        "Сохранить отзыв PNG", "внешний вид к последнему патрулю",
        "Test Current Map", "Apply Raw Source", "Companion MOB", "Хюэ",
        "ВИЭ", "Орфографическая", "среда выполнения", "среды выполнения",
        "время выполнения", "времени выполнения", "видовой экран",
        "Стол дипломатии", "дипломатического стола", "Кастинг целей",
        "Заказать/выбрать", "Смотри %1", "грязных документов",
        "поколения восстановления", "Сериализованная", "сериализованный",
        "игровое владение", "затенён встроенным графом", "принадлежащий хешу",
        "прокси-серверов столкновений", "Карта горячей перезагрузки",
        "Рандомизировать", "неотменяемое изменение", "Неполная семья",
        "Primary MOB", "Bake or Switch", "Stop Test",
        "зафиксир", "канонич", "транзакц", "мутац",
        "поставщик услуг", "промежуточн",
        "Фильтровать", "Части фильтра", "English", "Evil Islands",
        "Островов Зла", "Map файл", "вьюпорт",
    }
    combined_russian = "\n".join(translations.values())
    combined_russian_folded = combined_russian.casefold()
    for phrase in forbidden_machine_phrases:
        require(phrase.casefold() not in combined_russian_folded,
                f"machine-literal Russian wording remains: {phrase!r}")
    require(not re.search(r"\bAI\b", combined_russian),
            "English AI abbreviation remains in Russian UI text")
    require(not re.search(r"\([ыяи]\)", combined_russian),
            "slash-style machine plural remains in Russian UI text")

    # Current-main adds the resource browser, database editor, item/spell
    # constructor, and the expanded terrain/atlas workflow.  The previous
    # 2,222-message catalog silently passed this test while omitting all of
    # those visible surfaces, so keep the extracted-current-main floor here.
    require(message_count >= 2904,
            f"catalog coverage unexpectedly fell to {message_count} messages")

    expected = {
        "&File": "&Файл",
        "Settings": "Настройки",
        "Object Properties": "Свойства объекта",
        "MOB Family Validation": "Проверка семейства MOB",
        "Initializing editor...": "Инициализация редактора...",
        "Diff loaded / regenerated": "Сравнить графы",
        "Perspective MOB Diff - Details": "Сравнение MOB — подробности",
        "Regenerate preview": "Обновить граф",
        "Filter parts...": "Фильтр частей...",
        "Filter maps...": "Фильтр карт...",
        "English": "Английский",
        "Lighting mode": "Режим освещения",
        "Unlit": "Без освещения",
        "Simple shading": "Простой",
        "Full environment": "Полный",
        "Sculpt terrain": "Правка рельефа",
        "Hue": "Оттенок",
        "Destination RES": "Целевой RES",
        "Children": "Дочерние точки",
        "Cast targets": "Цели заклинания",
        "Diplomacy table": "Таблица дипломатии",
        "Vertical synchronization (restart required)":
            "Вертикальная синхронизация (требуется перезапуск)",
        "Play from Here": "Играть отсюда",
        "Export Runtime .grp": "Экспортировать игровой .grp",
        "\n\nIt is currently shadowed by the primary MOB's embedded graph.":
            "\n\nСейчас игра использует встроенный граф основного MOB вместо этого файла.",
        "Orthographic game orientation: world +X is right and world +Y is down":
            "Ортографическая ориентация: +X вправо, +Y вниз",
        "Materials and assigned tiles": "Материалы и назначенные тайлы",
        "Open prototype in Database Editor":
            "Открыть прототип в редакторе базы данных",
        "Safe: exact authored blends only":
            "Безопасный режим: только точные готовые переходы",
        "Database Editor": "Редактор базы данных",
        "Resource Browser": "Просмотр ресурсов",
        "Gold indicates the point currently selected in the viewport and outliner.":
            "Золотым цветом выделена точка, выбранная на карте и в иерархии.",
        "+ Add patrol": "+ Добавить патрульную точку",
    }
    for source, translated in expected.items():
        require(translations.get(source) == translated,
                f"representative translation missing: {source!r}")

    context_translations = {
        (context.findtext("name", default=""),
         message.findtext("source", default="")):
            "".join(message.find("translation").itertext())
        for context in root.findall("context")
        for message in context.findall("message")
        if message.find("translation") is not None
    }
    require(context_translations.get(("CLandscapeTools", "Smooth")) == "Сгладить",
            "terrain Smooth action must be translated as an action")
    require(context_translations.get(("LandscapeFalloff", "Smooth")) == "Гладкий",
            "smooth brush falloff must be translated as an adjective")
    require(context_translations.get(("CCollectionEditorDialog", "Remove")) == "Удалить",
            "collection Remove button must use the imperative")

    require("Language" in settings and "restart" in settings.lower(),
            "settings must explain the language restart behavior")
    print(json.dumps({"messages": message_count,
                      "unique_sources": len(translations),
                      "status": "PASS"}))


if __name__ == "__main__":
    try:
        main()
    except AssertionError as error:
        print(f"FAIL: {error}", file=sys.stderr)
        raise SystemExit(1)
