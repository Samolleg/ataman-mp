#!/usr/bin/env python3
"""Focused regressions for the four R77 non-rectilinear candidates."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
import re
import unittest

import numpy as np
from PIL import Image

from render_authored_mask_repair_pilot import expected_owner_corners
from render_r77_nonrectilinear_adjustments_questionnaire import (
    EXPECTED_KEYS,
    _noisy_south_feather,
    _support_corner_radial_fade,
    construct_candidate,
)


class NonRectilinearAdjustmentTests(unittest.TestCase):
    def test_routes_are_exactly_the_four_r76_requests(self) -> None:
        decisions = json.loads(Path(__file__).with_name(
            "authored_mask_r76_decisions.json").read_text(encoding="utf-8"))
        self.assertEqual(tuple(decisions["adjustmentRequests"]), EXPECTED_KEYS)
        self.assertEqual(len(EXPECTED_KEYS), 4)
        self.assertFalse(
            set(EXPECTED_KEYS) & set(decisions["approvedAdjustments"]))

    def test_only_explicit_r77_approvals_are_installed(self) -> None:
        scripts = Path(__file__).resolve().parent
        root = scripts.parent
        decisions = json.loads((
            scripts / "authored_mask_r77_decisions.json"
        ).read_text(encoding="utf-8"))
        approved = set(decisions["approvedAdjustments"])
        pending = set(decisions["adjustmentRequests"])
        self.assertEqual(approved, set(EXPECTED_KEYS))
        self.assertEqual(len(approved), 4)
        self.assertFalse(pending)

        source = (root / "source/documents/terrain_virtual_atlas.cpp").read_text(
            encoding="utf-8")
        block = source.split(
            "static const SReviewedCanonicalMask r77Masks[] = {", 1
        )[1].split("    };", 1)[0]
        records = re.findall(
            r'\{ETerrainBlendStyle::(\w+), "([^"]+)",\s*'
            r'"[^"]*/([^/"]+)",\s*"([0-9a-f]{64})",\s*'
            r'"([^"]+:r77-approved-nonrectilinear-adjustment:[^"]+)"\}',
            block)
        self.assertEqual(len(records), 4)
        installed = set()
        qrc = (root / "assets/resources/terrain_blend_masks.qrc").read_text(
            encoding="utf-8")
        for _, topology, filename, expected_hash, provenance in records:
            family = provenance.split(":", 2)[1]
            installed.add(f"mask-family-{family}-{topology}")
            self.assertIn(f"terrain_blend_masks/{filename}", qrc)
            image = Image.open(
                root / "assets/resources/terrain_blend_masks" / filename
            ).convert("L")
            self.assertEqual(hashlib.sha256(image.tobytes()).hexdigest(),
                             expected_hash)
        self.assertEqual(installed, approved)

    def test_paving_edge_has_no_straight_interior_fade_boundary(self) -> None:
        recovered = Image.new("L", (256, 256), 255)
        candidate, process = _noisy_south_feather(
            recovered, "edge-2of4-v2", reach_pixels=102.0,
            noise_pixels=8.0, seed=0x77A1,
            method="test-noisy-south-feather")
        values = np.asarray(candidate)
        self.assertTrue(np.all(values[-1] == 0))
        self.assertTrue(np.all(values[100, 1:-1] == 255))
        self.assertGreater(process["halfOpacityBoundaryYStdDev"], 1.0)
        low, high = process["halfOpacityBoundaryYRange"]
        self.assertGreater(high - low, 4)
        self.assertFalse(process["usesStraightInteriorBoundary"])
        self.assertFalse(process["usesHardQuadrantFill"])

    def test_paving_outer_uses_circular_not_quadrant_cleanup(self) -> None:
        recovered = Image.new("L", (256, 256), 255)
        candidate, process = _support_corner_radial_fade(
            recovered, "outer-1of4-v2", radius_pixels=164.0,
            noise_fraction=0.0, seed=0x77A2,
            method="test-circular-support-fade")
        values = np.asarray(candidate)
        self.assertEqual(int(values[0, 0]), 255)
        self.assertEqual(int(values[0, -1]), 0)
        self.assertEqual(int(values[-1, -1]), 0)
        self.assertEqual(int(values[-1, 0]), 0)
        # These interior points are the same distance from the NE support
        # corner and therefore receive the same circular attenuation.
        self.assertLessEqual(abs(int(values[48, 191]) -
                                 int(values[64, 207])), 1)
        self.assertEqual(process["shape"], "support-corner radial arcs")
        self.assertFalse(process["usesStraightInteriorBoundary"])
        self.assertFalse(process["usesHardQuadrantFill"])

    def test_uneven_routes_are_deterministic_and_visibly_warped(self) -> None:
        recovered = Image.new("L", (256, 256), 255)
        first, first_process = _support_corner_radial_fade(
            recovered, "outer-1of4-v2", radius_pixels=126.0,
            noise_fraction=0.09, seed=0x77A3,
            method="test-uneven-support-arcs")
        second, second_process = _support_corner_radial_fade(
            recovered, "outer-1of4-v2", radius_pixels=126.0,
            noise_fraction=0.09, seed=0x77A3,
            method="test-uneven-support-arcs")
        self.assertEqual(first.tobytes(), second.tobytes())
        self.assertEqual(first_process, second_process)
        circular, _ = _support_corner_radial_fade(
            recovered, "outer-1of4-v2", radius_pixels=126.0,
            noise_fraction=0.0, seed=0x77A3,
            method="test-unwarped-support-arcs")
        delta = np.abs(np.asarray(first, dtype=np.int16) -
                       np.asarray(circular, dtype=np.int16))
        # The coherent radius warp visibly changes a broad band, rather than
        # introducing isolated pixel noise or reproducing a perfect arc.
        self.assertGreater(int((delta > 5).sum()), 1000)
        self.assertGreater(int(delta.max()), 20)

    def test_all_four_candidates_preserve_canonical_corner_ownership(self) -> None:
        recovered = Image.new("L", (256, 256), 173)
        restored = Image.new("L", (256, 256), 201)
        topologies = {
            "mask-family-paving-courses-edge-2of4-v2": "edge-2of4-v2",
            "mask-family-paving-courses-outer-1of4-v2": "outer-1of4-v2",
            "mask-family-sand-grain-outer-1of4-v2": "outer-1of4-v2",
            "mask-family-snow-clumps-edge-2of4-v2": "edge-2of4-v2",
        }
        for key, topology in topologies.items():
            with self.subTest(key=key):
                first, _ = construct_candidate(
                    key, topology, recovered, restored)
                second, _ = construct_candidate(
                    key, topology, recovered, restored)
                self.assertEqual(first.tobytes(), second.tobytes())
                values = np.asarray(first)
                actual = (values[0, 0] == 255,
                          values[0, -1] == 255,
                          values[-1, -1] == 255,
                          values[-1, 0] == 255)
                self.assertEqual(actual, expected_owner_corners(topology))


if __name__ == "__main__":
    unittest.main()
