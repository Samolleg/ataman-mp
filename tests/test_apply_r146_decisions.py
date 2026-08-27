from __future__ import annotations

import base64
import json
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
CORNERS = {"NW": 0, "NE": 1, "SE": 2, "SW": 3}


class R146CatalogDecisionTests(unittest.TestCase):
    def setUp(self) -> None:
        self.catalog = json.loads((ROOT / "assets/resources/terrain_atlas_presets.json")
                                  .read_text(encoding="utf-8"))
        self.decisions = json.loads((ROOT / "scripts/semantic_atlas_r146_decisions.json")
                                    .read_text(encoding="utf-8"))
        self.presets = {preset["namespace"].casefold(): preset
                        for preset in self.catalog["presets"]}

    def mapping(self, namespace: str, slot: int) -> dict[str, str]:
        preset = self.presets[namespace.casefold()]
        names = {int(family["id"]): family["name"]
                 for family in preset["families"]}
        payload = base64.b64decode(preset["signaturesBase64"])
        return {corner: names[payload[slot * 4 + index]]
                for corner, index in CORNERS.items()}

    def test_single_logical_change_closes_the_last_card(self) -> None:
        self.assertEqual({
            "submittedCards": 1,
            "fullyResolvedCards": 1,
            "remainingCards": 0,
            "changedLogicalCorners": 1,
            "ownerExpandedCornerRows": 2,
        }, self.decisions["counts"])
        self.assertEqual({"NW": "Ground / Gravel greenish"},
                         self.decisions["changedCorners"])

    def test_exact_binding_closure_only_updates_bz4g_and_zone4(self) -> None:
        self.assertEqual(["Zone4", "bz4g"],
                         self.decisions["exactBindingOwners"])
        expected = {
            "NW": "Ground / Gravel greenish",
            "NE": "Sand / Dark pink",
            "SE": "Sand / Dark pink",
            "SW": "Sand / Dark pink",
        }
        for namespace in self.decisions["exactBindingOwners"]:
            self.assertEqual(expected, self.mapping(namespace, 488))
        self.assertEqual({"Zone4", "bz4g"},
                         set(self.decisions["cornerCorrections"]))

    def test_concrete_selection_wins_over_stale_carry_forward_note(self) -> None:
        self.assertEqual("Ground / Gravel greenish",
                         self.decisions["submittedMapping"]["NW"])
        self.assertIn("still uncertain",
                      self.decisions["retainedNote"].casefold())
        self.assertNotIn("Still uncertain",
                         self.decisions["changedCorners"].values())


if __name__ == "__main__":
    unittest.main()
