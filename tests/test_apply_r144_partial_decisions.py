from __future__ import annotations

import base64
import json
import unittest
from pathlib import Path


CORNERS = {"NW": 0, "NE": 1, "SE": 2, "SW": 3}


class R144CatalogDecisionTests(unittest.TestCase):
    def setUp(self) -> None:
        self.root = Path(__file__).resolve().parents[1]
        self.catalog = json.loads((self.root / "assets" / "resources" /
                                   "terrain_atlas_presets.json").read_text())
        self.decisions = json.loads((self.root / "scripts" /
            "semantic_atlas_r144_partial_decisions.json").read_text())
        self.presets = {row["namespace"].casefold(): row
                        for row in self.catalog["presets"]}

    def family(self, namespace: str, slot: int, corner: str) -> str:
        preset = self.presets[namespace.casefold()]
        names = {int(row["id"]): row["name"]
                 for row in preset["families"]}
        payload = base64.b64decode(preset["signaturesBase64"])
        return names[payload[slot * 4 + CORNERS[corner]]]

    def test_only_concrete_submitted_corners_are_applied(self) -> None:
        counts = self.decisions["counts"]
        self.assertEqual(43, counts["submittedCards"])
        self.assertEqual(42, counts["fullyResolvedCards"])
        self.assertEqual(1, counts["partiallyResolvedCards"])
        self.assertEqual(26, counts["unsubmittedCards"])
        self.assertEqual(27, counts["remainingCards"])
        self.assertEqual(78, counts["changedCornerRows"])
        self.assertEqual(
            ["semantic-atlas-hd-78c7da1531a7d8ea"],
            self.decisions["partiallyResolvedQuestionKeys"],
        )
        for namespace, slots in self.decisions["cornerCorrections"].items():
            for slot_text, mapping in slots.items():
                for corner, expected in mapping.items():
                    self.assertEqual(
                        expected, self.family(namespace, int(slot_text), corner),
                        f"{namespace}:{slot_text}:{corner}",
                    )

    def test_explicit_grayish_selection_creates_transition_family_in_exact_owners(self) -> None:
        for namespace in ("bz2g", "Zone2"):
            preset = self.presets[namespace.casefold()]
            grayish = next(row for row in preset["families"]
                           if row["name"] == "Stone / Grayish")
            self.assertEqual(26, int(grayish["id"]))
            self.assertFalse(grayish["paintSelectable"])
            self.assertEqual(221, int(grayish["thumbnailSlot"]))
            self.assertNotIn("sourceFamily", grayish)
            self.assertNotIn("uniformSlots", grayish)
            self.assertNotIn("memberSlots", grayish)
            self.assertEqual("Stone / Grayish", self.family(namespace, 221, "SW"))

    def test_uncertain_corner_is_not_manufactured_as_a_catalog_change(self) -> None:
        key = "semantic-atlas-hd-78c7da1531a7d8ea"
        self.assertEqual("Still uncertain",
                         self.decisions["submittedMappings"][key]["NW"])
        self.assertIn(key, self.decisions["unresolvedQuestionKeys"])
        for namespace, slots in self.decisions["cornerCorrections"].items():
            for mapping in slots.values():
                self.assertNotIn("Still uncertain", mapping.values())


if __name__ == "__main__":
    unittest.main()
