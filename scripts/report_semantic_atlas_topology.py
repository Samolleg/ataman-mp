#!/usr/bin/env python3
"""Report authored two-family transition topology coverage for every preset."""

from __future__ import annotations

import argparse
import base64
import csv
import hashlib
import itertools
import json
from collections import Counter
from pathlib import Path
from typing import Iterable


TOPOLOGIES = (
    "one-corner",
    "three-corner",
    "adjacent-two",
    "diagonal-two",
)


def decode_signatures(encoded: str, slot_count: int) -> list[tuple[int, ...]]:
    raw = base64.b64decode(encoded, validate=True)
    expected = slot_count * 4
    if len(raw) != expected:
        raise ValueError(
            f"Signature payload has {len(raw)} bytes; expected {expected}"
        )
    return [tuple(raw[index:index + 4]) for index in range(0, len(raw), 4)]


def signature_topology(signature: tuple[int, ...], first: int) -> str:
    occurrences = [index for index, family in enumerate(signature)
                   if family == first]
    if len(occurrences) == 1:
        return "one-corner"
    if len(occurrences) == 3:
        return "three-corner"
    if len(occurrences) != 2:
        raise ValueError(f"Not a two-family signature: {signature}")
    return ("diagonal-two" if set(occurrences) in ({0, 2}, {1, 3})
            else "adjacent-two")


def pair_topologies(
    signatures: Iterable[tuple[int, ...]], first: int, second: int
) -> set[str]:
    pair = {first, second}
    return {
        signature_topology(signature, first)
        for signature in signatures
        if set(signature) == pair
    }


def pair_topology_slots(
    signatures: Iterable[tuple[int, ...]], first: int, second: int
) -> dict[str, list[int]]:
    result = {name: [] for name in TOPOLOGIES}
    pair = {first, second}
    for slot, signature in enumerate(signatures):
        if set(signature) != pair:
            continue
        result[signature_topology(signature, first)].append(slot)
    return result


def runtime_identity(preset: dict) -> str:
    families = [
        {
            key: family.get(key)
            for key in (
                "id", "allowedLayers", "paintSelectable",
            )
        }
        for family in preset["families"]
    ]
    payload = {
        "slotCount": preset["slotCount"],
        "families": families,
        "signaturesBase64": preset["signaturesBase64"],
    }
    canonical = json.dumps(
        payload, sort_keys=True, separators=(",", ":"), ensure_ascii=True
    ).encode("ascii")
    return hashlib.sha256(canonical).hexdigest()


def build_report(catalog: dict) -> dict:
    grouped: dict[str, dict] = {}
    for preset in catalog["presets"]:
        identity = runtime_identity(preset)
        if identity in grouped:
            grouped[identity]["aliases"].append(preset["namespace"])
            continue
        grouped[identity] = {
            "identity": identity,
            "preset": preset,
            "aliases": [preset["namespace"]],
        }

    profiles = []
    global_counts: Counter[int] = Counter()
    total_pairs = 0
    for item in grouped.values():
        preset = item["preset"]
        signatures = decode_signatures(
            preset["signaturesBase64"], preset["slotCount"]
        )
        land_families = sorted(
            (
                family for family in preset["families"]
                if "land" in family.get("allowedLayers", [])
            ),
            key=lambda family: family["id"],
        )
        family_records = [
            {
                "id": family["id"],
                "name": family["name"],
                "paintSelectable": family.get("paintSelectable", True),
                "uniformSlots": [
                    slot for slot, signature in enumerate(signatures)
                    if signature == (family["id"],) * 4
                ],
            }
            for family in land_families
        ]
        pairs = []
        profile_counts: Counter[int] = Counter()
        for left, right in itertools.combinations(land_families, 2):
            topology_slots = pair_topology_slots(
                signatures, left["id"], right["id"]
            )
            observed = {
                name for name, slots in topology_slots.items() if slots
            }
            missing = [name for name in TOPOLOGIES if name not in observed]
            count = len(observed)
            profile_counts[count] += 1
            global_counts[count] += 1
            total_pairs += 1
            pairs.append({
                "familyAId": left["id"],
                "familyA": left["name"],
                "familyBId": right["id"],
                "familyB": right["name"],
                "topologies": [name for name in TOPOLOGIES if name in observed],
                "missingTopologies": missing,
                "coverageClassCount": count,
                "topologySlots": topology_slots,
            })
        profiles.append({
            "namespace": preset["namespace"],
            "aliases": sorted(item["aliases"]),
            "runtimeIdentity": item["identity"],
            "landFamilyCount": len(land_families),
            "families": family_records,
            "pairCount": len(pairs),
            "coverageCounts": {
                str(count): profile_counts[count] for count in range(5)
            },
            "pairs": pairs,
        })

    profiles.sort(key=lambda profile: profile["namespace"].casefold())
    return {
        "schema": "ei-semantic-atlas-two-family-topology-v1",
        "topologyClasses": list(TOPOLOGIES),
        "namespaceCount": len(catalog["presets"]),
        "runtimeProfileCount": len(profiles),
        "totalPairCount": total_pairs,
        "coverageCounts": {
            str(count): global_counts[count] for count in range(5)
        },
        "profiles": profiles,
    }


