#!/usr/bin/env python3
from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path

from PIL import Image

from render_lava_mask_seam_pilot import ROUTES, TOPOLOGIES, render


class LavaMaskSeamPilotTests(unittest.TestCase):
    def test_pilot_uses_only_approved_exact_seam_masks(self) -> None:
        root = Path(__file__).resolve().parents[1]
        masks = root / "assets" / "resources" / "terrain_blend_masks"
        red = Image.new("RGB", (256, 256), (178, 38, 18))
        orange = Image.new("RGB", (256, 256), (238, 112, 14))
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary) / "pilot"
            result = render(masks, output, red, orange, {
                "archive": "synthetic-test",
                "red": {"namespace": "bz14h", "slot": 4},
                "orange": {"namespace": "Zone10", "slot": 5},
            }, "R123-LAVA-SEAM-PILOT-TEST")
            self.assertEqual(4, result["questionCount"])
            analysis = json.loads(result["analysis"].read_text("utf-8"))
            self.assertEqual("review-only-not-installed", analysis["status"])
            self.assertEqual(
                "method-only; zero EI-HD pixels copied", analysis["eiHdAssetUse"])
            self.assertEqual(set(ROUTES), set(analysis["routes"]))
            for route in analysis["routes"].values():
                self.assertEqual(4, len(route["topologies"]))
                for topology, _label in TOPOLOGIES:
                    contract = route["topologies"][topology]["seamContract"]
                    self.assertTrue(contract["allBoundaryProfilesExact"])
                    self.assertEqual(
                        {"north": 0, "east": 0, "south": 0, "west": 0},
                        contract["edgeMaxAbsoluteErrors"])
            page = result["html"].read_text("utf-8")
            self.assertEqual(4, page.count("<article data-key="))
            self.assertIn("Neither; make a distinct Lava route", page)
            self.assertIn("zero ei-hd pixels", page.casefold())
            for image in output.joinpath("images").glob("*.png"):
                self.assertGreater(image.stat().st_size, 0)
            self.assertGreater(len(list(output.joinpath("images").glob("*.png"))), 30)


if __name__ == "__main__":
    unittest.main()
