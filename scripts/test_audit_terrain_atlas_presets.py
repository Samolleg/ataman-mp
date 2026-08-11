#!/usr/bin/env python3
"""Focused synthetic checks for semantic-atlas visual audit primitives."""

from __future__ import annotations

import base64
import ast
import unittest
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
from audit_terrain_atlas_presets import (
    archive_contains_preset,
    cross_atlas_uniform_anchor_groups,
    cross_atlas_review_components,
    intra_tile_corner_distances,
    ranked_anchor_candidates,
    verified_anchor_prototypes,
    uniform_exact_owner_review,
)
from audit_semantic_atlas_high_confidence import reviewed_keep_current
from audit_terrain_atlas_binding_conflicts import decoded_identity_rows
from render_semantic_atlas_review import (
    pending_review_namespaces,
    review_document_errors,
)
from render_pending_family_questionnaire import (
    archive_has_namespace,
    family_references,
    original_dimensions_are_lower,
    questionnaire_interaction_script,
    save_family_reference,
)
from audit_semantic_atlas_map_vertices import (
    D4_BASE_MAPPINGS,
    infer_target_corners,
    mapped_source_corner_for_world_corner,
    ranked_conventions,
    rotate_signature_clockwise,
    source_corner_for_world_corner,
)
from generate_terrain_atlas_presets import (
    apply_canonical_identity,
    corroborated_quadrant_identity_evidence,
    exact_pixel_identity_evidence,
    finalize_identity_decisions,
    reviewed_fragment_corrections,
    reviewed_unresolved_questionnaire_corrections,
    reviewed_zone2_stone_families,
)


_R102_RENAMED_EXPECTATIONS = {
    ("bz11k", "Ground / Trampled"): "Ground / Stony gray",
    ("zone14", "Ground / Trampled"): "Ground / Stony gray",
    ("bz13h", "Sand / Common"): "Sand / Common grayish",
    ("zone15", "Sand / Common"): "Sand / Common grayish",
    ("bz16h", "Sand / Common"): "Sand / Common grayish",
    ("zone18", "Sand / Common"): "Sand / Common grayish",
    ("zone13", "Sand / Common"): "Sand / Common grayish",
    ("zone19", "Sand / Common"): "Sand / Common grayish",
    ("bz4g", "Ground / Gravel"): "Ground / Gravel greenish",
    ("zone4", "Ground / Gravel"): "Ground / Gravel greenish",
    ("bz7g", "Stone / Black"): "Stone / Dark gray",
    ("zone10", "Stone / Black"): "Stone / Dark gray",
    ("zone20", "Stone / Black"): "Stone / Dark gray",
    ("zone5_1", "Stone / Black"): "Stone / Dark gray",
    ("zone7", "Ground / Cracked"): "Rubble / Gray",
    ("bz16h", "Rock / Lime"): "Paving / Sand ruins",
    ("bz16h", "Rock / Orange"): "Rubble / Dry",
    ("zone18", "Rock / Orange"): "Rubble / Dry",
    ("bz4g", "Sand / Dark"): "Sand / Dark pink",
    ("zone4", "Sand / Dark"): "Sand / Dark pink",
    ("bz3g", "Sand / Dark"): "Sand / Dark pink",
    ("zone9", "Sand / Dark"): "Sand / Dark pink",
    ("bz5g", "Sand / Dark"): "Sand / Dark pink",
    ("bz6g", "Sand / Dark"): "Sand / Dark pink",
    ("zone8", "Sand / Dark"): "Sand / Dark pink",
}


def r102_expected_name(namespace: str, historical_name: str) -> str:
    """Translate an earlier reviewed label to its explicit R102 final name."""
    return _R102_RENAMED_EXPECTATIONS.get(
        (namespace.casefold(), historical_name), historical_name)


class ExactPixelIdentityOwnershipTests(unittest.TestCase):
    def test_exact_local_pixels_cannot_overwrite_authored_transition(self) -> None:
        uniform = np.full((32, 32, 4), (40, 80, 120, 255), dtype=np.uint8)
        transition = np.full((32, 32, 4), (190, 30, 20, 255), dtype=np.uint8)
        transition[:8, :8] = uniform[:8, :8]
        recipe = {
            "namespace": "synthetic",
            "families": [{"id": 1}, {"id": 2}, {"id": 3}],
            "slots": [
                {"slot": 0, "signature": {
                    "northWest": 1, "northEast": 1,
                    "southEast": 1, "southWest": 1}},
                {"slot": 1, "signature": {
                    "northWest": 2, "northEast": 3,
                    "southEast": 3, "southWest": 3}},
            ],
        }

        audited, suggestions, ambiguities, decisions = (
            exact_pixel_identity_evidence(recipe, [uniform, transition]))

        self.assertEqual(recipe, audited)
        self.assertEqual(2, audited["slots"][1]["signature"]["northWest"])
        self.assertEqual(1, suggestions)
        self.assertEqual(0, ambiguities)
        self.assertEqual("suggestion", decisions[0]["kind"])
        self.assertEqual([1], decisions[0]["candidateFamilyIds"])


class CorroboratedQuadrantIdentityTests(unittest.TestCase):
    @staticmethod
    def family(family_id: int, layers=("land",), underwater=False) -> dict:
        return {
            "id": family_id,
            "allowedLayers": list(layers),
            "underwater": underwater,
        }

    @staticmethod
    def slot(slot_id: int, family_id: int) -> dict:
        return {"slot": slot_id, "signature": {
            "northWest": family_id, "northEast": family_id,
            "southEast": family_id, "southWest": family_id}}

    def test_two_independent_full_quadrant_anchors_allow_exact_repair(self) -> None:
        anchor = np.full((32, 32, 4), (40, 80, 120, 255), dtype=np.uint8)
        target = np.full((32, 32, 4), (180, 20, 10, 255), dtype=np.uint8)
        target[:16, :16] = anchor[:16, :16]
        recipe = {
            "namespace": "synthetic",
            "families": [self.family(1), self.family(2), self.family(3)],
            "slots": [self.slot(0, 1), self.slot(1, 1), {
                "slot": 2, "signature": {
                    "northWest": 2, "northEast": 3,
                    "southEast": 3, "southWest": 3}}],
        }

        audited, accepted, suspicious = corroborated_quadrant_identity_evidence(
            recipe, [anchor, anchor.copy(), target], apply=True)

        self.assertEqual(1, len(accepted))
        self.assertEqual([], suspicious)
        self.assertEqual(1, audited["slots"][2]["signature"]["northWest"])
        self.assertEqual([0, 1], accepted[0]["candidateFamilies"][0]["anchorSlots"])

    def test_one_anchor_is_review_only(self) -> None:
        anchor = np.full((32, 32, 4), (40, 80, 120, 255), dtype=np.uint8)
        target = np.full((32, 32, 4), (180, 20, 10, 255), dtype=np.uint8)
        target[:16, :16] = anchor[:16, :16]
        recipe = {
            "namespace": "synthetic",
            "families": [self.family(1), self.family(2), self.family(3)],
            "slots": [self.slot(0, 1), {
                "slot": 1, "signature": {
                    "northWest": 2, "northEast": 3,
                    "southEast": 3, "southWest": 3}}],
        }

        audited, accepted, suspicious = corroborated_quadrant_identity_evidence(
            recipe, [anchor, target], apply=True)

        self.assertEqual([], accepted)
        self.assertEqual(1, len(suspicious))
        self.assertEqual("fewer-than-two-independent-anchors",
                         suspicious[0]["reason"])
        self.assertEqual(2, audited["slots"][1]["signature"]["northWest"])

    def test_tiny_shared_vertex_patch_is_not_full_quadrant_evidence(self) -> None:
        anchor = np.full((32, 32, 4), (40, 80, 120, 255), dtype=np.uint8)
        target = np.full((32, 32, 4), (180, 20, 10, 255), dtype=np.uint8)
        target[:8, :8] = anchor[:8, :8]
        recipe = {
            "namespace": "synthetic",
            "families": [self.family(1), self.family(2), self.family(3)],
            "slots": [self.slot(0, 1), self.slot(1, 1), {
                "slot": 2, "signature": {
                    "northWest": 2, "northEast": 3,
                    "southEast": 3, "southWest": 3}}],
        }

        audited, accepted, suspicious = corroborated_quadrant_identity_evidence(
            recipe, [anchor, anchor.copy(), target], apply=True)

        self.assertEqual([], accepted)
        self.assertEqual([], suspicious)
        self.assertEqual(2, audited["slots"][2]["signature"]["northWest"])

    def test_layer_or_underwater_mismatch_cannot_reassign(self) -> None:
        anchor = np.full((32, 32, 4), (40, 80, 120, 255), dtype=np.uint8)
        recipe = {
            "namespace": "synthetic",
            "families": [
                self.family(1, ("overlay",)),
                self.family(2, ("land",)),
                self.family(3, ("land",), underwater=True),
            ],
            "slots": [self.slot(0, 1), self.slot(1, 1), self.slot(2, 2),
                      self.slot(3, 3)],
        }

        audited, accepted, suspicious = corroborated_quadrant_identity_evidence(
            recipe, [anchor.copy() for _ in range(4)], apply=True)

        self.assertEqual([], accepted)
        self.assertEqual([], suspicious)
        self.assertEqual(recipe, audited)

    def test_r76_reviewed_keep_requires_exact_location_and_families(self) -> None:
        record = {
            "namespace": "Zone16", "slot": 146,
            "corner": "northWest", "currentFamilyName": "Ground / Tan",
            "candidateFamilies": [{"familyName": "Stone / Red"}],
        }
        self.assertTrue(reviewed_keep_current(record))
        self.assertFalse(reviewed_keep_current({**record, "slot": 147}))
        self.assertFalse(reviewed_keep_current({
            **record,
            "candidateFamilies": [{"familyName": "Rubble / Red"}],
        }))


class AuthoredMapVertexAuditTests(unittest.TestCase):
    def test_clockwise_rotation_maps_preview_corners_into_world_space(self) -> None:
        signature = (1, 2, 3, 4)
        self.assertEqual((1, 2, 3, 4), rotate_signature_clockwise(signature, 0))
        self.assertEqual((4, 1, 2, 3), rotate_signature_clockwise(signature, 1))
        self.assertEqual((3, 4, 1, 2), rotate_signature_clockwise(signature, 2))
        self.assertEqual((2, 3, 4, 1), rotate_signature_clockwise(signature, 3))
        self.assertEqual(0, source_corner_for_world_corner(1, 1))
        self.assertEqual(
            3,
            mapped_source_corner_for_world_corner(
                0, 0, D4_BASE_MAPPINGS["mirror-vertical"], 1))

    def test_neighbor_vertices_identify_suspect_uniform_corners(self) -> None:
        # Slot 0 is the suspect tile. Every independently assigned incident
        # neighbor is family 2, so all four of its source corners receive one
        # unanimous authored-map vote for family 2.
        grid = [[(1, 0) for _ in range(3)] for _ in range(3)]
        grid[1][1] = (0, 0)
        result = infer_target_corners(grid, [(1, 1, 1, 1), (2, 2, 2, 2)], [0])
        self.assertEqual(1, result[0]["occurrences"])
        for corner in ("NW", "NE", "SE", "SW"):
            self.assertEqual({2: 1}, result[0]["corners"][corner]["consensusVotes"])
            self.assertEqual(0, result[0]["corners"][corner]["disagreeingVertices"])

    def test_whole_map_audit_keeps_other_target_slots_as_evidence(self) -> None:
        # Whole-map discovery selects both slots.  Copies of focal slot 0 are
        # still excluded from its vote, while the distinct slot 1 remains
        # valid evidence for family 2 at all four corners.
        grid = [[(1, 0) for _ in range(3)] for _ in range(3)]
        grid[1][1] = (0, 0)
        result = infer_target_corners(
            grid, [(1, 1, 1, 1), (2, 2, 2, 2)], [0, 1],
            exclude_all_target_slots=False)
        for corner in ("NW", "NE", "SE", "SW"):
            self.assertEqual(
                {2: 1}, result[0]["corners"][corner]["consensusVotes"])

    def test_authored_vertex_consistency_selects_identity_convention(self) -> None:
        # Only the central vertex matters. Under the identity convention its
        # four incident corners are family 9; mirrored conventions break it.
        signatures = [
            (1, 2, 9, 4),  # northwest tile contributes SE
            (1, 2, 3, 9),  # northeast tile contributes SW
            (1, 9, 3, 4),  # southwest tile contributes NE
            (9, 2, 3, 4),  # southeast tile contributes NW
        ]
        grid = [[(0, 0), (1, 0)], [(2, 0), (3, 0)]]
        ranking = ranked_conventions(grid, signatures)
        self.assertEqual("identity", ranking[0]["baseMapping"])
        self.assertEqual(1, ranking[0]["unanimousVertices"])

    def test_log_final_family_uses_post_canonical_alias_identity(self) -> None:
        decisions = [{
            "kind": "ambiguous",
            "namespace": "alias",
            "slot": 7,
            "corner": "southWest",
            "currentFamilyId": 6,
            "candidateFamilyIds": [5, 10],
        }]
        emitted_recipes = [{
            "namespace": "alias",
            "slots": [{"slot": 7, "signature": {
                "northWest": 6, "northEast": 10,
                "southEast": 6, "southWest": 5,
            }}],
        }]

        finalize_identity_decisions(decisions, emitted_recipes)

        self.assertEqual(5, decisions[0]["finalFamilyId"])
        self.assertTrue(decisions[0]["appliedToReviewedRecipe"])


class Zone2StoneIdentityTests(unittest.TestCase):
    @staticmethod
    def signature(family_id: int) -> dict:
        return {
            "northWest": family_id, "northEast": family_id,
            "southEast": family_id, "southWest": family_id,
        }

    def test_visible_variants_converge_without_forcing_brown_into_stone(self) -> None:
        tiles = [np.full((32, 32, 4), (30, 100, 30, 255), dtype=np.uint8)
                 for _ in range(512)]
        anchor_slots = {
            4: (113, 114, 134, 138, 324),
            6: (164, 205, 206, 162, 279, 496),
            8: (165,),
            23: (285, 339, 401),
        }
        colors = {
            4: (118, 124, 112, 255),
            6: (148, 101, 116, 255),
            8: (104, 72, 39, 255),
            23: (67, 103, 55, 255),
        }
        slots = []
        for family_id, indices in anchor_slots.items():
            source_id = 5 if family_id == 23 else family_id
            for slot in indices:
                tiles[slot][:] = colors[family_id]
                slots.append({"slot": slot,
                              "signature": self.signature(source_id)})

        tiles[179][:] = colors[6]
        slots.append({"slot": 179, "signature": self.signature(10)})
        tiles[439][:16, :16] = colors[8]
        tiles[439][:16, 16:] = colors[4]
        tiles[439][16:, 16:] = colors[6]
        tiles[439][16:, :16] = colors[6]
        slots.append({"slot": 439, "signature": {
            "northWest": 11, "northEast": 4,
            "southEast": 6, "southWest": 6}})
        slots.sort(key=lambda value: value["slot"])
        families = [
            {"id": family_id, "uniformSlots": [165] if family_id == 8 else [],
             "suggestedName": str(family_id), "thumbnailSlot": 0}
            for family_id in (4, 5, 6, 8, 10, 11, 19)
        ]
        recipe = {"namespace": "Zone2", "slots": slots,
                  "families": families}

        result = reviewed_zone2_stone_families(recipe, tiles)
        by_slot = {slot["slot"]: slot["signature"]
                   for slot in result["slots"]}

        self.assertEqual({6}, set(by_slot[179].values()))
        self.assertEqual(8, by_slot[439]["northWest"])
        self.assertEqual(4, by_slot[439]["northEast"])
        self.assertEqual(6, by_slot[439]["southEast"])
        self.assertEqual(6, by_slot[439]["southWest"])
        by_family = {family["id"]: family for family in result["families"]}
        self.assertEqual("Stone / Lime", by_family[4]["suggestedName"])


