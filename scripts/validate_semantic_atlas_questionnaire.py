#!/usr/bin/env python3
"""Validate the permanent semantic-atlas questionnaire handoff contract."""

from __future__ import annotations

import argparse
import json
from dataclasses import dataclass, field
from html.parser import HTMLParser
from pathlib import Path

from PIL import Image


CONTRACT = "semantic-atlas-questionnaire-v1"
DISPLAY_ORDER = ["NW", "NE", "SW", "SE"]
SERIALIZATION_ORDER = ["NW", "NE", "SE", "SW"]


def attrs_dict(attrs: list[tuple[str, str | None]]) -> dict[str, str]:
    return {key: value or "" for key, value in attrs}


@dataclass
class SelectRecord:
    question: str
    section: str
    field: str
    current_option: str = ""
    review_kind: str = "assignment"
    options: list[str] = field(default_factory=list)


@dataclass
class FigureRecord:
    question: str
    section: str
    row: str
    attributes: dict[str, str]
    image: dict[str, str] | None = None
    text: list[str] = field(default_factory=list)


class QuestionnaireParser(HTMLParser):
    def __init__(self) -> None:
        super().__init__(convert_charrefs=True)
        self.body: dict[str, str] = {}
        self.questions: list[str] = []
        self.defaults: dict[str, dict] = {}
        self.selects: list[SelectRecord] = []
        self.figures: list[FigureRecord] = []
        self.ids: set[str] = set()
        self.checkbox_count = 0
        self.not_answered = False
        self.note_questions: set[str] = set()
        self.script_text: list[str] = []
        self._question = ""
        self._section = ""
        self._section_number = 0
        self._row = ""
        self._row_number = 0
        self._div_stack: list[bool] = []
        self._select: SelectRecord | None = None
        self._option_parts: list[str] | None = None
        self._figure: FigureRecord | None = None
        self._in_script = False

    def handle_starttag(self, tag: str, attrs) -> None:
        values = attrs_dict(attrs)
        if "id" in values:
            self.ids.add(values["id"])
        classes = set(values.get("class", "").split())
        if tag == "body":
            self.body = values
        elif tag == "article" and "question" in classes:
            self._question = values.get("data-key", "")
            self.questions.append(self._question)
            try:
                self.defaults[self._question] = json.loads(values.get("data-defaults", "{}"))
            except json.JSONDecodeError:
                self.defaults[self._question] = {"__invalid__": True}
        elif tag == "section":
            self._section_number += 1
            self._section = values.get("data-scope", f"section-{self._section_number}")
        elif tag == "div":
            is_row = "comparison-row" in classes
            self._div_stack.append(is_row)
            if is_row:
                self._row_number += 1
                self._row = values.get("data-row", f"row-{self._row_number}")
        elif tag == "figure":
            self._figure = FigureRecord(self._question,
                                        self._section or self._question,
                                        self._row or self._section or self._question,
                                        values)
            self.figures.append(self._figure)
        elif tag == "img" and self._figure is not None:
            self._figure.image = values
        elif tag == "select" and "answer" in classes:
            self._select = SelectRecord(self._question,
                                        self._section or self._question,
                                        values.get("data-field", ""),
                                        values.get("data-current-option", ""),
                                        values.get("data-review-kind", "assignment"))
            self.selects.append(self._select)
        elif tag == "option" and self._select is not None:
            self._option_parts = []
        elif tag == "textarea" and "note" in classes:
            self.note_questions.add(self._question)
        elif tag == "input" and values.get("type", "").casefold() == "checkbox":
            self.checkbox_count += 1
        elif tag == "script":
            self._in_script = True

    def handle_endtag(self, tag: str) -> None:
        if tag == "article":
            self._question = ""
            self._section = ""
        elif tag == "section":
            self._section = ""
        elif tag == "figure":
            self._figure = None
        elif tag == "div":
            if self._div_stack and self._div_stack.pop():
                self._row = ""
        elif tag == "option" and self._option_parts is not None:
            value = "".join(self._option_parts).strip()
            self._select.options.append(value)
            if value.casefold() == "not answered":
                self.not_answered = True
            self._option_parts = None
        elif tag == "select":
            self._select = None
        elif tag == "script":
            self._in_script = False

    def handle_data(self, data: str) -> None:
        if self._option_parts is not None:
            self._option_parts.append(data)
        if self._in_script:
            self.script_text.append(data)
        if self._figure is not None:
            self._figure.text.append(data)


