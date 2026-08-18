from __future__ import annotations

import base64
import json
import unittest
from pathlib import Path


CORNERS = {"NW": 0, "NE": 1, "SE": 2, "SW": 3}


class R143CatalogDecisionTests(unittest.TestCase):
    def test_only_explicit_partial_and_exact_hd_closure_rows_are_applied(self) -> None:
        root = Path(__file__).resolve().parents[1]
        catalog = json.loads((root / "assets" / "resources" /
                              "terrain_atlas_presets.json").read_text())
        decisions = json.loads((root / "scripts" /
            "semantic_atlas_r143_partial_decisions.json").read_text())
        presets = {row["namespace"].casefold(): row
                   for row in catalog["presets"]}

        def family(namespace: str, slot: int, corner: str) -> str:
            preset = presets[namespace.casefold()]
            names = {int(row["id"]): row["name"]
                     for row in preset["families"]}
            payload = base64.b64decode(preset["signaturesBase64"])
            return names[payload[slot * 4 + CORNERS[corner]]]

        self.assertEqual(76, decisions["counts"]["submittedCards"])
        self.assertEqual(1, decisions["counts"]["exactHdClosureCards"])
        self.assertEqual(113, decisions["counts"]["changedCornerRows"])
        for namespace, slots in decisions["cornerCorrections"].items():
            for slot_text, mapping in slots.items():
                for corner, expected in mapping.items():
                    self.assertEqual(
                        expected, family(namespace, int(slot_text), corner),
                        f"{namespace}:{slot_text}:{corner}")

        # Both exact-HD Zone19 copies now have the single reviewed mapping.
        self.assertEqual("Sand / Gravel", family("Zone19", 309, "SW"))
        self.assertEqual("Sand / Gravel", family("Zone19", 310, "SW"))

        # The R143 queue omitted several byte-identical binding aliases.  The
        # audit occurrence is authoritative, so one answer must reach them all.
        self.assertEqual("Stone / Dark", family("bz11k", 55, "SW"))
        self.assertEqual("Stone / Dark", family("Zone14", 55, "SW"))
        self.assertEqual("Sand / Common grayish", family("bz13h", 192, "NW"))
        self.assertEqual("Sand / Common grayish", family("Zone15", 192, "NW"))


if __name__ == "__main__":
    unittest.main()