class ReviewedFragmentCorrectionTests(unittest.TestCase):
    @staticmethod
    def uniform_slots(count: int, family_id: int = 1) -> list[dict]:
        return [{"slot": slot, "signature": {
            "northWest": family_id, "northEast": family_id,
            "southEast": family_id, "southWest": family_id,
        }} for slot in range(count)]

    def test_zone18_red_false_anchors_form_rotated_algae_stamps(self) -> None:
        for namespace in ("Zone18", "bz16h"):
            recipe = {
                "namespace": namespace,
                "slots": self.uniform_slots(512, 4),
                "families": [{"id": family_id}
                             for family_id in range(1, 13)],
            }

            result = reviewed_fragment_corrections(recipe)
            by_slot = {slot["slot"]: slot["signature"]
                       for slot in result["slots"]}

            self.assertEqual(5, by_slot[397]["northWest"])
            self.assertEqual(5, by_slot[400]["northWest"])
            self.assertEqual(5, by_slot[413]["northEast"])
            self.assertEqual(5, by_slot[441]["southEast"])
            for slot, corner in ((397, "northEast"), (400, "southWest"),
                                 (413, "southWest"), (441, "northWest")):
                self.assertEqual(4, by_slot[slot][corner])

    def test_zone2_reviewed_splatter_tiles_use_vertex_corner_identity(self) -> None:
        recipe = {
            "namespace": "Zone2",
            "slots": self.uniform_slots(512, 1),
            "families": [{"id": family_id}
                         for family_id in range(1, 24)],
        }

        result = reviewed_fragment_corrections(recipe)
        by_slot = {slot["slot"]: slot["signature"]
                   for slot in result["slots"]}

        self.assertEqual(
            {"northWest": 2, "northEast": 1,
             "southEast": 2, "southWest": 1}, by_slot[128])
        self.assertEqual(
            {"northWest": 2, "northEast": 1,
             "southEast": 2, "southWest": 1}, by_slot[129])
        self.assertEqual(
            {"northWest": 7, "northEast": 2,
             "southEast": 2, "southWest": 7}, by_slot[500])

    def test_zone2_faded_transitions_are_not_uniform_interiors(self) -> None:
        recipe = {
            "namespace": "Zone2",
            "slots": self.uniform_slots(512, 7),
            "families": [{"id": family_id}
                         for family_id in range(1, 24)],
        }

        result = reviewed_fragment_corrections(recipe)
        by_slot = {slot["slot"]: slot["signature"]
                   for slot in result["slots"]}

        self.assertEqual(
            {"northWest": 8, "northEast": 7,
             "southEast": 7, "southWest": 7}, by_slot[268])
        self.assertEqual(
            {"northWest": 7, "northEast": 7,
             "southEast": 8, "southWest": 7}, by_slot[284])
        for slot in (268, 284):
            self.assertGreater(len(set(by_slot[slot].values())), 1)

    def test_zone2_checkerboard_review_separates_faded_and_burnt(self) -> None:
        slots = self.uniform_slots(512, 7)
        slots[198]["signature"]["northWest"] = 14
        slots[493]["signature"]["southEast"] = 14
        slots[494]["signature"]["southEast"] = 14
        recipe = {
            "namespace": "Zone2",
            "slots": slots,
            "families": [{"id": family_id}
                         for family_id in range(1, 24)],
        }

        result = reviewed_fragment_corrections(recipe)
        by_slot = {slot["slot"]: slot["signature"]
                   for slot in result["slots"]}

        expected = {
            26: (7, 7, 7, 7),
            142: (7, 14, 14, 14),
            195: (14, 14, 14, 14),
            299: (8, 14, 14, 14),
            198: (14, 8, 14, 14),
            493: (7, 14, 14, 14),
            494: (7, 8, 14, 14),
        }
        names = ("northWest", "northEast", "southEast", "southWest")
        for slot, signature in expected.items():
            self.assertEqual(signature,
                             tuple(by_slot[slot][name] for name in names))
        families = {family["id"]: family for family in result["families"]}
        self.assertEqual(195, families[14]["thumbnailSlot"])

    def test_bz3g_radial_false_anchors_keep_all_four_orientations(self) -> None:
        recipe = {
            "namespace": "bz3g",
            "slots": self.uniform_slots(512),
            "families": [{"id": family_id}
                         for family_id in range(1, 24)],
        }

        result = reviewed_fragment_corrections(recipe)
        by_slot = {slot["slot"]: slot["signature"]
                   for slot in result["slots"]}

        self.assertEqual(
            {"northWest": 11, "northEast": 11,
             "southEast": 5, "southWest": 7}, by_slot[335])
        self.assertEqual(
            {"northWest": 3, "northEast": 11,
             "southEast": 11, "southWest": 7}, by_slot[481])

    def test_intra_tile_false_uniforms_recover_connected_signatures(self) -> None:
        cases = {
            "zone2": {
                339: {"northWest": 23, "northEast": 23,
                      "southEast": 4, "southWest": 4},
                401: {"northWest": 23, "northEast": 23,
                      "southEast": 23, "southWest": 4},
            },
            "zone8": {
                420: {"northWest": 8, "northEast": 4,
                      "southEast": 4, "southWest": 5},
                428: {"northWest": 10, "northEast": 5,
                      "southEast": 8, "southWest": 8},
                # R80 explicitly reclassifies the NE quarter as Ground /
                # Common after the earlier false-uniform recovery.
                445: {"northWest": 10, "northEast": 6,
                      "southEast": 10, "southWest": 5},
            },
            "zone14": {
                497: {"northWest": 8, "northEast": 1,
                      "southEast": 7, "southWest": 7},
                506: {"northWest": 7, "northEast": 10,
                      "southEast": 10, "southWest": 10},
                64: {"northWest": 8, "northEast": 1,
                     "southEast": 8, "southWest": 16},
            },
            "zone6": {
                148: {"northWest": 4, "northEast": 2,
                      "southEast": 4, "southWest": 2},
                216: {"northWest": 8, "northEast": 8,
                      "southEast": 12, "southWest": 12},
                318: {"northWest": 4, "northEast": 4,
                      "southEast": 2, "southWest": 4},
                230: {"northWest": 8, "northEast": 12,
                      "southEast": 12, "southWest": 12},
                231: {"northWest": 8, "northEast": 12,
                      "southEast": 12, "southWest": 12},
            },
            "zone6_2": {
                148: {"northWest": 4, "northEast": 2,
                      "southEast": 4, "southWest": 2},
                216: {"northWest": 8, "northEast": 8,
                      "southEast": 15, "southWest": 15},
                318: {"northWest": 4, "northEast": 4,
                      "southEast": 2, "southWest": 4},
                230: {"northWest": 8, "northEast": 15,
                      "southEast": 15, "southWest": 15},
                231: {"northWest": 8, "northEast": 15,
                      "southEast": 15, "southWest": 15},
            },
            "zone7": {
                44: {"northWest": 3, "northEast": 3,
                     "southEast": 2, "southWest": 2},
                171: {"northWest": 3, "northEast": 2,
                      "southEast": 3, "southWest": 3},
                173: {"northWest": 2, "northEast": 3,
                      "southEast": 3, "southWest": 3},
                193: {"northWest": 3, "northEast": 3,
                      "southEast": 2, "southWest": 3},
                199: {"northWest": 2, "northEast": 3,
                      "southEast": 3, "southWest": 3},
            },
            "bz4g": {
                58: {"northWest": 1, "northEast": 9,
                     "southEast": 9, "southWest": 9},
                59: {"northWest": 1, "northEast": 9,
                     "southEast": 9, "southWest": 9},
            },
            "bz9k": {
                37: {"northWest": 3, "northEast": 1,
                     "southEast": 1, "southWest": 1},
                40: {"northWest": 3, "northEast": 1,
                     "southEast": 1, "southWest": 1},
                43: {"northWest": 3, "northEast": 1,
                     "southEast": 1, "southWest": 1},
                45: {"northWest": 3, "northEast": 1,
                     "southEast": 1, "southWest": 1},
                371: {"northWest": 6, "northEast": 6,
                      "southEast": 6, "southWest": 10},
                372: {"northWest": 6, "northEast": 6,
                      "southEast": 6, "southWest": 10},
                374: {"northWest": 6, "northEast": 6,
                      "southEast": 6, "southWest": 10},
            },
            "zone10": {
                153: {"northWest": 2, "northEast": 1,
                      "southEast": 3, "southWest": 3},
            },
            "zone13": {
                312: {"northWest": 3, "northEast": 6,
                      "southEast": 6, "southWest": 6},
            },
            "zone19": {
                152: {"northWest": 1, "northEast": 1,
                      "southEast": 1, "southWest": 3},
                180: {"northWest": 10, "northEast": 10,
                      "southEast": 10, "southWest": 2},
                193: {"northWest": 10, "northEast": 2,
                      "southEast": 2, "southWest": 2},
                292: {"northWest": 10, "northEast": 4,
                      "southEast": 4, "southWest": 4},
                445: {"northWest": 6, "northEast": 1,
                      "southEast": 3, "southWest": 3},
                65: {"northWest": 1, "northEast": 1,
                     "southEast": 4, "southWest": 4},
                153: {"northWest": 1, "northEast": 4,
                      "southEast": 4, "southWest": 4},
                189: {"northWest": 10, "northEast": 2,
                      "southEast": 2, "southWest": 2},
                226: {"northWest": 6, "northEast": 6,
                      "southEast": 4, "southWest": 4},
                241: {"northWest": 2, "northEast": 4,
                      "southEast": 4, "southWest": 4},
                252: {"northWest": 6, "northEast": 4,
                      "southEast": 4, "southWest": 4},
                296: {"northWest": 10, "northEast": 4,
                      "southEast": 4, "southWest": 4},
                435: {"northWest": 7, "northEast": 4,
                      "southEast": 4, "southWest": 4},
            },
            "zone20": {
                43: {"northWest": 2, "northEast": 3,
                     "southEast": 3, "southWest": 3},
                45: {"northWest": 2, "northEast": 3,
                     "southEast": 3, "southWest": 3},
                49: {"northWest": 2, "northEast": 3,
                     "southEast": 3, "southWest": 3},
                51: {"northWest": 2, "northEast": 3,
                     "southEast": 3, "southWest": 3},
                53: {"northWest": 2, "northEast": 3,
                     "southEast": 3, "southWest": 3},
                55: {"northWest": 2, "northEast": 3,
                     "southEast": 3, "southWest": 3},
                56: {"northWest": 2, "northEast": 3,
                     "southEast": 3, "southWest": 3},
                58: {"northWest": 2, "northEast": 3,
                     "southEast": 3, "southWest": 3},
                60: {"northWest": 2, "northEast": 3,
                     "southEast": 3, "southWest": 3},
                62: {"northWest": 2, "northEast": 3,
                     "southEast": 3, "southWest": 3},
            },
            "zone16": {
                118: {"northWest": 4, "northEast": 5,
                      "southEast": 5, "southWest": 5},
                119: {"northWest": 4, "northEast": 5,
                      "southEast": 5, "southWest": 5},
                125: {"northWest": 4, "northEast": 5,
                      "southEast": 5, "southWest": 5},
                160: {"northWest": 5, "northEast": 5,
                      "southEast": 5, "southWest": 2},
                238: {"northWest": 5, "northEast": 5,
                      "southEast": 4, "southWest": 2},
                255: {"northWest": 5, "northEast": 2,
                      "southEast": 5, "southWest": 2},
            },
            "zone17": {
                20: {"northWest": 3, "northEast": 1,
                     "southEast": 1, "southWest": 1},
                22: {"northWest": 3, "northEast": 1,
                     "southEast": 1, "southWest": 1},
                24: {"northWest": 3, "northEast": 1,
                     "southEast": 1, "southWest": 1},
                28: {"northWest": 3, "northEast": 1,
                     "southEast": 1, "southWest": 1},
                29: {"northWest": 3, "northEast": 1,
                     "southEast": 1, "southWest": 1},
                30: {"northWest": 3, "northEast": 3,
                     "southEast": 3, "southWest": 1},
                31: {"northWest": 3, "northEast": 3,
                     "southEast": 3, "southWest": 1},
                32: {"northWest": 3, "northEast": 3,
                     "southEast": 3, "southWest": 1},
                33: {"northWest": 3, "northEast": 3,
                     "southEast": 3, "southWest": 1},
                34: {"northWest": 3, "northEast": 3,
                     "southEast": 3, "southWest": 1},
                38: {"northWest": 3, "northEast": 3,
                     "southEast": 3, "southWest": 1},
                39: {"northWest": 3, "northEast": 3,
                     "southEast": 3, "southWest": 1},
            },
            "zone15": {
                106: {"northWest": 3, "northEast": 3,
                      "southEast": 3, "southWest": 7},
                170: {"northWest": 3, "northEast": 7,
                      "southEast": 7, "southWest": 7},
                201: {"northWest": 3, "northEast": 3,
                      "southEast": 3, "southWest": 11},
                203: {"northWest": 3, "northEast": 3,
                      "southEast": 3, "southWest": 11},
                204: {"northWest": 3, "northEast": 3,
                      "southEast": 3, "southWest": 11},
                205: {"northWest": 3, "northEast": 3,
                      "southEast": 3, "southWest": 11},
                206: {"northWest": 3, "northEast": 3,
                      "southEast": 3, "southWest": 11},
            },
            "bz3g": {
                312: {"northWest": 11, "northEast": 6,
                      "southEast": 6, "southWest": 6},
                # R116 resolves the held NW corner from Gravel to Common.
                388: {"northWest": 4, "northEast": 4,
                      "southEast": 1, "southWest": 4},
                510: {"northWest": 4, "northEast": 4,
                      "southEast": 13, "southWest": 1},
                23: {"northWest": 5, "northEast": 3,
                     "southEast": 3, "southWest": 3},
                109: {"northWest": 3, "northEast": 3,
                      "southEast": 5, "southWest": 6},
                387: {"northWest": 5, "northEast": 5,
                      "southEast": 3, "southWest": 3},
                389: {"northWest": 3, "northEast": 5,
                      "southEast": 3, "southWest": 3},
                330: {"northWest": 1, "northEast": 5,
                      "southEast": 6, "southWest": 5},
            },
        }
        original_families = {
            ("zone2", 339): 23, ("zone2", 401): 23,
            ("zone8", 420): 5, ("zone8", 428): 8,
            ("zone8", 445): 8, ("zone14", 497): 13,
            ("zone14", 506): 10,
            ("zone14", 64): 11,
            ("zone6", 148): 2, ("zone6", 216): 12,
            ("zone6", 318): 4,
            ("zone6", 230): 12, ("zone6", 231): 12,
            ("zone6_2", 148): 2, ("zone6_2", 216): 15,
            ("zone6_2", 318): 4,
            ("zone6_2", 230): 15, ("zone6_2", 231): 15,
            ("zone7", 44): 2,
            ("zone7", 171): 3, ("zone7", 173): 3,
            ("zone7", 193): 3, ("zone7", 199): 3,
            ("bz4g", 58): 9, ("bz4g", 59): 9,
            ("bz9k", 37): 1, ("bz9k", 40): 1,
            ("bz9k", 43): 1, ("bz9k", 45): 1,
            ("bz9k", 371): 6, ("bz9k", 372): 6,
            ("bz9k", 374): 6,
            ("zone10", 153): 3,
            ("zone13", 312): 6,
            ("zone19", 152): 1, ("zone19", 180): 10,
            ("zone19", 193): 2, ("zone19", 292): 4,
            ("zone19", 445): 3,
            ("zone19", 65): 4, ("zone19", 153): 4,
            ("zone19", 189): 2, ("zone19", 226): 4,
            ("zone19", 241): 4, ("zone19", 252): 4,
            ("zone19", 296): 4, ("zone19", 435): 4,
            ("zone20", 43): 3, ("zone20", 45): 3,
            ("zone20", 49): 3, ("zone20", 51): 3,
            ("zone20", 53): 3, ("zone20", 55): 3,
            ("zone20", 56): 3, ("zone20", 58): 3,
            ("zone20", 60): 3, ("zone20", 62): 3,
            ("zone16", 118): 5, ("zone16", 119): 5,
            ("zone16", 125): 5, ("zone16", 160): 5,
            ("zone16", 238): 5, ("zone16", 255): 5,
            ("zone17", 20): 1, ("zone17", 22): 1,
            ("zone17", 24): 1, ("zone17", 28): 1,
            ("zone17", 29): 1, ("zone17", 30): 3,
            ("zone17", 31): 3, ("zone17", 32): 3,
            ("zone17", 33): 3, ("zone17", 34): 3,
            ("zone17", 38): 3, ("zone17", 39): 3,
            ("zone15", 106): 3, ("zone15", 170): 7,
            ("zone15", 201): 3, ("zone15", 203): 3,
            ("zone15", 204): 3, ("zone15", 205): 3,
            ("zone15", 206): 3,
            ("bz3g", 312): 6,
            # R116 supersedes the older Gravel assignment with Common.
            ("bz3g", 388): 4,
            ("bz3g", 510): 4,
            ("bz3g", 23): 3, ("bz3g", 109): 3,
            ("bz3g", 387): 3, ("bz3g", 389): 3,
            ("bz3g", 330): 5,
        }

        for namespace, expected_slots in cases.items():
            slots = self.uniform_slots(512)
            for slot in expected_slots:
                family_id = original_families[(namespace, slot)]
                slots[slot]["signature"] = {
                    "northWest": family_id, "northEast": family_id,
                    "southEast": family_id, "southWest": family_id,
                }
            recipe = {
                "namespace": namespace,
                "slots": slots,
                "families": [{"id": family_id}
                             for family_id in range(1, 26)],
            }
            result = reviewed_fragment_corrections(recipe)
            by_slot = {slot["slot"]: slot["signature"]
                       for slot in result["slots"]}
            for slot, expected in expected_slots.items():
                self.assertEqual(expected, by_slot[slot],
                                 f"{namespace} slot {slot}")

    def test_bz3g_slot273_gray_stone_does_not_remain_mountain_ground(self) -> None:
        slots = [{"slot": slot, "signature": {
            "northWest": 1, "northEast": 1,
            "southEast": 1, "southWest": 1,
        }} for slot in range(512)]
        slots[273]["signature"] = {
            "northWest": 1, "northEast": 1,
            "southEast": 7, "southWest": 7,
        }
        slots[472]["signature"] = {
            "northWest": 6, "northEast": 7,
            "southEast": 2, "southWest": 7,
        }
        recipe = {
            "namespace": "bz3g",
            "slots": slots,
            "families": [{"id": family_id}
                         for family_id in range(1, 24)],
        }

        result = reviewed_fragment_corrections(recipe)

        signature = result["slots"][273]["signature"]
        self.assertEqual(3, signature["southWest"])
        self.assertEqual(7, signature["southEast"])
        self.assertEqual(3, result["slots"][472]["signature"]["southWest"])

    def test_bz5g_irregular_fragment_is_split_only_at_reviewed_corners(self) -> None:
        slots = [{"slot": slot, "signature": {
            "northWest": 1, "northEast": 1,
            "southEast": 1, "southWest": 1}}
                 for slot in range(512)]
        slots[269]["signature"]["northEast"] = 14
        slots[308]["signature"]["northWest"] = 14
        slots[369]["signature"]["southEast"] = 14
        slots[384]["signature"]["northEast"] = 14  # unresolved control
        slots[431]["signature"]["northWest"] = 20
        slots[490]["signature"]["northEast"] = 20
        slots[492]["signature"]["northWest"] = 20
        recipe = {
            "namespace": "Zone8", "slots": slots,
            "families": [{"id": family_id} for family_id in range(1, 26)],
        }

        result = reviewed_fragment_corrections(recipe)
        by_slot = {slot["slot"]: slot["signature"]
                   for slot in result["slots"]}

        self.assertEqual(8, by_slot[269]["northEast"])
        self.assertEqual(5, by_slot[308]["northWest"])
        self.assertEqual(5, by_slot[369]["southEast"])
        self.assertEqual(8, by_slot[384]["northEast"])
        self.assertEqual(5, by_slot[384]["southWest"])
        self.assertEqual(8, by_slot[431]["northWest"])
        self.assertEqual(8, by_slot[490]["northEast"])
        self.assertEqual(8, by_slot[492]["northWest"])
        self.assertNotIn(14, {family["id"] for family in result["families"]})
        self.assertNotIn(20, {family["id"] for family in result["families"]})
        self.assertEqual(
            "Grass / Swamp",
            next(family for family in result["families"]
                 if family["id"] == 8)["suggestedName"])

    def test_bz5g_wet_sand_anchor_keeps_its_rock_vertex(self) -> None:
        slots = self.uniform_slots(512, family_id=2)
        recipe = {
            "namespace": "Zone8",
            "slots": slots,
            "families": [{"id": family_id} for family_id in range(1, 26)],
        }

        result = reviewed_fragment_corrections(recipe)

        self.assertEqual(
            {"northWest": 1, "northEast": 2,
             "southEast": 2, "southWest": 2},
            result["slots"][227]["signature"],
        )

    def test_zone5_1_uses_reviewed_swamp_name(self) -> None:
        recipe = {
            "namespace": "Zone5_1",
            "slots": [{"slot": 0, "signature": {
                "northWest": 4, "northEast": 4,
                "southEast": 4, "southWest": 4}}],
            "families": [{"id": 4, "suggestedName": "Grass / Irregular dark"}],
        }

        result = reviewed_fragment_corrections(recipe)

        self.assertEqual("Grass / Swamp",
                         result["families"][0]["suggestedName"])

    def test_bz4g_false_trampled_split_merges_into_ground_common(self) -> None:
        slots = [{"slot": slot, "signature": {
            "northWest": 1, "northEast": 1,
            "southEast": 1, "southWest": 1}}
                 for slot in range(512)]
        slots[64]["signature"] = {
            "northWest": 12, "northEast": 4,
            "southEast": 4, "southWest": 4}
        slots[10]["signature"] = {
            "northWest": 6, "northEast": 2,
            "southEast": 2, "southWest": 2}
        slots[261]["signature"] = {
            "northWest": 6, "northEast": 6,
            "southEast": 2, "southWest": 6}
        slots[224]["signature"] = {
            "northWest": 1, "northEast": 1,
            "southEast": 12, "southWest": 2}
        slots[474]["signature"] = {
            "northWest": 17, "northEast": 17,
            "southEast": 17, "southWest": 17}
        recipe = {
            "namespace": "bz4g", "slots": slots,
            "families": [{"id": family_id, "suggestedName": name}
                         for family_id, name in (
                             (1, "Grass / Common"),
                             (2, "Stone / Lime"),
                             (3, "Ground / Common"),
                             (4, "Grass / Irregular"),
                             (6, "Ground / Light"),
                             (8, "Sand / Common"),
                             (12, "Ground / Trampled"))],
        }

        result = reviewed_fragment_corrections(recipe)

        self.assertEqual(3, result["slots"][64]["signature"]["northWest"])
        self.assertEqual(3, result["slots"][224]["signature"]["southEast"])
        self.assertEqual(3, result["slots"][10]["signature"]["northWest"])
        self.assertEqual(
            [3, 3, 2, 3],
            list(result["slots"][261]["signature"].values()))
        self.assertEqual(
            {"northWest": 4, "northEast": 4,
             "southEast": 17, "southWest": 17},
            result["slots"][474]["signature"])
        self.assertNotIn(12, {family["id"] for family in result["families"]})
        self.assertNotIn(6, {family["id"] for family in result["families"]})

    def test_zone7_false_gray_stone_split_merges_into_stone_light(self) -> None:
        slots = [{"slot": slot, "signature": {
            "northWest": 1, "northEast": 1,
            "southEast": 1, "southWest": 1}}
                 for slot in range(512)]
        for slot in (52, 53):
            slots[slot]["signature"] = {
                "northWest": 2, "northEast": 2,
                "southEast": 2, "southWest": 8}
        for slot in (54, 55):
            slots[slot]["signature"] = {
                "northWest": 3, "northEast": 3,
                "southEast": 3, "southWest": 8}
        slots[393]["signature"] = {
            "northWest": 3, "northEast": 7,
            "southEast": 8, "southWest": 7}
        recipe = {
            "namespace": "Zone7", "slots": slots,
            "families": [{"id": family_id}
                         for family_id in range(1, 24)],
        }

        result = reviewed_fragment_corrections(recipe)

        for slot in (52, 53, 54, 55):
            self.assertEqual(
                6, result["slots"][slot]["signature"]["southWest"])
        self.assertEqual(6, result["slots"][393]["signature"]["southEast"])
        self.assertNotIn(8, {family["id"] for family in result["families"]})

    def test_pale_stone_fragments_merge_into_lime_in_related_atlases(self) -> None:
        cases = {
            "bz4g": ((11, 16), 2),
            "Zone3Obr": ((8, 11, 12), 1),
            "zone6": ((14, 16, 20), 1),
            "zone6_2": ((17, 20, 23), 1),
        }
        corners = ("northWest", "northEast", "southEast", "southWest")

        for namespace, (fragment_ids, lime_id) in cases.items():
            slots = self.uniform_slots(512)
            fragment_values = list(fragment_ids)
            fragment_values.extend(
                [fragment_ids[-1]] * (len(corners) - len(fragment_values)))
            slots[0]["signature"] = dict(zip(corners, fragment_values))
            recipe = {
                "namespace": namespace,
                "slots": slots,
                "families": [{"id": family_id}
                             for family_id in range(1, 26)],
            }

            result = reviewed_fragment_corrections(recipe)

            self.assertEqual(
                {lime_id}, set(result["slots"][0]["signature"].values()),
                namespace)
            remaining = {family["id"] for family in result["families"]}
            self.assertTrue(set(fragment_ids).isdisjoint(remaining), namespace)

    def test_zone6_2_cracked_ice_is_a_common_ice_variant(self) -> None:
        slots = self.uniform_slots(512)
        slots[50]["signature"] = {
            "northWest": 9, "northEast": 9,
            "southEast": 9, "southWest": 9,
        }
        slots[60]["signature"] = {
            "northWest": 8, "northEast": 8,
            "southEast": 9, "southWest": 9,
        }
        recipe = {
            "namespace": "Zone6_2",
            "slots": slots,
            "families": [{"id": family_id} for family_id in range(1, 24)],
        }

        result = reviewed_fragment_corrections(recipe)

        self.assertEqual(
            {10}, set(result["slots"][50]["signature"].values()))
        self.assertEqual(10, result["slots"][60]["signature"]["southEast"])
        self.assertEqual(10, result["slots"][60]["signature"]["southWest"])
        self.assertNotIn(9, {family["id"] for family in result["families"]})

    def test_r41_visual_similarity_cannot_override_authored_map_identity(self) -> None:
        cases = (
            ("Zone6", 84, 12),
            ("Zone6_2", 84, 15),
            ("Zone6_2", 10, 5),
            ("Zone7", 57, 10),
        )
        for namespace, slot, family_id in cases:
            slots = self.uniform_slots(512)
            slots[slot]["signature"] = {
                "northWest": family_id, "northEast": family_id,
                "southEast": family_id, "southWest": family_id,
            }
            result = reviewed_fragment_corrections({
                "namespace": namespace,
                "slots": slots,
                "families": [{"id": value} for value in range(1, 24)],
            })
            self.assertEqual(
                {family_id}, set(result["slots"][slot]["signature"].values()),
                f"{namespace} slot {slot}",
            )

    def test_zone14_cracked_ice_is_a_common_ice_variant(self) -> None:
        slots = self.uniform_slots(512)
        slots[3]["signature"] = {
            "northWest": 2, "northEast": 2,
            "southEast": 2, "southWest": 2,
        }
        slots[56]["signature"] = {
            "northWest": 2, "northEast": 3,
            "southEast": 3, "southWest": 14,
        }
        result = reviewed_fragment_corrections({
            "namespace": "Zone14",
            "slots": slots,
            "families": [{"id": value} for value in range(1, 21)],
        })

        self.assertEqual(
            {3}, set(result["slots"][3]["signature"].values()))
        self.assertEqual(3, result["slots"][56]["signature"]["northWest"])
        self.assertNotIn(2, {family["id"] for family in result["families"]})

    def test_zone11_winter_normal_retains_its_authored_identity(self) -> None:
        slots = self.uniform_slots(512)
        slots[254]["signature"] = {
            "northWest": 11, "northEast": 11,
            "southEast": 11, "southWest": 11,
        }
        result = reviewed_fragment_corrections({
            "namespace": "bz9k",
            "slots": slots,
            "families": [{"id": value} for value in range(1, 17)],
        })

        # HD-only similarity to Winter faded (9) is not enough to erase a
        # distinct authored material: 21-22 of 23 Zone 11 occurrences vote
        # for family 11 at each corner, with at most two disagreements.
        self.assertEqual(
            {11}, set(result["slots"][254]["signature"].values()))
        self.assertIn(11, {family["id"] for family in result["families"]})

    def test_zone12_false_light_family_merges_into_gray_stone(self) -> None:
        slots = self.uniform_slots(512)
        for slot in (49, 50, 51, 52):
            slots[slot]["signature"] = {
                "northWest": 1, "northEast": 1,
                "southEast": 1, "southWest": 4,
            }
        slots[75]["signature"] = {
            "northWest": 1, "northEast": 1,
            "southEast": 1, "southWest": 4,
        }
        slots[230]["signature"] = {
            "northWest": 4, "northEast": 7,
            "southEast": 7, "southWest": 7,
        }
        slots[233]["signature"] = {
            "northWest": 4, "northEast": 1,
            "southEast": 7, "southWest": 7,
        }
        slots[235]["signature"] = {
            "northWest": 1, "northEast": 4,
            "southEast": 7, "southWest": 7,
        }
        result = reviewed_fragment_corrections({
            "namespace": "Zone12",
            "slots": slots,
            "families": [{"id": value} for value in range(1, 13)],
        })

        for slot in (49, 50, 51, 52):
            self.assertEqual(
                {"northWest": 1, "northEast": 1,
                 "southEast": 1, "southWest": 3},
                result["slots"][slot]["signature"])
        self.assertEqual(3, result["slots"][75]["signature"]["southWest"])
        self.assertEqual(3, result["slots"][230]["signature"]["northWest"])
        self.assertEqual(3, result["slots"][233]["signature"]["northWest"])
        self.assertEqual(3, result["slots"][235]["signature"]["northEast"])
        self.assertNotIn(4, {family["id"] for family in result["families"]})

    def test_map_pixel_intersection_repairs_only_reviewed_false_uniform_corners(self) -> None:
        # Each tuple is (source uniform family, reviewed NW/NE/SE/SW result).
        # Zone8 slot 19 is included as an explicit retention: its earlier
        # human Ground / Gravel decision overrides the weaker Common-ground
        # continuity/pixel proposal.
        cases = {
            "Zone2": {188: (3, (3, 3, 2, 3))},
            "Zone6": {60: (3, (3, 3, 3, 5))},
            "Zone7": {
                149: (15, (10, 15, 15, 15)),
                322: (7, (13, 7, 13, 7)),
            },
            "Zone8": {19: (5, (5, 5, 5, 5))},
            "Zone10": {
                97: (1, (1, 3, 1, 3)),
                137: (11, (11, 5, 11, 11)),
                308: (9, (9, 7, 9, 9)),
            },
            "Zone13": {
                166: (4, (3, 4, 4, 4)),
                170: (4, (3, 4, 4, 4)),
                240: (3, (3, 3, 3, 8)),
                401: (3, (3, 3, 3, 8)),
            },
            "Zone14": {32: (13, (1, 8, 8, 8))},
            "Zone15": {
                131: (5, (1, 5, 5, 5)),
                219: (11, (3, 11, 11, 11)),
                221: (11, (3, 11, 11, 11)),
                222: (11, (3, 11, 11, 11)),
            },
            "Zone16": {186: (2, (2, 4, 2, 2))},
            "Zone19": {
                57: (1, (1, 1, 1, 5)),
                100: (5, (1, 5, 5, 5)),
                118: (1, (1, 1, 1, 4)),
                124: (1, (1, 1, 1, 4)),
                214: (7, (1, 7, 7, 7)),
                216: (7, (1, 7, 7, 7)),
                309: (1, (1, 4, 1, 1)),
                310: (1, (1, 4, 1, 4)),
                446: (3, (3, 1, 3, 3)),
            },
            "bz3g": {
                45: (7, (6, 7, 7, 7)),
                222: (5, (6, 5, 5, 5)),
            },
            "bz9k": {368: (10, (10, 6, 10, 10))},
        }
        names = ("northWest", "northEast", "southEast", "southWest")
        for namespace, slot_cases in cases.items():
            slots = self.uniform_slots(512)
            for slot, (source_family, _) in slot_cases.items():
                slots[slot]["signature"] = {
                    name: source_family for name in names}
            result = reviewed_fragment_corrections({
                "namespace": namespace,
                "slots": slots,
                "families": [{"id": value} for value in range(1, 26)],
            })
            for slot, (_, expected) in slot_cases.items():
                self.assertEqual(
                    dict(zip(names, expected)),
                    result["slots"][slot]["signature"],
                    f"{namespace} slot {slot}",
                )

    def test_zone2_exact_patch_ambiguities_retain_map_supported_corners(self) -> None:
        slots = self.uniform_slots(512)
        source = {
            146: (2, 2, 3, 3),
            169: (4, 4, 7, 7),
            283: (3, 3, 3, 7),
            421: (2, 3, 3, 4),
        }
        names = ("northWest", "northEast", "southEast", "southWest")
        for slot, values in source.items():
            slots[slot]["signature"] = dict(zip(names, values))
        result = reviewed_fragment_corrections({
            "namespace": "Zone2",
            "slots": slots,
            "families": [{"id": value} for value in range(1, 24)],
        })

        expected = {
            (146, "southEast"): 3,
            (146, "southWest"): 3,
            (169, "southWest"): 7,
            (283, "southEast"): 3,
            (421, "southEast"): 3,
        }
        for (slot, corner), family_id in expected.items():
            self.assertEqual(
                family_id,
                result["slots"][slot]["signature"][corner],
                f"Zone2 slot {slot} {corner}",
            )

    def test_zone2_checkerboard_and_followup_corners_are_exact(self) -> None:
        slots = self.uniform_slots(512)
        result = reviewed_fragment_corrections({
            "namespace": "Zone2",
            "slots": slots,
            "families": [{"id": value} for value in range(1, 24)],
        })
        names = ("northWest", "northEast", "southEast", "southWest")
        expected = {
            115: (1, 1, 1, 3),
            142: (7, 14, 14, 14),
            195: (14, 14, 14, 14),
            198: (1, 8, 14, 14),
            268: (8, 1, 1, 1),
            284: (1, 1, 8, 1),
            299: (8, 14, 14, 14),
            309: (1, 1, 1, 14),
            493: (7, 14, 1, 14),
            494: (7, 8, 1, 14),
        }
        for slot, values in expected.items():
            self.assertEqual(
                dict(zip(names, values)),
                result["slots"][slot]["signature"],
                f"Zone2 slot {slot}",
            )

    def test_reviewed_mixed_tile_corners_do_not_guess_unnamed_corners(self) -> None:
        cases = {
            "bz3g": {
                83: {"northWest": 4, "northEast": 1,
                     "southEast": 4, "southWest": 1},
                390: {"northWest": 13, "northEast": 13,
                      "southEast": 13, "southWest": 13},
                414: {"northWest": 8, "northEast": 2,
                      "southEast": 2, "southWest": 3},
            },
            "bz4g": {
                382: {"northWest": 1, "northEast": 1,
                      "southEast": 9, "southWest": 17},
                434: {"northWest": 9, "northEast": 1,
                      "southEast": 1, "southWest": 4},
            },
            "Zone8": {
                273: {"northWest": 5, "northEast": 4,
                      "southEast": 5, "southWest": 5},
                274: {"northWest": 5, "northEast": 4,
                      "southEast": 5, "southWest": 5},
            },
        }

        for namespace, expected_slots in cases.items():
            recipe = {
                "namespace": namespace,
                "slots": self.uniform_slots(512),
                "families": [{"id": family_id}
                             for family_id in range(1, 26)],
            }
            if namespace == "bz3g":
                recipe["slots"][390]["signature"] = {
                    "northWest": 13, "northEast": 13,
                    "southEast": 13, "southWest": 13}
                recipe["slots"][414]["signature"] = {
                    "northWest": 2, "northEast": 2,
                    "southEast": 2, "southWest": 2}
            if namespace == "Zone8":
                recipe["slots"][273]["signature"] = {
                    "northWest": 5, "northEast": 5,
                    "southEast": 5, "southWest": 5}
                recipe["slots"][274]["signature"] = {
                    "northWest": 15, "northEast": 15,
                    "southEast": 15, "southWest": 15}

            result = reviewed_fragment_corrections(recipe)

            for slot, expected in expected_slots.items():
                self.assertEqual(expected, result["slots"][slot]["signature"],
                                 f"{namespace} slot {slot}")

    def test_r40_strict_candidates_apply_only_after_complete_tile_review(self) -> None:
        zone10_slots = self.uniform_slots(512)
        zone10_slots[298]["signature"] = {
            "northWest": 5, "northEast": 5,
            "southEast": 5, "southWest": 12}
        zone10_slots[342]["signature"] = {
            "northWest": 1, "northEast": 2,
            "southEast": 1, "southWest": 2}
        zone10 = reviewed_fragment_corrections({
            "namespace": "Zone10",
            "slots": zone10_slots,
            "families": [{"id": family_id} for family_id in range(1, 26)],
        })
        self.assertEqual(6, zone10["slots"][298]["signature"]["southWest"])
        self.assertEqual(
            {"northWest": 1, "northEast": 2,
             "southEast": 1, "southWest": 2},
            zone10["slots"][342]["signature"])

    def test_zone10_ground_dry_review_reclassifies_only_explicit_slab_corners(self) -> None:
        slots = self.uniform_slots(512)
        names = ("northWest", "northEast", "southEast", "southWest")
        source = {
            178: (6, 6, 6, 12),
            211: (6, 12, 12, 12),
            260: (3, 12, 12, 12),
            326: (7, 12, 12, 12),
            396: (6, 6, 6, 6),
            398: (12, 12, 12, 12),
            491: (9, 9, 12, 12),
            503: (9, 9, 12, 12),
            180: (6, 6, 6, 12),
            212: (6, 12, 12, 12),
            217: (1, 1, 12, 12),
            220: (1, 1, 12, 12),
            276: (1, 12, 12, 12),
            324: (3, 3, 3, 12),
            361: (1, 3, 12, 12),
            391: (3, 1, 3, 12),
            392: (3, 1, 3, 12),
            412: (9, 9, 9, 12),
            427: (5, 7, 12, 12),
            476: (7, 6, 3, 12),
            481: (7, 7, 9, 12),
            483: (1, 1, 1, 12),
            487: (7, 5, 12, 12),
            489: (9, 5, 12, 12),
            495: (12, 1, 12, 12),
            500: (2, 3, 12, 12),
        }
        for slot, values in source.items():
            slots[slot]["signature"] = dict(zip(names, values))

        result = reviewed_fragment_corrections({
            "namespace": "Zone10",
            "slots": slots,
            "families": [{"id": family_id} for family_id in range(1, 26)],
        })

        expected = {
            178: (6, 6, 6, 6),
            211: (6, 6, 6, 6),
            260: (3, 6, 6, 6),
            326: (7, 6, 6, 6),
            396: (6, 6, 6, 6),
            398: (6, 6, 6, 6),
            491: (9, 9, 6, 6),
            503: (9, 9, 6, 6),
            180: (6, 6, 6, 6),
            212: (6, 6, 6, 6),
            217: (1, 1, 6, 6),
            220: (1, 1, 6, 6),
            276: (1, 6, 6, 6),
            324: (3, 3, 3, 6),
            361: (1, 3, 6, 6),
            391: (3, 1, 3, 6),
            392: (3, 1, 3, 6),
            412: (9, 9, 9, 6),
            427: (5, 7, 6, 6),
            476: (7, 6, 3, 6),
            481: (7, 7, 9, 6),
            483: (1, 1, 1, 6),
            487: (7, 5, 6, 6),
            489: (9, 5, 6, 6),
            495: (9, 1, 6, 6),
            500: (2, 3, 6, 6),
        }
        for slot, values in expected.items():
            self.assertEqual(
                dict(zip(names, values)),
                result["slots"][slot]["signature"],
                f"Zone10 slot {slot}",
            )

        zone7_slots = self.uniform_slots(512)
        zone7_slots[86]["signature"] = {
            "northWest": 7, "northEast": 3,
            "southEast": 3, "southWest": 3}
        zone7_slots[168]["signature"] = {
            "northWest": 13, "northEast": 13,
            "southEast": 13, "southWest": 16}
        zone7_slots[226]["signature"] = {
            "northWest": 3, "northEast": 3,
            "southEast": 3, "southWest": 16}
        zone7 = reviewed_fragment_corrections({
            "namespace": "Zone7",
            "slots": zone7_slots,
            "families": [{"id": family_id} for family_id in range(1, 24)],
        })
        self.assertEqual(17, zone7["slots"][168]["signature"]["southWest"])
        self.assertEqual(7, zone7["slots"][86]["signature"]["northWest"])
        self.assertEqual(
            17, zone7["slots"][226]["signature"]["southWest"],
            "R50 explicitly resolved the previously withheld corner")

    def test_r59_zone10_ground_dry_is_exhausted_by_explicit_corner_decisions(self) -> None:
        slots = self.uniform_slots(512)
        corner_index = {"NW": "northWest", "NE": "northEast",
                        "SE": "southEast", "SW": "southWest"}
        square = {
            (144, "SW"), (145, "SW"), (146, "SW"), (147, "SW"),
            (148, "SW"), (217, "SE"), (219, "SE"), (219, "SW"),
            (220, "SE"), (276, "NE"), (276, "SW"), (294, "NE"),
            (294, "SW"), (295, "NE"), (295, "SW"), (297, "SW"),
            (317, "SE"), (317, "SW"), (332, "SE"), (332, "SW"),
            (333, "SE"), (333, "SW"), (346, "SW"), (353, "SW"),
            (361, "SW"), (381, "SW"), (382, "SW"), (399, "SW"),
            (410, "SW"), (427, "SW"), (433, "SW"), (458, "SW"),
            (463, "SW"), (487, "SE"), (489, "SE"), (495, "SE"),
            (500, "SW"), (502, "SE"), (502, "SW"),
        }
        carved = {(495, "NW"), (502, "NE")}
        for slot, corner in square | carved:
            slots[slot]["signature"][corner_index[corner]] = 12

        result = reviewed_fragment_corrections({
            "namespace": "Zone10",
            "slots": slots,
            "families": [{"id": family_id} for family_id in range(1, 26)],
        })

        for slot, corner in square:
            self.assertEqual(
                6, result["slots"][slot]["signature"][corner_index[corner]],
                f"Zone10 slot {slot} {corner}")
        for slot, corner in carved:
            self.assertEqual(
                9, result["slots"][slot]["signature"][corner_index[corner]],
                f"Zone10 slot {slot} {corner}")
        self.assertNotIn(12, {family["id"] for family in result["families"]})
        self.assertFalse(any(
            family_id == 12
            for slot in result["slots"]
            for family_id in slot["signature"].values()
        ))

    def test_r59_global_confidence_applies_only_explicit_merges_and_corners(self) -> None:
        corner_names = ("northWest", "northEast", "southEast", "southWest")
        cases = {
            "bz11k": ({18: 16}, {(235, 3): 16, (300, 3): 20}),
            # This historical fixture has IDs but deliberately no semantic
            # names, so later name-based R106 corrections do not apply here.
            "bz13h": ({12: 1, 13: 2}, {(13, 0): 8}),
            "bz16h": ({}, {(95, 0): 6, (449, 0): 13}),
            "bz3g": ({}, {(265, 3): 10, (383, 0): 5}),
            "bz4g": ({}, {
                (174, 2): 3, (280, 3): 5, (294, 0): 3,
                (370, 0): 9, (370, 1): 9,
                (370, 2): 1, (370, 3): 3, (437, 3): 5,
            }),
            "bz5g": ({}, {(310, 2): 8, (317, 3): 4}),
            "Bz7g": ({}, {(162, 2): 1}),
            "bz8k": ({}, {(323, 2): 2, (323, 3): 2}),
            "Zone1": ({11: 8, 12: 7}, {(338, 3): 7}),
            "Zone10": ({18: 6, 20: 6, 21: 9}, {
                (287, 0): 3, (287, 3): 6, (323, 3): 6,
                (354, 3): 6, (416, 3): 6, (479, 3): 6,
                (465, 2): 2, (471, 3): 6, (486, 3): 6,
            }),
            "Zone13": ({}, {
                (178, 1): 6, (178, 2): 6, (178, 3): 6,
                (283, 3): 7, (450, 3): 7,
                (477, 0): 6, (477, 2): 6, (497, 3): 3,
            }),
            "Zone20": ({}, {(189, 0): 2}),
            "Zone6": ({}, {(278, 0): 8, (471, 0): 3}),
            "Zone6_2": ({}, {(278, 0): 8, (471, 0): 3}),
            "Zone7": ({}, {(333, 3): 1}),
        }
        for namespace, (aliases, exact) in cases.items():
            with self.subTest(namespace=namespace):
                slots = self.uniform_slots(512)
                for corner, source in enumerate(aliases):
                    slots[511]["signature"][corner_names[corner]] = source
                result = reviewed_fragment_corrections({
                    "namespace": namespace,
                    "slots": slots,
                    "families": [
                        {"id": family_id, "thumbnailSlot": 0}
                        for family_id in range(1, 26)
                    ],
                })
                by_slot = {slot["slot"]: slot for slot in result["slots"]}
                for (slot, corner), target in exact.items():
                    self.assertEqual(
                        target, by_slot[slot]["signature"][corner_names[corner]],
                        f"{namespace} slot {slot} {corner_names[corner]}")
                for corner, (source, target) in enumerate(aliases.items()):
                    self.assertEqual(
                        target,
                        by_slot[511]["signature"][corner_names[corner]])
                    self.assertNotIn(
                        source, {family["id"] for family in result["families"]})

    def test_r59_reviews_survive_content_identical_canonical_atlas_chains(self) -> None:
        corner_names = ("northWest", "northEast", "southEast", "southWest")

        def recipe(namespace: str) -> dict:
            slots = self.uniform_slots(512)
            for slot in slots:
                slot.update({
                    "allowedLayers": ["land"],
                    "tileType": 0,
                    "observedUsage": {"land": 1, "belowOverlay": 0},
                })
            return {
                "namespace": namespace,
                "slotCount": 512,
                "slots": slots,
                "families": [
                    {"id": family_id, "thumbnailSlot": 0}
                    for family_id in range(1, 26)
                ],
            }

        cases = (
            ("bz11k", "Zone14", {18: 16}, {
                (235, 3): (17, 16), (300, 3): (20, 20),
            }),
            ("bz13h", "Zone15", {12: 1, 13: 2}, {
                (13, 0): (9, 8),
            }),
            ("bz5g", "Zone8", {}, {
                (310, 2): (16, 8), (317, 3): (17, 4),
            }),
            ("bz8k", "bz9k", {}, {
                (323, 2): (2, 2), (323, 3): (5, 2),
            }),
        )
        for target_name, canonical_name, aliases, exact in cases:
            with self.subTest(target=target_name, canonical=canonical_name):
                target = recipe(target_name)
                canonical = recipe(canonical_name)
                for source, _target in aliases.items():
                    target["slots"][511]["signature"]["northWest"] = source
                    canonical["slots"][511]["signature"]["northWest"] = source
                for (slot, corner), (source, _target) in exact.items():
                    name = corner_names[corner]
                    target["slots"][slot]["signature"][name] = source
                    canonical["slots"][slot]["signature"][name] = source

                final = apply_canonical_identity(
                    reviewed_fragment_corrections(target),
                    reviewed_fragment_corrections(canonical))
                by_slot = {slot["slot"]: slot for slot in final["slots"]}
                for (slot, corner), (_source, expected) in exact.items():
                    self.assertEqual(
                        expected,
                        by_slot[slot]["signature"][corner_names[corner]],
                        f"{target_name} via {canonical_name} slot {slot}")
                family_ids = {family["id"] for family in final["families"]}
                for source in aliases:
                    self.assertNotIn(source, family_ids)

    def test_r62_applies_only_explicit_family_merges_and_corners(self) -> None:
        corner_names = ("northWest", "northEast", "southEast", "southWest")
        cases = {
            "bz11k": ({}, {(235, 2): 16}),
            "Zone14": ({}, {(235, 2): 16}),
            "bz4g": ({25: 8}, {}),
            "Zone4": ({25: 8}, {}),
            "bz8k": ({}, {(428, 3): 7, (444, 3): 7}),
            "bz9k": ({}, {(428, 3): 7, (444, 3): 7}),
            "Zone10": ({10: 5}, {}),
            "Zone20": ({}, {(38, 0): 2, (190, 0): 2}),
            "Zone3Obr": ({10: 4}, {}),
        }
        for namespace, (aliases, exact) in cases.items():
            with self.subTest(namespace=namespace):
                slots = self.uniform_slots(512)
                slots[235]["signature"]["southEast"] = 17
                slots[428]["signature"]["southWest"] = 14
                slots[444]["signature"]["southWest"] = 15
                slots[190]["signature"]["northWest"] = 6
                slots[38]["signature"]["northWest"] = 5
                for corner, source in enumerate(aliases):
                    slots[511]["signature"][corner_names[corner]] = source
                result = reviewed_fragment_corrections({
                    "namespace": namespace,
                    "slots": slots,
                    "families": [
                        {"id": family_id, "thumbnailSlot": 0}
                        for family_id in range(1, 26)
                    ],
                })
                by_slot = {slot["slot"]: slot for slot in result["slots"]}
                for (slot, corner), target in exact.items():
                    self.assertEqual(
                        target, by_slot[slot]["signature"][corner_names[corner]],
                        f"{namespace} slot {slot} {corner_names[corner]}")
                family_ids = {family["id"] for family in result["families"]}
                for corner, (source, target) in enumerate(aliases.items()):
                    self.assertEqual(
                        target, by_slot[511]["signature"][corner_names[corner]])
                    self.assertNotIn(source, family_ids)

                if namespace == "Zone10":
                    self.assertIn(17, family_ids,
                                  "Ground / Mountainous was only a tentative merge")
                if namespace == "bz8k":
                    family = next(value for value in result["families"]
                                  if value["id"] == 7)
                    self.assertNotEqual(
                        "Yellow lichen", family.get("suggestedName", ""),
                        "the reviewer requested a rename comparison, not a guessed name")

    def test_r63_applies_only_explicit_family_merges_corners_and_names(self) -> None:
        corner_names = ("northWest", "northEast", "southEast", "southWest")
        cases = {
            # R65 subsequently identified every reviewed Rock / Dark member
            # as Stone / Black, so the cumulative recipe emits family 8.
            "bz11k": ({}, {(33, 0): 1, (33, 1): 8, (33, 2): 8,
                              (33, 3): 8, (55, 3): 8, (58, 3): 8}, {}),
            "Zone14": ({}, {(33, 0): 1, (33, 1): 8, (33, 2): 8,
                               (33, 3): 8, (55, 3): 8, (58, 3): 8}, {}),
            "bz8k": ({}, {}, {7: "Grass / Yellow lichen"}),
            "bz9k": ({}, {}, {7: "Grass / Yellow lichen"}),
            "Zone10": ({}, {(291, 1): 5}, {}),
            # R80 supersedes the earlier provisional Ground / Common choice
            # after the full-map visual review selected Ground / Light.
            "Zone13": ({14: 3}, {(325, 0): 3}, {}),
        }
        for namespace, (aliases, exact, names) in cases.items():
            with self.subTest(namespace=namespace):
                slots = self.uniform_slots(512)
                slots[33]["signature"] = {
                    "northWest": 13, "northEast": 13,
                    "southEast": 13, "southWest": 13,
                }
                slots[55]["signature"]["southWest"] = 14
                slots[58]["signature"]["southWest"] = 14
                slots[291]["signature"] = {
                    "northWest": 1, "northEast": 17,
                    "southEast": 2, "southWest": 2,
                }
                slots[213]["signature"]["southWest"] = 13
                slots[325]["signature"] = {
                    "northWest": 14, "northEast": 2,
                    "southEast": 2, "southWest": 2,
                }
                slots[409]["signature"]["northEast"] = 14
                result = reviewed_fragment_corrections({
                    "namespace": namespace,
                    "slots": slots,
                    "families": [
                        {"id": family_id,
                         "suggestedName": f"Family {family_id}",
                         "thumbnailSlot": 0}
                        for family_id in range(1, 26)
                    ],
                })
                by_slot = {slot["slot"]: slot for slot in result["slots"]}
                for (slot, corner), target in exact.items():
                    self.assertEqual(
                        target, by_slot[slot]["signature"][corner_names[corner]],
                        f"{namespace} slot {slot} {corner_names[corner]}")
                family_ids = {family["id"] for family in result["families"]}
                for source, target in aliases.items():
                    self.assertNotIn(source, family_ids)
                    self.assertEqual(
                        target, by_slot[409]["signature"]["northEast"])
                for family_id, expected_name in names.items():
                    self.assertEqual(
                        expected_name,
                        next(family for family in result["families"]
                             if family["id"] == family_id)["suggestedName"])

                if namespace == "Zone10":
                    self.assertEqual(
                        6, by_slot[213]["signature"]["southWest"],
                        "R64 subsequently resolved slot 213 SW as Slabs / Square")
                    self.assertEqual(
                        1, by_slot[291]["signature"]["northWest"],
                        "slot 291 NW already matches slot 213's north/east family")

    def test_r64_applies_exact_zone10_corners_and_shared_sand_name(self) -> None:
        corner_names = ("northWest", "northEast", "southEast", "southWest")
        rename_cases = {
            "bz5g": (14, "Sand / Grainy"),
            "bz6g": (14, "Sand / Grainy"),
            "Zone8": (11, "Sand / Grainy"),
        }
        for namespace, (family_id, expected_name) in rename_cases.items():
            with self.subTest(namespace=namespace):
                result = reviewed_fragment_corrections({
                    "namespace": namespace,
                    "slots": self.uniform_slots(512, family_id),
                    "families": [
                        {"id": value,
                         "suggestedName": f"Family {value}",
                         "thumbnailSlot": 0}
                        for value in range(1, 27)
                    ],
                })
                self.assertEqual(
                    expected_name,
                    next(family for family in result["families"]
                         if family["id"] == family_id)["suggestedName"])

        slots = self.uniform_slots(512)
        signatures = {
            213: (1, 1, 1, 13),
            292: (11, 17, 2, 2),
            293: (17, 17, 2, 2),
            306: (17, 17, 1, 2),
            313: (17, 1, 2, 2),
            314: (17, 1, 2, 2),
            322: (1, 17, 1, 3),
            375: (19, 17, 19, 14),
            419: (17, 2, 17, 2),
            420: (17, 2, 17, 11),
            445: (1, 17, 11, 11),
            452: (9, 17, 17, 9),
            486: (2, 2, 16, 6),
        }
        for slot, signature in signatures.items():
            slots[slot]["signature"] = dict(zip(corner_names, signature))
        result = reviewed_fragment_corrections({
            "namespace": "Zone10",
            "slots": slots,
            "families": [
                {"id": value, "suggestedName": f"Family {value}",
                 "thumbnailSlot": 0}
                for value in range(1, 22)
            ],
        })
        by_slot = {slot["slot"]: slot for slot in result["slots"]}
        expected = {
            (213, 3): 6,
            (292, 1): 5,
            (293, 1): 5,
            (306, 0): 5,
            (313, 0): 5,
            (314, 0): 5,
            (322, 1): 5,
            (375, 1): 5,
            (419, 0): 5,
            (486, 2): 1,
        }
        for (slot, corner), family_id in expected.items():
            self.assertEqual(
                family_id, by_slot[slot]["signature"][corner_names[corner]],
                f"Zone10 slot {slot} {corner_names[corner]}")

        self.assertEqual(
            (5, 2, 1, 11), tuple(by_slot[420]["signature"].values()),
            "R66 subsequently resolved 420 NW and SE independently")
        self.assertEqual(
            (1, 5, 11, 11), tuple(by_slot[445]["signature"].values()),
            "R66 subsequently resolved the exact 445 NE corner")
        self.assertEqual(
            (9, 3, 1, 9), tuple(by_slot[452]["signature"].values()),
            "R66 subsequently reviewed 452 NE and SE independently")
        self.assertEqual(2, by_slot[486]["signature"]["northWest"])
        self.assertEqual(2, by_slot[486]["signature"]["northEast"])
        self.assertEqual(6, by_slot[486]["signature"]["southWest"])

    def test_r65_merges_reviewed_bz11k_rock_dark_into_stone_black(self) -> None:
        corner_names = ("northWest", "northEast", "southEast", "southWest")
        reviewed_members = (
            (7, 3), (8, 3), (9, 3), (10, 3), (11, 3),
            (33, 1), (33, 2), (33, 3), (55, 3), (58, 3),
            (170, 2), (170, 3), (320, 1), (451, 1), (451, 3),
        )
        for namespace in ("bz11k", "Zone14"):
            with self.subTest(namespace=namespace):
                slots = self.uniform_slots(512, 1)
                for slot, corner in reviewed_members:
                    slots[slot]["signature"][corner_names[corner]] = 5
                result = reviewed_fragment_corrections({
                    "namespace": namespace,
                    "slots": slots,
                    "families": [
                        {"id": value, "suggestedName": f"Family {value}",
                         "thumbnailSlot": 0}
                        for value in range(1, 20)
                    ],
                })
                by_slot = {slot["slot"]: slot for slot in result["slots"]}
                self.assertNotIn(5, {family["id"] for family in result["families"]})
                for slot, corner in reviewed_members:
                    self.assertEqual(
                        8, by_slot[slot]["signature"][corner_names[corner]],
                        f"{namespace} slot {slot} {corner_names[corner]}")

    def test_r65_uses_zone1_two_family_water_model_for_basegipat(self) -> None:
        slots = self.uniform_slots(512, 4)
        for slot, corner in (
                (180, 0), (180, 1), (182, 0), (182, 1),
                (377, 0), (378, 0), (380, 0)):
            slots[slot]["signature"][
                ("northWest", "northEast", "southEast", "southWest")[corner]
            ] = 10
        result = reviewed_fragment_corrections({
            "namespace": "BaseGipat",
            "slots": slots,
            "families": [
                {"id": value, "suggestedName": f"Family {value}",
                 "thumbnailSlot": 0}
                for value in range(1, 11)
            ],
        })
        self.assertNotIn(10, {family["id"] for family in result["families"]})
        water = next(family for family in result["families"]
                     if family["id"] == 5)
        self.assertEqual("Water / Still", water["suggestedName"])
        by_slot = {slot["slot"]: slot for slot in result["slots"]}
        self.assertEqual(5, by_slot[180]["signature"]["northWest"])
        self.assertEqual(5, by_slot[180]["signature"]["northEast"])
        self.assertEqual(5, by_slot[380]["signature"]["northWest"])

    def test_r66_applies_only_explicit_bz11k_and_zone10_corner_reviews(self) -> None:
        corner_names = ("northWest", "northEast", "southEast", "southWest")
        bz11k_expected = {
            36: (1, 8, 8, 8),
            61: (8, 1, 8, 16),
        }
        for namespace in ("bz11k", "Zone14"):
            with self.subTest(namespace=namespace):
                slots = self.uniform_slots(512, 2)
                slots[36]["signature"] = dict(zip(corner_names, (13, 13, 13, 13)))
                slots[61]["signature"] = dict(zip(corner_names, (11, 11, 11, 11)))
                result = reviewed_fragment_corrections({
                    "namespace": namespace,
                    "slots": slots,
                    "families": [
                        {"id": value, "suggestedName": f"Family {value}",
                         "thumbnailSlot": 0}
                        for value in range(1, 20)
                    ],
                })
                by_slot = {slot["slot"]: slot for slot in result["slots"]}
                for slot, expected in bz11k_expected.items():
                    self.assertEqual(
                        expected,
                        tuple(by_slot[slot]["signature"][corner]
                              for corner in corner_names))

        zone10_initial = {
            214: (1, 1, 1, 13),
            293: (17, 5, 2, 2),
            306: (5, 17, 1, 2),
            375: (19, 5, 19, 14),
            419: (5, 2, 17, 2),
            420: (17, 2, 17, 11),
            445: (1, 17, 11, 11),
            452: (9, 17, 17, 9),
        }
        zone10_expected = {
            214: (1, 1, 1, 6),
            293: (1, 5, 2, 2),
            306: (5, 1, 1, 2),
            375: (2, 5, 2, 3),
            419: (5, 2, 1, 2),
            420: (5, 2, 1, 11),
            445: (1, 5, 11, 11),
            452: (9, 3, 1, 9),
        }
        slots = self.uniform_slots(512, 4)
        for slot, signature in zone10_initial.items():
            slots[slot]["signature"] = dict(zip(corner_names, signature))
        # Adjacent, unreviewed control remains unchanged.
        slots[453]["signature"] = dict(zip(corner_names, (9, 17, 17, 9)))
        result = reviewed_fragment_corrections({
            "namespace": "Zone10",
            "slots": slots,
            "families": [
                {"id": value, "suggestedName": f"Family {value}",
                 "thumbnailSlot": 0}
                for value in range(1, 20)
            ],
        })
        by_slot = {slot["slot"]: slot for slot in result["slots"]}
        for slot, expected in zone10_expected.items():
            self.assertEqual(
                expected,
                tuple(by_slot[slot]["signature"][corner]
                      for corner in corner_names))
        self.assertEqual(
            (9, 17, 17, 9),
            tuple(by_slot[453]["signature"][corner] for corner in corner_names))

    def test_r67_unifies_exact_cross_atlas_names_without_usage_changes(self) -> None:
        cases = {
            "bz3g": {6: "Ground / Dead"},
            "Zone6": {5: "Ground / Dead"},
            "Zone6_2": {5: "Ground / Dead"},
            "bz5g": {
                1: "Rock / Underwater emerald",
                2: "Sand / Underwater",
                14: "Sand / Grainy",
            },
            "bz6g": {
                1: "Rock / Underwater emerald",
                2: "Sand / Underwater",
                14: "Sand / Grainy",
            },
            "Zone8": {
                1: "Rock / Underwater emerald",
                2: "Sand / Underwater",
                11: "Sand / Grainy",
            },
            "Zone7": {
                1: "Rock / Underwater emerald",
                4: "Sand / Underwater",
                15: "Sand / Grainy",
            },
        }
        for namespace, expected in cases.items():
            for family_id, expected_name in expected.items():
                with self.subTest(namespace=namespace, family_id=family_id):
                    family = {
                        "id": family_id,
                        "suggestedName": f"Old family {family_id}",
                        "thumbnailSlot": 0,
                        "allowedLayers": ["land"],
                        "underwater": family_id % 2 == 0,
                    }
                    result = reviewed_fragment_corrections({
                        "namespace": namespace,
                        "slots": self.uniform_slots(512, family_id),
                        "families": [family],
                    })
                    renamed = result["families"][0]
                    self.assertEqual(expected_name, renamed["suggestedName"])
                    self.assertEqual(["land"], renamed["allowedLayers"])
                    self.assertEqual(family_id % 2 == 0, renamed["underwater"])

    def test_r69_applies_reviewed_corners_to_identical_atlas_owners(self) -> None:
        def corrected(namespace: str) -> dict:
            return reviewed_fragment_corrections({
                "namespace": namespace,
                "slots": self.uniform_slots(512),
                "families": [
                    {"id": family_id,
                     "suggestedName": (
                         "Grass / Burnt"
                         if namespace.casefold() in ("bz2g", "zone2")
                         and family_id == 13 else
                         "Sand / Grainy"
                         if namespace.casefold() == "bz5g"
                         and family_id == 14 else
                         "Sand / Grainy"
                         if namespace.casefold() == "zone8"
                         and family_id == 11 else
                         f"Family {family_id}"),
                     "thumbnailSlot": 0,
                     "uniformSlots": [],
                     "memberSlots": [],
                     "sourceFamily": f"synthetic/{family_id}",
                     "allowedLayers": ["land"],
                     "underwater": False,
                     "tileType": 0}
                    for family_id in range(1, 26)
                ],
            })

        cases = {
            "bz10k": {(316, "northWest"): 5,
                       (316, "southEast"): 9,
                       (316, "southWest"): 10},
            "Zone12": {(316, "northWest"): 5,
                        (316, "southEast"): 9,
                        (316, "southWest"): 10},
            "bz11k": {(246, "northEast"): 8,
                       (477, "southEast"): 8,
                       (509, "southWest"): 16},
            "Zone14": {(246, "northEast"): 8,
                        (477, "southEast"): 8,
                        (509, "southWest"): 16},
            "bz13h": {(248, "northWest"): 4,
                       (248, "southWest"): 2,
                       (495, "southWest"): 6},
            "Zone15": {(248, "northWest"): 4,
                        (248, "southWest"): 2,
                        (495, "southWest"): 6},
            "bz2g": {(98, "northWest"): 2,
                      (98, "southEast"): 1,
                      (120, "southWest"): 4,
                      (306, "southWest"): 13},
            "Zone2": {(98, "northWest"): 2,
                       (98, "southEast"): 1,
                       (120, "southWest"): 4,
                       (306, "southWest"): 13},
            "bz5g": {(318, "northEast"): 2,
                      (462, "southWest"): 14},
            "Zone8": {(318, "northEast"): 2,
                       (462, "southWest"): 11},
            "bz8k": {(323, "northEast"): 6},
            "bz9k": {(323, "northEast"): 6},
            "Zone11": {(323, "northEast"): 6},
        }
        for namespace, expected in cases.items():
            with self.subTest(namespace=namespace):
                result = corrected(namespace)
                by_slot = {slot["slot"]: slot["signature"]
                           for slot in result["slots"]}
                for (slot, corner), family_id in expected.items():
                    self.assertEqual(family_id, by_slot[slot][corner])

    def test_r69_adds_only_reviewed_zone3obr_greenish_transition_family(self) -> None:
        result = reviewed_fragment_corrections({
            "namespace": "Zone3Obr",
            "slots": self.uniform_slots(512),
            "families": [
                {"id": family_id,
                 "suggestedName": f"Family {family_id}",
                 "thumbnailSlot": 0,
                 "uniformSlots": [],
                 "memberSlots": [],
                 "sourceFamily": f"synthetic/{family_id}",
                 "allowedLayers": ["land"],
                 "underwater": False,
                 "tileType": 0}
                for family_id in range(1, 11)
            ],
        })
        by_slot = {slot["slot"]: slot["signature"]
                   for slot in result["slots"]}
        for slot in (38, 39, 218):
            self.assertEqual(13, by_slot[slot]["southWest"])
        family = next(value for value in result["families"]
                      if value["id"] == 13)
        self.assertEqual("Stone / Greenish", family["suggestedName"])
        self.assertEqual([], family["uniformSlots"])
        self.assertEqual([38, 39, 218], family["memberSlots"])

    def test_r70_applies_only_explicit_reviewed_corners(self) -> None:
        def corrected(namespace: str, names: dict[int, str]) -> dict:
            slots = self.uniform_slots(512)
            for family_id in names:
                slots[family_id]["signature"] = {
                    corner: family_id
                    for corner in ("northWest", "northEast",
                                   "southEast", "southWest")
                }
            return reviewed_fragment_corrections({
                "namespace": namespace,
                "slots": slots,
                "families": [
                    {"id": family_id,
                     "suggestedName": name,
                     "thumbnailSlot": 0,
                     "uniformSlots": [],
                     "memberSlots": [],
                     "sourceFamily": f"synthetic/{family_id}",
                     "allowedLayers": ["land"],
                     "underwater": False,
                     "tileType": 0}
                    for family_id, name in names.items()
                ],
            })

        corner_names = ("northWest", "northEast", "southEast", "southWest")

        snow_names = {
            1: "Stone / Dark", 6: "Ground / Light",
            8: "Stone / Black", 10: "Rock / Greenish",
            13: "Ground / Mountainous", 16: "Ground / Trampled",
        }
        for namespace in ("bz11k", "Zone14"):
            with self.subTest(namespace=namespace):
                result = corrected(namespace, snow_names)
                by_slot = {slot["slot"]: slot["signature"]
                           for slot in result["slots"]}
                self.assertEqual(16, by_slot[246]["northWest"])
                self.assertEqual(1, by_slot[346]["southEast"])
                self.assertEqual(6, by_slot[450]["northEast"])
                self.assertEqual(8, by_slot[281]["northWest"])
                self.assertEqual(
                    (1, 16, 16, 16),
                    tuple(by_slot[377][corner] for corner in corner_names))
                self.assertEqual(16, by_slot[480]["northWest"])
                self.assertEqual(1, by_slot[480]["northEast"])
                self.assertEqual(
                    8, by_slot[480]["southEast"],
                    "R71 explicitly resolved the former R70 contradiction")
                self.assertEqual(10, by_slot[480]["southWest"])
                self.assertEqual(1, by_slot[491]["southEast"])
                self.assertEqual(10, by_slot[491]["southWest"])

        desert_names = {
            1: "Ground / Red", 2: "Stone / Weathered",
            3: "Sand / Common", 4: "Stone / Orange",
            8: "Stone / Beige", 11: "Sand / Gravel",
        }
        for namespace in ("bz13h", "Zone15"):
            with self.subTest(namespace=namespace):
                result = corrected(namespace, desert_names)
                by_slot = {slot["slot"]: slot["signature"]
                           for slot in result["slots"]}
                self.assertEqual(
                    (4, 2, 2, 2),
                    tuple(by_slot[12][corner] for corner in corner_names))
                self.assertEqual(
                    (3, 11, 3, 11),
                    tuple(by_slot[408][corner] for corner in corner_names))

        ground_names = {1: "Grass / Common", 3: "Ground / Common",
                        8: "Sand / Common",
                        18: "Ground / Gravel"}
        for namespace in ("bz4g", "Zone4"):
            with self.subTest(namespace=namespace):
                result = corrected(namespace, ground_names)
                signature = result["slots"][204]["signature"]
                self.assertEqual(
                    (18, 18, 18, 18),
                    tuple(signature[corner] for corner in corner_names))

        underwater_names = {1: "Rock / Underwater emerald",
                            2: "Sand / Underwater"}
        for namespace in ("bz5g", "Zone8"):
            with self.subTest(namespace=namespace):
                result = corrected(namespace, underwater_names)
                self.assertEqual(
                    2, result["slots"][212]["signature"]["southWest"])
                self.assertEqual(
                    1, result["slots"][229]["signature"]["northWest"])

        zone10 = corrected(
            "Zone10", {1: "Grass / Sparse dark", 11: "Grass / Irregular"})
        self.assertEqual(
            1, zone10["slots"][439]["signature"]["northWest"])

        zone19 = corrected(
            "Zone19", {1: "Sand / Common", 5: "Sand / Light"})
        self.assertEqual(
            (1, 1, 5, 5),
            tuple(zone19["slots"][46]["signature"][corner]
                  for corner in corner_names))
        self.assertEqual(
            (1, 1, 1, 5),
            tuple(zone19["slots"][59]["signature"][corner]
                  for corner in corner_names))

        for namespace in ("bz16h", "Zone18"):
            with self.subTest(namespace=namespace):
                result = corrected(
                    namespace,
                    {1: "Sand / Common", 7: "Rock / Lime",
                     8: "Stone / Weathered", 12: "Stone / Desert"})
                signature = result["slots"][480]["signature"]
                self.assertEqual(8, signature["northEast"])
                self.assertEqual(8, signature["southEast"])
                self.assertEqual(
                    2, signature["southWest"],
                    "R71 explicitly resolved the former uncertain soil corner")
                self.assertEqual(
                    "Paving / Sand ruins",
                    next(family for family in result["families"]
                         if family["id"] == 7)["suggestedName"])

    def test_r71_applies_explicit_corners_names_and_exact_owners(self) -> None:
        def corrected(namespace: str, names: dict[int, str]) -> dict:
            slots = self.uniform_slots(512)
            for family_id in names:
                slots[family_id]["signature"] = {
                    corner: family_id
                    for corner in ("northWest", "northEast",
                                   "southEast", "southWest")
                }
            return reviewed_fragment_corrections({
                "namespace": namespace,
                "slots": slots,
                "families": [
                    {"id": family_id, "suggestedName": name,
                     "thumbnailSlot": family_id, "uniformSlots": [family_id],
                     "memberSlots": [family_id],
                     "sourceFamily": f"synthetic/{family_id}",
                     "allowedLayers": ["land"], "underwater": False,
                     "tileType": 0}
                    for family_id, name in names.items()
                ],
            })

        snow = {1: "Stone / Dark", 6: "Ground / Light",
                8: "Stone / Black", 10: "Rock / Greenish",
                13: "Ground / Mountainous", 16: "Ground / Trampled"}
        for namespace in ("bz11k", "Zone14"):
            with self.subTest(namespace=namespace):
                result = corrected(namespace, snow)
                self.assertEqual(
                    16, result["slots"][182]["signature"]["southWest"])
                self.assertEqual(
                    1, result["slots"][431]["signature"]["northWest"])
                self.assertEqual(
                    1, result["slots"][470]["signature"]["northWest"])
                self.assertEqual(
                    8, result["slots"][480]["signature"]["southEast"])
                self.assertEqual(
                    "Rock / Black",
                    next(family for family in result["families"]
                         if family["id"] == 10)["suggestedName"])

        ground = {1: "Grass / Common", 3: "Ground / Common",
                  8: "Sand / Common", 18: "Ground / Gravel"}
        for namespace in ("bz4g", "Zone4"):
            with self.subTest(namespace=namespace):
                result = corrected(namespace, ground)
                for slot in (169, 185, 195):
                    self.assertEqual(
                        18, result["slots"][slot]["signature"]["southWest"])

        bz16h = corrected(
            "bz16h", {1: "Sand / Common", 2: "Ground / Plant soil",
                       8: "Stone / Weathered", 12: "Stone / Desert"})
        self.assertEqual(
            2, bz16h["slots"][480]["signature"]["southWest"])
        zone18 = corrected(
            "Zone18", {1: "Sand / Common", 2: "Ground / Plant soil",
                        4: "Ground / Red", 7: "Rock / Lime",
                        8: "Stone / Weathered", 12: "Stone / Desert"})
        self.assertEqual(4, zone18["slots"][480]["signature"]["northWest"])
        self.assertEqual(2, zone18["slots"][480]["signature"]["southWest"])

        rename_cases = {
            "bz13h": {1: "Ground / Red", 7: "Sand / Light"},
            "Zone15": {1: "Ground / Red", 7: "Sand / Light"},
            "Zone19": {1: "Sand / Common", 5: "Sand / Light"},
        }
        for namespace, names in rename_cases.items():
            with self.subTest(namespace=namespace):
                result = corrected(namespace, names)
                renamed_id = 5 if namespace.casefold() == "zone19" else 7
                self.assertEqual(
                    "Sand / Yellow",
                    next(family for family in result["families"]
                         if family["id"] == renamed_id)["suggestedName"])

        # The visually distinct bz5g/Zone8 family was explicitly kept Light.
        for namespace in ("bz5g", "Zone8"):
            with self.subTest(namespace=namespace):
                result = corrected(
                    namespace,
                    {1: "Rock / Underwater emerald", 2: "Sand / Underwater",
                     12: "Sand / Light", 14: "Sand / Grainy"}
                    if namespace.casefold() == "bz5g" else
                    {1: "Rock / Underwater emerald", 2: "Sand / Underwater",
                     11: "Sand / Grainy", 12: "Sand / Light"})
                self.assertEqual(
                    "Sand / Light",
                    next(family for family in result["families"]
                         if family["id"] == 12)["suggestedName"])

    def test_family_catalog_decisions_change_only_verified_corners(self) -> None:
        def recipe(namespace: str, count: int = 512) -> dict:
            return {
                "namespace": namespace,
                "slots": self.uniform_slots(count),
                "families": [
                    {"id": family_id, "suggestedName": f"Family {family_id}"}
                    for family_id in range(1, 24)
                ],
            }

        zone13 = recipe("Zone13")
        zone13["slots"][63]["signature"] = {
            "northWest": 9, "northEast": 9,
            "southEast": 10, "southWest": 9,
        }
        zone13["slots"][164]["signature"] = {
            "northWest": 9, "northEast": 9,
            "southEast": 4, "southWest": 4,
        }
        zone13 = reviewed_fragment_corrections(zone13)
        self.assertEqual(3, zone13["slots"][63]["signature"]["northEast"])
        self.assertEqual(4, zone13["slots"][63]["signature"]["southEast"])
        self.assertEqual(
            4, zone13["slots"][63]["signature"]["southWest"],
            "the focused human comparison selected Grass / Tundra")
        self.assertEqual(
            "Grass / Tundra",
            next(family for family in zone13["families"]
                 if family["id"] == 4)["suggestedName"])
        self.assertEqual(
            289,
            next(family for family in zone13["families"]
                 if family["id"] == 9)["thumbnailSlot"],
            "a transition-only family should show its 3-of-4 stamp, not slot 164")
        self.assertEqual(3, zone13["slots"][164]["signature"]["northWest"])
        self.assertEqual(4, zone13["slots"][164]["signature"]["northEast"])

        zone6_recipe = recipe("Zone6")
        zone6_recipe["slots"][16]["signature"] = {
            "northWest": 6, "northEast": 6,
            "southEast": 6, "southWest": 6,
        }
        zone6_recipe["slots"][485]["signature"]["northEast"] = 19
        zone6 = reviewed_fragment_corrections(zone6_recipe)
        self.assertNotIn(6, {family["id"] for family in zone6["families"]})
        self.assertEqual(5, zone6["slots"][16]["signature"]["northWest"])
        self.assertEqual(8, zone6["slots"][394]["signature"]["northWest"])
        self.assertEqual(3, zone6["slots"][394]["signature"]["northEast"])
        self.assertEqual(8, zone6["slots"][485]["signature"]["northEast"])
        self.assertNotIn(19, {family["id"] for family in zone6["families"]})

        zone6_2_recipe = recipe("Zone6_2")
        zone6_2_recipe["slots"][16]["signature"] = {
            "northWest": 6, "northEast": 6,
            "southEast": 6, "southWest": 6,
        }
        zone6_2_recipe["slots"][59]["signature"]["southWest"] = 11
        zone6_2_recipe["slots"][401]["signature"] = {
            "northWest": 19, "northEast": 1,
            "southEast": 19, "southWest": 1,
        }
        zone6_2_recipe["slots"][485]["signature"]["northEast"] = 19
        zone6_2 = reviewed_fragment_corrections(zone6_2_recipe)
        self.assertNotIn(6, {family["id"] for family in zone6_2["families"]})
        self.assertEqual(
            1, zone6_2["slots"][394]["signature"]["southWest"],
            "the focused review kept Stone / Lime")
        self.assertEqual(
            8, zone6_2["slots"][261]["signature"]["northEast"],
            "the focused review selected Ground / Common")
        self.assertEqual(
            11, zone6_2["slots"][59]["signature"]["southWest"],
            "the focused review kept the Snow / Dark identity")
        self.assertEqual(1, zone6_2["slots"][401]["signature"]["northWest"])
        self.assertEqual(1, zone6_2["slots"][401]["signature"]["southEast"])
        self.assertEqual(8, zone6_2["slots"][485]["signature"]["northEast"])
        self.assertNotIn(19, {family["id"] for family in zone6_2["families"]})

        zone7_recipe = recipe("Zone7")
        zone7_recipe["slots"][1]["signature"] = {
            "northWest": 4, "northEast": 4,
            "southEast": 4, "southWest": 4,
        }
        zone7_recipe["slots"][2]["signature"] = {
            "northWest": 14, "northEast": 14,
            "southEast": 14, "southWest": 14,
        }
        zone7_recipe["slots"][226]["signature"]["southWest"] = 16
        zone7_recipe["slots"][329]["signature"] = {
            "northWest": 16, "northEast": 1,
            "southEast": 16, "southWest": 16,
        }
        zone7_recipe["slots"][335]["signature"]["southEast"] = 16
        zone7_recipe["slots"][480]["signature"]["southWest"] = 16
        zone7_recipe["slots"][412]["signature"]["southWest"] = 22
        zone7 = reviewed_fragment_corrections(zone7_recipe)
        self.assertEqual(
            13, zone7["slots"][151]["signature"]["northWest"],
            "the focused review selected Ground / Brown")
        self.assertEqual(10, zone7["slots"][163]["signature"]["southEast"])
        names = {family["id"]: family["suggestedName"]
                 for family in zone7["families"]}
        self.assertEqual("Rock / Underwater emerald", names[1])
        self.assertEqual("Sand / Underwater", names[4])
        self.assertEqual("Rubble / Gray", names[14])
        self.assertEqual(17, zone7["slots"][412]["signature"]["southWest"])
        self.assertEqual(17, zone7["slots"][226]["signature"]["southWest"])
        self.assertEqual(13, zone7["slots"][329]["signature"]["northWest"])
        self.assertEqual(13, zone7["slots"][329]["signature"]["southEast"])
        self.assertEqual(17, zone7["slots"][329]["signature"]["southWest"])
        self.assertEqual(16, zone7["slots"][335]["signature"]["southEast"])
        self.assertEqual(17, zone7["slots"][480]["signature"]["southWest"])
        self.assertIn(16, {family["id"] for family in zone7["families"]})
        self.assertIn(21, {family["id"] for family in zone7["families"]})
        self.assertNotIn(
            22, {family["id"] for family in zone7["families"]},
            "focused recipes without binding provenance must not invent R106 families")

        zone19 = reviewed_fragment_corrections(recipe("Zone19"))
        self.assertEqual(
            "Paving / Sand ruins",
            next(family for family in zone19["families"]
                 if family["id"] == 7)["suggestedName"])

    def test_r73_shipped_snow_atlas_replays_full_corner_review(self) -> None:
        root = Path(__file__).resolve().parent.parent
        catalog = json.loads((
            root / "assets" / "resources" / "terrain_atlas_presets.json"
        ).read_text(encoding="utf-8"))
        presets = {value["namespace"]: value for value in catalog["presets"]}
        bz11k = presets["bz11k"]
        zone14 = presets["Zone14"]
        bz_payload = base64.b64decode(bz11k["signaturesBase64"])
        zone_payload = base64.b64decode(zone14["signaturesBase64"])
        self.assertEqual(
            bz_payload, zone_payload,
            "byte-identical bz11k/Zone14 atlases must keep exact ownership")

        family_ids = {family["name"]: int(family["id"])
                      for family in bz11k["families"]}
        remaining_grayish = [
            (slot, corner)
            for slot in range(bz11k["slotCount"])
            for corner in range(4)
            if bz_payload[slot * 4 + corner]
            == family_ids["Stone / Grayish"]
        ]
        self.assertEqual(
            [(437, 0), (437, 1), (446, 1), (446, 2)],
            remaining_grayish,
            "only the four explicitly retained Stone / Grayish corners remain")

        def signature(slot: int) -> tuple[int, int, int, int]:
            start = slot * 4
            return tuple(bz_payload[start:start + 4])

        self.assertEqual(
            (family_ids["Stone / Black"], family_ids["Stone / Black"],
             family_ids["Ground / Stony gray"], family_ids["Rock / Black"]),
            signature(489))
        self.assertEqual(family_ids["Stone / Dark"], signature(56)[3])
        self.assertEqual(family_ids["Ground / Light"], signature(176)[0])
        self.assertEqual(
            (family_ids["Stone / Dark"], family_ids["Stone / Black"],
             family_ids["Rock / Black"], family_ids["Rock / Black"]),
            signature(285))
        self.assertEqual(
            (family_ids["Stone / Black"], family_ids["Ground / Light"],
             family_ids["Rock / Black"], family_ids["Rock / Black"]),
            signature(495))
        self.assertNotIn(
            "Stone / Gray", family_ids,
            "R73 resolves both remaining local Stone / Gray corners")
        self.assertNotIn(
            "Ground / Mountainous", family_ids,
            "R75 reassigns the sole remaining corner to Ground / Trampled")
        self.assertEqual(
            family_ids["Ground / Stony gray"], signature(70)[3])
        self.assertEqual(
            (family_ids["Stone / Dark"], family_ids["Ice / Common"],
             family_ids["Stone / Dark"], family_ids["Stone / Black"]),
            signature(288))
        self.assertEqual(
            (family_ids["Stone / Black"], family_ids["Ice / Common"],
             family_ids["Stone / Black"], family_ids["Ground / Light"]),
            signature(498))

    def test_family_catalog_mixed_thumbnail_corners_are_explicit(self) -> None:
        cases = {
            "Zone20": ((37, "northWest", 2),
                       (158, "southEast", 2),
                       (158, "southWest", 3)),
            "Zone3Obr": ((96, "northWest", 4),),
        }
        for namespace, claims in cases.items():
            result = reviewed_fragment_corrections({
                "namespace": namespace,
                "slots": self.uniform_slots(512),
                "families": [{"id": family_id}
                             for family_id in range(1, 24)],
            })
            for slot, corner, family_id in claims:
                self.assertEqual(
                    family_id, result["slots"][slot]["signature"][corner],
                    f"{namespace} slot {slot} {corner}")


