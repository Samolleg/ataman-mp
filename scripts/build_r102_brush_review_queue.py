#!/usr/bin/env python3
"""Build the combined R101 + R102 authored-corner review queue."""

from __future__ import annotations

import argparse
import base64
import json
from pathlib import Path

from build_r101_residual_queue import CONNECTED_REVIEW, PENDING_FAMILIES, UNTOUCHED


REPRESENTATIVE_ONLY = {
    ("zone7", 13),
    ("zone3obr", 4),
    ("zonemainmenunew", 6),
}


def signatures(preset: dict) -> bytes:
    return base64.b64decode(preset["signaturesBase64"], validate=True)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--presets", required=True, type=Path)
    parser.add_argument(
        "--decisions", type=Path,
        default=Path(__file__).with_name(
            "semantic_atlas_r102_brush_inventory_decisions.json"))
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()

    catalog = json.loads(args.presets.read_text(encoding="utf-8"))
    decisions = json.loads(args.decisions.read_text(encoding="utf-8"))
    presets = {row["namespace"].casefold(): row for row in catalog["presets"]}
    items: dict[tuple[str, int], dict] = {}

    def add(namespace: str, slot: int, reason: str,
            corroboration: str) -> None:
        preset = presets[namespace.casefold()]
        key = (preset["namespace"].casefold(), int(slot))
        if key in items:
            if reason not in items[key]["reason"]:
                items[key]["reason"] += " " + reason
            if corroboration not in items[key]["corroboration"]:
                items[key]["corroboration"] += "; " + corroboration
            return
        items[key] = {
            "namespace": preset["namespace"],
            "slot": int(slot),
            "reason": reason,
            "corroboration": corroboration,
        }

    # Preserve every unanswered R101 item.
    for namespace, family_name, alias, reason in PENDING_FAMILIES:
        preset = presets[namespace.casefold()]
        family = next((row for row in preset["families"]
                       if row["name"] == family_name), None)
        if family is None:
            continue
        family_id = int(family["id"])
        payload = signatures(preset)
        for slot in range(int(preset["slotCount"])):
            if family_id in payload[slot * 4:slot * 4 + 4]:
                add(namespace, slot,
                    f"{reason} Exact shared atlas owner: {alias}.",
                    "R101 unanswered family-wide review")
    for namespace, slot in UNTOUCHED:
        add(namespace, slot,
            "R100 card was left untouched; no default is confirmation.",
            "R101 carried forward")
    for namespace, slot, reason in CONNECTED_REVIEW:
        add(namespace, slot, reason,
            "R101 connected atlas review; no automatic propagation")

    # Every challenged nominal uniform is a full four-corner question and is
    # excluded from all reference rows by the questionnaire renderer.
    for namespace, slots in decisions["falseUniformSlots"].items():
        for slot in slots:
            add(namespace, slot,
                "R102 brush review says this nominal uniform is visibly mixed; review all four corners.",
                "explicit false-uniform report")

    # Families said to contain no instance of their current material must have
    # each affected authored tile reviewed. Naming/comparison-only requests use
    # the currently claimed uniform anchors instead of creating hundreds of
    # redundant transition cards.
    for namespace, family_ids in decisions["reviewFamilies"].items():
        preset = presets[namespace.casefold()]
        payload = signatures(preset)
        by_id = {int(row["id"]): row for row in preset["families"]}
        for family_id in family_ids:
            family = by_id.get(int(family_id))
            if family is None:
                continue
            matching = sorted({index // 4 for index, value in enumerate(payload)
                               if value == int(family_id)})
            if (namespace.casefold(), int(family_id)) in REPRESENTATIVE_ONLY:
                uniform = [slot for slot in matching
                           if list(payload[slot * 4:slot * 4 + 4])
                           == [int(family_id)] * 4]
                matching = uniform or matching[:6]
            for slot in matching:
                add(namespace, slot,
                    f"R102 requests review of current {family['name']} family {family_id}; it may be a false family, merge, or naming conflict.",
                    "brush-inventory family review")

    for namespace, slots in decisions["reviewSlots"].items():
        for slot in slots:
            add(namespace, slot,
                "R102 explicitly reports that this tile contains none of the challenged family; review all four corners.",
                "explicit connected-slot review")

    ordered = sorted(items.values(), key=lambda row: (
        row["namespace"].casefold(), row["slot"]))
    payload = {
        "schema": "ei-semantic-atlas-r102-brush-review-queue-v1",
        "authoredItems": ordered,
        "evidenceGaps": [],
        "counts": {
            "cards": len(ordered),
            "falseUniformInputs": sum(map(len, decisions["falseUniformSlots"].values())),
            "reviewFamilyRows": sum(map(len, decisions["reviewFamilies"].values())),
            "explicitReviewSlots": sum(map(len, decisions["reviewSlots"].values())),
        },
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(payload["counts"]))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
