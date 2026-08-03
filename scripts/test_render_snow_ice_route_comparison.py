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
    remove_disconnected_owner_quadrant_support,
)
from render_snow_ice_owner_plateau_review import (
    reconstruct_ice_owner_plateau,
)
from render_snow_ice_gradient_repair_review import (
    repair_owner_artifacts_with_edge_gradient,
)
from render_snow_ice_boundary_gradient_review import (
    repair_owner_artifacts_by_boundary_distance,
    validate_review_decisions as validate_r58_review_decisions,
)
from render_snow_ice_feathered_boundary_review import (
    repair_owner_artifacts_with_feathered_boundary,
    validate_review_decisions as validate_r59_review_decisions,
)
from render_snow_ice_directional_edge_review import (
    preserve_authored_detail_on_support_adjacent_edges,
    validate_review_decisions as validate_r60_review_decisions,
)
from render_snow_ice_pronounced_reach_review import (
    strengthen_connected_support_reach,
    validate_review_decisions as validate_r61_review_decisions,
)
from render_snow_ice_owner_reach_review import (
    raise_owner_opacity_toward_reviewed_reach,
    validate_review_decisions as validate_r62_review_decisions,
)
from render_snow_ice_continuous_owner_review import (
    apply_continuous_owner_opacity_reach,
    validate_review_decisions as validate_r63_review_decisions,
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
    def test_r63_approvals_verify_uniforms_and_both_outcomes(self) -> None:
        root = Path(__file__).resolve().parent.parent
        result = validate_r63_review_decisions(
            root / "artifacts" /
            "modern-atlas-r63-ice-continuous-owner-reach" /
            "questionnaire",
            Path(__file__).with_name(
                "authored_mask_ice_continuous_r63_decisions.json"),
            root / "assets" / "resources" /
            "terrain_atlas_presets.json")

        self.assertEqual("PASS", result["status"])
        self.assertEqual(2, result["approved"])
        self.assertEqual(0, result["pending"])
        self.assertEqual(4, result["approvedUniformReferencesVerified"])
        self.assertEqual(4, result["approvedRenderedOutcomesVerified"])
        self.assertEqual(
            "72e934360bd84b485f135dd5ee398c13e9fbd9fd382c207606f0a5e2971b9642",
            result["reviewedPresetSha256"])
        self.assertEqual(
            "7ca428aeb09a154138e15f69db40087f6ff60feb53d566c6ab95a74a974b9987",
            result["validatedPresetSha256"])
        self.assertTrue(result["presetCompatibilityReverified"])
        self.assertTrue(result["generationAuthorized"])

    def test_r62_is_rejected_for_visible_quadrant_cutoff(self) -> None:
        root = Path(__file__).resolve().parent.parent
        review = Path(__file__).with_name(
            "authored_mask_ice_owner_r62_decisions.json")
        result = validate_r62_review_decisions(
            root / "artifacts" / "modern-atlas-r62-ice-opaque-owner-reach" /
            "questionnaire", review)

        self.assertEqual("PASS", result["status"])
        self.assertEqual(0, result["approved"])
        self.assertEqual(2, result["rejected"])
        self.assertEqual(2, result["pending"])
        self.assertFalse(result["generationChanged"])

    def test_continuous_owner_reach_has_no_tile_midline_cut(self) -> None:
        authored = np.full((128, 128), 180, dtype=np.uint8)
        authored[64:, :64] = 0
        authored[58:70, :60] = 96
        baseline = authored.copy()

        repaired, audit = apply_continuous_owner_opacity_reach(
            Image.fromarray(authored, "L"),
            Image.fromarray(baseline, "L"), "inside-3of4-v1")
        result = np.asarray(repaired)

        vertical_jump = np.abs(
            result[:56, 63].astype(np.int16) -
            result[:56, 64].astype(np.int16))
        horizontal_jump = np.abs(
            result[63, 72:].astype(np.int16) -
            result[64, 72:].astype(np.int16))
        self.assertLessEqual(int(vertical_jump.max()), 3)
        self.assertLessEqual(int(horizontal_jump.max()), 3)
        self.assertTrue(np.all(result >= baseline))
        self.assertEqual(int(baseline[0, -1]), int(result[0, -1]))
        self.assertEqual(int(baseline[-1, 0]), int(result[-1, 0]))
        self.assertGreater(audit["pixelsMadeMoreOpaque"], 0)
        self.assertEqual(0, audit["pixelsMadeMoreTransparent"])
        self.assertEqual(0, audit["changedInsideExactEdge"])
        self.assertEqual(0, audit["changedSemanticCornerPixels"])
        self.assertFalse(audit["usesHardQuadrantClipping"])
        self.assertFalse(audit["usesHardCornerPatchClipping"])
        self.assertTrue(audit["usesRadialCornerHold"])

    def test_r61_is_rejected_for_lowering_owner_alpha(self) -> None:
        root = Path(__file__).resolve().parent.parent
        review = Path(__file__).with_name(
            "authored_mask_ice_reach_r61_decisions.json")
        result = validate_r61_review_decisions(
            root / "artifacts" /
            "modern-atlas-r61-ice-pronounced-support-reach" /
            "questionnaire", review)

        self.assertEqual("PASS", result["status"])
        self.assertEqual(0, result["approved"])
        self.assertEqual(2, result["rejected"])
        self.assertEqual(2, result["pending"])
        self.assertFalse(result["generationChanged"])

    def test_owner_reach_raises_alpha_without_touching_ne_sw_or_edge(
            self) -> None:
        authored = np.full((128, 128), 180, dtype=np.uint8)
        authored[64:, :64] = 0
        authored[58:70, :60] = 96
        baseline = authored.copy()

        repaired, audit = raise_owner_opacity_toward_reviewed_reach(
            Image.fromarray(authored, "L"),
            Image.fromarray(baseline, "L"), "inside-3of4-v1")
        result = np.asarray(repaired)

        self.assertGreater(int(result[16, 32]), int(baseline[16, 32]))
        self.assertGreater(int(result[96, 112]), int(baseline[96, 112]))
        np.testing.assert_array_equal(
            baseline[:64, 64:], result[:64, 64:])
        np.testing.assert_array_equal(
            baseline[64:, :64], result[64:, :64])
        np.testing.assert_array_equal(
            baseline[58:70, :60], result[58:70, :60])
        self.assertTrue(np.all(result >= baseline))
        self.assertGreater(audit["pixelsMadeMoreOpaque"], 0)
        self.assertEqual(0, audit["pixelsMadeMoreTransparent"])
        self.assertEqual(0, audit["changedNEPixels"])
        self.assertEqual(0, audit["changedSWPixels"])
        self.assertEqual(0, audit["changedInsideExactEdge"])
        self.assertEqual(0, audit["newFullOpacityPixels"])

    def test_r60_is_rejected_as_weaker_without_blue_guide_reach(
            self) -> None:
        root = Path(__file__).resolve().parent.parent
        review = Path(__file__).with_name(
            "authored_mask_ice_directional_r60_decisions.json")
        result = validate_r60_review_decisions(
            root / "artifacts" /
            "modern-atlas-r60-ice-directional-edge-feather" /
            "questionnaire", review)

        self.assertEqual("PASS", result["status"])
        self.assertEqual(0, result["approved"])
        self.assertEqual(2, result["rejected"])
        self.assertEqual(2, result["pending"])
        self.assertFalse(result["generationChanged"])

    def test_pronounced_reach_strengthens_connected_soft_support_only(
            self) -> None:
        authored = np.full((128, 128), 250, dtype=np.uint8)
        authored[64:, :64] = 0
        authored[52:64, :70] = 210
        authored[40:52, :76] = 230
        authored[28:40, :82] = 238
        authored[0:4, 96:100] = 210  # Detached beyond the reach band.
        baseline = authored.copy()

        repaired, audit = strengthen_connected_support_reach(
            Image.fromarray(authored, "L"),
            Image.fromarray(baseline, "L"), "inside-3of4-v1")
        result = np.asarray(repaired)

        self.assertLess(int(result[45, 60]), int(baseline[45, 60]))
        self.assertLess(int(result[57, 60]), int(result[45, 60]))
        np.testing.assert_array_equal(
            baseline[0:4, 96:100], result[0:4, 96:100])
        self.assertEqual(int(baseline[0, 100]), int(result[0, 100]))
        self.assertGreater(audit["changedConnectedReachPixels"], 0)
        self.assertEqual(0, audit["changedOutsideConnectedReachPixels"])
        self.assertGreater(audit["newSupportDominantPixels"], 0)
        self.assertEqual(0, audit["changedDistantOwnerPixels"])
        self.assertTrue(audit["preservesContinuousAuthoredDetail"])
        self.assertFalse(audit["usesGlobalBlur"])

    def test_r59_keeps_ne_but_requests_two_stronger_edge_shoulders(
            self) -> None:
        root = Path(__file__).resolve().parent.parent
        review = Path(__file__).with_name(
            "authored_mask_ice_feather_r59_decisions.json")
        result = validate_r59_review_decisions(
            root / "artifacts" / "modern-atlas-r59-ice-feathered-boundary" /
            "questionnaire", review)

        self.assertEqual("PASS", result["status"])
        self.assertEqual(0, result["approved"])
        self.assertEqual(2, result["rejected"])
        self.assertEqual(2, result["pending"])
        self.assertFalse(result["generationChanged"])

    def test_directional_edge_feather_preserves_ne_and_authored_floor(
            self) -> None:
        source = np.full((128, 128), 176, dtype=np.uint8)
        source[64:, :64] = 0
        source[56:72, :58] = 64
        rounded = source.copy()
        rounded[:64, :64] = 246
        rounded[:64, 64:] = 254
        rounded[64:, 64:] = 246

        repaired, audit = preserve_authored_detail_on_support_adjacent_edges(
            Image.fromarray(source, "L"), Image.fromarray(rounded, "L"),
            "inside-3of4-v1", shoulder_strength=0.90)
        result = np.asarray(repaired)

        np.testing.assert_array_equal(rounded[:64, 64:], result[:64, 64:])
        np.testing.assert_array_equal(source[64:, :64], result[64:, :64])
        self.assertLess(int(result[0, 32]), int(rounded[0, 32]))
        self.assertLess(int(result[96, -1]), int(rounded[96, -1]))
        self.assertEqual(int(rounded[0, 0]), int(result[0, 0]))
        self.assertEqual(int(rounded[-1, -1]), int(result[-1, -1]))
        self.assertTrue(np.all(result >= source))
        self.assertEqual(0, audit["changedNEPixels"])
        self.assertEqual(0, audit["changedSupportQuadrantPixels"])
        self.assertEqual(0, audit["changedOwnerCornerPatchPixels"])
        self.assertGreater(audit["changedNWNorthShoulderPixels"], 0)
        self.assertGreater(audit["changedSEEastShoulderPixels"], 0)
        self.assertEqual(0, audit["pixelsBelowAuthoredSource"])
        self.assertTrue(audit["preservesAuthoredContinuousAlpha"])

    def test_r58_rejects_square_cutoff_for_rounded_feather(self) -> None:
        root = Path(__file__).resolve().parent.parent
        review = Path(__file__).with_name(
            "authored_mask_ice_boundary_r58_decisions.json")
        result = validate_r58_review_decisions(
            root / "artifacts" / "modern-atlas-r58-ice-boundary-gradient" /
            "questionnaire", review)

        self.assertEqual("PASS", result["status"])
        self.assertEqual(0, result["approved"])
        self.assertEqual(2, result["rejected"])
        self.assertEqual(2, result["pending"])
        self.assertFalse(result["generationChanged"])

    def test_feathered_boundary_has_no_square_saturation_contour(
            self) -> None:
        values = np.full((128, 128), 220, dtype=np.uint8)
        values[64:, :64] = 0
        values[56:72, :58] = 64
        values[40:44, 70:74] = 20
        values[8:12, 108:112] = 20
        original = values.copy()

        repaired, audit = repair_owner_artifacts_with_feathered_boundary(
            Image.fromarray(values, "L"), "inside-3of4-v1",
            exact_edge_radius=4, feather_scale=24.0)
        result = np.asarray(repaired)

        np.testing.assert_array_equal(original[64:, :64],
                                      result[64:, :64])
        np.testing.assert_array_equal(original[56:72, :58],
                                      result[56:72, :58])
        self.assertGreater(int(result[41, 71]), int(original[41, 71]))
        self.assertGreater(int(result[9, 109]), int(result[41, 71]))
        self.assertLess(int(result[9, 109]), 255)
        self.assertEqual("chamfer-euclidean", audit["distanceMetric"])
        self.assertFalse(audit["usesHardSaturation"])
        self.assertEqual(0, audit["newFullOpacityPixels"])
        self.assertEqual(0, audit["changedConnectedSupportPixels"])
        self.assertEqual(0, audit["changedInsideExactEdgeRadius"])
        self.assertEqual(0, audit["changedSupportQuadrantPixels"])

    def test_r57_rejects_tile_gradient_for_boundary_distance(self) -> None:
        review = json.loads((Path(__file__).with_name(
            "authored_mask_ice_gradient_r57_decisions.json")).read_text(
                encoding="utf-8"))

        self.assertEqual([], review["approvedRoutes"])
        self.assertEqual(2, len(review["pendingRoutes"]))
        self.assertTrue(all(
            decision["answer"] == "Reject tile-position gradient repair" and
            "distance from the irregular authored blend edge" in
            decision["note"].casefold() and
            "quadrant-to-center geometric haze" in
            decision["note"].casefold()
            for decision in review["decisions"].values()))

    def test_boundary_gradient_thickens_with_distance_from_authored_edge(
            self) -> None:
        values = np.full((128, 128), 220, dtype=np.uint8)
        values[64:, :64] = 0
        values[56:72, :58] = 64       # Connected irregular support edge.
        values[40:44, 70:74] = 20    # Artifact near, but outside edge hold.
        values[8:12, 108:112] = 20   # Artifact far inside owner field.
        original = values.copy()

        repaired, audit = repair_owner_artifacts_by_boundary_distance(
            Image.fromarray(values, "L"), "inside-3of4-v1",
            exact_edge_radius=4, full_owner_radius=32)
        result = np.asarray(repaired)

        np.testing.assert_array_equal(original[64:, :64],
                                      result[64:, :64])
        np.testing.assert_array_equal(original[56:72, :58],
                                      result[56:72, :58])
        self.assertGreater(int(result[41, 71]), int(original[41, 71]))
        self.assertGreater(int(result[9, 109]), int(result[41, 71]))
        self.assertEqual(255, int(result[9, 109]))
        self.assertEqual(0, audit["changedConnectedSupportPixels"])
        self.assertEqual(0, audit["changedInsideExactEdgeRadius"])
        self.assertEqual(0, audit["changedSupportQuadrantPixels"])
        self.assertTrue(audit["usesBoundaryDistanceGradient"])
        self.assertFalse(audit["usesTilePositionGradient"])

    def test_r56_rejects_hard_plateau_and_requests_gradient(self) -> None:
        review = json.loads((Path(__file__).with_name(
            "authored_mask_ice_plateau_r56_decisions.json")).read_text(
                encoding="utf-8"))

        self.assertEqual([], review["approvedRoutes"])
        self.assertEqual(2, len(review["pendingRoutes"]))
        self.assertTrue(all(
            decision["answer"] ==
            "Reject targeted owner-plateau route" and
            "edge-to-center opacity gradient" in
            decision["note"].casefold() and
            "original blend edge" in decision["note"].casefold()
            for decision in review["decisions"].values()))

    def test_gradient_repair_lifts_artifacts_without_flattening_edge(
            self) -> None:
        values = np.full((128, 128), 220, dtype=np.uint8)
        values[64:, :64] = 0
        values[56:72, :58] = 64       # Connected authored support edge.
        values[18:26, 96:104] = 20   # Disconnected NE artifact.
        original = values.copy()

        repaired, audit = repair_owner_artifacts_with_edge_gradient(
            Image.fromarray(values, "L"), "inside-3of4-v1",
            exact_edge_radius=4, feather_radius=12)
        result = np.asarray(repaired)

        np.testing.assert_array_equal(original[64:, :64],
                                      result[64:, :64])
        np.testing.assert_array_equal(original[56:72, :58],
                                      result[56:72, :58])
        self.assertEqual(255, int(result[0, 100]))
        self.assertGreater(int(result[21, 99]), int(original[21, 99]))
        self.assertLess(int(result[21, 99]), 255)
        self.assertEqual(0, audit["changedConnectedSupportPixels"])
        self.assertEqual(0, audit["changedInsideExactEdgeRadius"])
        self.assertEqual(0, audit["changedSupportQuadrantPixels"])
        self.assertFalse(audit["usesSolidOwnerPlateau"])

    def test_r55_decisions_keep_both_owner_cleanup_routes_pending(
            self) -> None:
        review = json.loads((Path(__file__).with_name(
            "authored_mask_ice_owner_r55_decisions.json")).read_text(
                encoding="utf-8"))

        self.assertEqual([], review["approvedRoutes"])
        self.assertEqual({
            "route-compare-ice-fracture-inside-3of4-v1",
            "route-compare-ice-fracture-inside-3of4-v2",
        }, set(review["pendingRoutes"]))
        self.assertTrue(all(
            decision["answer"] == "Still uncertain" and
            "support-texture artifact" in decision["note"].casefold()
            for decision in review["decisions"].values()))

    def test_ice_owner_plateau_enables_targeted_false_hole_cleanup(
            self) -> None:
        pixels = np.full((128, 128, 3), (211, 225, 235), dtype=np.uint8)
        pixels[64:, :64] = (36, 86, 43)
        pixels[24:34, 92:102] = (36, 86, 43)

        mask, process = reconstruct_ice_owner_plateau(
            Image.fromarray(pixels, "RGB"), "inside-3of4-v1")
        values = np.asarray(mask)

        self.assertTrue(process["usesTargetedFalseHoleCleanup"])
        self.assertNotEqual(
            "disabled", process["boundedHoleSuppression"]["rule"])
        self.assertEqual(255, int(values[0, 0]))
        self.assertEqual(255, int(values[0, -1]))
        self.assertEqual(255, int(values[-1, -1]))
        self.assertEqual(0, int(values[-1, 0]))

    def test_r54_decisions_keep_only_ice_inside_pending(self) -> None:
        review = json.loads((Path(__file__).with_name(
            "authored_mask_snow_ice_r54_decisions.json")).read_text(
                encoding="utf-8"))
        approved = {
            key for key, decision in review["decisions"].items()
            if decision["answer"] ==
            "Approve current detailed route despite measured gate"
        }
        pending = {
            key for key, decision in review["decisions"].items()
            if decision["answer"] == "Still uncertain"
        }

        self.assertEqual({
            "route-compare-snow-clumps-diagonal-2of4",
            "route-compare-snow-clumps-inside-3of4-v1",
            "route-compare-snow-clumps-inside-3of4-v2",
            "route-compare-ice-fracture-diagonal-2of4",
        }, approved)
        self.assertEqual({
            "route-compare-ice-fracture-inside-3of4-v1",
            "route-compare-ice-fracture-inside-3of4-v2",
        }, pending)
        for key in pending:
            note = review["decisions"][key]["note"].casefold()
            self.assertIn("owner quadrant", note)
            self.assertIn("especially ne", note)

    def test_owner_cleanup_fills_only_disconnected_support_islands(
            self) -> None:
        values = np.full((256, 256), 255, dtype=np.uint8)
        values[128:, :128] = 0
        values[112:128, :96] = 72       # Support edge connected to SW.
        values[30:38, 200:208] = 24     # Unrelated NE support island.
        values[44:52, 180:188] = 230    # Sub-20% painterly opacity.
        original = values.copy()

        cleaned, audit = remove_disconnected_owner_quadrant_support(
            Image.fromarray(values, "L"), "inside-3of4-v1")
        result = np.asarray(cleaned)

        np.testing.assert_array_equal(original[128:, :128],
                                      result[128:, :128])
        np.testing.assert_array_equal(original[112:128, :96],
                                      result[112:128, :96])
        self.assertEqual(255, int(result[33, 203]))
        self.assertEqual(230, int(result[47, 183]))
        self.assertGreater(audit["filledPixels"], 0)
        self.assertEqual(["NW", "NE", "SE"], audit["ownerCorners"])
        self.assertEqual(["SW"], audit["supportCorners"])
        self.assertEqual(0, audit["changedInSupportQuadrants"])
        self.assertEqual(0, audit["connectedPixelsChanged"])

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
