#!/usr/bin/env python3
"""Focused regression checks for the blend-mask review questionnaire."""

from __future__ import annotations

import csv
import hashlib
import json
from pathlib import Path
import shutil
import tempfile
import unittest

from PIL import Image, ImageOps

from render_blend_mask_questionnaire import render
from validate_blend_mask_questionnaire import validate


class BlendMaskQuestionnaireTests(unittest.TestCase):
    def _fixture(self, root: Path) -> tuple[Path, list[str]]:
        review = root / "blend-mask-review"
        uniforms = review / "uniform-materials"
        uniforms.mkdir(parents=True)
        fields = [
            "sheet", "row", "column", "owner_material_id",
            "owner_material", "owner_style", "mask_family",
            "support_scenario", "support_material_id", "support_material",
            "topology", "variation", "provenance", "mask_sha256",
            "missing", "heatmap_sheet", "heatmap_sha256", "heatmap_missing",
            "result_example_sheet", "result_example_sha256",
            "result_example_missing", "linear_result_sheet",
            "linear_result_sha256", "linear_result_missing", "owner_uniform",
            "owner_uniform_sha256", "support_uniform",
            "support_uniform_sha256",
        ]
        uniform_hashes: dict[str, str] = {}
        for index in range(10):
            path = uniforms / f"uniform-{index}.png"
            Image.new("RGB", (256, 256), (20 + index, 80, 40)).save(path)
            uniform_hashes[path.name] = hashlib.sha256(path.read_bytes()).hexdigest()

        mask_a = Image.new("L", (256, 256), 0)
        for y in range(256):
            for x in range(256):
                mask_a.putpixel((x, y), 255 if y < 96 + x // 8 else 0)
        mask_b = ImageOps.invert(mask_a.rotate(180))
        mask_c = Image.new("L", (256, 256), 0)
        for y in range(256):
            for x in range(256):
                mask_c.putpixel((x, y), 255 if x < 88 + y // 9 else 0)
        sheets: dict[str, Image.Image] = {
            "masks.png": Image.new("RGB", (512, 512), (0, 0, 0)),
            "heatmaps.png": Image.new("RGB", (512, 512), (35, 52, 96)),
            "results.png": Image.new("RGB", (512, 512), (0, 0, 0)),
            "linear.png": Image.new("RGB", (512, 512), (0, 0, 0)),
        }
        sheets["masks.png"].paste(mask_a.convert("RGB"), (0, 0))
        sheets["masks.png"].paste(mask_b.convert("RGB"), (256, 0))
        sheets["masks.png"].paste(mask_c.convert("RGB"), (0, 256))
        owner_pixels = Image.new("RGB", (256, 256), (20, 80, 40))
        support_pixels = Image.new("RGB", (256, 256), (25, 80, 40))
        cell_images: dict[tuple[int, int], tuple[Image.Image, Image.Image,
                                                 Image.Image]] = {}
        for mask, position in ((mask_a, (0, 0)), (mask_b, (256, 0)),
                               (mask_c, (0, 256))):
            channels = tuple(mask.point([
                color[channel] if value >= 204 else
                (35, 52, 96)[channel] if value <= 51 else
                (236, 154, 52)[channel]
                for value in range(256)])
                for channel, color in enumerate([(42, 184, 92)] * 3))
            heatmap = Image.merge("RGB", channels)
            outcome = Image.composite(owner_pixels, support_pixels, mask)
            sheets["heatmaps.png"].paste(heatmap, position)
            sheets["results.png"].paste(outcome, position)
            sheets["linear.png"].paste(outcome, position)
            cell_images[(position[0] // 256, position[1] // 256)] = (
                heatmap, outcome, outcome)
        for name, image in sheets.items():
            image.save(review / name)

        def file_hash(index: int) -> str:
            return uniform_hashes[f"uniform-{index}.png"]

        def cell_hash(image: Image.Image) -> str:
            return hashlib.sha256(image.convert("RGB").tobytes()).hexdigest()

        rows: list[dict] = []
        for index in range(5):
            column = index % 2
            mask = mask_a if column == 0 else mask_b
            rows.append({
                "sheet": "masks.png", "row": 0, "column": column,
                "owner_material_id": 100 + index,
                "owner_material": f"Grass / Example {index}",
                "owner_style": "grass-strands",
                "mask_family": "grass-strands",
                "support_scenario": (
                    "cross-style" if index < 3 else "same-style"),
                "support_material_id": 200 + index,
                "support_material": f"Ground / Example {index}",
                "topology": "edge-2of4-v1", "variation": 0,
                "provenance": "canonical-generated:grass-strands:edge-2of4-v1",
                "mask_sha256": hashlib.sha256(mask.tobytes()).hexdigest(),
                "missing": 0, "heatmap_sheet": "heatmaps.png",
                "heatmap_sha256": cell_hash(cell_images[(column, 0)][0]),
                "heatmap_missing": 0,
                "result_example_sheet": "results.png",
                "result_example_sha256": cell_hash(
                    cell_images[(column, 0)][1]),
                "result_example_missing": 0,
                "linear_result_sheet": "linear.png",
                "linear_result_sha256": cell_hash(
                    cell_images[(column, 0)][2]),
                "linear_result_missing": 0,
                "owner_uniform": "uniform-materials/uniform-0.png",
                "owner_uniform_sha256": file_hash(0),
                "support_uniform": "uniform-materials/uniform-5.png",
                "support_uniform_sha256": file_hash(5),
            })
        rows.append({
            **rows[0], "row": 1, "column": 0,
            "owner_material_id": 999, "owner_material": "Stone / Separate",
            "owner_style": "stone-fracture", "mask_family": "stone-fracture",
            "support_material_id": 888, "support_material": "Ground / Separate",
            "mask_sha256": hashlib.sha256(mask_c.tobytes()).hexdigest(),
            "heatmap_sha256": cell_hash(cell_images[(0, 1)][0]),
            "result_example_sha256": cell_hash(cell_images[(0, 1)][1]),
            "linear_result_sha256": cell_hash(cell_images[(0, 1)][2]),
            "provenance": "canonical-generated:stone-fracture:edge-2of4-v1",
        })
        with (review / "index.csv").open(
                "w", encoding="utf-8", newline="") as handle:
            writer = csv.DictWriter(handle, fieldnames=fields)
            writer.writeheader()
            writer.writerows(rows)
        return review, fields

    def test_family_topology_is_one_question_with_four_complete_examples(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            review, _ = self._fixture(root)
            result = render(review, root / "questionnaire", "R42-MASK")
            document = Path(result["html"]).read_text(encoding="utf-8")
            analysis = json.loads(
                Path(result["analysis"]).read_text(encoding="utf-8"))

            self.assertEqual(result["applications"], 6)
            self.assertEqual(result["questions"], 2)
            self.assertEqual(result["globalQuestions"], 1)
            self.assertEqual(document.count('class="mask-question"'), 2)
            self.assertEqual(document.count('class="application-example"'), 5)
            self.assertIn("5 material-pair applications", document)
            self.assertIn("4 representative outcomes shown", document)
            self.assertIn(
                "grass-strands · edge 2 of 4 · variation 1", document)
            self.assertNotIn(
                "grass-strands · cross-style · edge 2 of 4", document)
            grass = next(q for q in analysis["questions"]
                         if q["maskFamily"] == "grass-strands")
            self.assertEqual(grass["applicationCount"], 5)
            self.assertEqual(len(grass["examples"]), 4)
            self.assertEqual(len(grass["rawMaskSha256s"]), 2)
            self.assertEqual(document.count(
                'class="visual ownership-mask"'), 2)
            self.assertEqual(document.count(
                'class="visual threshold-heatmap"'), 2)
            self.assertEqual(document.count(
                'class="visual resulting-blend encoded-result"'), 5)
            self.assertEqual(document.count(
                'class="visual resulting-blend linear-result"'), 5)
            self.assertIn('data-key="blend-compositor-gamma"', document)
            self.assertIn("background-position:-256px 0px", document)

    def test_previous_rejections_are_context_only_and_keeps_are_ignored(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            review, _ = self._fixture(root)
            answers = root / "answers.txt"
            answers.write_text(
                "Blend-mask R41-MASK focused review decisions:\n"
                "mask-family-grass-strands-cross-style-edge-2of4-v1-aaaaaaaaaaaa: Keep current: Zone1:4\n"
                "mask-family-grass-strands-cross-style-edge-2of4-v1-bbbbbbbbbbbb: Reject: bad shape/coverage — covers all 4 corners\n"
                "mask-family-grass-strands-same-style-edge-2of4-v1-cccccccccccc: Reject: bad straight cut — too sharp\n",
                encoding="utf-8")
            result = render(
                review, root / "questionnaire", "R42-MASK", answers)
            document = Path(result["html"]).read_text(encoding="utf-8")
            analysis = json.loads(
                Path(result["analysis"]).read_text(encoding="utf-8"))

            self.assertEqual(result["priorRejectionGroups"], 1)
            self.assertEqual(result["priorRejections"], 2)
            self.assertEqual(result["ignoredPriorKeeps"], 1)
            self.assertEqual(result["unmappedPriorRejections"], [])
            grass = next(q for q in analysis["questions"]
                         if q["maskFamily"] == "grass-strands")
            self.assertEqual(len(grass["priorR41Notes"]), 2)
            self.assertEqual(len(grass["priorR41Decisions"]), 2)
            self.assertEqual(analysis["priorReview"], {
                "explicitRejections": 2,
                "affectedGroups": 1,
                "ignoredDefaultKeeps": 1,
                "unmappedExplicitRejectionKeys": [],
            })
            self.assertIn("R41 predecessor rejections", document)
            self.assertIn("covers all 4 corners", document)
            self.assertIn("too sharp", document)
            self.assertNotIn("Zone1:4", document)
            self.assertNotIn('value="Reject: bad shape/coverage" selected', document)

    def test_unrelated_masks_in_one_family_topology_are_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            review, fields = self._fixture(root)
            index_path = review / "index.csv"
            with index_path.open(encoding="utf-8", newline="") as handle:
                rows = list(csv.DictReader(handle))
            unrelated = Image.new("L", (256, 256), 127)
            with Image.open(review / "masks.png") as source:
                sheet = source.copy()
            sheet.paste(unrelated.convert("RGB"), (256, 256))
            sheet.save(review / "masks.png")
            rows[1]["row"] = "1"
            rows[1]["column"] = "1"
            rows[1]["mask_sha256"] = hashlib.sha256(
                unrelated.tobytes()).hexdigest()
            with index_path.open("w", encoding="utf-8", newline="") as handle:
                writer = csv.DictWriter(handle, fieldnames=fields)
                writer.writeheader()
                writer.writerows(rows)
            with self.assertRaisesRegex(ValueError, "unrelated candidate masks"):
                render(review, root / "questionnaire", "R42-MASK")

    def test_validator_checks_exact_uniform_hashes(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            review, _ = self._fixture(root)
            result = render(review, root / "questionnaire", "R42-MASK")
            report = validate(review, Path(result["html"]))
            self.assertEqual(report["status"], "PASS")
            self.assertGreater(report["heatmapIdentityChecks"], 0)
            self.assertGreater(report["htmlReferences"], 0)
            relocated = root / "relocated"
            shutil.copytree(review, relocated / "blend-mask-review")
            shutil.copytree(root / "questionnaire", relocated / "questionnaire")
            relocated_report = validate(
                relocated / "blend-mask-review",
                relocated / "questionnaire" /
                "blend-mask-r42-mask-questionnaire.html")
            self.assertEqual(relocated_report["status"], "PASS")
            Image.new("RGB", (256, 256), (255, 0, 255)).save(
                review / "uniform-materials" / "uniform-0.png")
            with self.assertRaisesRegex(ValueError, "uniform hash mismatch"):
                validate(review, Path(result["html"]))

    def test_validator_rejects_corrupt_heatmap_and_missing_html_reference(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            review, _ = self._fixture(root)
            result = render(review, root / "questionnaire", "R42-MASK")
            heatmap_path = review / "heatmaps.png"
            with Image.open(heatmap_path) as source:
                heatmap = source.copy()
            heatmap.putpixel((12, 12), (255, 0, 255))
            heatmap.save(heatmap_path)
            with self.assertRaisesRegex(ValueError, "heatmap.*mismatch|heatmap does"):
                validate(review, Path(result["html"]))
            missing_root = root / "missing-case"
            missing_review, _ = self._fixture(missing_root)
            missing_result = render(
                missing_review, missing_root / "questionnaire", "R42-MASK")
            html_path = Path(missing_result["html"])
            html_path.write_text(
                html_path.read_text(encoding="utf-8").replace(
                    "../blend-mask-review/masks.png",
                    "../blend-mask-review/missing.png", 1),
                encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "HTML reference is missing"):
                validate(missing_review, html_path)


if __name__ == "__main__":
    unittest.main()
