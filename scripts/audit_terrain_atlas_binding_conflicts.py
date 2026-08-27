#!/usr/bin/env python3
"""Report semantic-table disagreements for identical decoded terrain atlases."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
from collections import defaultdict
from pathlib import Path


def decoded_identity_rows(catalog: dict) -> list[dict]:
    """Group differently named presets by decoded atlas content identity."""
    groups: dict[tuple[str, str, int], list[dict]] = defaultdict(list)
    for preset in catalog["presets"]:
        for binding in preset["bindings"]:
            groups[(binding["variant"], binding["pixelSha256"],
                    preset["slotCount"])].append(preset)

    rows = []
    for (variant, pixel_hash, slot_count), presets in sorted(groups.items()):
        if len(presets) < 2:
            continue
        tables = defaultdict(list)
        for preset in presets:
            signature_hash = hashlib.sha256(
                preset["signaturesBase64"].encode("ascii")).hexdigest()
            tables[signature_hash].append(preset["namespace"])
        rows.append({
            "variant": variant,
            "pixelSha256": pixel_hash,
            "slotCount": slot_count,
            "namespaces": ";".join(
                preset["namespace"] for preset in presets),
            "namespaceCount": len(presets),
            "distinctSignatureTables": len(tables),
            "signatureGroups": " | ".join(
                f"{key[:12]}:{','.join(namespaces)}"
                for key, namespaces in sorted(tables.items())),
            "familyCounts": ";".join(
                f"{preset['namespace']}={len(preset['families'])}"
                for preset in presets),
        })
    return rows


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--presets", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    catalog = json.loads(args.presets.read_text(encoding="utf-8"))
    rows = decoded_identity_rows(catalog)

    args.output.parent.mkdir(parents=True, exist_ok=True)
    fields = ("variant", "pixelSha256", "slotCount", "namespaces",
              "namespaceCount", "distinctSignatureTables",
              "signatureGroups", "familyCounts")
    with args.output.open("w", newline="", encoding="utf-8-sig") as stream:
        writer = csv.DictWriter(stream, fieldnames=fields)
        writer.writeheader()
        writer.writerows(rows)
    conflicts = sum(int(row["distinctSignatureTables"]) > 1 for row in rows)
    print(json.dumps({"sharedBindings": len(rows), "conflicts": conflicts,
                      "output": str(args.output)}, sort_keys=True))
    # Conflicts are the report's subject, not an execution failure. Keep the
    # process successful so an expected finding cannot abort an audit worker.
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
