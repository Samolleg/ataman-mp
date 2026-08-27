from __future__ import annotations

import argparse
import csv
import hashlib
import json
from pathlib import Path


CORNER_KEYS = ("northWest", "northEast", "southEast", "southWest")


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest().lower()


def inspect_profile(path: Path) -> list[dict[str, object]]:
    profile = json.loads(path.read_text(encoding="utf-8-sig"))
    families = {int(item["id"]): item for item in profile.get("families", [])}
    pure_slots: dict[int, list[int]] = {family_id: [] for family_id in families}
    supports: dict[int, set[int]] = {family_id: set() for family_id in families}
    assigned_slots = 0
    unassigned_slots = 0
    for slot in profile.get("slots", []):
        if not slot.get("assigned", not slot.get("ambiguous", False)):
            unassigned_slots += 1
            continue
        assigned_slots += 1
        signature = slot.get("signature", {})
        corners = [int(signature[key]) for key in CORNER_KEYS]
        unique = set(corners)
        if len(unique) == 1 and corners[0] in pure_slots:
            pure_slots[corners[0]].append(int(slot["slot"]))
        for family_id in unique:
            if corners.count(family_id) != 1:
                continue
            others = [corner for corner in corners if corner != family_id]
            if len(others) == 3 and len(set(others)) == 1:
                supports.setdefault(family_id, set()).add(others[0])

    names = {family_id: str(item.get("name", ""))
             for family_id, item in families.items()}
    rows: list[dict[str, object]] = []
    for family_id in sorted(families):
        family = families[family_id]
        support_ids = sorted(supports.get(family_id, set()))
        rows.append({
            "profileFile": path.name,
            "profileSha256": sha256(path),
            "namespace": profile.get("mprNamespace", ""),
            "atlasPixelSha256": profile.get("atlasPixelSha256", ""),
            "atlasSlots": int(profile.get("atlasSlotCount", 0)),
            "assignedSlots": assigned_slots,
            "unassignedSlots": unassigned_slots,
            "familyId": family_id,
            "currentName": names[family_id],
            "correctedName": "",
            "layer": family.get("layer", ""),
            "underwater": bool(family.get("underwater", False)),
            "needsConfirmation": bool(family.get("ambiguous", False)),
            "memberSlots": len(family.get("slots", [])),
            "pureSlots": len(pure_slots.get(family_id, [])),
            "lowestPureSlot": min(pure_slots[family_id])
                if pure_slots.get(family_id) else "",
            "cornerOnly": not bool(pure_slots.get(family_id)),
            "oneOnThreeSupports": "; ".join(
                f"{support_id}:{names.get(support_id, 'material ' + str(support_id))}"
                for support_id in support_ids),
        })
    return rows


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Create a hash-bound material naming table from copied semantic-atlas profiles.")
    parser.add_argument("profiles", nargs="+", type=Path)
    parser.add_argument("--csv", required=True, type=Path)
    parser.add_argument("--json", required=True, type=Path)
    args = parser.parse_args()

    rows: list[dict[str, object]] = []
    for profile in sorted((path.resolve() for path in args.profiles), key=str):
        rows.extend(inspect_profile(profile))
    args.csv.parent.mkdir(parents=True, exist_ok=True)
    args.json.parent.mkdir(parents=True, exist_ok=True)
    fields = list(rows[0].keys()) if rows else []
    with args.csv.open("w", encoding="utf-8-sig", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=fields)
        writer.writeheader()
        writer.writerows(rows)
    args.json.write_text(json.dumps({"families": rows}, ensure_ascii=False, indent=2) + "\n",
                         encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
