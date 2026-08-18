#!/usr/bin/env python3
"""Validate a generated blend-mask questionnaire and every indexed visual."""

from __future__ import annotations

import argparse
import csv
import hashlib
from html import unescape
import json
import re
from collections import defaultdict
from pathlib import Path
from urllib.parse import unquote, urlparse

from PIL import Image, ImageChops

from render_blend_mask_questionnaire import (
    TILE_SIZE, _application_key, _canonical_mask_hash, _raw_mask_hash,
    _loaded_sheet, _sheet_cell, _slug,
)


def _rgb_hash(image: Image.Image) -> str:
    return hashlib.sha256(image.convert("RGB").tobytes()).hexdigest()


def _validate_html_references(
        document: str, questionnaire: Path, errors: list[str]) -> int:
    artifact_root = questionnaire.parent.parent.resolve()
    references: set[str] = set()
    references.update(unescape(value) for value in re.findall(
        r'\bsrc="([^"]+)"', document))
    references.update(unescape(value) for value in re.findall(
        r'\burl\([\'\"]?([^\'\")]+)', document))
    checked = 0
    for reference in sorted(references):
        parsed = urlparse(reference)
        if (parsed.scheme or parsed.netloc or reference.startswith(("/", "\\"))
                or re.match(r"^[A-Za-z]:", reference)):
            errors.append(f"HTML reference is not relocatable: {reference}")
            continue
        candidate = (questionnaire.parent / unquote(parsed.path)).resolve()
        try:
            candidate.relative_to(artifact_root)
        except ValueError:
            errors.append(f"HTML reference escapes artifact root: {reference}")
            continue
        if not candidate.is_file():
            errors.append(f"HTML reference is missing: {reference}")
            continue
        checked += 1
    return checked


