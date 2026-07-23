#!/usr/bin/env python3
"""Fingerprint matching text resources from a read-only EI RES or directory."""

from __future__ import annotations

import argparse
import fnmatch
import hashlib
import json
from pathlib import Path
import struct
import sys


RES_SIGNATURE = 0x019CE23C


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest().upper()


def members(data: bytes):
    if len(data) < 16:
        raise ValueError("RES header is truncated")
    signature, count, table_offset, names_size = struct.unpack_from("<4I", data)
    if signature != RES_SIGNATURE:
        raise ValueError("not an EI RES archive")
    entry_size = struct.calcsize("<iIIIHI")
    table_end = table_offset + count * entry_size
    names_end = table_end + names_size
    if table_offset < 16 or names_end > len(data):
        raise ValueError("RES table or names block is out of bounds")
    names = data[table_end:names_end]
    for index in range(count):
        _, size, offset, timestamp, name_len, name_offset = struct.unpack_from(
            "<iIIIHI", data, table_offset + index * entry_size)
        if name_offset + name_len > len(names) or offset + size > len(data):
            raise ValueError(f"RES member {index} is out of bounds")
        name = names[name_offset:name_offset + name_len].decode("cp1251")
        yield name, timestamp, data[offset:offset + size]


def report(path: Path, patterns: list[str]) -> dict:
    lowered = [pattern.lower() for pattern in patterns]
    selected = []
    if path.is_dir():
        source_items = ((str(item.relative_to(path)).replace("\\", "/"),
                         int(item.stat().st_mtime), item.read_bytes())
                        for item in path.rglob("*") if item.is_file())
        source = {"path": str(path.resolve()), "kind": "directory"}
    else:
        data = path.read_bytes()
        source_items = members(data)
        source = {
            "path": str(path.resolve()),
            "kind": "res",
            "size": len(data),
            "sha256": sha256(data),
        }
    for name, timestamp, payload in source_items:
        if not any(fnmatch.fnmatchcase(name.lower(), pattern) for pattern in lowered):
            continue
        entry = {
            "name": name,
            "size": len(payload),
            "sha256": sha256(payload),
            "timestamp": timestamp,
        }
        if name.lower().endswith((".ini", ".txt")):
            entry["text_cp1251"] = payload.decode("cp1251")
        selected.append(entry)
    selected.sort(key=lambda entry: entry["name"].lower())
    return {
        "format": "ei-lighting-resource-report-v1",
        "source": source,
        "patterns": patterns,
        "members": selected,
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("--match", action="append", required=True)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args(argv)
    try:
        encoded = json.dumps(report(args.source, args.match), indent=2,
                             ensure_ascii=False, sort_keys=True) + "\n"
        if args.output:
            args.output.write_text(encoded, encoding="utf-8", newline="\n")
        else:
            sys.stdout.write(encoded)
    except (OSError, UnicodeError, ValueError, struct.error) as error:
        parser.error(str(error))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
