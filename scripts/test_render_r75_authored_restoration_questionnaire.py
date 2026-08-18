#!/usr/bin/env python3
"""Focused regressions for the R75 authored-mask restoration workflow."""

from __future__ import annotations

import json
import hashlib
from pathlib import Path
import re
import tempfile
import unittest

import numpy as np
from PIL import Image

from render_r75_authored_restoration_questionnaire import (
    _align_candidate,
    _restore,
    _shared_edge_mask,
    expected_owner_corners,
)


class AuthoredRestorationTests(unittest.TestCase):
    def test_r74_decisions_partition_all_effective_masks(self) -> None:
        path = Path(__file__).with_name(
            "authored_mask_r74_effective_decisions.json")
        decisions = json.loads(path.read_text(encoding="utf-8"))
        accepted = set(decisions["accepted"])
        replacements = set(decisions["replaceWithAuthoredRestoration"])
        self.assertEqual(len(accepted), 16)
        self.assertEqual(len(replacements), 47)
        self.assertFalse(accepted & replacements)
        self.assertEqual(len(accepted | replacements), 63)
        self.assertIn(
            "Do not substitute a procedural geometric fallback",
            decisions["generationPolicy"])

    def test_alignment_uses_exact_corner_rotation(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for name, color in (("transition.png", (10, 20, 30)),
                                ("owner.png", (100, 110, 120)),
                                ("support.png", (200, 210, 220))):
                Image.new("RGB", (16, 16), color).save(root / name)
            candidate = {
                "source": "fixture:1",
                "owner_corners": [False, True, False, False],
                "transition_file": "transition.png",
                "owner_uniform_file": "owner.png",
                "support_uniform_file": "support.png",
            }
            _, _, _, alignment = _align_candidate(
                root, candidate, "outer-1of4-v1")
            self.assertEqual(
                tuple(alignment["canonicalOwnerCorners"]),
                expected_owner_corners("outer-1of4-v1"))
            self.assertEqual(
                alignment["orientationSelection"],
                "exact-corner-assignment-only")
            self.assertEqual(alignment["rotationCounterClockwise"], 90)

    def test_only_explicit_r75_approvals_are_installed(self) -> None:
        scripts = Path(__file__).resolve().parent
        root = scripts.parent
        decisions = json.loads((
            scripts / "authored_mask_r75_decisions.json"
        ).read_text(encoding="utf-8"))
        approved = set(decisions["approvedRestorations"])
        pending = set(decisions["adjustmentRequests"])
        self.assertEqual(len(approved), 27)
        self.assertEqual(len(pending), 20)
        self.assertFalse(approved & pending)

        source = (root / "source/documents/terrain_virtual_atlas.cpp").read_text(
            encoding="utf-8")
        r75_block = source.split(
            "static const SReviewedCanonicalMask r75Masks[] = {", 1
        )[1].split("    };", 1)[0]
        records = re.findall(
            r'\{ETerrainBlendStyle::(\w+), "([^"]+)",\s*'
            r'"[^"]*/([^/"]+)",\s*"([0-9a-f]{64})",\s*'
            r'"([^"]+:r75-approved-authored-restoration:[^"]+)"\}',
            r75_block)
        self.assertEqual(len(records), 27)
        self.assertEqual(len({(style, topology) for style, topology, *_ in records}), 27)

        qrc = (root / "assets/resources/terrain_blend_masks.qrc").read_text(
            encoding="utf-8")
        installed = set()
        for style, topology, filename, expected_hash, provenance in records:
            family = provenance.split(":", 2)[1]
            key = f"mask-family-{family}-{topology}"
            installed.add(key)
            self.assertIn(f"terrain_blend_masks/{filename}", qrc)
            image = Image.open(
                root / "assets/resources/terrain_blend_masks" / filename
            ).convert("L")
            self.assertEqual(hashlib.sha256(image.tobytes()).hexdigest(),
                             expected_hash)
        self.assertEqual(installed, approved)
        self.assertFalse(installed & pending)

    def test_runtime_edge_normalization_preserves_authored_interior(self) -> None:
        values = np.arange(32 * 32, dtype=np.uint16).reshape(32, 32) % 256
        mask = Image.fromarray(values.astype(np.uint8), "L")
        result = np.asarray(
            _shared_edge_mask(mask, "diagonal-2of4"), dtype=np.uint8)
        self.assertTrue(np.array_equal(
            result[1:-1, 1:-1], values.astype(np.uint8)[1:-1, 1:-1]))

    def test_only_approved_material_routes_are_selected(self) -> None:
        owner = Image.new("RGB", (256, 256), (220, 80, 40))
        support = Image.new("RGB", (256, 256), (20, 80, 180))
        transition = Image.new("RGB", (256, 256), (20, 80, 180))
        for y in range(256):
            for x in range(256):
                if x + y < 250:
                    transition.putpixel((x, y), (220, 80, 40))
        routes = {}
        for family in ("grass-strands", "paving-courses", "sand-grain"):
            _, _, effective, route, _ = _restore(
                family, "outer-1of4-v1", transition, owner, support, 17)
            routes[family] = route
            self.assertEqual(effective.size, (256, 256))
        self.assertEqual(routes, {
            "grass-strands": "conservative-authored-repair",
            "paving-courses": "raw-authored-corner-cleanup",
            "sand-grain": "transition-local-detailed-opacity",
        })


if __name__ == "__main__":
    unittest.main()