class UniformIntraTileAuditTests(unittest.TestCase):
    def test_solid_tile_has_zero_corner_distance(self) -> None:
        tile = np.full((128, 128, 4), (72, 118, 43, 255), dtype=np.uint8)
        distances = intra_tile_corner_distances(tile)
        self.assertEqual(6, len(distances))
        self.assertTrue(all(value == 0.0 for value, _, _ in distances))

    def test_mixed_corner_tile_is_detected(self) -> None:
        tile = np.full((128, 128, 4), (72, 118, 43, 255), dtype=np.uint8)
        tile[:32, :32, :3] = (190, 45, 31)
        distances = intra_tile_corner_distances(tile)
        maximum = max(distances)
        self.assertGreater(maximum[0], 0.10)
        self.assertIn(0, maximum[1:])

    def test_center_decoration_does_not_create_corner_conflict(self) -> None:
        tile = np.full((128, 128, 4), (72, 118, 43, 255), dtype=np.uint8)
        tile[40:88, 40:88, :3] = (230, 220, 30)
        distances = intra_tile_corner_distances(tile)
        self.assertTrue(all(value == 0.0 for value, _, _ in distances))

    def test_vertical_flip_preserves_maximum_distance(self) -> None:
        tile = np.full((128, 128, 4), (72, 118, 43, 255), dtype=np.uint8)
        tile[:32, :32, :3] = (190, 45, 31)
        tile[96:, 96:, :3] = (35, 65, 190)
        normal = max(value for value, _, _ in intra_tile_corner_distances(tile))
        flipped = max(value for value, _, _ in intra_tile_corner_distances(
            np.flipud(tile).copy()))
        self.assertAlmostEqual(normal, flipped, places=12)

    def test_candidate_ranking_excludes_complete_source_slot(self) -> None:
        sample = np.asarray((0.0, 0.0), dtype=np.float64)
        prototypes = {
            1: {
                10: [np.asarray((0.0, 0.0), dtype=np.float64)],
                11: [np.asarray((0.4, 0.4), dtype=np.float64)],
            },
            2: {
                20: [np.asarray((0.1, 0.1), dtype=np.float64)],
            },
        }
        ranked = ranked_anchor_candidates(
            sample, prototypes, {1: "Self", 2: "Other"}, 10,
            {11: 0.4, 20: 0.1})
        self.assertEqual([2, 1], [value["familyId"] for value in ranked])
        self.assertEqual(20, ranked[0]["anchorSlot"])
        self.assertEqual(11, ranked[1]["anchorSlot"])
        self.assertEqual("NW", ranked[0]["anchorCorner"])
        self.assertEqual(0.1, ranked[0]["anchorMaxCornerDistance"])
        self.assertEqual(0.4, ranked[1]["anchorMaxCornerDistance"])

    def test_unverified_uniform_tile_cannot_become_titled_reference(self) -> None:
        prototypes = {
            1: {
                10: [np.asarray((0.0, 0.0), dtype=np.float64)],
                11: [np.asarray((0.2, 0.2), dtype=np.float64)],
            },
            2: {
                20: [np.asarray((0.1, 0.1), dtype=np.float64)],
            },
        }
        verified = verified_anchor_prototypes(prototypes, {1: {11}, 2: {20}})
        self.assertNotIn(10, verified[1])
        ranked = ranked_anchor_candidates(
            np.asarray((0.0, 0.0), dtype=np.float64), prototypes,
            {1: "Reviewed family", 2: "Other"}, -1,
            verified_anchor_slots={1: {11}, 2: {20}})
        self.assertEqual([2, 1], [value["familyId"] for value in ranked])
        self.assertEqual([20, 11], [value["anchorSlot"] for value in ranked])


