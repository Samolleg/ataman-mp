from __future__ import annotations

import base64
import json
import unittest
from pathlib import Path


CORNERS = {"NW": 0, "NE": 1, "SE": 2, "SW": 3}


class R141ExactReviewClosureTests(unittest.TestCase):
    def test_r139_accepted_exact_corner_counterparts_are_closed(self) -> None:
        root = Path(__file__).resolve().parents[1]
        catalog = json.loads((root / "assets" / "resources" /
                              "terrain_atlas_presets.json").read_text())
        presets = {row["namespace"].casefold(): row
                   for row in catalog["presets"]}

        def family(namespace: str, slot: int, corner: str) -> str:
            preset = presets[namespace.casefold()]
            names = {int(row["id"]): row["name"]
                     for row in preset["families"]}
            payload = base64.b64decode(preset["signaturesBase64"])
            return names[payload[slot * 4 + CORNERS[corner]]]

        # R141's T1 rows are exact decoded-corner matches to already accepted
        # R139 counterparts. Once reviewed, both locations must remain closed
        # over the same semantic family even though their whole HD atlases or
        # whole tiles are not byte-identical.
        cases = (
            ("bz3g", "zone9", 228, "NE", "Grass / Sparse dark"),
            ("bz3g", "zone9", 262, "NE", "Stone / Lime"),
            ("bz3g", "zone9", 268, "SE", "Ground / Common"),
            ("bz3g", "zone9", 277, "NW", "Stone / Lime"),
            ("bz3g", "zone9", 339, "SW", "Ground / Gravel"),
            ("bz3g", "zone9", 499, "NE", "Stone / Grayish"),
            ("zone6", "zone6_2", 359, "NW", "Ground / Common"),
            ("zone6", "zone6_2", 398, "NW", "Ground / Common"),
        )
        for target, reviewed, slot, corner, expected in cases:
            with self.subTest(target=target, reviewed=reviewed,
                              slot=slot, corner=corner):
                self.assertEqual(expected, family(reviewed, slot, corner))
                self.assertEqual(expected, family(target, slot, corner))


if __name__ == "__main__":
    unittest.main()
