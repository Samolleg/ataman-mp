#!/usr/bin/env python3
"""Reduce terrain paint reports to provenance-qualified semantic review sources."""

from __future__ import annotations

import argparse
import base64
import hashlib
import json
from collections import defaultdict
from pathlib import Path
from typing import Any


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def load_catalog(path: Path) -> dict[tuple[str, int], dict[str, Any]]:
    catalog = json.loads(path.read_text(encoding="utf-8"))
    slots: dict[tuple[str, int], dict[str, Any]] = {}
    for preset in catalog["presets"]:
        namespace = preset["namespace"]
        slot_count = int(preset["slotCount"])
        packed = base64.b64decode(preset["signaturesBase64"])
        if len(packed) != slot_count * 4:
            raise ValueError(f"{namespace}: signature table length mismatch")
        families = {int(item["id"]): item for item in preset["families"]}
        for slot in range(slot_count):
            local_ids = list(packed[slot * 4 : slot * 4 + 4])
            try:
                names = [families[value]["name"] for value in local_ids]
                runtime_types = [int(families[value]["tileType"]) for value in local_ids]
            except KeyError as error:
                raise ValueError(
                    f"{namespace}:{slot}: unknown family id {error.args[0]}") from error
            slots[(namespace.casefold(), slot)] = {
                "namespace": namespace,
                "slot": slot,
                "signatureNwNeSeSw": names,
                "localFamilyIdsNwNeSeSw": local_ids,
                "runtimeTerrainTypesNwNeSeSw": runtime_types,
                "catalogUniform": len(set(local_ids)) == 1,
            }
    return slots


def load_inventory_manifests(paths: list[Path]) -> tuple[dict, dict, dict]:
    aliases: dict[tuple[str, int], dict[str, Any]] = {}
    material_names: dict[int, str] = {}
    fixtures: dict[str, dict[str, Any]] = {}
    for path in paths:
        manifest = json.loads(path.read_text(encoding="utf-8"))
        cards_by_slot: dict[int, dict[str, Any]] = {}
        for card in manifest.get("artwork_cards", []):
            material_id = int(card["material_id"])
            material_names[material_id] = card["material"]
            common = {
                "material": card["material"],
                "materialId": material_id,
                "runtimeTerrainType": int(card["runtime_terrain_type"]),
                "sourcePixelSha256": card["pixel_sha256"].lower(),
                "equivalentAliases": sorted(
                    [
                        {
                            "namespace": item["namespace"],
                            "slot": int(item["slot"]),
                        }
                        for item in card["source_aliases"]
                    ],
                    key=lambda item: (item["namespace"].casefold(), item["slot"]),
                ),
            }
            for alias in common["equivalentAliases"]:
                aliases[(alias["namespace"].casefold(), alias["slot"])] = common
            cards_by_slot[int(card["physical_slot"])] = {
                **common,
                "physicalSlot": int(card["physical_slot"]),
            }
        mpr_path = path.parent / manifest.get("mpr_file", "")
        if mpr_path.is_file():
            fixtures[sha256(mpr_path)] = {
                "manifestPath": str(path),
                "manifestSha256": sha256(path),
                "mprPath": str(mpr_path),
                "mprSha256": sha256(mpr_path),
                "width": int(manifest["packed_cell_width"]),
                "height": int(manifest["packed_cell_height"]),
                "packed": base64.b64decode(manifest["packed_cells_le16_base64"]),
                "cardsBySlot": cards_by_slot,
            }
    return aliases, material_names, fixtures


def corner_values(corners: dict[str, Any] | None) -> list[int]:
    if not corners:
        return []
    return [
        int(corners[name])
        for name in ("x0y0", "x1y0", "x1y1", "x0y1")
        if name in corners
    ]


