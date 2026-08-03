#!/usr/bin/env python3
"""Regressions for source-image soft authored-mask reconstruction."""

from __future__ import annotations

import json
import math
from pathlib import Path
import unittest

import numpy as np
from PIL import Image

from render_soft_authored_mask_pilot import (
    derive_inside_from_outer,
    reconstruct_soft_ownership,
)


def painterly_fixture(size: int = 96):
    yy, xx = np.mgrid[0:size, 0:size]
    owner = np.empty((size, size, 3), dtype=np.float32)
    support = np.empty_like(owner)
    owner[..., 0] = 40 + 12 * np.sin(xx * 0.21)
    owner[..., 1] = 116 + 15 * np.sin(yy * 0.17)
    owner[..., 2] = 132 + 9 * np.cos((xx + yy) * 0.13)
    support[..., 0] = 226 + 8 * np.sin(yy * 0.16)
    support[..., 1] = 202 + 7 * np.cos(xx * 0.19)
    support[..., 2] = 145 + 6 * np.sin((xx - yy) * 0.11)
    radial = np.sqrt((xx / size) ** 2 + (yy / size) ** 2)
    brush = 1.0 / (1.0 + np.exp((radial - 0.69 -
                                 0.055 * np.sin(yy * 0.18)) * 22.0))
    # A broad translucent painterly echo must survive as intermediate alpha.
    echo = np.exp(-((xx - 58) ** 2 + (yy - 28) ** 2) / 260.0) * 0.38
    alpha = np.clip(np.maximum(brush, echo), 0.0, 1.0)
    transition = owner * alpha[..., None] + support * (1.0 - alpha[..., None])
    return (Image.fromarray(np.uint8(np.clip(transition, 0, 255)), "RGB"),
            alpha)


class SoftAuthoredMaskPilotTests(unittest.TestCase):
    def test_r45_feedback_marks_extraction_as_the_failure(self) -> None:
        path = Path(__file__).with_name("authored_mask_repair_r45_feedback.json")
        payload = json.loads(path.read_text(encoding="utf-8"))
        self.assertEqual(payload["status"], "rejected-upstream-extraction")
        self.assertEqual(len(payload["decisions"]), 3)
        self.assertIn("painterly soft brushes",
                      payload["decisions"][
                          "authored-repair-sand-grain-outer-1of4-v1"]["note"])
        self.assertIn("introduces holes",
                      payload["decisions"][
                          "authored-repair-sand-grain-edge-2of4-v2"]["note"])

    def test_soft_reconstruction_retains_intermediate_painterly_ownership(self) -> None:
        transition, expected = painterly_fixture()
        reconstructed, metrics = reconstruct_soft_ownership(
            transition, (True, False, False, False))
        actual = np.asarray(reconstructed, dtype=np.float32) / 255.0
        self.assertLess(float(np.abs(actual - expected).mean()), 0.16)
        self.assertGreater(metrics["softPixelFraction"], 0.12)
        self.assertGreater(metrics["distinctAlphaValues"], 64)
        self.assertFalse(metrics["usesBinaryCleanup"])

    def test_soft_reconstruction_does_not_punch_texture_holes(self) -> None:
        transition, _ = painterly_fixture()
        pixels = np.asarray(transition).copy()
        # Fine high-contrast pits resemble the opposite material locally; the
        # multi-scale source model must not turn them into full support holes.
        for y in range(12, 50, 7):
            for x in range(12, 50, 9):
                pixels[y, x] = (235, 207, 151)
        reconstructed, _ = reconstruct_soft_ownership(
            Image.fromarray(pixels, "RGB"),
            (True, False, False, False))
        values = np.asarray(reconstructed, dtype=np.uint8)
        self.assertGreater(int(values[18:42, 18:42].min()), 48)
        self.assertGreater(float((values[18:42, 18:42] >= 128).mean()), 0.88)

    def test_inside_is_exact_rotated_polarity_inverse_of_outer(self) -> None:
        transition, _ = painterly_fixture()
        outer, _ = reconstruct_soft_ownership(
            transition, (True, False, False, False))
        inside = derive_inside_from_outer(outer)
        expected = 255 - np.asarray(outer.rotate(90), dtype=np.uint8)
        self.assertTrue(np.array_equal(np.asarray(inside), expected))
        values = np.asarray(inside, dtype=np.uint8)
        self.assertEqual(values[0, 0], 255)
        self.assertEqual(values[0, -1], 255)
        self.assertEqual(values[-1, -1], 255)
        self.assertEqual(values[-1, 0], 0)


if __name__ == "__main__":
    unittest.main()