class ReviewDocumentTests(unittest.TestCase):
    CATALOG_CONTROLS = (
        '<button id="submit-comments"></button>'
        '<button id="show-comments"></button>'
        '<textarea id="comment-export-text"></textarea>'
        '<script>localStorage.setItem("x","y");'
        'window.openai.sendFollowUpMessage({});</script>')
    QUESTIONNAIRE_CONTROLS = (
        '<button id="submit-answers"></button>'
        '<button id="show-answers"></button>'
        '<textarea id="answer-export-text"></textarea>'
        '<script>localStorage.setItem("x","y");'
        'window.openai.sendFollowUpMessage({});</script>')

    def test_questionnaire_detects_hd_only_namespace_without_substitution(self) -> None:
        preset = {"namespace": "ZoneOnlyHd", "slotCount": 128}
        hd_index = {"zoneonlyhd000.mmp": {}, "zoneonlyhd001.mmp": {}}
        original_index = {"zoneonlyhd000.mmp": {}}
        self.assertTrue(archive_has_namespace(hd_index, preset))
        self.assertFalse(archive_has_namespace(original_index, preset))

    def test_decoded_identity_groups_ignore_namespace_names(self) -> None:
        def preset(namespace: str, pixel_hash: str, signatures: str,
                   slot_count: int = 512) -> dict:
            return {
                "namespace": namespace,
                "slotCount": slot_count,
                "bindings": [{
                    "variant": "hd", "pixelSha256": pixel_hash}],
                "families": [{"id": 1}],
                "signaturesBase64": signatures,
            }

        rows = decoded_identity_rows({"presets": [
            preset("briefing-name", "a" * 64, "same-table"),
            preset("gameplay-name", "a" * 64, "same-table"),
            preset("different-content", "b" * 64, "same-table"),
            preset("different-size", "a" * 64, "same-table", 256),
        ]})
        self.assertEqual(1, len(rows))
        self.assertEqual("briefing-name;gameplay-name", rows[0]["namespaces"])
        self.assertEqual(1, rows[0]["distinctSignatureTables"])

    def test_family_references_never_enlarge_a_corner_as_a_full_tile(self) -> None:
        import tempfile

        tile = np.zeros((32, 32, 4), dtype=np.uint8)
        with tempfile.TemporaryDirectory() as directory:
            images = Path(directory)
            uniform_image, uniform_marker = save_family_reference(
                images, "hd", "ZoneX", 7, 0, "uniform", tile)
            mixed_image, mixed_marker = save_family_reference(
                images, "hd", "ZoneX", 8, 2, "thumbnail/stamp", tile)
        self.assertEqual("images/hd-zonex-slot-7.jpg", uniform_image)
        self.assertEqual("", uniform_marker)
        self.assertEqual("images/hd-zonex-slot-8.jpg", mixed_image)
        self.assertIn("corner-se", mixed_marker)
        self.assertNotIn("-se.png", mixed_image)

    def test_questionnaire_never_trusts_a_tile_queued_for_review(self) -> None:
        preset = {
            "families": [{"id": 1, "thumbnailSlot": 0}],
            "signaturesBase64": base64.b64encode(bytes([
                1, 1, 1, 1,
                1, 1, 1, 1,
                1, 1, 1, 1,
            ])).decode("ascii"),
        }
        references = family_references(
            preset, 1,
            preferred_slots=[{"slot": 1, "rotation": 0}],
            excluded_slots={0, 1}, limit=3)
        self.assertEqual([(2, 0, "uniform", 0)], references)

    def test_original_questionnaire_source_must_be_lower_resolution(self) -> None:
        self.assertTrue(original_dimensions_are_lower(
            {(2048, 2048)}, {(512, 512)}))
        self.assertTrue(original_dimensions_are_lower(
            {(2048, 2048)}, {(1024, 1024)}))
        self.assertFalse(original_dimensions_are_lower(
            {(2048, 2048)}, {(2048, 2048)}))
        self.assertFalse(original_dimensions_are_lower(
            {(1024, 1024)}, {(2048, 2048)}))

    def test_catalog_requires_one_comment_field_per_family(self) -> None:
        errors = review_document_errors(
            self.CATALOG_CONTROLS +
            '<textarea class="family-comment"></textarea>', "", 2)
        self.assertIn("family comments 1 != families 2", errors)

    def test_every_family_question_requires_a_visual_comparison(self) -> None:
        catalog = (self.CATALOG_CONTROLS +
                   '<textarea class="family-comment"></textarea>')
        errors = review_document_errors(
            catalog,
            self.QUESTIONNAIRE_CONTROLS +
            '<div class="corner-answer family-question"></div>', 1)
        self.assertIn("family-question visuals 0 != questions 1", errors)
        self.assertEqual([], review_document_errors(
            catalog,
            self.QUESTIONNAIRE_CONTROLS +
            '<div class="corner-answer family-question">'
            '<div class="compare-grid"><img src="a.jpg"></div></div>', 1))

    def test_questionnaire_requires_persistence_submit_and_export(self) -> None:
        catalog = (self.CATALOG_CONTROLS +
                   '<textarea class="family-comment"></textarea>')
        errors = review_document_errors(catalog, '<main></main>', 1)
        self.assertIn("questionnaire submit is missing", errors)
        self.assertIn("questionnaire persistence is missing", errors)
        self.assertIn("questionnaire export panel is missing", errors)

    def test_questionnaire_interaction_script_is_valid_javascript(self) -> None:
        script = questionnaire_interaction_script()
        self.assertIn("navigator.clipboard.writeText", script)
        self.assertIn("semantic-atlas-r49-answers.txt", script)
        self.assertIn("answer-export-text", script)
        self.assertNotIn("not answered", script.lower())
        self.assertIn("Comment", script)
        self.assertIn("answer!==block.dataset.default", script)
        self.assertNotIn("explicitly reviewed", script.lower())
        self.assertNotIn(
            "focused review decisions:\n'", script,
            "a literal newline inside a quoted JavaScript string disables every questionnaire action")
        node = shutil.which("node")
        if node is None:
            self.skipTest("Node.js is unavailable for JavaScript syntax validation")
        result = subprocess.run(
            [node, "--check", "-"], input=script, text=True,
            capture_output=True, check=False)
        self.assertEqual(0, result.returncode, result.stderr)
        harness = Path(__file__).with_name(
            "test_questionnaire_interaction.js")
        result = subprocess.run(
            [node, str(harness)], input=script, text=True,
            capture_output=True, check=False)
        self.assertEqual(0, result.returncode, result.stderr)

    def test_marked_tile_questionnaire_defaults_to_keep_current(self) -> None:
        source = Path(__file__).with_name(
            "render_marked_tile_questionnaire.py").read_text(encoding="utf-8")
        self.assertIn("Keep current:", source)
        self.assertNotIn('<option value="">Not answered</option>', source)
        self.assertIn("comparison-comment", source)

    def test_merged_questionnaire_has_no_separate_review_checkbox(self) -> None:
        source = Path(__file__).with_name(
            "render_r86_merged_modern_review.py").read_text(encoding="utf-8")
        self.assertNotIn('class="reviewed"', source)
        self.assertNotIn("querySelector('.reviewed')", source)
        self.assertNotIn("review-agreements", source)
        self.assertIn("value!==c.dataset.default", source)

    def test_combined_corner_questionnaire_resolves_tile_in_one_card(self) -> None:
        source = Path(__file__).with_name(
            "render_r87_combined_corner_questionnaire.py").read_text(
                encoding="utf-8")
        self.assertIn("This tile is uniform", source)
        self.assertIn("Assign all four corners individually", source)
        for corner in ("NW", "NE", "SE", "SW"):
            self.assertIn(f'"{corner}"', source)
        self.assertNotIn('<input type="checkbox"', source)
        self.assertIn("untouchedDefaultsOmitted", source)

    def test_uniform_variant_questionnaire_is_conservative_by_default(self) -> None:
        source = Path(__file__).with_name(
            "render_uniform_variant_whitelist_questionnaire.py").read_text(
                encoding="utf-8")
        self.assertIn("Keep included: current representative", source)
        self.assertIn("Keep excluded pending review", source)
        self.assertIn("Include as safe uniform paint variant", source)
        self.assertIn("Different family (note)", source)
        self.assertNotIn("Not answered", source)

    def test_questionnaire_interaction_uses_requested_checkpoint(self) -> None:
        script = questionnaire_interaction_script("R50")
        self.assertIn("Semantic-atlas R50 focused review decisions", script)
        self.assertIn("semantic-atlas-r50-answers.txt", script)
        self.assertIn("ei-semantic-r50-review:", script)

    def test_focused_ground_review_requires_hd_and_original_visuals(self) -> None:
        catalog = (self.CATALOG_CONTROLS +
                   '<textarea class="family-comment"></textarea>')
        focused = (self.QUESTIONNAIRE_CONTROLS +
                   '<div data-follow-up="common-gravel">'
                   '<div data-variant="hd"></div></div>')
        errors = review_document_errors(catalog, focused, 1)
        self.assertIn(
            "original comparison variants 0 != focused questions 1", errors)
        self.assertEqual([], review_document_errors(
            catalog, focused.replace(
                '</div></div>',
                '</div><div data-variant="original"></div></div>'), 1))

    def test_completed_catalog_prefix_is_removed_inclusively(self) -> None:
        namespaces = ["BaseGipat", "Zone1", "Zone10", "Zone11", "Zone12"]
        self.assertEqual(
            ["Zone11", "Zone12"],
            pending_review_namespaces(namespaces, "Zone10"))
        self.assertEqual(namespaces, pending_review_namespaces(namespaces, None))
        with self.assertRaisesRegex(ValueError, "not in catalog"):
            pending_review_namespaces(namespaces, "Zone99")

    def test_second_family_review_batch_tracks_applied_and_pending_items(self) -> None:
        root = Path(__file__).resolve().parent.parent
        evidence = json.loads((
            root / "scripts" / "semantic_atlas_family_review_evidence.json"
        ).read_text(encoding="utf-8"))
        presets = json.loads((
            root / "assets" / "resources" / "terrain_atlas_presets.json"
        ).read_text(encoding="utf-8"))
        family_ids = {
            preset["namespace"]: {int(family["id"])
                                  for family in preset["families"]}
            for preset in presets["presets"]
        }
        batch = evidence["batches"][0]
        self.assertEqual("Zone7", batch["reviewedThrough"])
        self.assertEqual("partially-verified-and-applied", batch["status"])
        self.assertEqual(35, len(batch["entries"]))
        self.assertEqual(6, len(batch["verification"]["withheld"]))
        for entry in batch["entries"]:
            namespace = entry["namespace"]
            self.assertIn(namespace, family_ids)
        questions = json.loads((
            root / "scripts" / "semantic_atlas_pending_family_questions.json"
        ).read_text(encoding="utf-8"))
        self.assertEqual("R52", questions["checkpoint"])
        self.assertEqual([], questions["questions"])
        for question in questions["questions"]:
            self.assertGreaterEqual(len(question["options"]), 3)
            for reference in question["references"]:
                namespace = reference["namespace"]
                self.assertIn(reference["familyId"], family_ids[namespace])
        final_batch = evidence["batches"][1]
        self.assertEqual("ZoneMainMenuNew", final_batch["reviewedThrough"])
        self.assertEqual("approved-no-changes", final_batch["status"])
        self.assertEqual([], final_batch["entries"])
        ground_batch = evidence["batches"][2]
        self.assertEqual("focused-family-review-r49", ground_batch["id"])
        self.assertEqual(6, len(ground_batch["entries"]))
        self.assertIn("ground-audit-pending", ground_batch["status"])
        completed_ground_batch = evidence["batches"][3]
        self.assertEqual("focused-ground-review-r50",
                         completed_ground_batch["id"])
        self.assertEqual("explicit-decisions-applied",
                         completed_ground_batch["status"])
        self.assertEqual(13, len(completed_ground_batch["entries"]))
        overlap_batch = evidence["batches"][4]
        self.assertEqual("focused-overlap-review-r51", overlap_batch["id"])
        self.assertEqual("explicit-keep-decision-recorded",
                         overlap_batch["status"])
        self.assertEqual(1, len(overlap_batch["entries"]))

    def test_shipped_catalog_contains_exact_r50_corner_decisions(self) -> None:
        root = Path(__file__).resolve().parent.parent
        catalog = json.loads((
            root / "assets" / "resources" / "terrain_atlas_presets.json"
        ).read_text(encoding="utf-8"))
        presets = {entry["namespace"]: entry for entry in catalog["presets"]}

        def corner(namespace: str, slot: int, index: int) -> int:
            payload = base64.b64decode(presets[namespace]["signaturesBase64"])
            return payload[slot * 4 + index]

        self.assertEqual(1, corner("Zone6_2", 401, 0))
        self.assertEqual(1, corner("Zone6_2", 401, 2))
        self.assertEqual(8, corner("Zone6_2", 485, 1))
        self.assertEqual(8, corner("Zone6", 485, 1))
        self.assertEqual(17, corner("Zone7", 412, 3))
        self.assertEqual(17, corner("Zone7", 226, 3))
        # R117 supersedes the two Ground / Brown corners with the reviewed
        # Ground / Dark brown identity; the retained SW remains Brownish.
        self.assertEqual((22, 22, 17), (
            corner("Zone7", 329, 0), corner("Zone7", 329, 2),
            corner("Zone7", 329, 3)))
        self.assertEqual(16, corner("Zone7", 335, 2))
        self.assertEqual(17, corner("Zone7", 480, 3))
        self.assertEqual(3, corner("Zone13", 164, 0))
        self.assertEqual(4, corner("Zone13", 164, 1))
        self.assertEqual(
            8, corner("Zone13", 61, 3),
            "R51 explicitly keeps the authored Paving / Square identity")

        family_ids = {
            namespace: {family["id"] for family in presets[namespace]["families"]}
            for namespace in ("Zone6", "Zone6_2", "Zone7")
        }
        self.assertNotIn(19, family_ids["Zone6"])
        self.assertNotIn(19, family_ids["Zone6_2"])
        self.assertIn(16, family_ids["Zone7"])
        # R106 reassigns the final Trampled corner, so the dead family record
        # is removed instead of surviving as an unreferenced support row.
        self.assertNotIn(21, family_ids["Zone7"])
        self.assertIn(22, family_ids["Zone7"])

    def test_shipped_catalog_contains_exact_r80_corner_decisions(self) -> None:
        """R80 keeps explicit human choices and no longer trusts false uniforms."""
        root = Path(__file__).resolve().parent.parent
        catalog = json.loads((
            root / "assets" / "resources" / "terrain_atlas_presets.json"
        ).read_text(encoding="utf-8"))
        presets = {entry["namespace"].casefold(): entry
                   for entry in catalog["presets"]}

        def corner_name(namespace: str, slot: int, corner_index: int) -> str:
            preset = presets[namespace.casefold()]
            names = {int(family["id"]): family["name"]
                     for family in preset["families"]}
            payload = base64.b64decode(preset["signaturesBase64"])
            return names[payload[slot * 4 + corner_index]]

        # All entries here are explicit choices. Still-uncertain R80 rows and
        # notes that only request another review are intentionally absent.
        decisions = {
            "bz5g": {
                (279, 0): "Ground / Common",
                # R116 supersedes the older R80 Gravel assignment.
                (465, 3): "Ground / Common",
                (356, 0): "Sand / Common",
                (18, 0): "Ground / Common",
                (367, 0): "Sand / Common",
                (445, 1): "Ground / Common",
                (458, 3): "Sand / Grainy",
                (465, 1): "Ground / Common",
            },
            "zone8": {
                (279, 0): "Ground / Common",
                # R116 propagates the reviewed exact slot to Zone8.
                (465, 3): "Ground / Common",
                (356, 0): "Sand / Common",
                (18, 0): "Ground / Common",
                (367, 0): "Sand / Common",
                (445, 1): "Ground / Common",
                (458, 3): "Sand / Grainy",
                (465, 1): "Ground / Common",
            },
            "zone19": {
                (309, 1): "Sand / Gravel",
                (446, 1): "Sand / Common",
                (310, 1): "Sand / Gravel",
                (310, 3): "Sand / Gravel",
            },
            "bz2g": {
                (180, 0): "Grass / Common",
                (376, 0): "Grass / Irregular",
            },
            "zone2": {
                (180, 0): "Grass / Common",
                (376, 0): "Grass / Irregular",
            },
            "bz16h": {
                (191, 0): "Ground / Red", (191, 1): "Ground / Red",
            },
            "zone18": {
                (191, 0): "Ground / Red", (191, 1): "Ground / Red",
            },
            "bz13h": {(288, 1): "Stone / Orange"},
            "zone15": {(288, 1): "Stone / Orange"},
            "bz11k": {(327, 3): "Rock / Black"},
            "zone14": {(327, 3): "Rock / Black"},
            "bz8k": {
                (337, 0): "Grass / Winter normal",
                (326, 0): "Ground / Light", (327, 0): "Ground / Light",
            },
            "bz9k": {
                (337, 0): "Grass / Winter normal",
                (326, 0): "Ground / Light", (327, 0): "Ground / Light",
            },
            "zone11": {
                (337, 0): "Grass / Winter normal",
                (326, 0): "Ground / Light", (327, 0): "Ground / Light",
            },
            # R114's focused audit review supersedes the older R80 choice.
            "bz3g": {(186, 1): "Ground / Common"},
            "zone10": {(471, 3): "Slabs / Square"},
            "zone13": {
                (112, 0): "Ground / Light", (279, 3): "Sand / Common",
                (283, 3): "Sand / Common", (63, 1): "Ground / Light",
                (164, 0): "Ground / Light", (164, 1): "Grass / Tundra",
                (324, 0): "Ground / Light", (325, 0): "Ground / Light",
                (326, 0): "Ground / Light", (327, 0): "Ground / Light",
                (398, 2): "Ground / Light", (409, 1): "Ground / Light",
                (449, 0): "Ground / Light", (450, 1): "Ground / Light",
            },
            "zone20": {
                (37, 0): "Stone / Greenish", (38, 0): "Stone / Greenish",
                (52, 0): "Stone / Greenish", (189, 0): "Stone / Greenish",
            },
            "zone6": {
                (28, 3): "Ground / Dead", (471, 0): "Grass / Sparse dark",
                (71, 0): "Ground / Common", (278, 0): "Ground / Common",
                (394, 0): "Ground / Common", (401, 3): "Ground / Dead",
                (60, 3): "Ground / Dead",
            },
            "zone6_2": {
                (28, 3): "Ground / Dead", (471, 0): "Grass / Sparse dark",
                (394, 1): "Grass / Sparse dark",
                (71, 0): "Ground / Common", (278, 0): "Ground / Common",
            },
        }
        for namespace, expected in decisions.items():
            for (slot, corner_index), family_name in expected.items():
                with self.subTest(namespace=namespace, slot=slot,
                                  corner=corner_index):
                        self.assertEqual(
                            r102_expected_name(namespace, family_name),
                            corner_name(namespace, slot, corner_index))

        # The non-HD semantic policy remains one identity per reviewed pair.
        for namespace in ("bz13h", "zone15", "bz15h", "zone17"):
            names = [family["name"]
                     for family in presets[namespace]["families"]]
            self.assertEqual(1, names.count("Ground / Red"))
            self.assertEqual(1, names.count("Rubble / Orange"))

    def test_shipped_catalog_contains_exact_r81_corner_decisions(self) -> None:
        """R81 applies explicit quarters and mirrors only canonical owners."""
        root = Path(__file__).resolve().parent.parent
        catalog = json.loads((
            root / "assets" / "resources" / "terrain_atlas_presets.json"
        ).read_text(encoding="utf-8"))
        presets = {entry["namespace"].casefold(): entry
                   for entry in catalog["presets"]}

        def corner_name(namespace: str, slot: int, corner: int) -> str:
            preset = presets[namespace.casefold()]
            names = {int(family["id"]): family["name"]
                     for family in preset["families"]}
            payload = base64.b64decode(preset["signaturesBase64"])
            return names[payload[slot * 4 + corner]]

        groups = [
            (("bz5g", "zone8"), {
                (345, 0): "Ground / Common", (345, 1): "Sand / Grainy",
                (345, 2): "Sand / Grainy", (345, 3): "Sand / Grainy",
                (106, 0): "Sand / Common", (106, 1): "Sand / Common",
                (106, 2): "Sand / Light", (106, 3): "Sand / Light",
                (150, 0): "Sand / Common", (150, 1): "Sand / Common",
                (150, 2): "Sand / Grainy", (150, 3): "Sand / Grainy",
            }),
            (("zone19",), {
                (488, 0): "Sand / Dark", (488, 1): "Sand / Gravel",
                (488, 2): "Sand / Gravel", (488, 3): "Sand / Gravel",
                (121, 0): "Sand / Common", (121, 1): "Sand / Common",
                (121, 2): "Sand / Common", (121, 3): "Sand / Gravel",
                (122, 0): "Sand / Common", (122, 1): "Sand / Common",
                (122, 2): "Sand / Common", (122, 3): "Sand / Gravel",
                (88, 0): "Sand / Common", (88, 1): "Sand / Common",
                (88, 2): "Sand / Dark", (88, 3): "Sand / Dark",
                (388, 1): "Sand / Dark", (388, 2): "Sand / Dark",
                (388, 3): "Sand / Dark",
            }),
            (("bz16h", "zone18"), {
                (193, 0): "Ground / Red", (193, 1): "Ground / Red",
                (193, 2): "Ground / Desert algae",
                (193, 3): "Ground / Desert algae",
                (194, 0): "Ground / Red", (194, 1): "Ground / Red",
                (194, 2): "Ground / Desert algae",
                (194, 3): "Ground / Desert algae",
            }),
            (("zone18",), {
                (244, 0): "Ground / Red",
                (244, 1): "Ground / Desert algae",
                (244, 2): "Ground / Desert algae",
                (244, 3): "Ground / Desert algae",
                (248, 0): "Ground / Red",
                (248, 1): "Ground / Desert algae",
                (248, 2): "Ground / Desert algae",
                (248, 3): "Ground / Desert algae",
                (249, 0): "Ground / Red",
                (249, 1): "Ground / Desert algae",
                (249, 2): "Ground / Desert algae",
                (249, 3): "Ground / Desert algae",
            }),
            (("bz13h", "zone15"), {
                (257, 0): "Ground / Red", (257, 1): "Stone / Orange",
                (257, 2): "Stone / Orange", (257, 3): "Stone / Orange",
            }),
            (("bz15h", "zone17"), {
                (236, 0): "Ground / Topsoil",
                (236, 1): "Rubble / Orange",
                (236, 2): "Rubble / Orange",
                (236, 3): "Rubble / Orange",
            }),
            (("zone10",), {
                (279, 0): "Grass / Sparse dark",
                (279, 1): "Grass / Sparse dark",
                (279, 2): "Stone / Black", (279, 3): "Slabs / Square",
            }),
            (("zone13",), {
                (370, 0): "Ground / Light", (370, 1): "Sand / Common",
                (370, 2): "Sand / Common", (370, 3): "Sand / Common",
                (289, 0): "Ground / Light", (289, 1): "Snow / Common",
                (289, 2): "Ground / Light", (289, 3): "Grass / Tundra",
                (173, 0): "Ground / Light", (173, 1): "Ground / Light",
                (173, 2): "Ground / Light", (173, 3): "Grass / Tundra",
                (444, 0): "Snow / Common", (444, 1): "Snow / Common",
                (444, 2): "Snow / Common", (444, 3): "Grass / Tundra",
            }),
            (("zone20",), {
                (39, 0): "Stone / Greenish", (39, 1): "Stone / Black",
                (39, 2): "Stone / Black", (39, 3): "Stone / Black",
                (40, 0): "Stone / Greenish", (41, 0): "Stone / Greenish",
                (42, 0): "Stone / Greenish", (44, 0): "Stone / Greenish",
                (46, 0): "Stone / Greenish", (47, 0): "Stone / Greenish",
                (48, 0): "Stone / Greenish", (50, 0): "Stone / Greenish",
                (54, 0): "Stone / Greenish", (57, 0): "Stone / Greenish",
                (61, 0): "Stone / Greenish", (151, 2): "Stone / Greenish",
                (155, 2): "Stone / Greenish", (156, 2): "Stone / Greenish",
                (157, 2): "Stone / Greenish", (160, 2): "Stone / Greenish",
                (183, 0): "Stone / Greenish", (183, 1): "Slabs / Carved",
                (183, 2): "Stone / Greenish", (184, 0): "Stone / Greenish",
                (185, 0): "Stone / Greenish", (186, 0): "Stone / Greenish",
                (186, 3): "Stone / Black", (187, 0): "Stone / Greenish",
                (187, 3): "Stone / Black", (188, 0): "Stone / Greenish",
            }),
            (("bz2g", "zone2"), {
                # R116 resolves both held Brown corners as Ground / Dirt.
                (448, 1): "Ground / Dirt", (448, 2): "Ground / Dirt",
                (448, 3): "Ground / Dirt", (502, 0): "Ground / Dirt",
            }),
        ]
        checked = 0
        for namespaces, decisions in groups:
            for namespace in namespaces:
                for (slot, corner), expected in decisions.items():
                    with self.subTest(namespace=namespace, slot=slot,
                                      corner=corner):
                        self.assertEqual(
                            r102_expected_name(namespace, expected),
                            corner_name(namespace, slot, corner))
                        checked += 1
        self.assertEqual(145, checked)
        self.assertNotIn(
            "Stone / Grayish",
            [family["name"] for family in presets["zone20"]["families"]])

    def test_shipped_catalog_contains_exact_r82_corner_decisions(self) -> None:
        root = Path(__file__).resolve().parent.parent
        catalog = json.loads((
            root / "assets" / "resources" / "terrain_atlas_presets.json"
        ).read_text(encoding="utf-8"))
        presets = {entry["namespace"].casefold(): entry
                   for entry in catalog["presets"]}

        def corner_name(namespace: str, slot: int, corner: int) -> str:
            preset = presets[namespace.casefold()]
            names = {int(family["id"]): family["name"]
                     for family in preset["families"]}
            payload = base64.b64decode(preset["signaturesBase64"])
            return names[payload[slot * 4 + corner]]

        groups = [
            (("bz5g", "zone8"), {
                (19, 0): "Ground / Common", (19, 1): "Ground / Gravel",
                (19, 2): "Ground / Gravel", (19, 3): "Ground / Gravel",
                (122, 0): "Sand / Common", (122, 1): "Sand / Light",
                (122, 2): "Sand / Light", (122, 3): "Sand / Light",
            }),
            (("zone19",), {
                (50, 0): "Sand / Common", (50, 1): "Sand / Common",
                (50, 2): "Sand / Yellow", (50, 3): "Sand / Yellow",
                (123, 0): "Sand / Common", (123, 1): "Sand / Common",
                (123, 2): "Sand / Common", (123, 3): "Sand / Gravel",
                (389, 0): "Sand / Grass", (389, 1): "Sand / Dark",
                (389, 2): "Sand / Dark", (389, 3): "Sand / Dark",
                (490, 1): "Sand / Yellow", (490, 2): "Sand / Yellow",
                (490, 3): "Sand / Yellow", (388, 0): "Sand / Grass",
            }),
            (("bz16h", "zone18"), {
                (79, 0): "Ground / Red", (79, 1): "Ground / Red",
                (79, 2): "Ground / Plant soil",
                (79, 3): "Ground / Plant soil",
            }),
            (("zone13",), {
                (65, 0): "Snow / Common", (65, 1): "Snow / Dark",
                (65, 2): "Snow / Dark", (65, 3): "Snow / Dark",
                (176, 0): "Ground / Light", (176, 1): "Ground / Light",
                (176, 2): "Ground / Light", (176, 3): "Grass / Tundra",
            }),
        ]
        checked = 0
        for namespaces, decisions in groups:
            for namespace in namespaces:
                for (slot, corner), expected in decisions.items():
                    with self.subTest(namespace=namespace, slot=slot,
                                      corner=corner):
                        self.assertEqual(
                            r102_expected_name(namespace, expected),
                            corner_name(namespace, slot, corner))
                        checked += 1
        self.assertEqual(48, checked)
        # R82 deliberately left this comment-only ambiguity unresolved. R83's
        # recovered human selection later resolves it as Paving / Sand ruins.
        self.assertEqual(
            "Paving / Sand ruins", corner_name("zone19", 490, 0))
        self.assertIn(
            "Ground / Dirt",
            {family["name"] for family in presets["zone10"]["families"]})
        self.assertIn(
            "Ground / Brown",
            {family["name"] for family in presets["bz2g"]["families"]})

    def test_shipped_catalog_contains_exact_r83_r84_corner_decisions(self) -> None:
        root = Path(__file__).resolve().parent.parent
        catalog = json.loads((
            root / "assets" / "resources" / "terrain_atlas_presets.json"
        ).read_text(encoding="utf-8"))
        presets = {entry["namespace"].casefold(): entry
                   for entry in catalog["presets"]}

        def corner_name(namespace: str, slot: int, corner: int) -> str:
            preset = presets[namespace.casefold()]
            names = {int(family["id"]): family["name"]
                     for family in preset["families"]}
            payload = base64.b64decode(preset["signaturesBase64"])
            return names[payload[slot * 4 + corner]]

        groups = [
            (("bz5g", "zone8"), {
                (119, 0): "Sand / Common", (119, 1): "Sand / Common",
                (119, 2): "Sand / Common", (119, 3): "Sand / Light",
                (124, 0): "Sand / Common", (124, 1): "Sand / Light",
                (124, 2): "Sand / Light", (124, 3): "Sand / Light",
                (171, 0): "Sand / Common", (171, 1): "Sand / Grainy",
                (171, 2): "Sand / Grainy", (171, 3): "Sand / Grainy",
            }),
            (("zone19",), {
                (313, 0): "Sand / Common", (313, 1): "Sand / Gravel",
                (313, 2): "Sand / Common", (313, 3): "Sand / Gravel",
                (479, 0): "Sand / Common", (479, 1): "Sand / Common",
                (479, 2): "Sand / Yellow", (479, 3): "Sand / Gravel",
                (490, 0): "Paving / Sand ruins",
            }),
            (("bz16h", "zone18"), {
                (80, 0): "Ground / Red", (80, 1): "Ground / Red",
                (80, 2): "Ground / Plant soil",
                (80, 3): "Ground / Plant soil",
            }),
            (("zone13",), {
                (70, 0): "Snow / Common", (70, 1): "Snow / Dark",
                (70, 2): "Snow / Dark", (70, 3): "Snow / Dark",
                (238, 0): "Ground / Light", (238, 1): "Ground / Light",
                (238, 2): "Ground / Light", (238, 3): "Paving / Square",
                (239, 0): "Ground / Light", (239, 1): "Ground / Light",
                (239, 2): "Ground / Light", (239, 3): "Paving / Square",
            }),
        ]
        checked = 0
        for namespaces, decisions in groups:
            for namespace in namespaces:
                for (slot, corner), expected in decisions.items():
                    with self.subTest(namespace=namespace, slot=slot,
                                      corner=corner):
                        self.assertEqual(
                            r102_expected_name(namespace, expected),
                            corner_name(namespace, slot, corner))
                        checked += 1
        self.assertEqual(53, checked)

    def test_shipped_catalog_contains_corrected_r87_corner_decisions(self) -> None:
        """R87 south fields follow physical SW-left/SE-right placement."""
        root = Path(__file__).resolve().parent.parent
        source = (root / "scripts" / "generate_terrain_atlas_presets.py").read_text(
            encoding="utf-8")
        module = ast.parse(source)
        signatures = next(
            ast.literal_eval(node.value)
            for node in ast.walk(module)
            if isinstance(node, ast.Assign)
            and any(isinstance(target, ast.Name)
                    and target.id == "r87_signatures"
                    for target in node.targets))
        catalog = json.loads((
            root / "assets" / "resources" / "terrain_atlas_presets.json"
        ).read_text(encoding="utf-8"))
        presets = {entry["namespace"].casefold(): entry
                   for entry in catalog["presets"]}
        owners = {
            "zone15": ("bz13h", "zone15"),
            "zone16": ("bz14h", "bz18h", "zone16"),
            "zone17": ("bz15h", "zone17"),
            "bz16h": ("bz16h",),
            "zone2": ("bz2g", "zone2"),
            "bz3g": ("bz3g", "zone9"),
            "bz4g": ("bz4g", "zone4"),
            "zone8": ("bz5g", "bz6g", "zone8"),
            "bz9k": ("bz8k", "bz9k", "zone11"),
            "zone13": ("zone13",),
            "zone18": ("zone18",),
            "zone5_1": ("zone5_1",),
        }

        checked = 0
        for owner, slots in signatures.items():
            for namespace in owners[owner]:
                preset = presets[namespace]
                names = {int(family["id"]): family["name"]
                         for family in preset["families"]}
                payload = base64.b64decode(preset["signaturesBase64"])
                for slot, expected in slots.items():
                    for corner, family_name in enumerate(expected):
                        if family_name is None:
                            continue
                        if owner in ("zone5_1", "zone8") \
                                and family_name == "Grass / Irregular":
                            family_name = "Grass / Swamp"
                        with self.subTest(namespace=namespace, slot=slot,
                                          corner=corner):
                            self.assertEqual(
                                r102_expected_name(namespace, family_name),
                                names[payload[slot * 4 + corner]])
                            checked += 1
        self.assertEqual(504, checked)

    def test_r89_preserves_distinct_zone7_ground_families(self) -> None:
        """R89 renames slot405 without swallowing Dirt or Trampled fragments."""
        root = Path(__file__).resolve().parent.parent
        catalog = json.loads((
            root / "assets" / "resources" / "terrain_atlas_presets.json"
        ).read_text(encoding="utf-8"))
        presets = {entry["namespace"].casefold(): entry
                   for entry in catalog["presets"]}

        zone7 = presets["zone7"]
        names = {int(family["id"]): family["name"]
                 for family in zone7["families"]}
        payload = base64.b64decode(zone7["signaturesBase64"])
        self.assertEqual("Ground / Dirt", names[16])
        self.assertEqual("Ground / Brownish", names[17])
        self.assertNotIn(21, names)
        self.assertEqual(16, payload[257 * 4 + 3])
        self.assertEqual(22, payload[335 * 4 + 3])
        self.assertEqual(
            17, payload[480 * 4 + 3],
            "R92 resolved the provisional corner as Ground / Brownish")
        self.assertEqual({17}, set(payload[405 * 4:405 * 4 + 4]))

        bz16h = presets["bz16h"]
        bz_names = {int(family["id"]): family["name"]
                    for family in bz16h["families"]}
        bz_payload = base64.b64decode(bz16h["signaturesBase64"])
        self.assertEqual("Ground / Red", bz_names[bz_payload[169 * 4]])
        self.assertEqual(
            ["Ground / Plant soil"] * 3,
            [bz_names[value] for value in bz_payload[169 * 4 + 1:169 * 4 + 4]],
            "unreviewable R89 defaults are unresolved, not confirmations")

    def test_r92_applies_only_resolved_family_and_corner_decisions(self) -> None:
        """R92 must split reviewed sand without guessing pending corners."""
        root = Path(__file__).resolve().parent.parent
        catalog = json.loads((
            root / "assets" / "resources" / "terrain_atlas_presets.json"
        ).read_text(encoding="utf-8"))
        presets = {entry["namespace"].casefold(): entry
                   for entry in catalog["presets"]}

        def names_and_payload(namespace: str):
            preset = presets[namespace]
            return ({int(family["id"]): family["name"]
                     for family in preset["families"]},
                    base64.b64decode(preset["signaturesBase64"]))

        for namespace in ("bz11k", "zone14"):
            names, payload = names_and_payload(namespace)
            self.assertEqual(
                ["Ground / Light", "Ground / Stony gray",
                 "Ground / Stony gray", "Ground / Stony gray"],
                [names[value] for value in payload[462 * 4:462 * 4 + 4]])
            self.assertEqual("Sand / Irregular", names[20])
            for slot in (218, 219):
                self.assertEqual(
                    ["Sand / Irregular"] * 4,
                    [names[value] for value in payload[slot * 4:slot * 4 + 4]])

        zone7_names, zone7_payload = names_and_payload("zone7")
        self.assertEqual(
            "Ground / Brownish", zone7_names[zone7_payload[480 * 4 + 3]])

        for namespace in ("bz15h", "zone17"):
            names, _ = names_and_payload(namespace)
            self.assertEqual("Rubble / Dry", names[2])

        # The user explicitly retained these base/original-identical HD colour
        # variants in one family; they are not part of the follow-up queue.
        for namespace, slot in (("bz13h", 10), ("bz4g", 6), ("zone4", 6)):
            names, payload = names_and_payload(namespace)
            self.assertEqual(
                [r102_expected_name(namespace, "Sand / Dark")] * 4,
                [names[value] for value in payload[slot * 4:slot * 4 + 4]])

    def test_r93_applies_resolved_corners_without_guessing_named_ground(self) -> None:
        """R93 must apply explicit corners and retain the five unnamed ones."""
        root = Path(__file__).resolve().parent.parent
        catalog = json.loads((
            root / "assets" / "resources" / "terrain_atlas_presets.json"
        ).read_text(encoding="utf-8"))
        presets = {entry["namespace"].casefold(): entry
                   for entry in catalog["presets"]}

        def signature(namespace: str, slot: int):
            preset = presets[namespace]
            names = {int(family["id"]): family["name"]
                     for family in preset["families"]}
            payload = base64.b64decode(preset["signaturesBase64"])
            return [names[value]
                    for value in payload[slot * 4:slot * 4 + 4]]

        for namespace in ("bz5g", "bz6g", "zone8"):
            self.assertEqual(
                ["Sand / Common", "Sand / Common",
                 "Sand / Light", "Sand / Grainy"],
                signature(namespace, 426))
            self.assertEqual(
                ["Sand / Common", "Sand / Common",
                 "Sand / Common", "Sand / Light"],
                signature(namespace, 121))

        for namespace in ("bz3g", "zone9"):
            self.assertEqual(
                ["Stone / Lime", "Ground / Common",
                 "Grass / Sparse dark", "Stone / Grayish"],
                signature(namespace, 416))
            self.assertEqual(
                ["Stone / Grayish", "Ground / Dead",
                 "Stone / Grayish", "Ground / Dead"],
                signature(namespace, 445),
                "NE/SW retain the reviewed family renamed Ground / Dead")
            self.assertEqual(
                ["Ground / Dead", "Stone / Grayish",
                 "Ground / Dead", "Ground / Dead"],
                signature(namespace, 468),
                "NW/SE/SW retain the reviewed family renamed Ground / Dead")

    def test_r97_r98_syncs_reviewed_ground_and_sand_corners(self) -> None:
        """R97 applies its queue; R98 closes the six reviewed SW corners."""
        root = Path(__file__).resolve().parent.parent
        catalog = json.loads((
            root / "assets" / "resources" / "terrain_atlas_presets.json"
        ).read_text(encoding="utf-8"))
        presets = {entry["namespace"].casefold(): entry
                   for entry in catalog["presets"]}

        def family_name(namespace: str, family_id: int) -> str:
            return next(family["name"]
                        for family in presets[namespace.casefold()]["families"]
                        if int(family["id"]) == family_id)

        def signature(namespace: str, slot: int) -> list[str]:
            preset = presets[namespace.casefold()]
            names = {int(family["id"]): family["name"]
                     for family in preset["families"]}
            payload = base64.b64decode(preset["signaturesBase64"])
            return [names[value]
                    for value in payload[slot * 4:slot * 4 + 4]]

        for namespace, family_id in (
                ("bz3g", 6), ("zone9", 6),
                ("zone6", 5), ("zone6_2", 5)):
            self.assertEqual("Ground / Dead",
                             family_name(namespace, family_id))
        for namespace in ("bz2g", "zone2"):
            self.assertEqual("Ground / Dark", family_name(namespace, 15))

        expected_ground = {
            ("zone9", 163): ["Stone / Grayish", "Ground / Dead",
                             "Ground / Dead", "Ground / Dead"],
            ("zone9", 198): ["Ground / Dead", "Ground / Dead",
                             "Stone / Grayish", "Ground / Dead"],
            ("zone9", 297): ["Stone / Grayish", "Grass / Sparse dark",
                             "Stone / Grayish", "Ground / Dead"],
            ("zone9", 476): ["Stone / Grayish", "Grass / Sparse dark",
                             "Ground / Dead", "Ground / Dead"],
            ("bz3g", 163): ["Stone / Grayish", "Ground / Dead",
                            "Ground / Dead", "Ground / Dead"],
            ("bz3g", 198): ["Ground / Dead", "Ground / Dead",
                            "Stone / Grayish", "Ground / Dead"],
            ("bz3g", 297): ["Stone / Grayish", "Grass / Sparse dark",
                            "Stone / Grayish", "Ground / Dead"],
        }
        for key, expected in expected_ground.items():
            self.assertEqual(expected, signature(*key))

        irregular = {
            194: (1, 2, 3), 195: (1, 2, 3), 196: (1, 2, 3),
            220: (0, 1, 2, 3), 292: (3,),
            374: (2, 3), 375: (2, 3),
            384: (1, 2, 3), 385: (1, 2, 3), 392: (2, 3),
            394: (3,), 395: (3,), 396: (2, 3), 397: (2,), 459: (3,),
            507: (3,), 508: (1, 3),
        }
        self.assertEqual(35, sum(len(corners) for corners in irregular.values()))
        for namespace in ("bz11k", "zone14"):
            for slot, corners in irregular.items():
                values = signature(namespace, slot)
                for corner in corners:
                    self.assertEqual("Sand / Irregular", values[corner])

    def test_r120_semantic_checkpoint_is_exact(self) -> None:
        """The modern branch carries the fully resolved R120 catalog verbatim."""
        root = Path(__file__).resolve().parent.parent
        catalog_path = root / "assets" / "resources" / "terrain_atlas_presets.json"
        payload = catalog_path.read_bytes()
        self.assertEqual(
            "cfc41cec99f70a96fda09b52df576ef150b29e67a6689f65a34c6605cad3e5c2",
            hashlib.sha256(payload).hexdigest(),
        )
        catalog = json.loads(payload.decode("utf-8"))
        presets = {entry["namespace"].casefold(): entry
                   for entry in catalog["presets"]}
        for namespace in ("zone5_1", "bz5g", "bz6g", "zone8"):
            self.assertIn(
                "Grass / Swamp",
                {family["name"] for family in presets[namespace]["families"]},
            )
        bz3g = base64.b64decode(presets["bz3g"]["signaturesBase64"])
        zone9 = base64.b64decode(presets["zone9"]["signaturesBase64"])
        self.assertEqual(
            bz3g[506 * 4:507 * 4],
            zone9[506 * 4:507 * 4],
            "the exact slot-506 propagation must remain synchronized",
        )

    def test_questionnaire_layout_contract_is_locked(self) -> None:
        policy = (Path(__file__).resolve().parent.parent / "docs" /
                  "TERRAIN_ATLAS_REVIEW.md").read_text(encoding="utf-8")
        for required in (
                "identical texture/pixel scale",
                "Every concrete family offered by a selector has a titled reference",
                "NW/NE above SW/SE",
                "serialize as NW, NE, SE, SW",
                "must not be redesigned without direct user authorization"):
            self.assertIn(required, policy)
    def test_r102_applies_only_structured_brush_inventory_decisions(self) -> None:
        """Every applied R102 change is enumerated; review leads stay data-only."""
        root = Path(__file__).resolve().parent.parent
        decisions = json.loads((root / "scripts" /
            "semantic_atlas_r102_brush_inventory_decisions.json"
        ).read_text(encoding="utf-8"))
        catalog = json.loads((root / "assets" / "resources" /
            "terrain_atlas_presets.json").read_text(encoding="utf-8"))
        presets = {entry["namespace"].casefold(): entry
                   for entry in catalog["presets"]}

        for namespace, renames in decisions["familyRenames"].items():
            families = {int(row["id"]): row["name"]
                        for row in presets[namespace]["families"]}
            for family_id, expected in renames.items():
                # R109 explicitly reassigns the sole remaining Bz7g
                # Ground / Common corner, so its formerly renamed family 7 is
                # correctly pruned instead of surviving as a dead brush.
                if namespace == "bz7g" and int(family_id) == 7:
                    self.assertNotIn(7, families)
                    continue
                self.assertEqual(expected, families[int(family_id)])

        for namespace, aliases in decisions["familyMerges"].items():
            ids = {int(row["id"]) for row in presets[namespace]["families"]}
            for source, target in aliases.items():
                self.assertNotIn(int(source), ids)
                self.assertIn(int(target), ids)

        corner_indices = {"NW": 0, "NE": 1, "SE": 2, "SW": 3}
        for namespace, slots in decisions["cornerCorrections"].items():
            preset = presets[namespace]
            names = {int(row["id"]): row["name"]
                     for row in preset["families"]}
            payload = base64.b64decode(preset["signaturesBase64"])
            for slot, corners in slots.items():
                for corner, expected in corners.items():
                    self.assertEqual(
                        expected,
                        names[payload[int(slot) * 4 + corner_indices[corner]]])

        self.assertEqual(42, sum(len(value) for value in
                                decisions["falseUniformSlots"].values()))
        self.assertEqual(34, sum(len(value) for value in
                                decisions["reviewFamilies"].values()))

    def test_completed_questionnaire_excludes_only_follow_up_corners(self) -> None:
        root = Path(__file__).resolve().parent.parent
        decisions = json.loads((
            root / "scripts" / "semantic_atlas_unresolved_review_decisions.json"
        ).read_text(encoding="utf-8"))
        self.assertEqual(101, len(decisions["cornerDecisions"]))
        self.assertEqual(101, len({
            (entry["slot"], entry["corner"])
            for entry in decisions["cornerDecisions"]
        }))
        self.assertEqual(0, sum(
            entry.get("status") == "needs-follow-up"
            for entry in decisions["cornerDecisions"]))
        stone_gray = decisions["familyDecisions"][1]
        self.assertEqual("remove-unused-family", stone_gray["decision"])
        self.assertEqual(
            {"slot": 106, "corner": "NW", "family": "Stone / Lime"},
            stone_gray["resolvedCorner"])

        family_names = [
            "Rock / Dark", "Ground / Mountainous", "Ground / Common",
            "Ground / Gravel", "Grass / Sparse light",
            "Grass / Sparse dark", "Stone / Grayish", "Stone / Gray",
            "Stone / Lime", "Ground / Gray", "Paving / Radial",
        ]
        recipe = {
            "namespace": "bz3g",
            "families": [
                {"id": index + 1, "suggestedName": name}
                for index, name in enumerate(family_names)
            ],
        }
        corrections, aliases, renames = (
            reviewed_unresolved_questionnaire_corrections(recipe))
        self.assertEqual(3, len(
            decisions["verifiedSupplementalCorrections"]))
        self.assertEqual(104, len(corrections))
        self.assertEqual(3, corrections[(191, 0)])
        self.assertEqual(4, corrections[(341, 3)])
        self.assertEqual(3, corrections[(454, 2)])
        self.assertEqual(4, corrections[(454, 3)])
        self.assertEqual({2: 1}, aliases)
        self.assertEqual({1: "Ground / Dark"}, renames)

        merged_recipe = {
            "namespace": "bz3g",
            "families": [
                {"id": 1, "suggestedName": "Ground / Dark"},
                *[
                    {"id": index + 3, "suggestedName": name}
                    for index, name in enumerate(family_names[2:])
                ],
            ],
        }
        _, repeated_aliases, repeated_renames = (
            reviewed_unresolved_questionnaire_corrections(merged_recipe))
        self.assertEqual({7: 1}, repeated_aliases)
        self.assertEqual({}, repeated_renames)


