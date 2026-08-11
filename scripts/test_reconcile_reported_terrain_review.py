#!/usr/bin/env python3
"""Focused regressions for provenance-qualified paint report reconciliation."""

from __future__ import annotations

import base64
import json
import tempfile
import unittest
from pathlib import Path

from reconcile_reported_terrain_review import reconcile


class ReconcileReportedTerrainReviewTests(unittest.TestCase):
    def test_context_layer_is_not_queued_as_reported_artwork(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            reports = root / "reports"
            reports.mkdir()
            catalog_path = root / "presets.json"
            catalog_path.write_text(json.dumps({"presets": [{
                "namespace": "Zone2", "slotCount": 2,
                "signaturesBase64": base64.b64encode(
                    bytes([1, 1, 1, 1, 2, 2, 2, 2])).decode("ascii"),
                "families": [
                    {"id": 1, "name": "Ground / Common", "tileType": 1},
                    {"id": 2, "name": "Water / Still", "tileType": 9},
                ],
            }]}), encoding="utf-8")
            inventory_path = root / "inventory.json"
            inventory_path.write_text(json.dumps({"artwork_cards": []}),
                                      encoding="utf-8")
            report = {
                "mprNamespace": "zone2_test", "mprFileName": "zone2_test.mpr",
                "sourceMprSha256": "", "marks": [{
                    "id": 7, "category": "no-blend", "layer": "all-available",
                    "activeEditLayer": "land",
                    "selectedCells": [{"x": 4, "y": 5}],
                    "tileSnapshot": [self.snapshot("land", 0, "aa"),
                                     self.snapshot("overlay", 1, "bb")],
                }],
            }
            (reports / "zone2-paint-issues.json").write_text(
                json.dumps(report), encoding="utf-8")

            result = reconcile(reports, catalog_path, [inventory_path])
            self.assertEqual(result["summary"]["selectedCells"], 1)
            self.assertEqual(result["summary"]["capturedSelectedCells"], 1)
            self.assertEqual(result["summary"]["automaticReviewGroups"], 1)
            self.assertEqual(result["automaticReviewGroups"][0]["sourceAliases"],
                             [{"namespace": "Zone2", "slot": 0}])

    @staticmethod
    def snapshot(layer: str, slot: int, pixel_hash: str) -> dict:
        return {
            "insideMark": True, "layer": layer, "x": 4, "y": 5,
            "packed": {"atlasSlot": slot, "rotation": 0},
            "physicalPixelSha256": pixel_hash,
            "physicalIdentity": {
                "origin": "authored-current-map", "sourceNamespace": "Zone2",
                "sourceSlot": slot, "sourcePixelSha256": pixel_hash,
            },
        }


if __name__ == "__main__":
    unittest.main()
