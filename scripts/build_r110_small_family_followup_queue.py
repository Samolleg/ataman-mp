#!/usr/bin/env python3
"""Build the R110 remainder after the user's partial R109 review.

Only R109 questions 1-22 were reviewed.  Confirmed questions are recorded as
resolved; questions whose comments identify missing comparison evidence remain
queued.  The queue also adds the explicitly requested bz5g Ground / Light
family-wide corner review.
"""

from __future__ import annotations

import argparse
import base64
import json
from pathlib import Path


RESOLVED = {
    "r109-bz7g-slot143",
    "r109-zone7-slot133",
    "r109-zone7-slot134",
    "r109-zone7-slot335",
    "r109-zone7-slot257",
    "r109-bz16h-slot462",
    "r109-bz16h-slot504",
    "r109-zone18-slot462",
    "r109-zone18-slot504",
}

RUBBLE_REVIEW = {
    "r109-zone18-slot320",
    "r109-bz16h-slot320", "r109-bz16h-slot321",
    "r109-bz16h-slot322", "r109-bz16h-slot323",
    "r109-bz16h-slot324", "r109-bz16h-slot449",
    "r109-zone18-slot321", "r109-zone18-slot322",
    "r109-zone18-slot323", "r109-zone18-slot324",
    "r109-zone18-slot449",
}

TOPSOIL_REVIEW = {"r109-bz15h-slot248", "r109-zone17-slot248"}


def signatures(preset: dict) -> bytes:
    return base64.b64decode(preset["signaturesBase64"], validate=True)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--r109-queue", required=True, type=Path)
    parser.add_argument("--presets", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()

    source = json.loads(args.r109_queue.read_text(encoding="utf-8"))
    catalog = json.loads(args.presets.read_text(encoding="utf-8"))
    by_namespace = {row["namespace"].casefold(): row
                    for row in catalog["presets"]}

    items = []
    for source_index, row in enumerate(source["authoredItems"], 1):
        key = f"r109-{row['namespace'].lower()}-slot{int(row['slot'])}"
        if source_index <= 22 and key in RESOLVED:
            continue
        updated = dict(row)
        updated["sourceQuestionKey"] = key
        updated["reason"] = (
            "R109 follow-up: review the complete current mapping with the "
            "family's own mapped members and every requested comparison "
            "family visible on this card.")
        required = list(updated.get("requiredReferenceFamilies", []))
        # The R109 auto-prefix expansion flooded cards with every cross-atlas
        # Ground/Stone/etc. name.  R110 keeps the target atlas's own families
        # plus exact user-requested cross-atlas identities only.
        prefixes = []
        if key in RUBBLE_REVIEW:
            required += ["Rubble / Dry", "Paving / Sand ruins", "Ground / Dry"]
        if key in TOPSOIL_REVIEW:
            required += ["Ground / Plant soil", "Ground / Topsoil",
                         "Stone / Orange"]
        updated["requiredReferenceFamilies"] = list(dict.fromkeys(required))
        updated["comparisonFamilyPrefixes"] = list(dict.fromkeys(prefixes))
        updated["familyMemberPreviewLimit"] = 5
        items.append(updated)

    # The user explicitly requested bz5g slot 300 and every other mapped
    # Ground / Light corner in the same atlas.  None are treated as trusted
    # references in this questionnaire.
    preset = by_namespace["bz5g"]
    family = next(row for row in preset["families"]
                  if row["name"] == "Ground / Light")
    family_id = int(family["id"])
    table = signatures(preset)
    existing = {(row["namespace"].casefold(), int(row["slot"])) for row in items}
    for slot in range(int(preset["slotCount"])):
        signature = list(table[slot * 4:slot * 4 + 4])
        if family_id not in signature or ("bz5g", slot) in existing:
            continue
        items.append({
            "namespace": "bz5g",
            "slot": slot,
            "owners": ["bz5g"],
            "comparisonFamilyPrefixes": [],
            "requiredReferenceFamilies": ["Ground / Light"],
            "familyMemberPreviewLimit": 5,
            "reason": (
                "Requested family-wide review: bz5g Ground / Light has no "
                "uniform tile. Confirm this tile's four corners against its "
                "actual family members and all available Ground uniforms."),
            "corroboration": (
                "Ground / Light appears in 14 bz5g transition tiles and has "
                "zero uniform tiles."),
            "smallFamilies": ["Ground / Light"],
        })

    items.sort(key=lambda row: (
        0 if row.get("sourceQuestionKey") else 1,
        source["authoredItems"].index(next(
            old for old in source["authoredItems"]
            if old["namespace"] == row["namespace"]
            and int(old["slot"]) == int(row["slot"])))
        if row.get("sourceQuestionKey") else int(row["slot"])))

    output = {
        "schema": "ei-semantic-atlas-r110-small-family-followup-queue-v1",
        "sourceR109Questions": len(source["authoredItems"]),
        "reviewedR109Cutoff": 22,
        "resolvedR109QuestionKeys": sorted(RESOLVED),
        "retainedOrAddedQuestions": len(items),
        "authoredItems": items,
        "evidenceGaps": [{
            "key": "r110-no-authored-original-page",
            "status": "explicit-per-source",
            "reason": (
                "Some JMV terrain namespaces exist only in the approved HD "
                "archive. The page keeps their HD evidence and labels the "
                "missing original counterpart explicitly; it never substitutes "
                "another atlas silently."),
        }],
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(output, indent=2) + "\n",
                           encoding="utf-8", newline="\n")
    print(json.dumps({
        "resolved": len(RESOLVED),
        "retainedOrAddedQuestions": len(items),
        "bz5gGroundLightQuestions": sum(
            row["namespace"].casefold() == "bz5g"
            and "Ground / Light" in row.get("smallFamilies", [])
            for row in items),
    }, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
