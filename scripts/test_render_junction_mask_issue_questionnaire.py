#!/usr/bin/env python3
"""Regressions for the compact reported-junction mask questionnaire."""

from __future__ import annotations

import json
from pathlib import Path
import tempfile
import unittest

from PIL import Image

from render_junction_mask_issue_questionnaire import render


class JunctionMaskIssueQuestionnaireTests(unittest.TestCase):
    def test_groups_questions_and_limits_genuine_examples(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            images = root / "images"
            images.mkdir()
            for name in ("mask.png", "result.png", "u1.png", "u2.png",
                         "u3.png"):
                Image.new("RGB", (256, 256), "white").save(images / name)
            signature = {
                "northWest": {"material_id": 1, "material": "Grass / A"},
                "northEast": {"material_id": 2, "material": "Ground / B"},
                "southEast": {"material_id": 3, "material": "Stone / C"},
                "southWest": {"material_id": 1, "material": "Grass / A"},
            }
            components = [
                {"material_id": value, "material": name,
                 "uniform_file": f"images/u{value}.png"}
                for value, name in ((1, "Grass / A"), (2, "Ground / B"),
                                    (3, "Stone / C"))]
            examples = []
            group_examples = []
            for index in range(7):
                example_id = f"mark-{index}-cell-{index}-4"
                examples.append({
                    "id": example_id, "components": components,
                })
                group_examples.append({
                    "example_id": example_id, "mark_id": index,
                    "cell_x": index, "cell_y": 4,
                    "material": "Grass / A",
                    "mask_file": "images/mask.png",
                    "generated_file": "images/result.png",
                    "provenance": "canonical-generated:grass-strands:diagonal-2of4",
                    "signature": signature,
                })
            manifest = {
                "schema": "ei-junction-mask-issue-review-v1",
                "selected_three_four_material_cells": 7,
                "selected_uniform_signature_cells": 2,
                "selected_two_material_cells": 3,
                "semantic_preset_sha256": "a" * 64,
                "issue_report_sha256": "b" * 64,
                "examples": examples,
                "mask_groups": [{
                    "key": "grass-strands-diagonal-2of4",
                    "style": "grass-strands",
                    "topology": "diagonal-2of4",
                    "examples": group_examples,
                }],
            }
            manifest_path = root / "manifest.json"
            manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
            output = root / "questionnaire"
            result = render(manifest_path, output, "R99-JUNCTION")
            self.assertEqual(result["questions"], 1)
            analysis = json.loads((output / "junction-mask-analysis.json")
                                  .read_text(encoding="utf-8"))
            self.assertEqual(analysis["maskQuestionCount"], 1)
            self.assertEqual(
                analysis["unreviewedSeparateSets"]["uniformSignatureCells"], 2)
            self.assertEqual(
                analysis["unreviewedSeparateSets"]["twoMaterialCells"], 3)
            self.assertEqual(len(analysis["questions"][0]["shownExamples"]), 4)
            page = Path(result["html"]).read_text(encoding="utf-8")
            self.assertEqual(page.count('class="question" data-key='), 1)
            self.assertEqual(page.count('class="application-example"'), 4)
            self.assertIn("EI-HD seam workflow", page)
            self.assertIn("class=\"answer\"", page)
            self.assertIn("class=\"note\"", page)
            self.assertIn("NW", page)
            self.assertIn("SW", page)


if __name__ == "__main__":
    unittest.main()
