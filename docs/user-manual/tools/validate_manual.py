#!/usr/bin/env python3
"""Validate the bilingual user-manual contract and generated offline HTML."""

from __future__ import annotations

import importlib.util
import json
import re
import sys
import xml.etree.ElementTree as ET
from html.parser import HTMLParser
from pathlib import Path
from urllib.parse import unquote, urlsplit

sys.dont_write_bytecode = True

ROOT = Path(__file__).resolve().parents[1]
REPO = ROOT.parents[1]
SOURCES = {lang: ROOT / lang / "manual.md" for lang in ("en", "ru")}
HTML_FILES = {lang: ROOT / "html" / lang / "index.html" for lang in ("en", "ru")}
ANCHOR_RE = re.compile(r'<a id="([A-Za-z0-9_-]+)"></a>')
MD_LINK_RE = re.compile(r'!?\[([^]]*)\]\(([^)]+)\)')


class DocumentParser(HTMLParser):
    def __init__(self) -> None:
        super().__init__(convert_charrefs=True)
        self.ids: list[str] = []
        self.hrefs: list[str] = []
        self.images: list[tuple[str, str | None]] = []
        self.lang: str | None = None

    def handle_starttag(self, tag: str, attrs: list[tuple[str, str | None]]) -> None:
        data = dict(attrs)
        if tag == "html":
            self.lang = data.get("lang")
        if data.get("id"):
            self.ids.append(data["id"] or "")
        if tag == "a" and data.get("href"):
            self.hrefs.append(data["href"] or "")
        if tag == "img":
            self.images.append((data.get("src") or "", data.get("alt")))


def fail(errors: list[str], message: str) -> None:
    errors.append(message)


def load_builder():
    path = ROOT / "tools" / "build_manual.py"
    spec = importlib.util.spec_from_file_location("manual_builder", path)
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def anchors(text: str) -> list[str]:
    return ANCHOR_RE.findall(text)


def resolve_local(base: Path, href: str) -> tuple[Path, str]:
    parts = urlsplit(href)
    path = (base / unquote(parts.path)).resolve() if parts.path else base.resolve()
    return path, unquote(parts.fragment)


def validate_markdown(errors: list[str], lang: str, text: str, known: set[str]) -> None:
    for label, href in MD_LINK_RE.findall(text):
        if re.match(r"^[A-Za-z][A-Za-z0-9+.-]*:", href):
            continue
        if href.startswith("#"):
            if href[1:] not in known:
                fail(errors, f"{lang}: broken Markdown fragment {href}")
            continue
        target, fragment = resolve_local(SOURCES[lang].parent, href)
        if not target.is_file():
            fail(errors, f"{lang}: missing Markdown link/image target {href}")
            continue
        if fragment:
            target_anchors = set(anchors(target.read_text(encoding="utf-8")))
            if fragment not in target_anchors:
                fail(errors, f"{lang}: missing fragment {fragment} in {target}")
        if href.startswith("!") and not label.strip():
            fail(errors, f"{lang}: image without alt text: {href}")


def validate_auxiliary_markdown(errors: list[str], path: Path) -> None:
    text = path.read_text(encoding="utf-8")
    for _label, href in MD_LINK_RE.findall(text):
        if href.startswith("#") or re.match(r"^[A-Za-z][A-Za-z0-9+.-]*:", href):
            continue
        target, fragment = resolve_local(path.parent, href)
        if not target.is_file():
            fail(errors, f"{path.name}: missing local target {href}")
            continue
        if fragment and target.suffix.lower() == ".md":
            if fragment not in set(anchors(target.read_text(encoding="utf-8"))):
                fail(errors, f"{path.name}: missing fragment {fragment} in {href}")


def validate_html(errors: list[str], lang: str, expected: str) -> None:
    path = HTML_FILES[lang]
    if not path.is_file():
        fail(errors, f"{lang}: generated HTML is missing")
        return
    actual = path.read_text(encoding="utf-8")
    if actual != expected:
        fail(errors, f"{lang}: generated HTML is stale; run build_manual.py")
    parser = DocumentParser()
    parser.feed(actual)
    if parser.lang != lang:
        fail(errors, f"{lang}: HTML lang is {parser.lang!r}")
    if len(parser.ids) != len(set(parser.ids)):
        fail(errors, f"{lang}: duplicate HTML ids")
    ids = set(parser.ids)
    for href in parser.hrefs:
        if re.match(r"^[A-Za-z][A-Za-z0-9+.-]*:", href):
            continue
        if href.startswith("#"):
            if href[1:] not in ids:
                fail(errors, f"{lang}: broken HTML fragment {href}")
            continue
        target, fragment = resolve_local(path.parent, href)
        if not target.is_file():
            fail(errors, f"{lang}: broken HTML link {href}")
            continue
        if fragment and target.suffix.lower() == ".html":
            other = DocumentParser()
            other.feed(target.read_text(encoding="utf-8"))
            if fragment not in set(other.ids):
                fail(errors, f"{lang}: missing HTML fragment {fragment} in {href}")
    for src, alt in parser.images:
        target, _ = resolve_local(path.parent, src)
        if not target.is_file():
            fail(errors, f"{lang}: missing HTML image {src}")
        if not alt or not alt.strip():
            fail(errors, f"{lang}: HTML image has no alt text: {src}")


