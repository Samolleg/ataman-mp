#!/usr/bin/env python3
"""Focused regression checks for the blend-mask review questionnaire."""

from __future__ import annotations

import csv
import json
from pathlib import Path
import tempfile
import unittest

from PIL import Image

from render_blend_mask_questionnaire import render


class BlendMaskQuestionnaireTests(unittest.TestCase):
    def _fixture(self, root: Path) -> tuple[Path, list[str]]:
        review = root / "blend-mask-review"
        uniforms = review / "uniform-materials"
        uniforms.mkdir(parents=True)
        fields = [
            "sheet", "row", "column", "owner_material_id",
            "owner_material", "owner_style", "support_scenario",
            "support_material_id", "support_material", "topology",
            "variation", "provenance", "mask_sha256", "missing",
            "source_example_sheet", "source_example_missing",
            "result_example_sheet", "result_example_missing",
            "owner_uniform", "support_uniform",
        ]
        for index in range(10):
            Image.new("RGB", (16, 16), (20 + index, 80, 40)).save(
                uniforms / f"uniform-{index}.png")
        for name, color in (("masks.png", (255, 255, 255)),
                            ("sources.png", (70, 90, 110)),
                            ("results.png", (45, 120, 65))):
            Image.new("RGB", (512, 512), color).save(review / name)

        rows = []
        for index in range(5):
            rows.append({
                "sheet": "masks.png", "row": index % 2,
                "column": index % 2,
                "owner_material_id": 100 + index,
                "owner_material": f"Grass / Example {index}",
                "owner_style": "grass-strands",
                "support_scenario": "cross-style",
                "support_material_id": 200 + index,
                "support_material": f"Ground / Example {index}",
                "topology": "edge-2of4-v1", "variation": 0,
                "provenance": "ZoneTest:17", "mask_sha256": "a" * 64,
                "missing": 0, "source_example_sheet": "sources.png",
                "source_example_missing": 0,
                "result_example_sheet": "results.png",
                "result_example_missing": 0,
                "owner_uniform": f"uniform-materials/uniform-{index}.png",
                "support_uniform": f"uniform-materials/uniform-{index + 5}.png",
            })
        rows.append({**rows[0], "owner_material_id": 999,
                     "owner_material": "Stone / Separate",
                     "owner_style": "stone-cracked",
                     "mask_sha256": "b" * 64,
                     "provenance": "ZoneOther:4"})
        with (review / "index.csv").open("w", encoding="utf-8", newline="") as handle:
            writer = csv.DictWriter(handle, fieldnames=fields)
            writer.writeheader()
            writer.writerows(rows)
        return review, fields

    def test_shared_mask_is_one_question_with_four_concrete_examples(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            review, _ = self._fixture(root)
            result = render(review, root / "questionnaire", "R41-MASK")
            document = Path(result["html"]).read_text(encoding="utf-8")
            analysis = json.loads(Path(result["analysis"]).read_text(encoding="utf-8"))

            self.assertEqual(result["applications"], 6)
            self.assertEqual(result["questions"], 2)
            self.assertEqual(document.count('class="mask-question"'), 2)
            self.assertEqual(document.count('class="application-example"'), 5)
            self.assertIn("5 material-pair applications", document)
            self.assertIn("4 representative outcomes shown", document)
            self.assertIn("grass-strands · cross-style · edge 2 of 4 · variation 1", document)
            self.assertIn("Authored source evidence", document)
            self.assertNotIn("1. Grass / Example 0", document)
            grass = next(q for q in analysis["questions"]
                         if q["ownerStyle"] == "grass-strands")
            self.assertEqual(grass["applicationCount"], 5)
            self.assertEqual(len(grass["examples"]), 4)
            self.assertEqual(len(grass["applications"]), 5)
            self.assertEqual(document.count('class="visual ownership-mask"'), 2)
            self.assertEqual(document.count('class="visual authored-source"'), 2)
            self.assertEqual(document.count('class="visual resulting-blend"'), 5)
            self.assertIn("background-position:-256px -256px", document)

    def test_old_per_material_answers_migrate_to_shared_decisions(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            review, _ = self._fixture(root)
            answers = root / "answers.txt"
            answers.write_text(
                "Blend-mask R39-MASK focused review decisions:\n"
                "mask-100-cross-style-edge-2of4-v1: Reject: bad shape/coverage — covers all 4 corners\n"
                "mask-101-cross-style-edge-2of4-v1: Reject: bad shape/coverage — covers all 4 corners\n",
                encoding="utf-8")
            result = render(review, root / "questionnaire", "R41-MASK", answers)
            document = Path(result["html"]).read_text(encoding="utf-8")
            analysis = json.loads(Path(result["analysis"]).read_text(encoding="utf-8"))

            self.assertEqual(result["migratedDecisions"], 1)
            self.assertIn('value="Reject: bad shape/coverage" selected', document)
            self.assertIn('value="covers all 4 corners"', document)
            migrated = next(q for q in analysis["questions"] if q["migratedAnswer"])
            self.assertEqual(migrated["migratedAnswer"], "Reject: bad shape/coverage")
            self.assertEqual(migrated["migratedNote"], "covers all 4 corners")
            self.assertFalse(migrated["migrationConflict"])

    def test_conflicting_old_answers_are_flagged_without_guessing(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            review, _ = self._fixture(root)
            answers = root / "answers.txt"
            answers.write_text(
                "mask-100-cross-style-edge-2of4-v1: Reject: wrong orientation\n"
                "mask-101-cross-style-edge-2of4-v1: Reject: bad shape/coverage\n",
                encoding="utf-8")
            result = render(review, root / "questionnaire", "R41-MASK", answers)
            document = Path(result["html"]).read_text(encoding="utf-8")
            analysis = json.loads(Path(result["analysis"]).read_text(encoding="utf-8"))

            conflicted = next(q for q in analysis["questions"]
                              if q["ownerStyle"] == "grass-strands")
            self.assertTrue(conflicted["migrationConflict"])
            self.assertEqual(conflicted["migratedAnswer"], "")
            self.assertIn("Conflicting previous per-material answers", document)


if __name__ == "__main__":
    unittest.main()
