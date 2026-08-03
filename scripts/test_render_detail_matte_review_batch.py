#!/usr/bin/env python3
"""Regressions for the approved-family detailed-opacity review batch."""

from __future__ import annotations

import json
from pathlib import Path
from tempfile import TemporaryDirectory
import unittest

import numpy as np
from PIL import Image

from render_authored_mask_repair_pilot import expected_owner_corners
from render_detail_matte_review_batch import (
    APPROVED_CASES,
    compatible_representatives,
)
from render_soft_authored_mask_pilot import reconstruct_soft_ownership


def _texture(path: Path, detail: int) -> None:
    yy, xx = np.mgrid[0:64, 0:64]
    phase = ((xx + yy) % 2) * detail
    pixels = np.empty((64, 64, 3), dtype=np.uint8)
    pixels[..., 0] = 80 + phase
    pixels[..., 1] = 110 + phase
    pixels[..., 2] = 140 + phase
    Image.fromarray(pixels, "RGB").save(path)


class DetailMatteReviewBatchTests(unittest.TestCase):
    def test_case_set_is_exactly_the_nine_r44_approvals(self) -> None:
        decision_path = Path(__file__).with_name(
            "authored_mask_repair_r44_decisions.json")
        payload = json.loads(decision_path.read_text(encoding="utf-8"))
        approved = {
            key for key, value in payload["decisions"].items()
            if value["answer"] == "Approve repaired authored mask"
        }
        configured = {case.decision_key for case in APPROVED_CASES}
        self.assertEqual(approved, configured)
        self.assertEqual(9, len(configured))
        self.assertFalse(any(case.family == "sand-grain"
                             for case in APPROVED_CASES))

    def test_representatives_prefer_effective_detail_match(self) -> None:
        with TemporaryDirectory() as temporary:
            review = Path(temporary)
            _texture(review / "owner.png", 45)
            _texture(review / "support-close.png", 42)
            _texture(review / "support-close-2.png", 38)
            _texture(review / "support-low.png", 3)
            base = {
                "owner_material_id": "1",
                "owner_material": "Owner",
                "owner_uniform": "owner.png",
                "provenance": "Fixture:1",
            }
            rows = [
                {**base, "support_scenario": "cross-style",
                 "support_material_id": "2",
                 "support_material": "Low detail",
                 "support_uniform": "support-low.png"},
                {**base, "support_scenario": "cross-style",
                 "support_material_id": "3",
                 "support_material": "Close detail",
                 "support_uniform": "support-close.png"},
                {**base, "support_scenario": "same-style",
                 "support_material_id": "4",
                 "support_material": "Close detail 2",
                 "support_uniform": "support-close-2.png"},
            ]

            selected = compatible_representatives(review, rows, limit=3)

            self.assertEqual("Close detail", selected[0]["support_material"])
            self.assertEqual("Close detail 2", selected[1]["support_material"])
            self.assertNotIn("Low detail",
                             {row["support_material"] for row in selected})
            self.assertTrue(all(row["effective_detail_ratio"] < 1.5
                                for row in selected))

    def test_reconstruction_supports_every_approved_topology(self) -> None:
        size = 96
        yy, xx = np.mgrid[0:size, 0:size]
        owner_rgb = np.stack((
            40 + 10 * np.sin(xx * 0.2),
            120 + 12 * np.cos(yy * 0.17),
            70 + 8 * np.sin((xx + yy) * 0.13)), axis=2)
        support_rgb = np.stack((
            205 + 8 * np.cos(yy * 0.16),
            175 + 7 * np.sin(xx * 0.19),
            135 + 6 * np.cos((xx - yy) * 0.11)), axis=2)
        u = xx / (size - 1)
        v = yy / (size - 1)
        bases = ((1-u)*(1-v), u*(1-v), u*v, (1-u)*v)

        for topology in sorted({case.topology for case in APPROVED_CASES}):
            with self.subTest(topology=topology):
                corners = expected_owner_corners(topology)
                alpha = sum(base for base, owned in zip(bases, corners)
                            if owned)
                alpha = np.clip(alpha + 0.08 * np.sin((xx + yy) * 0.27) *
                                alpha * (1.0 - alpha), 0.0, 1.0)
                transition = owner_rgb * alpha[..., None] + support_rgb * (
                    1.0 - alpha[..., None])
                mask, metrics = reconstruct_soft_ownership(
                    Image.fromarray(np.uint8(np.clip(transition, 0, 255)),
                                    "RGB"), corners)
                values = np.asarray(mask, dtype=np.uint8)
                points = ((0, 0), (0, -1), (-1, -1), (-1, 0))
                self.assertEqual(
                    [255 if owned else 0 for owned in corners],
                    [int(values[point]) for point in points])
                self.assertGreater(metrics["distinctAlphaValues"], 64)
                self.assertGreater(metrics["softPixelFraction"], 0.10)


if __name__ == "__main__":
    unittest.main()
