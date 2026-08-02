#!/usr/bin/env python3
"""Focused regressions for conservative authored blend-mask repair."""

from __future__ import annotations

import hashlib
import math
import unittest

import numpy as np
from PIL import Image

from render_authored_mask_repair_pilot import (
    align_authored_mask,
    mask_metrics,
    repair_authored_mask,
)


def synthetic_outer_mask(kind: int, size: int = 96) -> Image.Image:
    """Return two visibly different authored-style NW corner silhouettes."""
    result = np.zeros((size, size), dtype=np.uint8)
    for y in range(size):
        for x in range(size):
            if kind == 0:
                edge = 69 + 8 * math.sin(y * 0.13) + 4 * math.sin(y * 0.37)
                owned = x + 0.72 * y < edge
            else:
                edge = 58 + 10 * math.sin((x + y) * 0.09)
                owned = max(x * 0.82, y) < edge
            result[y, x] = 255 if owned else 0
    # Authored-looking chips and pinholes: substantial features must survive;
    # isolated one-pixel noise may be removed.
    result[34:43, 55:65] = 255
    result[21:27, 18:25] = 0
    result[74, 10] = 255
    result[12, 78] = 255
    return Image.fromarray(result, "L")


class AuthoredMaskRepairPilotTests(unittest.TestCase):
    def test_distinct_authored_silhouettes_remain_distinct(self) -> None:
        first = synthetic_outer_mask(0)
        second = synthetic_outer_mask(1)
        repaired_first, first_metrics = repair_authored_mask(
            first, "outer-1of4-v1", seed=17)
        repaired_second, second_metrics = repair_authored_mask(
            second, "outer-1of4-v1", seed=17)
        first_hash = hashlib.sha256(repaired_first.tobytes()).hexdigest()
        second_hash = hashlib.sha256(repaired_second.tobytes()).hexdigest()
        self.assertNotEqual(first_hash, second_hash)
        self.assertGreaterEqual(first_metrics["strongIoU"], 0.68)
        self.assertGreaterEqual(second_metrics["strongIoU"], 0.68)

    def test_repair_is_deterministic_and_bounded(self) -> None:
        source = synthetic_outer_mask(0)
        first, metrics = repair_authored_mask(
            source, "outer-1of4-v1", seed=917)
        repeated, repeated_metrics = repair_authored_mask(
            source, "outer-1of4-v1", seed=917)
        self.assertEqual(first.tobytes(), repeated.tobytes())
        self.assertEqual(metrics, repeated_metrics)
        self.assertLessEqual(metrics["changedStrongFraction"], 0.22)
        self.assertLessEqual(metrics["meanAbsoluteChange"], 0.18)

    def test_repair_enforces_corner_ownership_without_rectangular_patch(self) -> None:
        raw = np.asarray(synthetic_outer_mask(0), dtype=np.uint8).copy()
        raw[:12, :12] = 0
        raw[:12, -12:] = 255
        repaired, _ = repair_authored_mask(
            Image.fromarray(raw, "L"), "outer-1of4-v1", seed=41)
        values = np.asarray(repaired, dtype=np.float32) / 255.0
        self.assertEqual(int(values[0, 0] * 255), 255)
        self.assertEqual(int(values[0, -1] * 255), 0)
        self.assertEqual(int(values[-1, -1] * 255), 0)
        self.assertEqual(int(values[-1, 0] * 255), 0)
        self.assertGreater(values[:8, :8].mean(), 0.72)
        self.assertLess(values[:8, -8:].mean(), 0.28)
        # A circular/tapered correction must not reproduce the former square
        # forced-corner patch.
        self.assertGreater(len(np.unique(values[:12, :12])), 4)

    def test_reported_metrics_match_repaired_image(self) -> None:
        source = synthetic_outer_mask(1)
        repaired, reported = repair_authored_mask(
            source, "outer-1of4-v2", seed=5)
        measured = mask_metrics(source, repaired, "outer-1of4-v2")
        self.assertEqual(reported, measured)
        self.assertEqual(reported["expectedOwnerCorners"], ["NW"])

    def test_alignment_normalizes_authored_orientation_before_repair(self) -> None:
        expected = synthetic_outer_mask(0)
        rotated = expected.rotate(90)
        aligned, transform = align_authored_mask(
            rotated, "outer-1of4-v1")
        expected_score = mask_metrics(
            expected, expected, "outer-1of4-v1")[
                "affectedQuarterStrongCoverage"]["NW"]
        aligned_score = mask_metrics(
            aligned, aligned, "outer-1of4-v1")[
                "affectedQuarterStrongCoverage"]["NW"]
        self.assertGreaterEqual(aligned_score, expected_score - 0.02)
        self.assertNotEqual(transform["rotationCounterClockwise"], 0)
        self.assertFalse(transform["inverted"])


if __name__ == "__main__":
    unittest.main()
