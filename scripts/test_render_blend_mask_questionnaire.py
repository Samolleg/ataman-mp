#!/usr/bin/env python3
"""Focused regression checks for the blend-mask review questionnaire."""

from __future__ import annotations

import csv
from pathlib import Path
import tempfile
import unittest

from PIL import Image

from render_blend_mask_questionnaire import render


class BlendMaskQuestionnaireTests(unittest.TestCase):
    def test_every_mask_choice_shows_uniforms_mask_source_and_result(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            review = root / "blend-mask-review"
            output = root / "questionnaire"
            uniforms = review / "uniform-materials"
            uniforms.mkdir(parents=True)
            for name, color in (("owner.png", (20, 150, 40)),
                                ("support.png", (120, 80, 30))):
                Image.new("RGB", (16, 16), color).save(uniforms / name)
            for name, color in (("masks.png", (255, 255, 255)),
                                ("sources.png", (70, 90, 110)),
                                ("results.png", (45, 120, 65))):
                Image.new("RGB", (32, 16), color).save(review / name)

            fields = [
                "sheet", "row", "column", "owner_material_id",
                "owner_material", "owner_style", "support_scenario",
                "support_material_id", "support_material", "topology",
                "variation", "provenance", "mask_sha256", "missing",
                "source_example_sheet", "source_example_missing",
                "result_example_sheet", "result_example_missing",
                "owner_uniform", "support_uniform",
            ]
            with (review / "index.csv").open("w", encoding="utf-8", newline="") as handle:
                writer = csv.DictWriter(handle, fieldnames=fields)
                writer.writeheader()
                for column, topology in enumerate(("outer-1of4-v1", "edge-2of4-v1")):
                    writer.writerow({
                        "sheet": "masks.png", "row": 0, "column": column,
                        "owner_material_id": 101, "owner_material": "Grass / Test",
                        "owner_style": "grass-strands",
                        "support_scenario": "cross-style",
                        "support_material_id": 202,
                        "support_material": "Ground / Test",
                        "topology": topology, "variation": 0,
                        "provenance": f"ZoneTest:{column}",
                        "mask_sha256": f"mask-{column}", "missing": 0,
                        "source_example_sheet": "sources.png",
                        "source_example_missing": 0,
                        "result_example_sheet": "results.png",
                        "result_example_missing": 0,
                        "owner_uniform": "uniform-materials/owner.png",
                        "support_uniform": "uniform-materials/support.png",
                    })

            result = render(review, output, "R37-MASK")
            document = Path(result["html"]).read_text(encoding="utf-8")
            self.assertEqual(result["questions"], 2)
            self.assertEqual(document.count('class="mask-question"'), 2)
            self.assertEqual(document.count('class="visual requested-uniform"'), 2)
            self.assertEqual(document.count('class="visual ownership-mask"'), 2)
            self.assertEqual(document.count('class="visual resulting-blend"'), 2)
            self.assertEqual(document.count('class="visual support-uniform"'), 2)
            self.assertEqual(document.count('class="visual authored-source"'), 2)
            self.assertIn('data-key="mask-101-cross-style-outer-1of4-v1"', document)
            self.assertIn('class="answer form-select"', document)
            self.assertIn('class="note form-control"', document)
            self.assertIn('Keep current: ZoneTest:0', document)
            self.assertNotIn("Not answered", document)
            for control in ("submit-answers", "copy-answers", "download-answers",
                            "show-answers", "answer-export-text", "status"):
                self.assertIn(f'id="{control}"', document)
            self.assertIn("window.openai.sendFollowUpMessage", document)
            self.assertIn("localStorage.setItem", document)
            self.assertIn("rows().join('\\n')", document)
            self.assertIn("Apply only explicit mask decisions", document)


if __name__ == "__main__":
    unittest.main()
