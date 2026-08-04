#!/usr/bin/env python3

from __future__ import annotations

import base64
import json
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from report_semantic_atlas_topology import (
    TOPOLOGIES,
    build_report,
    decode_signatures,
    pair_topologies,
    pair_topology_slots,
    strict_relabel_candidates,
)


class SemanticAtlasTopologyReportTests(unittest.TestCase):
    def test_classifies_all_four_rotational_topologies(self) -> None:
        signatures = [
            (1, 2, 2, 2),
            (1, 1, 1, 2),
            (1, 1, 2, 2),
            (1, 2, 1, 2),
        ]
        self.assertEqual(
            set(TOPOLOGIES), pair_topologies(signatures, 1, 2)
        )
        self.assertEqual(
            {
                "one-corner": [0],
                "three-corner": [1],
                "adjacent-two": [2],
                "diagonal-two": [3],
            },
            pair_topology_slots(signatures, 1, 2),
        )

    def test_rejects_truncated_signature_payload(self) -> None:
        encoded = base64.b64encode(bytes((1, 2, 3))).decode("ascii")
        with self.assertRaisesRegex(ValueError, "expected 4"):
            decode_signatures(encoded, 1)

    def test_strict_relabel_requires_every_changed_and_retained_corner(self) -> None:
        report = {
            "profiles": [{
                "namespace": "Synthetic",
                "aliases": ["Synthetic"],
                "families": [
                    {"id": 1, "name": "A"},
                    {"id": 2, "name": "B"},
                    {"id": 3, "name": "C"},
                ],
                "pairs": [{
                    "familyAId": 1,
                    "familyA": "A",
                    "familyBId": 2,
                    "familyB": "B",
                    "coverageClassCount": 3,
                    "missingTopologies": ["diagonal-two"],
                }],
            }]
        }
        rows = []
        for corner, family, nearest in (
            ("NW", 1, 1), ("NE", 3, 2),
            ("SE", 1, 1), ("SW", 3, 2),
        ):
            rows.append({
                "namespace": "Synthetic", "slot": "7",
                "signature": "1/3/1/3", "corner": corner,
                "nearestAnchorId": str(nearest), "nearestScore": "0.01",
            })
        candidates = strict_relabel_candidates(report, rows)
        self.assertEqual(1, len(candidates))
        self.assertEqual([1, 2, 1, 2], candidates[0]["proposedSignature"])
        rows[1]["nearestAnchorId"] = "3"
        self.assertEqual([], strict_relabel_candidates(report, rows))

    def test_current_catalog_coverage_is_explicit(self) -> None:
        root = Path(__file__).resolve().parents[1]
        catalog = json.loads(
            (root / "assets/resources/terrain_atlas_presets.json")
            .read_text(encoding="utf-8")
        )
        report = build_report(catalog)
        self.assertEqual(38, report["namespaceCount"])
        self.assertEqual(25, report["runtimeProfileCount"])
        # R81 removes two disproven land families: Zone20 Stone / Grayish and
        # Zone2's transition-only Ground / Dirt.  Their 16 pair combinations
        # must disappear rather than remain as phantom topology coverage.
        self.assertEqual(1470, report["totalPairCount"])
        # R87 replaces false-uniform ownership with the reviewed mixed-corner
        # signatures. This redistributes eight family pairs between supported
        # topology counts without changing the catalog's total pair count.
        self.assertEqual(
            {"0": 887, "1": 125, "2": 80, "3": 178, "4": 200},
            report["coverageCounts"],
        )
        for namespace in ("bz3g", "zone9"):
            preset = next(item for item in catalog["presets"]
                          if item["namespace"] == namespace)
            self.assertNotIn(
                "Ground / Trampled",
                {family["name"] for family in preset["families"]})


if __name__ == "__main__":
    unittest.main()
