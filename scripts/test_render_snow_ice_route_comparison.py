#!/usr/bin/env python3
"""Regressions for Snow/Ice R44-versus-detailed route review."""

from __future__ import annotations

import unittest

import numpy as np
from PIL import Image, ImageOps

from render_snow_ice_route_comparison import (
    REVIEW_CASES,
    choose_route_candidate,
    choose_shared_route_pair,
    contract_violation,
    orient_reconstructed_mask,
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
    def test_reconstructed_mask_is_rotated_and_inverted_to_authored_owner(self) -> None:
        """Reproduce R49's opposite-polarity Snow route comparison."""
        values = np.zeros((64, 64), dtype=np.uint8)
        values[:32, :32] = 255
        values[7:17, 22:32] = 96
        values[22:32, 28:38] = 180
        reference = Image.fromarray(values, "L")
        reconstructed = ImageOps.invert(reference.rotate(90))

        oriented, decision = orient_reconstructed_mask(
            reconstructed, reference, "outer-1of4-v1")

        self.assertTrue(np.array_equal(
            np.asarray(reference), np.asarray(oriented)))
        self.assertEqual(270, decision["rotationCounterClockwise"])
        self.assertTrue(decision["inverted"])

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