class CrossAtlasUniformAnchorTests(unittest.TestCase):
    @staticmethod
    def record(namespace: str, family: str, *, layer: str = "land",
               role: str = "surface", pixel_hash: str = "a") -> dict:
        return {
            "pixelSha256": pixel_hash,
            "namespace": namespace,
            "familyId": 1,
            "family": family,
            "allowedLayers": [layer],
            "underwaterRole": role,
            "slot": 0,
            "corner": "NW",
        }

    def test_matching_cross_namespace_identity_is_corroborated(self) -> None:
        corroborated, review = cross_atlas_uniform_anchor_groups([
            self.record("Zone1", "Grass / Common"),
            self.record("Zone2", "Grass / Common"),
        ])
        self.assertEqual(1, len(corroborated))
        self.assertEqual([], review)

    def test_different_name_requires_review(self) -> None:
        corroborated, review = cross_atlas_uniform_anchor_groups([
            self.record("Zone1", "Grass / Common"),
            self.record("Zone2", "Grass / Fresh"),
        ])
        self.assertEqual([], corroborated)
        self.assertEqual(1, len(review))

    def test_different_underwater_usage_is_not_identity_conflict(self) -> None:
        corroborated, review = cross_atlas_uniform_anchor_groups([
            self.record("Zone1", "Grass / Common", role="surface"),
            self.record("Zone2", "Grass / Common", role="underwater"),
        ])
        self.assertEqual(1, len(corroborated))
        self.assertEqual([], review)
        self.assertEqual(
            ["surface", "underwater"],
            corroborated[0]["classifications"][0]["underwaterRoles"])

    def test_different_layer_usage_is_not_cross_namespace_name_conflict(self) -> None:
        corroborated, review = cross_atlas_uniform_anchor_groups([
            self.record("Zone1", "Water / Still", layer="land"),
            self.record("Zone2", "Water / Still", layer="overlay"),
        ])
        self.assertEqual(1, len(corroborated))
        self.assertEqual([], review)
        self.assertEqual(
            [["land"], ["overlay"]],
            corroborated[0]["classifications"][0]["allowedLayerSets"])

    def test_same_namespace_is_not_independent_evidence(self) -> None:
        corroborated, review = cross_atlas_uniform_anchor_groups([
            self.record("Zone1", "Grass / Common"),
            self.record("Zone1", "Grass / Fresh"),
        ])
        self.assertEqual([], corroborated)
        self.assertEqual([], review)

    def test_repeated_hashes_share_one_semantic_review_component(self) -> None:
        records = [
            self.record("Zone1", "Grass / Common", pixel_hash="a"),
            self.record("Zone2", "Grass / Dark", pixel_hash="a"),
            self.record("Zone1", "Grass / Common", pixel_hash="b"),
            self.record("Zone2", "Grass / Dark", pixel_hash="b"),
        ]
        _, review = cross_atlas_uniform_anchor_groups(records)
        components = cross_atlas_review_components(review)
        self.assertEqual(2, len(review))
        self.assertEqual(1, len(components))
        self.assertEqual(2, components[0]["pixelHashCount"])
        self.assertEqual(["name"], components[0]["reviewReasons"])


