from __future__ import annotations

import base64
import json
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
CORNER_INDEX = {"NW": 0, "NE": 1, "SE": 2, "SW": 3}


class R147CatalogAndPairDecisionTests(unittest.TestCase):
    def setUp(self) -> None:
        self.decisions = json.loads((
            ROOT / "scripts/semantic_atlas_r147_decisions.json"
        ).read_text(encoding="utf-8"))
        catalog = json.loads((
            ROOT / "assets/resources/terrain_atlas_presets.json"
        ).read_text(encoding="utf-8"))
        self.presets = {preset["namespace"].casefold(): preset
                        for preset in catalog["presets"]}

    def mapping(self, namespace: str, slot: int) -> dict[str, str]:
        preset = self.presets[namespace.casefold()]
        names = {int(family["id"]): family["name"]
                 for family in preset["families"]}
        payload = base64.b64decode(preset["signaturesBase64"])
        return {corner: names[payload[slot * 4 + index]]
                for corner, index in CORNER_INDEX.items()}

    def test_submission_preserves_untouched_and_partial_cards(self) -> None:
        counts = self.decisions["counts"]
        self.assertEqual(55, counts["queueCards"])
        self.assertEqual(54, counts["submittedCards"])
        self.assertEqual(1, counts["untouchedCards"])
        self.assertEqual(3, counts["partialCards"])
        self.assertEqual(
            ["semantic-atlas-hd-05b151e139db5da2"],
            self.decisions["untouchedQuestionKeys"])
        self.assertEqual(
            {"semantic-atlas-hd-6fe571e9f96115eb": ["NE", "SW"],
             "semantic-atlas-hd-b2f89d60932d89e2": ["SW"],
             "semantic-atlas-hd-e190fd86f98f29c7": ["SW"]},
            {row["key"]: row["unresolvedCorners"]
             for row in self.decisions["partialCards"]})

    def test_approved_pair_wins_over_rejected_pair_history(self) -> None:
        pairs = {(row["currentFamily"], row["auditFamily"]): row
                 for row in self.decisions["directedMaterialPairDispositions"]}
        conflict = pairs[("Ground / Common", "Ground / Gravel")]
        self.assertEqual((1, 1), (conflict["approved"], conflict["rejected"]))
        self.assertTrue(conflict["conflict"])
        self.assertEqual("promote", conflict["disposition"])
        self.assertTrue(conflict["promotionWins"])
        self.assertTrue(all(
            row["disposition"] == ("promote" if row["approved"] else "demote")
            for row in pairs.values()))

    def test_only_concrete_exact_owner_corrections_enter_catalog(self) -> None:
        self.assertEqual(15, self.decisions["counts"]["changedLogicalCorners"])
        self.assertEqual(
            30, self.decisions["counts"]["ownerExpandedCornerCorrections"])
        for namespace, slots in self.decisions["cornerCorrections"].items():
            for slot_text, corrections in slots.items():
                actual = self.mapping(namespace, int(slot_text))
                for corner, family in corrections.items():
                    self.assertNotEqual("Still uncertain", family)
                    self.assertEqual(family, actual[corner],
                                     f"{namespace}:{slot_text}:{corner}")

    def test_withdrawn_transition_only_sand_dark_is_not_catalogued(self) -> None:
        new_families = self.decisions["newFamilies"]
        self.assertEqual({}, new_families)
        self.assertEqual(3, self.decisions["counts"]
                         ["ownerExpandedWithdrawnRestorations"])
        for namespace in ("bz5g", "bz6g", "Zone8"):
            self.assertNotIn(
                "Sand / Dark",
                {row["name"] for row in
                 self.presets[namespace.casefold()]["families"]})
            self.assertEqual(
                {"NW": "Stone / Light", "NE": "Stone / Light",
                 "SE": "Stone / Light", "SW": "Sand / Dark pink"},
                self.mapping(namespace, 215))


if __name__ == "__main__":
    unittest.main()
