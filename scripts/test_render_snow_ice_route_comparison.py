#!/usr/bin/env python3
"""Regressions for Snow/Ice R44-versus-detailed route review."""

from __future__ import annotations

import json
from pathlib import Path
import unittest

import numpy as np
from PIL import Image

from render_snow_ice_route_comparison import (
    REVIEW_CASES,
    choose_route_candidate,
    choose_shared_route_pair,
    contract_violation,
    explicit_source_alignment,
    physical_to_logical,
)
from render_snow_ice_pending_review import (
    remove_disconnected_protected_alpha,
)


def metrics(*, affected: float, protected: float,
            owner_corner: float = 0.95,
            protected_corner: float = 0.05) -> dict:
    passed = (affected >= 0.70 and protected <= 0.30 and
              owner_corner >= 0.80 and protected_corner <= 0.20)
    return {
        "affectedQuarterStrongCoverage": {"NW": affected},
        "protectedQuarterOwnerPresence": {"SE": protected},
        "cornerPatchMeans": {
            "NW": owner_corner, "NE": protected_corner,
            "SE": protected_corner, "SW": protected_corner,
        },
        "expectedOwnerCorners": ["NW"],
        "structuralTargetMet": passed,
    }


class SnowIceRouteComparisonTests(unittest.TestCase):
    def test_protected_corner_cleanup_removes_only_disconnected_alpha(
            self) -> None:
        values = np.full((256, 256), 255, dtype=np.uint8)
        values[128:, :128] = 0
        values[128:190, 112:128] = 176  # Main owner edge, connected north.
        values[170:178, 36:44] = 224    # Unrelated isolated alpha island.
        values[224:232, 8:16] = 48      # Low-alpha protected-corner noise.
        original = values.copy()

        cleaned, audit = remove_disconnected_protected_alpha(
            Image.fromarray(values, "L"), "inside-3of4-v1")
        result = np.asarray(cleaned)

        np.testing.assert_array_equal(original[:128, :], result[:128, :])
        np.testing.assert_array_equal(original[128:, 128:],
                                      result[128:, 128:])
        np.testing.assert_array_equal(original[128:190, 112:128],
                                      result[128:190, 112:128])
        self.assertEqual(0, int(result[173, 39]))
        self.assertEqual(0, int(result[227, 11]))
        self.assertGreater(audit["removedPixels"], 0)
        self.assertEqual(["SW"], audit["protectedCorners"])

    def test_r53_decisions_approve_only_reviewed_outer_and_edge_routes(
            self) -> None:
        review = json.loads((Path(__file__).with_name(
            "authored_mask_snow_ice_r53_decisions.json")).read_text(
                encoding="utf-8"))
        approved = {
            key for key, decision in review["decisions"].items()
            if decision["answer"] == "Prefer detailed sand route"
        }
        pending = {
            key for key, decision in review["decisions"].items()
            if decision["answer"] == "Still uncertain"
        }

        expected_approved = {
            f"route-compare-{family}-{topology}"
            for family in ("snow-clumps", "ice-fracture")
            for topology in (
                "outer-1of4-v1", "outer-1of4-v2",
                "edge-2of4-v1", "edge-2of4-v2")
        }
        self.assertEqual(expected_approved, approved)
        self.assertEqual(6, len(pending))
        self.assertTrue(all(
            "diagonal-2of4" in key or "inside-3of4" in key
            for key in pending))
        self.assertEqual(set(review["decisions"]), approved | pending)

    def test_physical_mask_is_mirrored_to_explicit_logical_corners(self) -> None:
        """Reproduce R51's incorrect rotation of a physical SW owner."""
        values = np.zeros((64, 64), dtype=np.uint8)
        values[32:, :32] = 255
        values[37:47, 22:32] = 96
        values[54:64, 28:38] = 180
        physical = Image.fromarray(values, "L")

        logical, decision = physical_to_logical(physical)

        logical_values = np.asarray(logical)
        self.assertEqual(255, logical_values[0, 0])
        self.assertEqual(0, logical_values[-1, 0])
        self.assertEqual(180, logical_values[0, 28])
        self.assertEqual("vertical-mirror", decision["transform"])
        self.assertEqual(0, decision["rotationCounterClockwise"])
        self.assertFalse(decision["inverted"])

    def test_physical_to_logical_mirror_does_not_swap_left_and_right(self) -> None:
        values = np.zeros((8, 8), dtype=np.uint8)
        values[7, 1] = 91
        values[7, 6] = 203

        logical, _ = physical_to_logical(Image.fromarray(values, "L"))

        self.assertEqual(91, np.asarray(logical)[0, 1])
        self.assertEqual(203, np.asarray(logical)[0, 6])

    def test_source_rotation_comes_only_from_semantic_corner_assignment(self) -> None:
        outer = explicit_source_alignment((
            "Snow / Common", "Stone / Gray",
            "Stone / Gray", "Stone / Gray"), "outer-1of4-v1")
        edge = explicit_source_alignment((
            "Ground / Light", "Ground / Light",
            "Snow / Common", "Snow / Common"), "edge-2of4-v1")
        inside = explicit_source_alignment((
            "Snow / Common", "Snow / Common",
            "Snow / Common", "Grass / Yellow lichen"),
            "inside-3of4-v2")

        self.assertEqual(270, outer["sourceRotationClockwise"])
        self.assertEqual("Snow / Common", outer["mappedOwnerMaterial"])
        self.assertEqual(0, edge["sourceRotationClockwise"])
        self.assertEqual("Snow / Common", edge["mappedOwnerMaterial"])
        self.assertEqual(90, inside["sourceRotationClockwise"])
        self.assertEqual("Snow / Common", inside["mappedOwnerMaterial"])
        for decision in (outer, edge, inside):
            self.assertEqual("semantic-corner-pattern",
                             decision["transformAuthority"])

    def test_case_set_covers_every_snow_and_ice_topology_once(self) -> None:
        self.assertEqual(14, len(REVIEW_CASES))
        for family in ("snow-clumps", "ice-fracture"):
            cases = [case for case in REVIEW_CASES if case.family == family]
            self.assertEqual(7, len(cases))
            self.assertEqual(7, len({case.topology for case in cases}))

    def test_contract_violation_measures_every_hard_gate(self) -> None:
        self.assertEqual(0.0, contract_violation(metrics(
            affected=0.80, protected=0.20)))
        self.assertGreater(contract_violation(metrics(
            affected=0.60, protected=0.20)), 0.0)
        self.assertGreater(contract_violation(metrics(
            affected=0.80, protected=0.40)), 0.0)
        self.assertGreater(contract_violation(metrics(
            affected=0.80, protected=0.20, owner_corner=0.60)), 0.0)
        self.assertGreater(contract_violation(metrics(
            affected=0.80, protected=0.20, protected_corner=0.30)), 0.0)

    def test_eligible_candidate_wins_over_better_scoring_failed_source(self) -> None:
        candidates = [
            {"sourceQuality": 0.05, "provenance": "Fixture:1",
             "contractMetrics": metrics(affected=0.69, protected=0.10)},
            {"sourceQuality": 3.00, "provenance": "Fixture:2",
             "contractMetrics": metrics(affected=0.75, protected=0.20)},
        ]

        chosen = choose_route_candidate(candidates)

        self.assertEqual("Fixture:2", chosen["provenance"])
        self.assertTrue(chosen["contractMetrics"]["structuralTargetMet"])

    def test_nearest_contract_failure_is_used_only_as_diagnostic(self) -> None:
        candidates = [
            {"sourceQuality": 0.05, "provenance": "Fixture:1",
             "contractMetrics": metrics(affected=0.40, protected=0.45)},
            {"sourceQuality": 4.00, "provenance": "Fixture:2",
             "contractMetrics": metrics(affected=0.68, protected=0.31)},
        ]

        chosen = choose_route_candidate(candidates)

        self.assertEqual("Fixture:2", chosen["provenance"])
        self.assertFalse(chosen["contractMetrics"]["structuralTargetMet"])
        self.assertGreater(chosen["contractViolation"], 0.0)

    def test_two_routes_must_use_the_same_authored_source(self) -> None:
        r44 = [
            {"sourceQuality": 0.1, "provenance": "Fixture:A",
             "contractMetrics": metrics(affected=0.80, protected=0.20)},
            {"sourceQuality": 0.2, "provenance": "Fixture:B",
             "contractMetrics": metrics(affected=0.68, protected=0.20)},
        ]
        detailed = [
            {"sourceQuality": 0.1, "provenance": "Fixture:A",
             "contractMetrics": metrics(affected=0.68, protected=0.20)},
            {"sourceQuality": 0.2, "provenance": "Fixture:B",
             "contractMetrics": metrics(affected=0.80, protected=0.20)},
        ]

        selected_r44, selected_detailed = choose_shared_route_pair(
            r44, detailed)

        self.assertEqual(selected_r44["provenance"],
                         selected_detailed["provenance"])


if __name__ == "__main__":
    unittest.main()
