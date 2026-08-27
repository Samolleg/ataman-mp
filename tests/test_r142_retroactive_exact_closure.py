from __future__ import annotations

import base64
import json
import unittest
from pathlib import Path


CORNERS = {"NW": 0, "NE": 1, "SE": 2, "SW": 3}


class R142RetroactiveExactClosureTests(unittest.TestCase):
    def test_every_safe_complete_history_closure_is_applied(self) -> None:
        root = Path(__file__).resolve().parents[1]
        catalog = json.loads((root / "assets" / "resources" /
                              "terrain_atlas_presets.json").read_text())
        decisions = json.loads((root / "scripts" /
            "semantic_atlas_r142_retroactive_exact_closures.json").read_text())
        presets = {row["namespace"].casefold(): row
                   for row in catalog["presets"]}

        def family(namespace: str, slot: int, corner: str) -> str:
            preset = presets[namespace.casefold()]
            names = {int(row["id"]): row["name"]
                     for row in preset["families"]}
            payload = base64.b64decode(preset["signaturesBase64"])
            return names[payload[slot * 4 + CORNERS[corner]]]

        expanded = 0
        physical = decisions["physicalClosures"]
        self.assertEqual(10, len(physical))
        for row in physical:
            for owner in row["owners"]:
                expanded += 1
                with self.subTest(owner=owner, slot=row["slot"],
                                  corner=row["corner"]):
                    self.assertEqual(
                        row["reviewed"],
                        family(owner, int(row["slot"]), row["corner"]))
        self.assertEqual(15, expanded)

    def test_conflicts_remain_review_only(self) -> None:
        root = Path(__file__).resolve().parents[1]
        decisions = json.loads((root / "scripts" /
            "semantic_atlas_r142_retroactive_exact_closures.json").read_text())
        counts = decisions["counts"]
        self.assertEqual(228, counts["conflictPhysicalCorners"])
        self.assertEqual(174, counts["conflictPhysicalTileCards"])
        self.assertNotIn("conflictCorrections", decisions)


if __name__ == "__main__":
    unittest.main()