class UniformExactOwnerReviewTests(unittest.TestCase):
    def test_uniform_signature_surfaces_alternating_exact_corner_owners(self) -> None:
        tile = np.full((32, 32, 4), (20, 60, 20, 255), dtype=np.uint8)
        tile[:8, :8, :3] = (120, 180, 80)
        tile[24:, 24:, :3] = (120, 180, 80)
        common_hash = hashlib.sha256(
            tile[:8, :8, :3].tobytes(order="C")).hexdigest()
        dark_hash = hashlib.sha256(
            tile[:8, 24:, :3].tobytes(order="C")).hexdigest()
        records = [
            {"pixelSha256": common_hash, "namespace": "Other",
             "familyId": 2, "family": "Grass / Common",
             "slot": 1, "corner": "NW"},
            {"pixelSha256": dark_hash, "namespace": "Other",
             "familyId": 1, "family": "Grass / Dark",
             "slot": 2, "corner": "NW"},
            {"pixelSha256": common_hash, "namespace": "Target",
             "familyId": 1, "family": "Grass / Dark",
             "slot": 0, "corner": "NW"},
        ]
        contexts = {
            "Target": {
                "tiles": [tile],
                "table": [(1, 1, 1, 1)],
                "familyById": {1: {"name": "Grass / Dark"}},
            },
        }

        review = uniform_exact_owner_review(contexts, records)

        self.assertEqual(1, len(review))
        self.assertEqual(["NW", "SE"], [
            value["corner"] for value in review[0]["conflictingCorners"]])
        self.assertTrue(all(
            value["candidateFamily"] == "Grass / Common"
            for value in review[0]["conflictingCorners"]))