def source_aliases(identity: dict[str, Any]) -> list[dict[str, Any]]:
    result = []
    for item in identity.get("sourceAliases", []):
        result.append(
            {
                "namespace": item["sourceNamespace"],
                "slot": int(item["sourceSlot"]),
                "sourcePixelSha256": item.get("sourcePixelSha256", "").lower(),
            }
        )
    if not result and identity.get("sourceNamespace") and int(
        identity.get("sourceSlot", -1)
    ) >= 0:
        result.append(
            {
                "namespace": identity["sourceNamespace"],
                "slot": int(identity["sourceSlot"]),
                "sourcePixelSha256": "",
            }
        )
    return result


def reconcile(
    report_dir: Path,
    catalog_path: Path,
    inventory_paths: list[Path],
) -> dict[str, Any]:
    catalog = load_catalog(catalog_path)
    inventory, material_names, fixtures = load_inventory_manifests(inventory_paths)
    groups: dict[tuple, dict[str, Any]] = {}
    generated_blends = []
    unresolved = []
    report_rows = []
    total_marks = 0
    total_selected = 0
    captured_selected = 0
    recovered_selected = 0

    def queue_source(
        alias: dict[str, Any],
        trigger: str,
        evidence: dict[str, Any],
        claimed_material: str = "",
        claimed_material_id: int | None = None,
        runtime_type: int | None = None,
    ) -> None:
        namespace = alias["namespace"]
        slot = int(alias["slot"])
        catalog_row = catalog.get((namespace.casefold(), slot))
        if catalog_row is None:
            unresolved.append(
                {
                    "reason": "source alias is absent from the semantic catalog",
                    "sourceNamespace": namespace,
                    "sourceSlot": slot,
                    "evidence": evidence,
                }
            )
            return
        inventory_row = inventory.get((namespace.casefold(), slot), {})
        source_hash = (
            alias.get("sourcePixelSha256")
            or inventory_row.get("sourcePixelSha256")
            or evidence.get("physicalPixelSha256")
            or ""
        ).lower()
        signature = tuple(catalog_row["signatureNwNeSeSw"])
        key = (
            source_hash,
            signature,
            tuple(catalog_row["runtimeTerrainTypesNwNeSeSw"]),
        )
        group = groups.get(key)
        if group is None:
            group = {
                "reviewDisposition": (
                    "automatic-uniform-review"
                    if catalog_row["catalogUniform"]
                    else "automatic-authored-blend-review"
                ),
                "sourcePixelSha256": source_hash,
                "catalogSignatureNwNeSeSw": list(signature),
                "catalogUniform": catalog_row["catalogUniform"],
                "runtimeTerrainTypesNwNeSeSw": catalog_row[
                    "runtimeTerrainTypesNwNeSeSw"
                ],
                "sourceAliases": [],
                "inventoryEquivalentAliases": inventory_row.get(
                    "equivalentAliases", []
                ),
                "claimedMaterials": [],
                "triggers": [],
                "evidence": [],
            }
            groups[key] = group
        source_key = (namespace.casefold(), slot)
        known_sources = {
            (item["namespace"].casefold(), int(item["slot"]))
            for item in group["sourceAliases"]
        }
        if source_key not in known_sources:
            group["sourceAliases"].append(
                {"namespace": catalog_row["namespace"], "slot": slot}
            )
        claim = {
            "material": claimed_material,
            "materialId": claimed_material_id,
            "runtimeTerrainType": runtime_type,
        }
        if claimed_material and claim not in group["claimedMaterials"]:
            group["claimedMaterials"].append(claim)
        if trigger not in group["triggers"]:
            group["triggers"].append(trigger)
        if evidence not in group["evidence"]:
            group["evidence"].append(evidence)

    for report_path in sorted(report_dir.glob("*-paint-issues.json")):
        report = json.loads(report_path.read_text(encoding="utf-8"))
        fixture = fixtures.get(report.get("sourceMprSha256", "").lower())
        marks = report.get("marks", [])
        report_selected = 0
        report_captured = 0
        report_recovered = 0
        for mark in marks:
            total_marks += 1
            selected = {
                (int(cell["x"]), int(cell["y"]))
                for cell in mark.get("selectedCells", [])
            }
            total_selected += len(selected)
            report_selected += len(selected)
            captured_coordinates = set()
            for cell in mark.get("tileSnapshot", []):
                if not cell.get("insideMark"):
                    continue
                coordinate = (int(cell["x"]), int(cell["y"]))
                captured_coordinates.add(coordinate)
                captured_selected += 1
                report_captured += 1
                identity = cell.get("physicalIdentity", {})
                origin = identity.get("origin", "")
                evidence = {
                    "report": report_path.name,
                    "markId": int(mark.get("id", -1)),
                    "category": mark.get("category", "unknown"),
                    "layer": mark.get("layer", ""),
                    "x": coordinate[0],
                    "y": coordinate[1],
                    "packedSlot": int(cell.get("packed", {}).get("atlasSlot", -1)),
                    "packedRotation": int(cell.get("packed", {}).get("rotation", -1)),
                    "physicalPixelSha256": cell.get("physicalPixelSha256", "").lower(),
                    "origin": origin,
                }
                if origin == "canonical-generated":
                    inputs = identity.get("inputUniformSources", [])
                    generated = {
                        **evidence,
                        "blendProvenance": identity.get("blendProvenance", ""),
                        "canonicalMaterialIdsNwNeSeSw": corner_values(
                            identity.get("canonicalGridCorners")
                        ),
                        "inputUniformSources": inputs,
                    }
                    generated["canonicalMaterialsNwNeSeSw"] = [
                        material_names.get(value, f"material-id:{value}")
                        for value in generated["canonicalMaterialIdsNwNeSeSw"]
                    ]
                    generated_blends.append(generated)
                    if not inputs:
                        unresolved.append(
                            {
                                "reason": "generated blend has no captured input uniforms",
                                "evidence": evidence,
                            }
                        )
                    for source in inputs:
                        queue_source(
                            {
                                "namespace": source["sourceNamespace"],
                                "slot": int(source["sourceSlot"]),
                                "sourcePixelSha256": source.get(
                                    "physicalPixelSha256", ""
                                ).lower(),
                            },
                            "reported-generated-blend-input",
                            evidence,
                            source.get("material", ""),
                            int(source.get("materialId", -1)),
                            int(identity.get("runtimeTerrainType", -1)),
                        )
                elif origin in ("authored-current-map", "authored-global"):
                    aliases = source_aliases(identity)
                    if not aliases:
                        unresolved.append(
                            {
                                "reason": "authored report has no captured source alias",
                                "evidence": evidence,
                            }
                        )
                    for alias in aliases:
                        inventory_row = inventory.get(
                            (alias["namespace"].casefold(), int(alias["slot"])), {}
                        )
                        queue_source(
                            alias,
                            "reported-authored-tile",
                            evidence,
                            inventory_row.get("material", ""),
                            inventory_row.get("materialId"),
                            int(identity.get("runtimeTerrainType", -1)),
                        )
                else:
                    unresolved.append(
                        {
                            "reason": f"unsupported or missing physical origin: {origin!r}",
                            "evidence": evidence,
                        }
                    )
            for x, y in sorted(selected - captured_coordinates):
                recovered = False
                if fixture and 0 <= x < fixture["width"] and 0 <= y < fixture["height"]:
                    offset = (y * fixture["width"] + x) * 2
                    packed = fixture["packed"][offset] | (
                        fixture["packed"][offset + 1] << 8
                    )
                    physical_slot = packed & 0x3FFF
                    rotation = (packed >> 14) & 3
                    card = fixture["cardsBySlot"].get(physical_slot)
                    if card:
                        evidence = {
                            "report": report_path.name,
                            "markId": int(mark.get("id", -1)),
                            "category": mark.get("category", "unknown"),
                            "layerRecordedByMarker": mark.get("layer", ""),
                            "x": x,
                            "y": y,
                            "packedSlot": physical_slot,
                            "packedRotation": rotation,
                            "physicalPixelSha256": card["sourcePixelSha256"],
                            "origin": "exact-inventory-manifest-recovery",
                            "recoveredFromManifest": fixture["manifestPath"],
                            "manifestSha256": fixture["manifestSha256"],
                            "matchedSourceMprSha256": fixture["mprSha256"],
                        }
                        for alias in card["equivalentAliases"]:
                            queue_source(
                                {
                                    **alias,
                                    "sourcePixelSha256": card["sourcePixelSha256"],
                                },
                                "reported-authored-tile-recovered-from-exact-inventory",
                                evidence,
                                card["material"],
                                card["materialId"],
                                card["runtimeTerrainType"],
                            )
                        recovered_selected += 1
                        report_recovered += 1
                        recovered = True
                if recovered:
                    continue
                unresolved.append(
                    {
                        "reason": "selected cell has no captured tile snapshot",
                        "report": report_path.name,
                        "markId": int(mark.get("id", -1)),
                        "category": mark.get("category", "unknown"),
                        "layer": mark.get("layer", ""),
                        "x": x,
                        "y": y,
                    }
                )
        report_rows.append(
            {
                "file": report_path.name,
                "sha256": sha256(report_path),
                "mprNamespace": report.get("mprNamespace", ""),
                "mprFileName": report.get("mprFileName", ""),
                "sourceMprSha256": report.get("sourceMprSha256", ""),
                "marks": len(marks),
                "selectedCells": report_selected,
                "capturedSelectedCells": report_captured,
                "recoveredSelectedCells": report_recovered,
            }
        )

    review_groups = list(groups.values())
    for group in review_groups:
        group["sourceAliases"].sort(
            key=lambda item: (item["namespace"].casefold(), item["slot"])
        )
        group["triggers"].sort()
    review_groups.sort(
        key=lambda group: (
            not group["catalogUniform"],
            group["catalogSignatureNwNeSeSw"],
            group["sourcePixelSha256"],
        )
    )
    uniform_groups = sum(item["catalogUniform"] for item in review_groups)
    authored_blend_groups = len(review_groups) - uniform_groups
    missing_snapshot_count = sum(
        item.get("reason") == "selected cell has no captured tile snapshot"
        for item in unresolved
    )
    return {
        "schema": "ei-modern-atlas-reported-source-review-v1",
        "policy": {
            "reportedUniform": "automatic semantic/artwork review",
            "reportedAuthoredBlend": "automatic all-four-corner semantic/artwork review",
            "reportedGeneratedBlend": (
                "queue every captured pre-blend uniform input; do not select a likely culprit"
            ),
            "missingProvenance": "preserve as an evidence gap; never infer a source",
        },
        "catalog": {
            "path": str(catalog_path),
            "sha256": sha256(catalog_path),
        },
        "inventoryManifests": [
            {"path": str(path), "sha256": sha256(path)} for path in inventory_paths
        ],
        "reports": report_rows,
        "summary": {
            "reports": len(report_rows),
            "marks": total_marks,
            "selectedCells": total_selected,
            "capturedSelectedCells": captured_selected,
            "recoveredSelectedCells": recovered_selected,
            "provenanceQualifiedSelectedCells": (
                captured_selected + recovered_selected
            ),
            "generatedBlendOccurrences": len(generated_blends),
            "automaticReviewGroups": len(review_groups),
            "uniformReviewGroups": uniform_groups,
            "authoredBlendReviewGroups": authored_blend_groups,
            "unresolvedEvidenceRows": len(unresolved),
            "selectedCellsWithoutSnapshot": missing_snapshot_count,
        },
        "generatedBlendOccurrences": generated_blends,
        "automaticReviewGroups": review_groups,
        "unresolvedEvidence": unresolved,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reports", required=True, type=Path)
    parser.add_argument("--presets", required=True, type=Path)
    parser.add_argument(
        "--inventory-manifest", required=True, action="append", type=Path
    )
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    result = reconcile(
        args.reports, args.presets, list(args.inventory_manifest)
    )
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(
        json.dumps(result, indent=2, ensure_ascii=False) + "\n", encoding="utf-8"
    )
    print(json.dumps(result["summary"], indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
