from __future__ import annotations

import base64
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DECISIONS = ROOT / "scripts/semantic_atlas_r148_batch1_decisions.json"
CATALOG = ROOT / "assets/resources/terrain_atlas_presets.json"
APPLY = ROOT / "scripts/apply_r148_batch1_decisions.py"
CORNER_INDEX = {"NW": 0, "NE": 1, "SE": 2, "SW": 3}


def mapping(catalog: dict, namespace: str, slot: int) -> dict[str, str]:
    preset = next(row for row in catalog["presets"]
                  if row["namespace"].casefold() == namespace.casefold())
    names = {int(row["id"]): row["name"] for row in preset["families"]}
    payload = base64.b64decode(preset["signaturesBase64"])
    return {corner: names[payload[slot * 4 + index]]
            for corner, index in CORNER_INDEX.items()}


def set_mapping(catalog: dict, namespace: str, slot: int,
                values: dict[str, str]) -> None:
    preset = next(row for row in catalog["presets"]
                  if row["namespace"].casefold() == namespace.casefold())
    ids = {row["name"]: int(row["id"]) for row in preset["families"]}
    payload = bytearray(base64.b64decode(preset["signaturesBase64"]))
    for corner, family in values.items():
        payload[slot * 4 + CORNER_INDEX[corner]] = ids[family]
    preset["signaturesBase64"] = base64.b64encode(payload).decode("ascii")


class R148Batch1DecisionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.decisions = json.loads(DECISIONS.read_text(encoding="utf-8"))

    def test_submission_boundary_and_pair_votes_are_explicit(self) -> None:
        counts = self.decisions["counts"]
        self.assertEqual((69, 68, 1), (
            counts["queueCards"], counts["submittedCards"],
            counts["untouchedCards"]))
        self.assertEqual(1, counts["partialCards"])
        self.assertEqual(
            ["semantic-atlas-pixels-c115e9deb4764a94"],
            self.decisions["untouchedQuestionKeys"])
        self.assertEqual(
            {"semantic-atlas-pixels-42a42cb742729134": ["NE", "SW"]},
            {row["key"]: row["unresolvedCorners"]
             for row in self.decisions["partialCards"]})
        self.assertEqual((28, 48, 2), (
            counts["approvedAuditRows"], counts["rejectedAuditRows"],
            counts["unresolvedAuditRows"]))
        self.assertTrue(all(
            row["disposition"] == ("promote" if row["approved"] else "demote")
            for row in self.decisions["directedMaterialPairDispositions"]))

    def test_concrete_edit_wins_over_stale_reject_comment(self) -> None:
        card = next(row for row in self.decisions["reviewedCards"]
                    if row["key"] == "semantic-atlas-pixels-da7f8811bb957afa")
        self.assertTrue(card["explicitAuditRejection"])
        self.assertEqual("Ground / Plant soil",
                         card["submittedMapping"]["SW"])
        for namespace in ("bz16h", "Zone18"):
            self.assertEqual(
                "Ground / Plant soil",
                self.decisions["cornerCorrections"][namespace]["344"]["SW"])

    def test_applicator_changes_exactly_the_reviewed_exact_occurrences(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            target = Path(directory) / "terrain_atlas_presets.json"
            catalog = json.loads(CATALOG.read_text(encoding="utf-8"))
            # Recreate the pinned questionnaire-time catalog state so this
            # remains a genuine apply regression after the repository catalog
            # itself has advanced to R148.
            for card in self.decisions["reviewedCards"]:
                for occurrence in card["exactOccurrenceMappings"]:
                    set_mapping(catalog, occurrence["namespace"],
                                int(occurrence["slot"]), occurrence["mapping"])
            target.write_bytes((
                json.dumps(catalog, ensure_ascii=False, separators=(",", ":"))
                + "\n").encode("utf-8"))
            completed = subprocess.run(
                [sys.executable, str(APPLY), str(target)],
                cwd=ROOT, check=True, capture_output=True, text=True)
            report = json.loads(completed.stdout)
            self.assertEqual(52, report["appliedCornerRows"])
            self.assertEqual(52, report["changedCorners"])
            catalog = json.loads(target.read_text(encoding="utf-8"))
            for namespace, slots in self.decisions["cornerCorrections"].items():
                for slot_text, corrections in slots.items():
                    actual = mapping(catalog, namespace, int(slot_text))
                    for corner, family in corrections.items():
                        self.assertEqual(family, actual[corner])
            # The two unresolved Zone7 slot 423 corners remain unchanged.
            self.assertEqual("Sand / Dark", mapping(catalog, "Zone7", 423)["NE"])
            self.assertEqual("Sand / Dark", mapping(catalog, "Zone7", 423)["SW"])

            repeated = subprocess.run(
                [sys.executable, str(APPLY), str(target)],
                cwd=ROOT, check=True, capture_output=True, text=True)
            repeated_report = json.loads(repeated.stdout)
            self.assertEqual(52, repeated_report["appliedCornerRows"])
            self.assertEqual(0, repeated_report["changedCorners"])
            self.assertEqual(report["sha256"], repeated_report["sha256"])


if __name__ == "__main__":
    unittest.main()
