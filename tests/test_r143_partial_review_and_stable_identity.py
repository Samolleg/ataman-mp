from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path


class R143StableIdentityRegressionTests(unittest.TestCase):
    def test_exact_hd_artwork_identity_does_not_depend_on_alias_membership(self) -> None:
        from scripts.semantic_atlas_question_identity import stable_hd_artwork_key

        pixel_sha = "A" * 64
        self.assertEqual(
            stable_hd_artwork_key(pixel_sha),
            stable_hd_artwork_key(pixel_sha.lower()),
        )
        self.assertEqual("semantic-atlas-hd-aaaaaaaaaaaaaaaa",
                         stable_hd_artwork_key(pixel_sha))

    def test_partial_answers_resolve_named_escapes_and_do_not_require_all_cards(self) -> None:
        from scripts.build_r143_partial_decisions import parse_answers

        text = """Semantic-atlas R143 combined outstanding review decisions:
r143-bz11k-slot55-038bc1bbd4: NW=Ice / Common, NE=Ice / Common, SE=Ice / Common, SW=Different existing family (describe in comment) - stone dark
r143-bz8k-slot122-981739d737: NW=Snow / Common, NE=Snow / Common, SE=Snow / Common, SW=Stone / Gray - Audit recommendation rejected; keep current assignments.
Apply only explicit changed decisions; untouched current assignments remain context, not confirmations.
"""
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "answers.txt"
            path.write_text(text, encoding="utf-8")
            mappings, notes = parse_answers(path)
        self.assertEqual(2, len(mappings))
        self.assertEqual("Stone / Dark",
                         mappings["r143-bz11k-slot55-038bc1bbd4"]["SW"])
        self.assertEqual("Stone / Gray",
                         mappings["r143-bz8k-slot122-981739d737"]["SW"])
        self.assertIn("Audit recommendation rejected", notes[
            "r143-bz8k-slot122-981739d737"])

    def test_zone19_exact_hd_duplicate_is_resolved_by_one_review(self) -> None:
        root = Path(__file__).resolve().parents[1]
        decisions_path = root / "scripts" / "semantic_atlas_r143_partial_decisions.json"
        if not decisions_path.exists():
            self.fail("R143 partial decision artifact is missing")
        decisions = json.loads(decisions_path.read_text(encoding="utf-8"))
        closure = decisions["exactHdArtworkClosures"]
        pair = next(row for row in closure
                    if row["sourceQuestionKey"].startswith(
                        "r143-zone19-slot309-"))
        self.assertIn("r143-zone19-slot310-4fb1472636",
                      pair["resolvedQuestionKeys"])
        # Slot 310 already has the reviewed exact-artwork mapping.  It is
        # resolved without manufacturing a redundant catalog write.
        self.assertNotIn("310", decisions["cornerCorrections"].get(
            "Zone19", {}))


if __name__ == "__main__":
    unittest.main()