class ArchiveCoverageTests(unittest.TestCase):
    def test_archive_presence_requires_every_atlas_page(self) -> None:
        preset = {"namespace": "ZoneX", "slotCount": 192}
        complete = {
            "zonex000.mmp": {}, "zonex001.mmp": {}, "zonex002.mmp": {}}
        missing_middle = {"zonex000.mmp": {}, "zonex002.mmp": {}}

        self.assertTrue(archive_contains_preset(complete, preset))
        self.assertFalse(archive_contains_preset(missing_middle, preset))


class R105AuthoredBlendDecisionTests(unittest.TestCase):
    def test_r105_applies_only_reviewed_authored_blend_corners(self) -> None:
        """R105 replaces all six marked signatures in engine corner order."""
        root = Path(__file__).resolve().parent.parent
        catalog = json.loads((
            root / "assets" / "resources" / "terrain_atlas_presets.json"
        ).read_text(encoding="utf-8"))
        presets = {entry["namespace"].casefold(): entry
                   for entry in catalog["presets"]}

        expected = {
            ("bz3g", 204): ["Ground / Common", "Grass / Sparse light",
                             "Stone / Lime", "Stone / Lime"],
            ("bz3g", 218): ["Ground / Common", "Grass / Sparse light",
                             "Grass / Sparse light", "Stone / Lime"],
            ("bz3g", 281): ["Grass / Dark", "Stone / Grayish",
                             "Grass / Sparse light", "Stone / Grayish"],
            ("bz3g", 470): ["Grass / Sparse light", "Grass / Sparse light",
                             "Grass / Sparse dark", "Stone / Lime"],
            ("zone6_2", 390): ["Stone / Lime", "Grass / Common",
                                "Ground / Dead", "Ground / Dead"],
            ("zone7", 249): ["Ground / Brown", "Grass / Fresh",
                              "Grass / Fresh", "Grass / Fresh"],
        }
        for (namespace, slot), names_expected in expected.items():
            preset = presets[namespace]
            names = {int(row["id"]): row["name"]
                     for row in preset["families"]}
            payload = base64.b64decode(preset["signaturesBase64"])
            actual = [names[value]
                      for value in payload[slot * 4:slot * 4 + 4]]
            with self.subTest(namespace=namespace, slot=slot):
                self.assertEqual(names_expected, actual)

    def test_r105_does_not_propagate_across_nonidentical_bindings(self) -> None:
        """Similar sibling maps are evidence leads, not exact propagation peers."""
        root = Path(__file__).resolve().parent.parent
        catalog = json.loads((
            root / "assets" / "resources" / "terrain_atlas_presets.json"
        ).read_text(encoding="utf-8"))
        presets = {entry["namespace"].casefold(): entry
                   for entry in catalog["presets"]}

        def binding(namespace: str, variant: str) -> str:
            return next(row["pixelSha256"] for row in presets[namespace]["bindings"]
                        if row["variant"] == variant)

        for left, right in (("bz3g", "zone9"),
                            ("zone6_2", "zone6")):
            for variant in ("hd", "original"):
                with self.subTest(left=left, right=right, variant=variant):
                    self.assertNotEqual(binding(left, variant),
                                        binding(right, variant))


