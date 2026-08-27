#!/usr/bin/env python3
"""Normalize the exact Zone18 244/248/249 authored aliases."""

from __future__ import annotations

import argparse
import base64
import json
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()

    catalog = json.loads(args.input.read_text(encoding="utf-8"))
    preset = next(row for row in catalog["presets"]
                  if row["namespace"].casefold() == "zone18")
    ids = {row["name"]: int(row["id"]) for row in preset["families"]}
    payload = bytearray(base64.b64decode(
        preset["signaturesBase64"], validate=True))
    mixed = bytes((ids["Ground / Red"],) +
                  (ids["Ground / Desert algae"],) * 3)
    uniform = bytes((ids["Ground / Desert algae"],) * 4)
    before = {}
    for slot in (244, 248, 249):
        offset = slot * 4
        found = bytes(payload[offset:offset + 4])
        if found not in (mixed, uniform):
            raise ValueError(
                f"Zone18 slot {slot} precondition changed: {list(found)}")
        before[str(slot)] = list(found)
        payload[offset:offset + 4] = mixed
    preset["signaturesBase64"] = base64.b64encode(payload).decode("ascii")
    args.output.write_text(json.dumps(catalog, indent=2) + "\n",
                           encoding="utf-8", newline="\n")
    print(json.dumps({"namespace": "Zone18", "slots": [244, 248, 249],
                      "before": before, "after": list(mixed)}))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