def is_concrete_option(value: str) -> bool:
    prefixes = (
        "different existing family", "different family", "different name",
        "new distinct family", "not uniform", "still uncertain",
        "keep current assignment", "keep current name",
        "keep current family", "keep family identity",
    )
    return bool(value) and not value.casefold().startswith(prefixes)


def validate_questionnaire(html_path: Path, manifest_path: Path,
                           *, handoff: bool = False) -> list[str]:
    errors: list[str] = []
    parser = QuestionnaireParser()
    text = html_path.read_text(encoding="utf-8")
    parser.feed(text)
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))

    if parser.body.get("data-questionnaire-contract") != CONTRACT:
        errors.append(f"body must declare {CONTRACT}")
    if manifest.get("questionnaireContract") != CONTRACT:
        errors.append(f"manifest questionnaireContract must be {CONTRACT}")
    if manifest.get("displayOrder") != DISPLAY_ORDER:
        errors.append(f"manifest displayOrder must be {DISPLAY_ORDER}")
    if manifest.get("serializationOrder") != SERIALIZATION_ORDER:
        errors.append(f"manifest serializationOrder must be {SERIALIZATION_ORDER}")
    if len(parser.questions) != len(set(parser.questions)) or "" in parser.questions:
        errors.append("question data-key values must be nonempty and unique")
    if manifest.get("questionKeys") != parser.questions:
        errors.append("manifest questionKeys must exactly match page question order")
    missing = sorted(set(manifest.get("markedItemKeys", [])) - set(parser.questions)
                     - set(manifest.get("resolvedMarkedItemKeys", [])))
    for key in missing:
        errors.append(f"marked item {key} is neither questioned nor explicitly resolved")
    if parser.checkbox_count:
        errors.append("confirmation checkbox is forbidden")
    if parser.not_answered or "Not answered" in text:
        errors.append("Not answered is forbidden; use Keep current defaults or Still uncertain")
    for key in parser.questions:
        if key not in parser.note_questions:
            errors.append(f"question {key} has no comment field")
        if parser.defaults.get(key, {}).get("__invalid__"):
            errors.append(f"question {key} has invalid data-defaults JSON")

    required_ids = {"copy", "download", "select", "submit", "reset", "export"}
    for missing_id in sorted(required_ids - parser.ids):
        errors.append(f"missing interaction control #{missing_id}")
    script = "\n".join(parser.script_text)
    for token in ("localStorage", "clipboard", "sendFollowUpMessage", "Blob", "reset"):
        if token not in script:
            errors.append(f"interaction script is missing {token}")
    if not all(corner in script for corner in SERIALIZATION_ORDER):
        errors.append("interaction script does not expose NW, NE, SE, SW serialization")

    target_sources = {
        row.attributes.get("data-source-key") for row in parser.figures
        if row.attributes.get("data-role") == "target"
        and row.attributes.get("data-source-key")
    }
    references_by_scope: dict[tuple[str, str], set[str]] = {}
    unavailable_by_scope: dict[tuple[str, str], set[str]] = {}
    for figure in parser.figures:
        attrs = figure.attributes
        role = attrs.get("data-role")
        if role not in {"target", "reference", "context"}:
            errors.append(f"{figure.question}/{figure.row} figure lacks valid data-role")
        if figure.image is None:
            errors.append(f"{figure.question}/{figure.row} figure has no image")
            continue
        caption = " ".join(part.strip() for part in figure.text if part.strip())
        unavailable = attrs.get("data-evidence-unavailable") == "true"
        if not unavailable and "Uniform:" not in caption and "NW:" not in caption:
            errors.append(
                f"{figure.question}/{figure.row} comparison tile lacks visible uniform/corner mapping")
        if "Native " not in caption or "displayed 256x256" not in caption:
            errors.append(f"{figure.question}/{figure.row} comparison tile lacks visible native/display size")
        source = html_path.parent / figure.image.get("src", "")
        if not source.is_file():
            errors.append(f"missing image {figure.image.get('src', '')}")
            continue
        try:
            with Image.open(source) as image:
                actual = image.size
        except OSError as error:
            errors.append(f"unreadable image {source}: {error}")
            continue
        declared = (int(figure.image.get("width", "0") or 0),
                    int(figure.image.get("height", "0") or 0))
        if actual != declared:
            errors.append(f"{source.name} actual image size {actual} differs from declared {declared}")
        for name in ("data-source-key", "data-variant", "data-comparison-kind",
                     "data-native-width", "data-native-height"):
            if not attrs.get(name):
                errors.append(f"{figure.question}/{figure.row} figure lacks {name}")
        if role == "reference" and attrs.get("data-trusted") == "true":
            if attrs.get("data-source-key") in target_sources:
                errors.append(f"questioned source {attrs.get('data-source-key')} is reused as trusted reference")
            option = attrs.get("data-option", "")
            if not option:
                errors.append(f"trusted reference {attrs.get('data-source-key')} lacks data-option")
            references_by_scope.setdefault(
                (figure.question, figure.section, attrs.get("data-variant", "")), set()).add(option)
        elif role == "reference" and unavailable:
            option = attrs.get("data-option", "")
            if not option:
                errors.append(
                    f"unavailable reference {attrs.get('data-source-key')} lacks data-option")
            unavailable_by_scope.setdefault(
                (figure.question, figure.section, attrs.get("data-variant", "")), set()).add(option)

    for select in parser.selects:
        if not select.field:
            errors.append(f"question {select.question} has an answer without data-field")
        if select.field.startswith("hd-") or select.field.startswith("original-"):
            errors.append(f"{select.question}/{select.field} incorrectly makes asset variant part of semantic identity")
        concrete = [value for value in select.options if is_concrete_option(value)]
        if len(concrete) < 2:
            errors.append(f"{select.question}/{select.field} offers fewer than two concrete choices")
        for variant in ("hd", "original"):
            visible = references_by_scope.get((select.question, select.section, variant), set())
            unavailable = unavailable_by_scope.get(
                (select.question, select.section, variant), set())
            if select.review_kind == "assignment":
                if not select.current_option:
                    errors.append(f"{select.question}/{select.field} lacks data-current-option")
                elif (is_concrete_option(select.current_option)
                      and select.current_option not in visible
                      and select.current_option not in unavailable):
                    errors.append(
                        f"{select.question}/{select.field} current assignment {select.current_option!r} "
                        f"has no visible trusted {variant} reference")
            for option in concrete:
                if option not in visible and option not in unavailable:
                    errors.append(
                        f"{select.question}/{select.field} option {option!r} has no visible trusted {variant} reference")

    rows: dict[tuple[str, str], list[FigureRecord]] = {}
    for row in parser.figures:
        rows.setdefault((row.question, row.row), []).append(row)
    for key, figures in rows.items():
        expected = None
        for figure in figures:
            attrs = figure.attributes
            signature = (attrs.get("data-variant"), attrs.get("data-comparison-kind"))
            if expected is None:
                expected = signature
            elif signature != expected:
                errors.append(f"{key[0]}/{key[1]} mixes asset variant or comparison kind")
                break

    if handoff and manifest.get("browserValidation", {}).get("status") != "passed":
        errors.append("handoff requires browserValidation.status=passed")
    return errors


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("html", type=Path)
    parser.add_argument("--manifest", type=Path)
    parser.add_argument("--handoff", action="store_true")
    args = parser.parse_args()
    manifest = args.manifest or args.html.with_suffix(".manifest.json")
    errors = validate_questionnaire(args.html, manifest, handoff=args.handoff)
    if errors:
        print(f"QUESTIONNAIRE CONTRACT FAIL ({len(errors)} error(s))")
        for error in errors:
            print(f"- {error}")
        return 1
    print("QUESTIONNAIRE CONTRACT PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