def strict_relabel_candidates(
    report: dict, audit_rows: Iterable[dict[str, str]]
) -> list[dict]:
    corner_index = {"NW": 0, "NE": 1, "SE": 2, "SW": 3}
    rows_by_namespace_slot: dict[tuple[str, int], list[dict[str, str]]] = {}
    for row in audit_rows:
        key = (row["namespace"].casefold(), int(row["slot"]))
        rows_by_namespace_slot.setdefault(key, []).append(row)

    candidates = []
    for profile in report["profiles"]:
        audit_namespace = next(
            (
                alias for alias in profile["aliases"]
                if any(key[0] == alias.casefold()
                       for key in rows_by_namespace_slot)
            ),
            None,
        )
        if audit_namespace is None:
            continue
        family_names = {
            family["id"]: family["name"] for family in profile["families"]
        }
        slot_groups = {
            slot: rows
            for (namespace, slot), rows in rows_by_namespace_slot.items()
            if namespace == audit_namespace.casefold()
        }
        for pair in profile["pairs"]:
            if pair["coverageClassCount"] != 3:
                continue
            missing = pair["missingTopologies"]
            if len(missing) != 1:
                continue
            missing_topology = missing[0]
            first = pair["familyAId"]
            second = pair["familyBId"]
            for slot, rows in slot_groups.items():
                signature = tuple(
                    int(value) for value in rows[0]["signature"].split("/")
                )
                current = set(signature)
                if len(current) != 2 or current == {first, second}:
                    continue
                if first in current and second not in current:
                    retained, target = first, second
                elif second in current and first not in current:
                    retained, target = second, first
                else:
                    continue
                replaced = next(iter(current - {retained}))
                proposed = tuple(
                    target if value == replaced else value
                    for value in signature
                )
                if (set(proposed) != {first, second}
                    or signature_topology(proposed, first)
                        != missing_topology):
                    continue
                by_corner = {row["corner"]: row for row in rows}
                if set(by_corner) != set(corner_index):
                    continue
                replaced_rows = [
                    by_corner[name] for name, index in corner_index.items()
                    if signature[index] == replaced
                ]
                retained_rows = [
                    by_corner[name] for name, index in corner_index.items()
                    if signature[index] == retained
                ]
                if (not replaced_rows
                    or any(int(row["nearestAnchorId"]) != target
                           for row in replaced_rows)
                    or any(int(row["nearestAnchorId"]) != retained
                           for row in retained_rows)):
                    continue
                candidates.append({
                    "namespace": profile["namespace"],
                    "auditNamespace": audit_namespace,
                    "slot": slot,
                    "currentSignature": list(signature),
                    "proposedSignature": list(proposed),
                    "missingTopology": missing_topology,
                    "familyAId": first,
                    "familyA": pair["familyA"],
                    "familyBId": second,
                    "familyB": pair["familyB"],
                    "replacedFamilyId": replaced,
                    "replacedFamily": family_names.get(
                        replaced, f"Family {replaced}"
                    ),
                    "replacementFamilyId": target,
                    "replacementFamily": family_names[target],
                    "replacementCornerCount": len(replaced_rows),
                    "replacementMeanScore": round(
                        sum(float(row["nearestScore"])
                            for row in replaced_rows) / len(replaced_rows),
                        6,
                    ),
                })
    return sorted(
        candidates,
        key=lambda item: (
            item["namespace"].casefold(), item["missingTopology"],
            item["replacementMeanScore"], item["slot"],
        ),
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("catalog", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--corner-audit", type=Path)
    args = parser.parse_args()
    catalog = json.loads(args.catalog.read_text(encoding="utf-8"))
    report = build_report(catalog)
    if args.corner_audit:
        with args.corner_audit.open(encoding="utf-8-sig", newline="") as stream:
            report["strictRelabelCandidates"] = strict_relabel_candidates(
                report, csv.DictReader(stream)
            )
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(
        json.dumps(report, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8",
    )
    print(
        f"{report['namespaceCount']} namespaces, "
        f"{report['runtimeProfileCount']} runtime profiles, "
        f"{report['totalPairCount']} land-family pairs: "
        + ", ".join(
            f"{count} classes={report['coverageCounts'][str(count)]}"
            for count in range(5)
        )
        + (f"; strict relabel candidates="
           f"{len(report.get('strictRelabelCandidates', []))}"
           if args.corner_audit else "")
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