def validate(review: Path, questionnaire: Path) -> dict:
    review = review.resolve()
    questionnaire = questionnaire.resolve()
    _loaded_sheet.cache_clear()
    with (review / "index.csv").open(encoding="utf-8", newline="") as handle:
        rows = list(csv.DictReader(handle))
    document = questionnaire.read_text(encoding="utf-8")
    analysis = json.loads(
        (questionnaire.parent / "blend-mask-analysis.json").read_text(
            encoding="utf-8"))
    if analysis.get("rowSelection") == "unapproved-only":
        rows = [
            row for row in rows
            if row.get("missing", "").casefold() not in {"1", "true", "yes"}
            and "-approved" not in row.get("provenance", "").casefold()
        ]
    errors: list[str] = []
    application_keys: set[str] = set()
    uniform_files: set[str] = set()
    sheet_dimensions: dict[str, tuple[int, int]] = {}
    uniform_images: dict[str, Image.Image] = {}
    pure_pixel_identity_checks = 0
    heatmap_identity_checks = 0
    group_hashes: dict[tuple[str, str], set[str]] = defaultdict(set)
    missing_fields = (
        "missing", "heatmap_missing", "result_example_missing",
        "linear_result_missing")
    sheet_fields = (
        "sheet", "heatmap_sheet", "result_example_sheet",
        "linear_result_sheet")
    for number, row in enumerate(rows, 1):
        key = _application_key(row)
        if key in application_keys:
            errors.append(f"duplicate key {key}")
        application_keys.add(key)
        for field in missing_fields:
            if row.get(field) != "0":
                errors.append(f"row {number} {field}={row.get(field)!r}")
        for field, hash_field in (
                ("owner_uniform", "owner_uniform_sha256"),
                ("support_uniform", "support_uniform_sha256")):
            relative = row[field]
            uniform_files.add(relative)
            path = review / relative
            if not path.is_file():
                errors.append(f"missing uniform {relative}")
                continue
            actual_hash = hashlib.sha256(path.read_bytes()).hexdigest()
            if actual_hash.lower() != row[hash_field].lower():
                errors.append(f"uniform hash mismatch {relative}")
            with Image.open(path) as image:
                if image.size != (TILE_SIZE, TILE_SIZE):
                    errors.append(f"uniform {relative} is {image.size}")
                image.load()
                uniform_images[relative] = image.convert("RGB")
        for field in sheet_fields:
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
            if (width < (column + 1) * TILE_SIZE or
                    height < (sheet_row + 1) * TILE_SIZE):
                errors.append(
                    f"out-of-range sprite {relative}:{sheet_row},{column}")
        try:
            mask = _sheet_cell(review, row["sheet"], int(row["row"]),
                               int(row["column"]), "L")
            if _raw_mask_hash(mask).lower() != row["mask_sha256"].lower():
                errors.append(f"row {number} mask hash mismatch")
            group_hashes[(row["mask_family"], row["topology"])].add(
                _canonical_mask_hash(mask, row["topology"]))
            encoded = _sheet_cell(
                review, row["result_example_sheet"], int(row["row"]),
                int(row["column"]), "RGB")
            linear = _sheet_cell(
                review, row["linear_result_sheet"], int(row["row"]),
                int(row["column"]), "RGB")
            heatmap = _sheet_cell(
                review, row["heatmap_sheet"], int(row["row"]),
                int(row["column"]), "RGB")
            for image, field in (
                    (heatmap, "heatmap_sha256"),
                    (encoded, "result_example_sha256"),
                    (linear, "linear_result_sha256")):
                if _rgb_hash(image).lower() != row.get(field, "").lower():
                    errors.append(f"row {number} {field} mismatch")
            channel_values = (
                [42 if value >= 204 else 35 if value <= 51 else 236
                 for value in range(256)],
                [184 if value >= 204 else 52 if value <= 51 else 154
                 for value in range(256)],
                [92 if value >= 204 else 96 if value <= 51 else 52
                 for value in range(256)],
            )
            expected_heatmap = Image.merge(
                "RGB", tuple(mask.point(values) for values in channel_values))
            heatmap_identity_checks += TILE_SIZE * TILE_SIZE
            if heatmap.tobytes() != expected_heatmap.tobytes():
                errors.append(
                    f"row {number} heatmap does not encode its mask cell")
            owner = uniform_images.get(row["owner_uniform"])
            support = uniform_images.get(row["support_uniform"])
            if owner is not None and support is not None:
                pure_owner = mask.point(
                    [255 if value == 255 else 0 for value in range(256)])
                pure_support = mask.point(
                    [255 if value == 0 else 0 for value in range(256)])
                pure_pixel_identity_checks += 2 * sum(
                    value in (0, 255) for value in mask.tobytes())
                black = Image.new("RGB", mask.size)
                identity_error = any(
                    Image.composite(
                        ImageChops.difference(result, expected), black,
                        selection).getbbox() is not None
                    for result in (encoded, linear)
                    for expected, selection in (
                        (owner, pure_owner), (support, pure_support)))
                if identity_error:
                    errors.append(
                        f"row {number} pure mask pixels do not match the "
                        "declared owner/support uniforms")
        except (FileNotFoundError, ValueError) as error:
            errors.append(f"row {number} mask cell: {error}")

    for logical, hashes in group_hashes.items():
        if len(hashes) != 1:
            errors.append(
                f"{logical[0]}/{logical[1]} has {len(hashes)} unrelated masks")
    for needle in (
            'class="answer form-select"', 'class="note form-control"',
            'id="submit-answers"', "window.openai.sendFollowUpMessage",
            "localStorage.setItem", "Apply only explicit mask decisions",
            'class="visual threshold-heatmap"',
            'class="visual resulting-blend encoded-result"',
            'class="visual resulting-blend linear-result"'):
        if needle not in document:
            errors.append(f"HTML missing contract marker {needle}")
    global_questions = analysis.get("globalQuestions", [])
    expected_global_keys = [question.get("key")
                            for question in global_questions]
    if expected_global_keys == ["blend-compositor-gamma"]:
        if 'data-key="blend-compositor-gamma"' not in document:
            errors.append("HTML missing global compositor question")
    elif expected_global_keys:
        errors.append("analysis contains an unknown global question")
    elif 'data-key="blend-compositor-gamma"' in document:
        errors.append("HTML contains an undeclared global compositor question")
    if "Not answered" in document:
        errors.append("HTML contains forbidden Not answered option")
    html_references = _validate_html_references(
        document, questionnaire, errors)

    questions = analysis.get("questions", [])
    grouped_keys = [question.get("key", "") for question in questions]
    if len(grouped_keys) != len(set(grouped_keys)):
        errors.append("analysis has duplicate grouped keys")
    expected_group_keys = {
        f"mask-family-{_slug(family)}-{_slug(topology)}"
        for family, topology in group_hashes
    }
    if set(grouped_keys) != expected_group_keys:
        errors.append(
            f"analysis group coverage differs: {len(grouped_keys)} != "
            f"{len(expected_group_keys)}")
    for key in grouped_keys:
        if f'data-key="{key}"' not in document:
            errors.append(f"HTML missing grouped question {key}")

    analyzed_applications = {
        application["key"]
        for question in questions
        for application in question.get("applications", [])
    }
    if analyzed_applications != application_keys:
        errors.append(
            "analysis application coverage differs: "
            f"{len(analyzed_applications)} != {len(application_keys)}")
    for question in questions:
        applications = question.get("applications", [])
        examples = question.get("examples", [])
        if question.get("applicationCount") != len(applications):
            errors.append(f'{question.get("key")} applicationCount mismatch')
        expected_examples = min(4, len(applications))
        if len(examples) != expected_examples:
            errors.append(
                f'{question.get("key")} has {len(examples)} examples; '
                f"expected {expected_examples}")
        expected_hash = next(iter(group_hashes.get(
            (question.get("maskFamily"), question.get("topology")), {""})))
        if question.get("canonicalMaskSha256") != expected_hash:
            errors.append(f'{question.get("key")} canonical hash mismatch')
    question_count = document.count('class="mask-question"')
    if question_count != len(questions):
        errors.append(
            f"question count {question_count} != analysis questions "
            f"{len(questions)}")
    if analysis.get("maskQuestionCount") != len(questions):
        errors.append("analysis maskQuestionCount mismatch")
    prior_review = analysis.get("priorReview", {})
    if prior_review.get("explicitRejections", 0) != sum(
            len(question.get("priorR41Decisions", []))
            for question in questions):
        errors.append("analysis prior-review rejection count mismatch")
    if prior_review.get("affectedGroups", 0) != sum(
            bool(question.get("priorR41Decisions", []))
            for question in questions):
        errors.append("analysis prior-review group count mismatch")
    if errors:
        raise ValueError("\n".join(errors[:100]))
    return {
        "status": "PASS",
        "maskQuestions": len(questions),
        "globalQuestions": len(global_questions),
        "applications": len(rows),
        "keys": len(grouped_keys),
        "uniformTiles": len(uniform_files),
        "spriteSheets": len(sheet_dimensions),
        "maskFamilies": len({row["mask_family"] for row in rows}),
        "purePixelIdentityChecks": pure_pixel_identity_checks,
        "heatmapIdentityChecks": heatmap_identity_checks,
        "htmlReferences": html_references,
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