def validate_coverage(errors: list[str], manual_anchors: set[str]) -> int:
    coverage = (ROOT / "COVERAGE.md").read_text(encoding="utf-8")
    rows = []
    for line in coverage.splitlines():
        if not re.match(r"^\| [A-Z]+-\d+ \|", line):
            continue
        cells = [cell.strip().strip("`") for cell in line.strip().strip("|").split("|")]
        if len(cells) != 5:
            fail(errors, f"coverage: malformed row {line[:80]}")
            continue
        feature_id, kind, surface, evidence, anchor = cells
        if kind not in {"Current", "Limitation", "Advanced"}:
            fail(errors, f"coverage {feature_id}: invalid class {kind}")
        if not surface or not evidence:
            fail(errors, f"coverage {feature_id}: missing surface/evidence")
        if anchor not in manual_anchors:
            fail(errors, f"coverage {feature_id}: missing manual anchor {anchor}")
        rows.append(feature_id)
    if len(rows) < 60:
        fail(errors, f"coverage: expected at least 60 rows, found {len(rows)}")
    if len(rows) != len(set(rows)):
        fail(errors, "coverage: duplicate feature IDs")
    return len(rows)


def validate_terminology(errors: list[str], en_text: str, ru_text: str) -> int:
    terms = json.loads((ROOT / "TERMINOLOGY.json").read_text(encoding="utf-8"))
    tree = ET.parse(REPO / "translations" / "ei_mapper_ru.ts")
    catalog: dict[str, set[str]] = {}
    for message in tree.findall(".//message"):
        source = message.findtext("source")
        translation = message.findtext("translation")
        if source and translation:
            catalog.setdefault(source, set()).add(translation)
    normalized_en = re.sub(r"\s+", " ", en_text)
    normalized_ru = re.sub(r"\s+", " ", ru_text)
    for term in terms:
        source, ru = term["source"], term["ru"]
        display_en = term.get("en", source.replace("&&", "&"))
        if not term.get("manual_only") and ru not in catalog.get(source, set()):
            fail(errors, f"terminology: TS mismatch {source!r} => {ru!r}")
        if re.sub(r"\s+", " ", display_en) not in normalized_en:
            fail(errors, f"terminology: English manual does not use {display_en!r}")
        if re.sub(r"\s+", " ", ru) not in normalized_ru:
            fail(errors, f"terminology: Russian manual does not use {ru!r}")
    return len(terms)


def main() -> int:
    errors: list[str] = []
    texts = {lang: path.read_text(encoding="utf-8") for lang, path in SOURCES.items()}
    ordered = {lang: anchors(text) for lang, text in texts.items()}
    for lang in ("en", "ru"):
        if len(ordered[lang]) != len(set(ordered[lang])):
            fail(errors, f"{lang}: duplicate explicit anchors")
    if ordered["en"] != ordered["ru"]:
        fail(errors, "EN/RU explicit anchor order differs")
    for lang in ("en", "ru"):
        validate_markdown(errors, lang, texts[lang], set(ordered[lang]))
    for auxiliary in (ROOT / "README.md", ROOT / "COVERAGE.md", ROOT / "SCREENSHOT_CHECKLIST.md"):
        validate_auxiliary_markdown(errors, auxiliary)
    for asset in (ROOT / "assets").glob("*.svg"):
        try:
            ET.parse(asset)
        except ET.ParseError as exc:
            fail(errors, f"{asset.name}: invalid SVG XML: {exc}")

    coverage_rows = validate_coverage(errors, set(ordered["en"]))
    term_count = validate_terminology(errors, texts["en"], texts["ru"])

    exact_lava = (
        "**Lava + Resolve Blends:** direct Modern painting follows the selected\n"
        "> Lava material, but **Shift+R** cannot yet preserve one visually consistent\n"
        "> route owner across every rare connected Lava-on-Water transition without\n"
        "> regressing ordinary Ground/Overlay repair. The bounded generic resolver is\n"
        "> retained while this remains under review."
    )
    for lang in ("en", "ru"):
        if exact_lava not in texts[lang]:
            fail(errors, f"{lang}: exact ROADMAP Lava limitation is missing")
    required_en = [
        "4,096 terrain-atlas slots / 64 pages",
        "sizes above 4,096 are not",
        "8,192-slot",
        "32-bit address-space pressure",
        "packed format can encode more",
    ]
    for phrase in required_en:
        if phrase not in texts["en"]:
            fail(errors, f"en: required atlas boundary phrase missing: {phrase}")
    for phrase in ("4096 слотов атласа / 64 страницы", "8192 слотов", "32-битное"):
        if phrase not in texts["ru"]:
            fail(errors, f"ru: required atlas boundary phrase missing: {phrase}")

    builder = load_builder()
    for lang in ("en", "ru"):
        validate_html(errors, lang, builder.render_document(lang))

    if errors:
        print("manual validation FAILED", file=sys.stderr)
        for error in errors:
            print(f"- {error}", file=sys.stderr)
        return 1
    print(
        f"manual validation passed: {len(ordered['en'])} paired anchors, "
        f"{coverage_rows} coverage rows, {term_count} terminology pairs, "
        "all local links/images and generated HTML verified"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
