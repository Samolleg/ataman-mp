#!/usr/bin/env python3
"""Focused regressions for conservative authored blend-mask repair."""

from __future__ import annotations

import hashlib
import json
import math
from pathlib import Path
import unittest

import numpy as np
from PIL import Image

from render_authored_mask_repair_pilot import (
    HARD_BASELINE_SOURCE_GAPS,
    REMAINING_HARD_BASELINE_CASES,
    align_authored_mask,
    align_runtime_review_mask_exact,
    band_visualization,
    mask_metrics,
    repair_authored_mask,
    repair_reviewed_mask,
    repair_sand_banded_mask,
)
from render_soft_family_route_comparison import (
    CASES as SOFT_ROUTE_CASES,
    SOURCE_OVERRIDES,
    _align_source_image,
    _source_alignment,
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
    def test_authored_rgb_alignment_uses_exact_provenance_corners(self) -> None:
        payload = json.loads((Path(__file__).resolve().parents[1]
            / "assets" / "resources" / "terrain_atlas_presets.json")
            .read_text(encoding="utf-8"))
        presets = {preset["namespace"]: preset
                   for preset in payload["presets"]}
        outer = _source_alignment(
            presets, "Zone3Obr:235", "outer-1of4-v1", "water-ripples")
        edge = _source_alignment(
            presets, "BaseGipat:190", "edge-2of4-v1", "water-ripples")
        inside = _source_alignment(
            presets, "Zone3Obr:435", "inside-3of4-v1", "water-ripples")
        diagonal = _source_alignment(
            presets, "Zone1:351", "diagonal-2of4", "water-ripples")
        # Preset signatures describe the physical tile preview.  The stock
        # source must first receive the resolver's exact packed quarter-turn;
        # only then is the fixed preview-to-grid Y mirror applied.  Treating
        # the preset as a grid signature mirrors it twice and maps the wrong
        # material field (BaseGipat:190 used to become Grass / Common).
        self.assertEqual(outer["stockRotationClockwise"], 0)
        self.assertEqual(outer["mappedSourceMaterial"], "Ground / Water")
        self.assertEqual(edge["stockRotationClockwise"], 0)
        self.assertEqual(edge["mappedSourceMaterial"], "Ground / Water")
        self.assertEqual(inside["stockRotationClockwise"], 90)
        self.assertEqual(inside["mappedSourceMaterial"], "Ground / Water")
        self.assertEqual(diagonal["stockRotationClockwise"], 90)
        self.assertEqual(diagonal["mappedSourceMaterial"], "Ground / Water")
        self.assertTrue(all(value["fixedPreviewToGridMirrorY"]
                            for value in (outer, edge, inside, diagonal)))

    def test_diagonal_alignment_rotates_without_polarity_inversion(self) -> None:
        raw = np.zeros((64, 64), dtype=np.uint8)
        raw[:32, 32:] = 255
        raw[32:, :32] = 255
        image, transform = align_authored_mask(
            Image.fromarray(raw, "L"), "diagonal-2of4")
        values = np.asarray(image, dtype=np.uint8)
        self.assertEqual(
            (values[0, 0], values[0, -1], values[-1, -1], values[-1, 0]),
            (255, 0, 255, 0))
        self.assertFalse(transform["inverted"])
        self.assertIn(transform["rotationCounterClockwise"], (90, 270))

    def test_runtime_review_alignment_uses_only_fixed_grid_mirror(self) -> None:
        raw = np.zeros((8, 8), dtype=np.uint8)
        raw[:4, 4:] = 255
        raw[4:, :4] = 255
        aligned, transform = align_runtime_review_mask_exact(
            Image.fromarray(raw, "L"), "diagonal-2of4")
        values = np.asarray(aligned, dtype=np.uint8)
        self.assertEqual(
            (values[0, 0], values[0, -1], values[-1, -1], values[-1, 0]),
            (255, 0, 255, 0))
        self.assertEqual(transform["rotationCounterClockwise"], 0)
        self.assertTrue(transform["physicalPreviewToLogicalGridMirrorY"])

        source = Image.new("RGB", (4, 4))
        source.putpixel((0, 0), (1, 0, 0))
        source.putpixel((3, 0), (2, 0, 0))
        source.putpixel((3, 3), (3, 0, 0))
        source.putpixel((0, 3), (4, 0, 0))
        transformed = _align_source_image(source, {
            "stockRotationClockwise": 90})
        self.assertEqual(
            tuple(transformed.getpixel(point)[0] for point in
                  ((0, 0), (3, 0), (3, 3), (0, 3))),
            (3, 2, 1, 4))

    def test_r70_rejected_sand_diagonal_uses_alternate_source(self) -> None:
        self.assertEqual(
            SOURCE_OVERRIDES[("sand-grain", "diagonal-2of4")],
            "Zone7:423")

    def test_soft_route_batch_is_five_unique_family_topology_questions(self) -> None:
        self.assertEqual(len(SOFT_ROUTE_CASES), 5)
        self.assertEqual(len(set(SOFT_ROUTE_CASES)), 5)
        self.assertEqual(
            SOFT_ROUTE_CASES[0], ("sand-grain", "diagonal-2of4"))
        self.assertEqual(
            {topology for family, topology in SOFT_ROUTE_CASES
             if family == "water-ripples"},
            {"outer-1of4-v1", "edge-2of4-v1", "diagonal-2of4",
             "inside-3of4-v1"})

    def test_remaining_hard_batch_is_compact_unique_and_excludes_soft_routes(self) -> None:
        self.assertEqual(len(REMAINING_HARD_BASELINE_CASES), 10)
        self.assertEqual(len(set(REMAINING_HARD_BASELINE_CASES)), 10)
        families = {family for family, _ in REMAINING_HARD_BASELINE_CASES}
        self.assertEqual(
            families,
            {"grass-strands", "stone-fracture", "gravel-chunks",
             "paving-courses"})
        self.assertEqual(
            HARD_BASELINE_SOURCE_GAPS,
            (("soil-mottle", "diagonal-2of4"),))
        self.assertTrue(all(
            family not in {"sand-grain", "snow-clumps", "ice-fracture",
                           "water-ripples"}
            for family, _ in REMAINING_HARD_BASELINE_CASES))

    def test_r44_decisions_preserve_nine_approvals_and_three_sand_rejections(self) -> None:
        decision_path = Path(__file__).with_name(
            "authored_mask_repair_r44_decisions.json")
        payload = json.loads(decision_path.read_text(encoding="utf-8"))
        decisions = payload["decisions"]
        approved = [key for key, value in decisions.items()
                    if value["answer"] == "Approve repaired authored mask"]
        rejected = [key for key, value in decisions.items()
                    if value["answer"] == "Reject this source/topology"]
        self.assertEqual(len(approved), 9)
        self.assertEqual(len(rejected), 3)
        self.assertTrue(all("sand-grain" in key for key in rejected))
        self.assertTrue(all("opacity edge bands" in decisions[key]["note"]
                            for key in rejected))

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

    def test_approved_non_sand_repairs_keep_the_reviewed_single_field_path(self) -> None:
        source = synthetic_outer_mask(0)
        expected = repair_authored_mask(
            source, "outer-1of4-v1", seed=71)
        actual = repair_reviewed_mask(
            source, "grass-strands", "outer-1of4-v1", seed=71)
        self.assertEqual(actual[0].tobytes(), expected[0].tobytes())
        self.assertEqual(actual[1], expected[1])

    def test_sand_repair_preserves_strong_faceted_silhouette_and_adds_bands(self) -> None:
        source = synthetic_outer_mask(0)
        repaired, metrics = repair_sand_banded_mask(
            source, "outer-1of4-v1", seed=71)
        raw = np.asarray(source.convert("L"), dtype=np.uint8)
        result = np.asarray(repaired.convert("L"), dtype=np.uint8)
        self.assertTrue(np.array_equal(raw >= 128, result >= 128))
        self.assertEqual(metrics["strongIoU"], 1.0)
        self.assertEqual(metrics["repairMethod"], "faceted-opacity-bands")
        self.assertGreaterEqual(metrics["distinctEdgeBands"], 3)
        self.assertGreater(int((result == 48).sum()), 20)
        self.assertGreater(int((result == 144).sum()), 20)
        self.assertGreater(int((result == 216).sum()), 20)
        self.assertEqual(result[0, 0], 255)
        self.assertEqual(result[0, -1], 0)

    def test_sand_bands_are_nested_deterministic_and_not_global_blur(self) -> None:
        source = synthetic_outer_mask(1)
        first, first_metrics = repair_sand_banded_mask(
            source, "outer-1of4-v2", seed=2024)
        repeated, repeated_metrics = repair_sand_banded_mask(
            source, "outer-1of4-v2", seed=2024)
        values = np.asarray(first.convert("L"), dtype=np.uint8)
        self.assertEqual(first.tobytes(), repeated.tobytes())
        self.assertEqual(first_metrics, repeated_metrics)
        self.assertTrue(np.all((values >= 216) <= (values >= 144)))
        self.assertTrue(np.all((values >= 144) <= (values >= 48)))
        # A global Gaussian field would contain many arbitrary intermediate
        # values. The sand repair intentionally retains explicit authored-style
        # opacity plateaus and a hard strong-silhouette boundary.
        self.assertEqual(set(np.unique(values)).difference(
            {0, 48, 144, 216, 255}), set())
        preview = band_visualization(first)
        self.assertEqual(preview.mode, "RGB")
        self.assertEqual(preview.size, first.size)

    def test_review_dispatch_uses_banded_path_only_for_sand(self) -> None:
        source = synthetic_outer_mask(0)
        sand, sand_metrics = repair_reviewed_mask(
            source, "sand-grain", "outer-1of4-v1", seed=9)
        legacy, legacy_metrics = repair_authored_mask(
            source, "outer-1of4-v1", seed=9)
        self.assertEqual(sand_metrics["repairMethod"],
                         "faceted-opacity-bands")
        self.assertNotEqual(sand.tobytes(), legacy.tobytes())
        self.assertNotIn("repairMethod", legacy_metrics)


if __name__ == "__main__":
    unittest.main()