class R106ConsolidatedDecisionTests(unittest.TestCase):
    def test_every_catalog_family_is_referenced_and_brush_expressible(self) -> None:
        """Dead R106 records cannot survive as empty or misleading brushes."""
        root = Path(__file__).resolve().parent.parent
        catalog = json.loads((
            root / "assets" / "resources" / "terrain_atlas_presets.json"
        ).read_text(encoding="utf-8"))

        for preset in catalog["presets"]:
            payload = base64.b64decode(
                preset["signaturesBase64"], validate=True)
            signatures = [tuple(payload[offset:offset + 4])
                          for offset in range(0, len(payload), 4)]
            referenced = set(payload)
            for family in preset["families"]:
                family_id = int(family["id"])
                with self.subTest(namespace=preset["namespace"],
                                  family=family["name"]):
                    self.assertIn(family_id, referenced)
                    expressible = any(
                        corners.count(family_id) == 4
                        or (corners.count(family_id) == 1
                            and len(set(corners) - {family_id}) == 1)
                        for corners in signatures)
                    self.assertEqual(bool(family["paintSelectable"]),
                                     expressible)

    def test_byte_identical_bindings_share_semantic_identity(self) -> None:
        """One physical atlas cannot compile to two corner interpretations."""
        root = Path(__file__).resolve().parent.parent
        catalog = json.loads((
            root / "assets" / "resources" / "terrain_atlas_presets.json"
        ).read_text(encoding="utf-8"))
        by_binding = {}
        for preset in catalog["presets"]:
            identity = (
                preset["signaturesBase64"],
                tuple((int(family["id"]), family["name"],
                       int(family["thumbnailSlot"]),
                       bool(family["paintSelectable"]))
                      for family in preset["families"]),
            )
            for binding in preset["bindings"]:
                key = (binding["variant"], binding["pixelSha256"],
                       int(preset["slotCount"]))
                previous = by_binding.setdefault(key, identity)
                with self.subTest(namespace=preset["namespace"], binding=key):
                    self.assertEqual(previous, identity)

    def test_r106_catalog_matches_every_explicit_and_safe_duplicate_decision(self) -> None:
        """R106 replays changed corners plus only conflict-free exact duplicates."""
        root = Path(__file__).resolve().parent.parent
        catalog = json.loads((
            root / "assets" / "resources" / "terrain_atlas_presets.json"
        ).read_text(encoding="utf-8"))
        decisions = json.loads((
            root / "scripts" / "semantic_atlas_r106_consolidated_decisions.json"
        ).read_text(encoding="utf-8"))
        later_reviews = [json.loads((
            root / "scripts" / name
        ).read_text(encoding="utf-8")) for name in (
            "semantic_atlas_r110_consolidated_decisions.json",
            "semantic_atlas_r111_consolidated_decisions.json",
            "semantic_atlas_r112_consolidated_decisions.json",
        )]
        later_corrections = [{
            key.casefold(): value
            for key, value in review.get("cornerCorrections", {}).items()
        } for review in later_reviews]
        presets = {entry["namespace"].casefold(): entry
                   for entry in catalog["presets"]}

        self.assertEqual(249, decisions["counts"]["submittedRows"])
        self.assertEqual(482, decisions["counts"][
            "explicitAndPropagatedCorners"])
        self.assertEqual(5, decisions["counts"][
            "exactArtworkGroupsPropagated"])
        self.assertEqual(12, decisions["counts"][
            "exactBindingCornersPropagated"])
        self.assertEqual(2, decisions["counts"][
            "exactArtworkConflictGroupsQueued"])

        for namespace, slots in decisions["cornerCorrections"].items():
            preset = presets[namespace.casefold()]
            names = {int(row["id"]): row["name"]
                     for row in preset["families"]}
            payload = base64.b64decode(preset["signaturesBase64"])
            for slot_text, corners in slots.items():
                slot = int(slot_text)
                for corner_name, expected in corners.items():
                    for corrections in later_corrections:
                        expected = corrections.get(
                            namespace.casefold(), {}).get(
                                slot_text, {}).get(corner_name, expected)
                    corner = {"NW": 0, "NE": 1, "SE": 2, "SW": 3}[
                        corner_name]
                    actual = names[payload[slot * 4 + corner]]
                    with self.subTest(namespace=namespace, slot=slot,
                                      corner=corner_name):
                        self.assertEqual(expected, actual)

        zone7 = presets["zone7"]
        dark_brown = [row for row in zone7["families"]
                      if row["name"] == "Ground / Dark brown"]
        self.assertEqual(1, len(dark_brown))
        self.assertEqual(133, dark_brown[0]["thumbnailSlot"])
        self.assertTrue(dark_brown[0]["paintSelectable"])

    def test_r106_exact_artwork_mapping_conflicts_remain_unpropagated(self) -> None:
        """Identical pixels do not erase a known semantic mapping conflict."""
        root = Path(__file__).resolve().parent.parent
        decisions = json.loads((
            root / "scripts" / "semantic_atlas_r106_consolidated_decisions.json"
        ).read_text(encoding="utf-8"))

        self.assertEqual({
            (("bz11k", "Zone14"), (122, 125)),
            (("bz5g", "bz6g", "Zone8"), (172, 173)),
        }, {
            (tuple(row["owners"]), tuple(row["slots"]))
            for row in decisions["exactArtworkMappingConflicts"]
        })

        # The submitted source rows may incidentally resolve the old conflict,
        # but the unreviewed duplicate slots were never mutated by propagation.
        self.assertNotIn("125", decisions["cornerCorrections"]["bz11k"])
        self.assertNotIn("172", decisions["cornerCorrections"]["bz5g"])

    def test_r107_applies_only_explicit_correction_and_preserves_confirmations(self) -> None:
        """R107 changes bz4g/Zone4 slot 280 NE and records confirmations."""
        root = Path(__file__).resolve().parent.parent
        catalog = json.loads((
            root / "assets" / "resources" / "terrain_atlas_presets.json"
        ).read_text(encoding="utf-8"))
        decisions = json.loads((
            root / "scripts" / "semantic_atlas_r107_consolidated_decisions.json"
        ).read_text(encoding="utf-8"))
        presets = {entry["namespace"].casefold(): entry
                   for entry in catalog["presets"]}

        expected = {
            "bz16h:166": ["Ground / Red", "Ground / Plant soil",
                           "Ground / Plant soil", "Ground / Plant soil"],
            "zone3obr:8": ["Ground / Dry"] * 4,
            "bz5g:173": ["Sand / Common", "Sand / Grainy",
                          "Sand / Grainy", "Sand / Grainy"],
            "zone7:133": ["Ground / Dark brown"] * 4,
        }
        for identity, names_expected in expected.items():
            namespace, slot_text = identity.split(":")
            preset = presets[namespace]
            names = {int(row["id"]): row["name"]
                     for row in preset["families"]}
            payload = base64.b64decode(preset["signaturesBase64"])
            slot = int(slot_text)
            self.assertEqual(names_expected, [
                names[value] for value in payload[slot * 4:slot * 4 + 4]
            ])

        for namespace in ("bz4g", "zone4"):
            preset = presets[namespace]
            names = {int(row["id"]): row["name"]
                     for row in preset["families"]}
            payload = base64.b64decode(preset["signaturesBase64"])
            self.assertEqual("Ground / Common", names[payload[280 * 4 + 1]])
        self.assertEqual({"bz4g", "zone4"}, {
            namespace.casefold()
            for namespace in decisions["cornerCorrections"]
        })

    def test_r108_exhausts_dry_and_trampled_without_empty_brushes(self) -> None:
        """R108 reassigns the last reviewed corners and prunes dead families."""
        root = Path(__file__).resolve().parent.parent
        catalog = json.loads((
            root / "assets" / "resources" / "terrain_atlas_presets.json"
        ).read_text(encoding="utf-8"))
        presets = {entry["namespace"].casefold(): entry
                   for entry in catalog["presets"]}
        for namespace, slot in (("bz4g", 405), ("zone4", 405),
                                ("zone6", 389), ("zone6_2", 389)):
            preset = presets[namespace]
            names = {int(row["id"]): row["name"]
                     for row in preset["families"]}
            payload = base64.b64decode(preset["signaturesBase64"])
            signature = [names[value]
                         for value in payload[slot * 4:slot * 4 + 4]]
            if namespace in ("bz4g", "zone4"):
                self.assertEqual([
                    "Sand / Dark pink", "Ground / Common",
                    "Grass / Irregular", "Grass / Irregular",
                ], signature)
                self.assertNotIn(
                    "Ground / Dry",
                    {row["name"] for row in preset["families"]})
            else:
                self.assertEqual([
                    "Grass / Sparse dark", "Ground / Common",
                    "Stone / Lime", "Stone / Lime",
                ], signature)
                self.assertNotIn(
                    "Ground / Trampled",
                    {row["name"] for row in preset["families"]})

    def test_r112_applies_every_explicit_and_exact_alias_mapping(self) -> None:
        """R112 is a complete replay of the 35 reviewed source groups."""
        root = Path(__file__).resolve().parent.parent
        catalog = json.loads((
            root / "assets" / "resources" / "terrain_atlas_presets.json"
        ).read_text(encoding="utf-8"))
        decisions = json.loads((
            root / "scripts" / "semantic_atlas_r112_consolidated_decisions.json"
        ).read_text(encoding="utf-8"))
        later_records = [json.loads((root / "scripts" / filename).read_text())
                         for filename in (
            "semantic_atlas_r117_consolidated_decisions.json",
            "semantic_atlas_r119_recovered_decisions.json",
            "semantic_atlas_r119_consolidated_decisions.json",
            "semantic_atlas_r120_consolidated_decisions.json",
        )]
        presets = {entry["namespace"].casefold(): entry
                   for entry in catalog["presets"]}

        self.assertEqual(35, decisions["counts"]["submittedCards"])
        self.assertEqual(63, sum(len(slots) for slots in
                                 decisions["cornerCorrections"].values()))
        self.assertEqual(252, sum(
            len(corners)
            for slots in decisions["cornerCorrections"].values()
            for corners in slots.values()))
        for namespace, slots in decisions["cornerCorrections"].items():
            preset = presets[namespace.casefold()]
            names = {int(row["id"]): row["name"]
                     for row in preset["families"]}
            payload = base64.b64decode(preset["signaturesBase64"])
            for slot_text, expected in slots.items():
                offset = int(slot_text) * 4
                actual = [names[value]
                          for value in payload[offset:offset + 4]]
                expected = dict(expected)
                for record in later_records:
                    expected.update(record["cornerCorrections"].get(
                        namespace, {}).get(slot_text, {}))
                self.assertEqual(
                    [expected[name] for name in ("NW", "NE", "SE", "SW")],
                    actual, f"{namespace} slot {slot_text}")

        zone10 = presets["zone10"]
        irregular = next(row for row in zone10["families"]
                         if row["name"] == "Slabs / Faces irregular")
        self.assertEqual(8, irregular["id"])
        self.assertEqual(396, irregular["thumbnailSlot"])
        self.assertTrue(irregular["paintSelectable"])

    def test_r113_confirms_slot237_uniform_stone_lime_for_exact_owners(self) -> None:
        """R113 applies the user's uniform decision to bz2g and Zone2 only."""
        root = Path(__file__).resolve().parent.parent
        catalog = json.loads((
            root / "assets" / "resources" / "terrain_atlas_presets.json"
        ).read_text(encoding="utf-8"))
        decisions = json.loads((
            root / "scripts" / "semantic_atlas_r113_consolidated_decisions.json"
        ).read_text(encoding="utf-8"))
        presets = {entry["namespace"].casefold(): entry
                   for entry in catalog["presets"]}

        self.assertEqual(1, decisions["counts"]["submittedCards"])
        self.assertEqual(2, decisions["counts"]["exactArtworkOwners"])
        self.assertEqual(8, decisions["counts"]["explicitCorners"])
        for namespace in ("bz2g", "Zone2"):
            preset = presets[namespace.casefold()]
            names = {int(row["id"]): row["name"]
                     for row in preset["families"]}
            payload = base64.b64decode(preset["signaturesBase64"])
            actual = [names[value]
                      for value in payload[237 * 4:237 * 4 + 4]]
            self.assertEqual(["Stone / Lime"] * 4, actual, namespace)

    def test_r114_applies_all_explicit_audit_reviews_to_exact_owners(self) -> None:
        """R114 applies 50 changed corners plus one explicit comment safely."""
        root = Path(__file__).resolve().parent.parent
        catalog = json.loads((
            root / "assets" / "resources" / "terrain_atlas_presets.json"
        ).read_text(encoding="utf-8"))
        decisions = json.loads((
            root / "scripts" / "semantic_atlas_r114_consolidated_decisions.json"
        ).read_text(encoding="utf-8"))
        presets = {entry["namespace"].casefold(): entry
                   for entry in catalog["presets"]}

        self.assertEqual(48, decisions["counts"]["submittedCards"])
        self.assertEqual(50, decisions["counts"]["questionnaireChangedCorners"])
        self.assertEqual(1, decisions["counts"]["commentExplicitCorners"])
        self.assertEqual(66, decisions["counts"]["expandedOwnerCornerRows"])
        self.assertNotIn("zone9", {
            namespace.casefold()
            for namespace in decisions["cornerCorrections"]})

        applied = 0
        for namespace, slots in decisions["cornerCorrections"].items():
            preset = presets[namespace.casefold()]
            names = {int(row["id"]): row["name"]
                     for row in preset["families"]}
            payload = base64.b64decode(preset["signaturesBase64"])
            for slot_text, corners in slots.items():
                slot = int(slot_text)
                for corner, expected in corners.items():
                    index = {"NW": 0, "NE": 1, "SE": 2, "SW": 3}[corner]
                    self.assertEqual(expected, names[payload[slot * 4 + index]],
                                     f"{namespace} slot {slot} {corner}")
                    applied += 1
        self.assertEqual(66, applied)

        for left, right in (("bz2g", "Zone2"), ("bz4g", "Zone4")):
            left_bindings = {
                (row["variant"], row["pixelSha256"])
                for row in presets[left.casefold()]["bindings"]}
            right_bindings = {
                (row["variant"], row["pixelSha256"])
                for row in presets[right.casefold()]["bindings"]}
            self.assertEqual(left_bindings, right_bindings)

    def test_r115_applies_supported_changes_and_holds_audit_conflicts(self) -> None:
        """R115 applies 133 logical changes and preserves five conflicts."""
        root = Path(__file__).resolve().parent.parent
        catalog = json.loads((
            root / "assets" / "resources" / "terrain_atlas_presets.json"
        ).read_text(encoding="utf-8"))
        decisions = json.loads((
            root / "scripts" / "semantic_atlas_r115_consolidated_decisions.json"
        ).read_text(encoding="utf-8"))
        presets = {entry["namespace"].casefold(): entry
                   for entry in catalog["presets"]}

        self.assertEqual(138, decisions["logicalChangedCorners"])
        self.assertEqual(133, decisions["appliedLogicalChangedCorners"])
        self.assertEqual(158, decisions["propagatedCornerCorrections"])
        self.assertEqual(5, len(decisions["heldConflicts"]))

        applied = 0
        for namespace, slots in decisions["cornerCorrections"].items():
            preset = presets[namespace.casefold()]
            names = {int(row["id"]): row["name"]
                     for row in preset["families"]}
            payload = base64.b64decode(preset["signaturesBase64"])
            for slot_text, corners in slots.items():
                slot = int(slot_text)
                for corner, expected in corners.items():
                    index = {"NW": 0, "NE": 1, "SE": 2, "SW": 3}[corner]
                    self.assertEqual(expected, names[payload[slot * 4 + index]],
                                     f"{namespace} slot {slot} {corner}")
                    applied += 1
        self.assertEqual(158, applied)

        # The five held rows remain recorded as R115 provenance even after
        # their explicit R116 follow-up supersedes the catalog assignments.
        self.assertEqual(
            {
                ("r115-bz2g-slot448", "NE"),
                ("r115-bz2g-slot448", "SW"),
                ("r115-bz4g-slot351", "NW"),
                ("r115-bz5g-slot465", "SW"),
                ("r115-zone9-slot388", "NW"),
            },
            {(row["key"], row["corner"])
             for row in decisions["heldConflicts"]},
        )

        # R117 supplies later uniform evidence for Ground / Dirt, making the
        # exact bz2g/Zone2 brush paint-selectable.
        for namespace in ("bz2g", "Zone2"):
            family = next(row for row in presets[namespace.casefold()]["families"]
                          if row["name"] == "Ground / Dirt")
            self.assertTrue(family["paintSelectable"])
        pink = next(row for row in presets["zone7"]["families"]
                    if row["name"] == "Sand / Dark pink")
        self.assertFalse(pink["paintSelectable"])

    def test_r116_resolves_only_the_five_held_conflicts(self) -> None:
        """R116 applies five explicit decisions to eleven exact owners."""
        root = Path(__file__).resolve().parent.parent
        catalog = json.loads((
            root / "assets" / "resources" / "terrain_atlas_presets.json"
        ).read_text(encoding="utf-8"))
        decisions = json.loads((
            root / "scripts" / "semantic_atlas_r116_consolidated_decisions.json"
        ).read_text(encoding="utf-8"))
        presets = {entry["namespace"].casefold(): entry
                   for entry in catalog["presets"]}

        self.assertEqual(5, decisions["logicalChangedCorners"])
        expected_rows = sum(
            len(corners)
            for slots in decisions["cornerCorrections"].values()
            for corners in slots.values()
        )
        self.assertEqual(11, expected_rows)
        self.assertEqual(11, decisions["propagatedCornerCorrections"])

        for namespace, slots in decisions["cornerCorrections"].items():
            preset = presets[namespace.casefold()]
            names = {int(row["id"]): row["name"]
                     for row in preset["families"]}
            payload = base64.b64decode(preset["signaturesBase64"])
            for slot_text, corners in slots.items():
                slot = int(slot_text)
                for corner, expected in corners.items():
                    index = {"NW": 0, "NE": 1, "SE": 2, "SW": 3}[corner]
                    self.assertEqual(expected, names[payload[slot * 4 + index]],
                                     f"{namespace} slot {slot} {corner}")

    def test_r120_checkpoint_replays_all_released_semantic_authority(self) -> None:
        """R117 through R120 replay every exact owner without inference."""
        root = Path(__file__).resolve().parent.parent
        catalog = json.loads((root / "assets" / "resources" /
                              "terrain_atlas_presets.json").read_text())
        presets = {row["namespace"].casefold(): row
                   for row in catalog["presets"]}
        records = [
            "semantic_atlas_r117_consolidated_decisions.json",
            "semantic_atlas_r119_recovered_decisions.json",
            "semantic_atlas_r119_consolidated_decisions.json",
            "semantic_atlas_r120_consolidated_decisions.json",
        ]

        applied_rows = []
        for filename in records:
            decisions = json.loads((root / "scripts" / filename).read_text())
            for namespace, slots in decisions["cornerCorrections"].items():
                preset = presets[namespace.casefold()]
                names = {int(row["id"]): row["name"]
                         for row in preset["families"]}
                payload = base64.b64decode(preset["signaturesBase64"])
                for slot_text, corners in slots.items():
                    slot = int(slot_text)
                    for corner, expected in corners.items():
                        index = ("NW", "NE", "SE", "SW").index(corner)
                        self.assertEqual(
                            expected, names[payload[slot * 4 + index]],
                            f"{filename}:{namespace}:{slot}:{corner}")
                        applied_rows.append((filename, namespace, slot, corner))

        self.assertEqual(750, len(applied_rows))
        r117 = json.loads((root / "scripts" /
            "semantic_atlas_r117_consolidated_decisions.json").read_text())
        self.assertEqual(228, r117["submittedCards"])
        self.assertEqual(439, r117["propagatedCornerCorrections"])
        r120 = json.loads((root / "scripts" /
            "semantic_atlas_r120_consolidated_decisions.json").read_text())
        self.assertEqual(19, r120["counts"]["submittedCards"])
        self.assertEqual(32, r120["counts"]["serializedOwnerCornerRows"])

        for namespace, family_name, paint_selectable in (
                ("bz2g", "Stone / Gray", True),
                ("Zone2", "Stone / Gray", True),
                ("Zone3Obr", "Stone / Gray", True),
                ("Zone3Obr", "Stone / Grayish", False)):
            family = next(row for row in presets[namespace.casefold()]["families"]
                          if row["name"] == family_name)
            self.assertEqual(paint_selectable, family["paintSelectable"])


if __name__ == "__main__":
    unittest.main()
