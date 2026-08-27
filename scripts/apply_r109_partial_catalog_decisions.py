#!/usr/bin/env python3
"""Apply only the explicit catalog-changing part of the partial R109 review."""

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
                  if row["namespace"].casefold() == "bz7g")
    ids = {row["name"]: int(row["id"]) for row in preset["families"]}
    payload = bytearray(base64.b64decode(
        preset["signaturesBase64"], validate=True))
    offset = 143 * 4
    before = list(payload[offset:offset + 4])
    expected = [ids["Ground / Common"]] + [ids["Stone / Greenish"]] * 3
    after = [ids["Ground / Dirt"]] + [ids["Stone / Greenish"]] * 3
    if before not in (expected, after):
        raise ValueError(f"Bz7g slot143 precondition changed: {before} != {expected}")
    payload[offset:offset + 4] = bytes(after)
    # Slot 143 was the last remaining Ground / Common corner in Bz7g.  Once
    # explicitly reassigned, retain no dead brush record.
    ground_common_id = ids["Ground / Common"]
    if ground_common_id in payload:
        raise ValueError("Bz7g Ground / Common remains referenced unexpectedly")
    preset["families"] = [row for row in preset["families"]
                          if int(row["id"]) != ground_common_id]
    preset["signaturesBase64"] = base64.b64encode(payload).decode("ascii")
    args.output.write_text(json.dumps(catalog, indent=2) + "\n",
                           encoding="utf-8", newline="\n")
    print(json.dumps({"namespace": "Bz7g", "slot": 143,
                      "before": before,
                      "after": list(payload[offset:offset + 4])}))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
