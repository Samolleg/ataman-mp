from __future__ import annotations

import base64
import json
import unittest
from pathlib import Path


CORNERS = {"NW": 0, "NE": 1, "SE": 2, "SW": 3}


class R141CatalogDecisionTests(unittest.TestCase):
    def test_only_explicit_r141_changes_are_applied(self) -> None:
        root = Path(__file__).resolve().parents[1]
        catalog = json.loads((root / "assets" / "resources" /
                              "terrain_atlas_presets.json").read_text())
        decisions = json.loads((root / "scripts" /
            "semantic_atlas_r141_consolidated_decisions.json").read_text())
        presets = {row["namespace"].casefold(): row
                   for row in catalog["presets"]}

        def family(namespace: str, slot: int, corner: str) -> str:
            preset = presets[namespace.casefold()]
            names = {int(row["id"]): row["name"]
                     for row in preset["families"]}
            payload = base64.b64decode(preset["signaturesBase64"])
            return names[payload[slot * 4 + CORNERS[corner]]]

        self.assertEqual(12, decisions["counts"]["changedCardCorners"])
        for namespace, slots in decisions["cornerCorrections"].items():
            for slot_text, mapping in slots.items():
                for corner, expected in mapping.items():
                    self.assertEqual(
                        expected, family(namespace, int(slot_text), corner),
                        f"{namespace}:{slot_text}:{corner}")

        # Optional diagnostic cards were explicitly rejected.
        for corner in CORNERS:
            self.assertEqual(
                "Water / Still dark", family("BaseGipat", 42, corner))
            self.assertEqual(
                "Slabs / Faces irregular", family("Zone10", 396, corner))


if __name__ == "__main__":
    unittest.main()
