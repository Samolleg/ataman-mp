#!/usr/bin/env python3
"""Focused regressions for the R76 authored-mask adjustments."""

from __future__ import annotations

import json
from pathlib import Path
import unittest

import numpy as np
from PIL import Image

from render_authored_mask_repair_pilot import expected_owner_corners
from render_r76_authored_adjustments_questionnaire import (
    EXPECTED_KEYS,
    HYBRID_KEYS,
    OWNER_REACH_KEYS,
    RECOVERED_KEYS,
    SUPPORT_CLEANUP_KEYS,
    clean_support_outer_edges,
    construct_adjustment,
)


class AuthoredAdjustmentTests(unittest.TestCase):
    def test_routes_partition_exactly_twenty_pending_decisions(self) -> None:
        decisions = json.loads(Path(__file__).with_name(
            "authored_mask_r75_decisions.json").read_text(encoding="utf-8"))
        self.assertEqual(set(decisions["adjustmentRequests"]), EXPECTED_KEYS)
        self.assertEqual(len(EXPECTED_KEYS), 20)
        self.assertFalse(
            (RECOVERED_KEYS & HYBRID_KEYS) |
            (RECOVERED_KEYS & SUPPORT_CLEANUP_KEYS) |
            (RECOVERED_KEYS & OWNER_REACH_KEYS) |
            (HYBRID_KEYS & SUPPORT_CLEANUP_KEYS) |
            (HYBRID_KEYS & OWNER_REACH_KEYS) |
            (SUPPORT_CLEANUP_KEYS & OWNER_REACH_KEYS))

    def test_support_cleanup_changes_only_support_quadrants(self) -> None:
        source = Image.fromarray(
            np.full((256, 256), 180, dtype=np.uint8), "L")
        cleaned, audit = clean_support_outer_edges(
            source, "edge-2of4-v2", reach_pixels=48)
        values = np.asarray(cleaned)
        np.testing.assert_array_equal(values[:128], 180)
        self.assertTrue(np.all(values[-1] == 0))
        self.assertTrue(np.all(values[128:208, 48:208] == 180))
        self.assertFalse(audit["changesOwnerQuadrants"])
        self.assertFalse(audit["usesHardQuadrantFill"])

    def test_recovered_route_changes_only_shared_edge_ring(self) -> None:
        values = np.arange(256 * 256, dtype=np.uint32).reshape(256, 256)
        recovered = Image.fromarray(np.uint8(values % 256), "L")
        restored = Image.fromarray(np.uint8((values * 3) % 256), "L")
        adjusted, process = construct_adjustment(
            "mask-family-sand-grain-outer-1of4-v1",
            "outer-1of4-v1", recovered, restored)
        actual = np.asarray(adjusted)
        expected = np.asarray(recovered)
        np.testing.assert_array_equal(actual[1:-1, 1:-1],
                                      expected[1:-1, 1:-1])
        self.assertEqual(process["method"], "recovered-authored-ownership")

    def test_owner_reach_is_continuous_and_only_raises_opacity(self) -> None:
        values = np.full((256, 256), 180, dtype=np.uint8)
        values[128:, :128] = 0
        values[116:140, :120] = 96
        recovered = Image.fromarray(values, "L")
        adjusted, process = construct_adjustment(
            "mask-family-stone-fracture-inside-3of4-v2",
            "inside-3of4-v2", recovered, recovered)
        actual = np.asarray(adjusted)
        # Ignore the deliberate one-pixel seam ring when comparing opacity.
        self.assertTrue(np.all(actual[1:-1, 1:-1] >= values[1:-1, 1:-1]))
        self.assertGreater(int((actual[1:-1, 1:-1] >
                                values[1:-1, 1:-1]).sum()), 0)
        self.assertEqual(
            tuple(process["method"].split("-", 1))[0], "continuous")
        self.assertFalse(process["usesHardQuadrantClipping"])

    def test_shared_ring_encodes_exact_canonical_corner_ownership(self) -> None:
        recovered = Image.new("L", (256, 256), 127)
        restored = Image.new("L", (256, 256), 190)
        adjusted, _ = construct_adjustment(
            "mask-family-ice-fracture-inside-3of4-v1",
            "inside-3of4-v1", recovered, restored)
        values = np.asarray(adjusted)
        actual = (values[0, 0] == 255, values[0, -1] == 255,
                  values[-1, -1] == 255, values[-1, 0] == 255)
        self.assertEqual(actual, expected_owner_corners("inside-3of4-v1"))


if __name__ == "__main__":
    unittest.main()
