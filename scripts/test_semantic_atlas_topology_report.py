#!/usr/bin/env python3

from __future__ import annotations

import base64
import json
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from report_semantic_atlas_topology import (
    TOPOLOGIES,
    build_report,
    decode_signatures,
    pair_topologies,
    pair_topology_slots,
    strict_relabel_candidates,
)


class SemanticAtlasTopologyReportTests(unittest.TestCase):
    def test_classifies_all_four_rotational_topologies(self) -> None:
        signatures = [
            (1, 2, 2, 2),
            (1, 1, 1, 2),
            (1, 1, 2, 2),
            (1, 2, 1, 2),
        ]
        self.assertEqual(
            set(TOPOLOGIES), pair_topologies(signatures, 1, 2)
        )
        self.assertEqual(
            {
                "one-corner": [0],
                "three-corner": [1],
                "adjacent-two": [2],
                "diagonal-two": [3],
            },
            pair_topology_slots(signatures, 1, 2),
        )

    def test_rejects_truncated_signature_payload(self) -> None:
        encoded = base64.b64encode(bytes((1, 2, 3))).decode("ascii")
        with self.assertRaisesRegex(ValueError, "expected 4"):
            decode_signatures(encoded, 1)

    def test_strict_relabel_requires_every_changed_and_retained_corner(self) -> None:
        report = {
            "profiles": [{
                "namespace": "Synthetic",
                "aliases": ["Synthetic"],
                "families": [
                    {"id": 1, "name": "A"},
                    {"id": 2, "name": "B"},
                    {"id": 3, "name": "C"},
                ],
                "pairs": [{
                    "familyAId": 1,
                    "familyA": "A",
                    "familyBId": 2,
                    "familyB": "B",
                    "coverageClassCount": 3,
                    "missingTopologies": ["diagonal-two"],
                }],
            }]
        }
        rows = []
        for corner, family, nearest in (
            ("NW", 1, 1), ("NE", 3, 2),
            ("SE", 1, 1), ("SW", 3, 2),
        ):
            rows.append({
                "namespace": "Synthetic", "slot": "7",
                "signature": "1/3/1/3", "corner": corner,
                "nearestAnchorId": str(nearest), "nearestScore": "0.01",
            })
        candidates = strict_relabel_candidates(report, rows)
        self.assertEqual(1, len(candidates))
        self.assertEqual([1, 2, 1, 2], candidates[0]["proposedSignature"])
        rows[1]["nearestAnchorId"] = "3"
        self.assertEqual([], strict_relabel_candidates(report, rows))

    def test_current_catalog_coverage_is_explicit(self) -> None:
        root = Path(__file__).resolve().parents[1]
        catalog = json.loads(
            (root / "assets/resources/terrain_atlas_presets.json")
            .read_text(encoding="utf-8")
        )
        report = build_report(catalog)
        self.assertEqual(38, report["namespaceCount"])
        self.assertEqual(27, report["runtimeProfileCount"])
        # R97/R98 resolve the reviewed Sand / Irregular transition corners.
        # R105 corrects six provenance-qualified authored blend signatures;
        # R106 then applies the completed 249-card review and one explicit new
        # Zone7 material identity. Their real pair rows are reported honestly,
        # including zero-coverage topology, rather than hidden to preserve an
        # earlier aggregate count.
        # R106's 30 dead family rows previously fabricated 269 zero-coverage
        # pairs. Pruning them changes only the honest denominator.
        # R108 removes the three exhausted transition-only family records;
        # their 25 zero-coverage pair rows no longer inflate the denominator.
        # R109 removes the final Bz7g Ground / Common corner and prunes that
        # now-unreferenced brush, removing five topology pairs.
        # R110 exhausts six further family rows across exact atlas bindings.
        # Their removal eliminates 42 now-impossible pair rows: 37 with zero
        # support and five with one supported topology.
        # R111 restores the reviewed transition-only Ground / Tan identity in
        # bz15h and Zone17. Six pair rows return; one prior single-topology row
        # becomes a zero-coverage row after the slot-248 corner split.
        # R112 separates Zone10 slot 396 into one genuine uniform
        # Slabs / Faces irregular family. Its eight relationships with the
        # existing Zone10 families are now represented explicitly; reviewed
        # corner corrections also move three existing pair rows between
        # topology-coverage buckets.
        # R114 changes only reviewed corner ownership. It retains the same
        # family/pair denominator while moving seven pair rows among topology
        # coverage buckets.
        # R115 adds two honest transition-only identities where the reviewed
        # cross-atlas family was absent locally (bz2g/Zone2 Ground / Dirt and
        # Zone7 Sand / Dark pink), then applies 158 exact-owner corrections.
        # The two exact bz2g/Zone2 bindings each add 12 relationships and the
        # Zone7 family adds four, for 28 additional pair rows.
        # R116 resolves the five held R115 decisions across eleven exact owner
        # corners. The denominator is unchanged; two previously unsupported
        # relationships gain one authored topology.
        # R117 adds the reviewed transition-only Zone3Obr Stone / Grayish
        # identity and applies 439 exact-owner corner corrections. The new
        # identity adds eight honest pair rows; changed ownership also restores
        # twenty previously pruned relationships across the reviewed atlases.
        # R119 changes reviewed corner ownership only. The family/pair
        # denominator remains stable while eight relationships move between
        # authored-topology coverage buckets.
        # R122 changes reviewed corner ownership and renames one cross-atlas
        # water identity without changing the pair denominator. Two pair rows
        # move from one authored topology to zero.
        # R125 changes reviewed corner ownership only. The pair denominator is
        # unchanged; seven relationship rows move among authored-topology
        # coverage buckets.
        # R126 completes the exact bz15h/Zone17 signature alignment. Those two
        # namespaces now share one byte-identical runtime profile, so its 21
        # family-pair rows are counted once rather than twice. R126 corner
        # corrections also redistribute coverage inside the retained rows.
        # R127 changes two reviewed corners only. The pair denominator remains
        # stable; bz3g slot 272 reduces one relationship from two authored
        # topology classes to one.
        # R128 applies 15 ownership-corroborated decisions across their exact
        # artwork owners. One retained family pair loses its last two authored
        # topology classes; the denominator remains stable.
        # R129 adds Bz7g Stone / Dark as explicit one-corner support and
        # applies 126 exact-owner corner corrections. The new family has five
        # honest relationships; reviewed rewrites redistribute four existing
        # rows among coverage buckets.
        # R132 adds Zone20 Stone / Dark as an explicitly reviewed,
        # transition-only identity at slot 189 SW and applies 75 exact-owner
        # corner corrections. The new identity adds four honest pair rows.
        # R133 applies 31 exact-owner corner rows and resolves the Zone7
        # byte-identical 86/87 mapping conflict. No family pair is added or
        # removed; the accepted rewrites only redistribute coverage classes.
        # R138 applies 46 exact-owner corner rows. Zone18 slot 347 introduces
        # one explicitly reviewed transition-only Ground / Tan corner where
        # Zone18 previously had no such family record. That deliberately
        # separates Zone18's runtime profile from bz16h and exposes its honest
        # family-pair denominator instead of hiding the cross-atlas identity.
        # R139 restores transition-only Stone / Desert in the byte-identical
        # bz14h/bz18h/Zone16 profile for slot 21's two explicitly reviewed
        # north corners. That adds four honest family-pair rows and moves one
        # existing pair from one topology class to two; no unreviewed Beige
        # corner is propagated.
        # R140 accepts Stone / Desert only at slot 30 NW and slot 31 NW across
        # those exact owners. The family-pair denominator remains stable, and
        # one existing relationship gains a second authored topology class.
        # R141 applies twelve reviewed corners without changing the pair
        # denominator or aggregate coverage buckets. R142 then closes fifteen
        # namespace-expanded, byte-identical historical corner acceptances.
        # Those exact closures move one existing relationship from three
        # authored topology classes to two; no family or pair is added.
        # R143 adds explicitly selected transition identities to every exact
        # decoded-HD alias, keeping byte-identical runtime profiles equal.
        # R144 adds the explicitly selected transition-only Stone / Grayish
        # identity to the exact bz2g/Zone2 owners.  One new family exposes its
        # thirteen honest pair rows; only the reviewed slot 221 SW supplies
        # authored topology evidence.
        # R146 resolves bz4g/Zone4 slot 488 NW as Ground / Gravel greenish.
        # The family-pair denominator stays fixed while one existing pair gains
        # a second authored topology class.
        # R147's provisional Sand / Dark interpretation for bz5g/bz6g/Zone8
        # slot 215 SW was withdrawn for re-review, so no transition-only family
        # or topology row is introduced.  The remaining exact reviewed corner
        # changes remove the only authored second topology for one existing
        # family pair.
        # R148 batch 1 applies 52 exact-owner corrections.  Those decisions
        # restore complete signature identity between bz15h and Zone17, so the
        # shared 21-pair runtime profile is counted once; accepted rewrites also
        # redistribute six retained pairs among topology coverage buckets.
        # R148 batch 2 applies 70 exact-owner corrections. The unavailable
        # bz4g/Zone4 Ground / Gravel choice remains open for re-review rather
        # than silently creating a transition-only non-brush family, so the
        # family-pair denominator is unchanged; retained rows only move among
        # topology coverage buckets.
        # R149 resolves two held corners. The Zone7 Sand / Dark -> Sand / Dark
        # pink change removes the only authored topology for one existing pair;
        # the bz4g/Zone4 Gravel greenish correction changes no pair denominator.
        # R150 reviews all seven remaining Zone7 Sand / Dark corners and
        # exhausts that transition-only family. Removing the unreferenced
        # family eliminates its seventeen remaining profile relationships;
        # accepted ownership changes redistribute five retained pair rows.
        # R151 changes no family-pair coverage bucket. R152 changes ten
        # logical corners through exact owner closure; one retained pair gains
        # a second authored topology while no family/pair record is added or
        # removed. R153 changes no aggregate coverage bucket. R154 explicitly
        # reassigns the last Stone / Beige brush witness in bz16h/Zone18;
        # Stone / Beige remains transition-only, moving two owner-profile pair
        # rows from one authored topology class to zero without changing the
        # family-pair denominator. R156 changes one Grass corner to Ground /
        # Light in the exact bz8k/bz9k/Zone11 owner group, moving one retained
        # pair from the two-topology bucket to the one-topology bucket.
        # R157 changes bz13h/Zone15 slot 384 SW from Sand / Common grayish to
        # Sand / Gravel. This preserves the aggregate pair-coverage buckets.
        # R158 applies four logical corrections through exact-owner closure.
        # The reported bz2g/Zone2 slot 209 correction removes the only authored
        # topology for one retained family pair, moving it from one topology
        # to zero; no family or pair record is added or removed.
        # R161 changes Zone7 slot 151 NW from the last Ground / Brown corner to
        # Ground / Dirt. The normal family prune removes Ground / Brown and its
        # sixteen remaining profile relationships; one Ground / Dirt pair also
        # gains its first authored topology witness.
        # R162's reviewed Zone17-only slot 233 corrections separate its runtime
        # signature profile from bz15h without asserting byte-identical owner
        # propagation. The additional seven-family profile contributes its 21
        # honest pair rows; three retained pairs move into full coverage.
        self.assertEqual(1343, report["totalPairCount"])
        self.assertEqual(
            {"0": 776, "1": 80, "2": 59, "3": 191, "4": 237},
            report["coverageCounts"],
        )
        for namespace in ("bz3g", "zone9"):
            preset = next(item for item in catalog["presets"]
                          if item["namespace"] == namespace)
            self.assertNotIn(
                "Ground / Trampled",
                {family["name"] for family in preset["families"]})


if __name__ == "__main__":
    unittest.main()
