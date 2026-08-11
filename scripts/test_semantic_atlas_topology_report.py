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
        self.assertEqual(26, report["runtimeProfileCount"])
        # R97/R98 resolve the reviewed Sand / Irregular transition corners.
        # R105 corrects six provenance-qualified authored blend signatures;
        # R106 then applies the completed 249-card review and one explicit new
        # Zone7 material identity. Their real pair rows are reported honestly,
        # including zero-coverage topology, rather than hidden to preserve an
        # earlier aggregate count.
        # R106's 30 dead family rows previously fabricated 269 zero-coverage
        # pairs. Pruning them changes only the honest denominator.
        # R108 removes the three exhausted transition-only family records;
        # their 25 zero-coverage pair rows no longer inflate the denominator.
        # R109 removes the final Bz7g Ground / Common corner and prunes that
        # now-unreferenced brush, removing five topology pairs. R111 restores
        # the reviewed transition-only Ground / Tan identity in bz15h and
        # Zone17. R112 separates Zone10 slot 396 into one genuine uniform
        # Slabs / Faces irregular family and moves three reviewed pair rows
        # between topology-coverage buckets.
        # R114 changes only reviewed corner ownership. It retains the same
        # family/pair denominator while moving seven pair rows among topology
        # coverage buckets.
        # R115 adds two honest transition-only identities where the reviewed
        # cross-atlas family was absent locally (bz2g/Zone2 Ground / Dirt and
        # Zone7 Sand / Dark pink), then applies 158 exact-owner corrections.
        # The two exact bz2g/Zone2 bindings each add 12 relationships and the
        # Zone7 family adds four, for 28 additional pair rows.
        self.assertEqual(1234, report["totalPairCount"])
        self.assertEqual(
            {"0": 662, "1": 84, "2": 80, "3": 186, "4": 222},
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
