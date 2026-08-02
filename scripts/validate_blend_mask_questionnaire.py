#!/usr/bin/env python3
"""Validate a generated blend-mask questionnaire and all indexed visuals."""

from __future__ import annotations

import argparse
import csv
import json
from pathlib import Path

from PIL import Image


def validate(review: Path, questionnaire: Path) -> dict:
    review = review.resolve()
    questionnaire = questionnaire.resolve()
    with (review / "index.csv").open(encoding="utf-8", newline="") as handle:
        rows = list(csv.DictReader(handle))
    document = questionnaire.read_text(encoding="utf-8")
    errors: list[str] = []
    keys: set[str] = set()
    uniform_files: set[str] = set()
    sheet_dimensions: dict[str, tuple[int, int]] = {}
    missing_fields = ("missing", "source_example_missing",
                      "result_example_missing")
    for number, row in enumerate(rows, 1):
        key = (f'mask-{row["owner_material_id"]}-'
               f'{row["support_scenario"]}-{row["topology"]}')
        if key in keys:
            errors.append(f"duplicate key {key}")
        keys.add(key)
        if f'data-key="{key}"' not in document:
            errors.append(f"HTML missing {key}")
        for field in missing_fields:
            if row.get(field) != "0":
                errors.append(f"row {number} {field}={row.get(field)!r}")
        for field in ("owner_uniform", "support_uniform"):
            relative = row[field]
            uniform_files.add(relative)
            path = review / relative
            if not path.is_file():
                errors.append(f"missing uniform {relative}")
                continue
            with Image.open(path) as image:
                if image.size != (256, 256):
                    errors.append(f"uniform {relative} is {image.size}")
        for field in ("sheet", "source_example_sheet", "result_example_sheet"):
            relative = row[field]
            path = review / relative
            if not path.is_file():
                errors.append(f"missing sheet {relative}")
                continue
            if relative not in sheet_dimensions:
                with Image.open(path) as image:
                    sheet_dimensions[relative] = image.size
            width, height = sheet_dimensions[relative]
            column, sheet_row = int(row["column"]), int(row["row"])
            if width < (column + 1) * 256 or height < (sheet_row + 1) * 256:
                errors.append(f"out-of-range sprite {relative}:{sheet_row},{column}")
    for needle in ("class=\"answer form-select\"", "class=\"note form-control\"",
                   "id=\"submit-answers\"", "window.openai.sendFollowUpMessage",
                   "localStorage.setItem", "Apply only explicit mask decisions"):
        if needle not in document:
            errors.append(f"HTML missing contract marker {needle}")
    if "Not answered" in document:
        errors.append("HTML contains forbidden Not answered option")
    question_count = document.count('class="mask-question"')
    if question_count != len(rows):
        errors.append(f"question count {question_count} != index rows {len(rows)}")
    if errors:
        raise ValueError("\n".join(errors[:100]))
    return {
        "status": "PASS",
        "questions": len(rows),
        "keys": len(keys),
        "uniformTiles": len(uniform_files),
        "spriteSheets": len(sheet_dimensions),
        "missingVisuals": 0,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--review", required=True, type=Path)
    parser.add_argument("--questionnaire", required=True, type=Path)
    args = parser.parse_args()
    print(json.dumps(validate(args.review, args.questionnaire), indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
