#!/usr/bin/env python3
"""Fingerprint selected particle atlases inside a read-only EI texture RES."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys
from typing import Any


RES_SIGNATURE = 0x019CE23C
MMP_SIGNATURE = 0x00504D4D
REFERENCE_JMV_TEXTURES_SHA256 = "2E8CA42FB75FAC12E5659C563C547FEE0F72D6C1B6E2E1C454B3072E3BE0703B"
REFERENCE_BASE_TEXTURES_SHA256 = "79548FBD94E797FE9EDC3E990EB0231A350571B2CEF575B009FA972F0002268C"
DEFAULT_NAMES = ("tornado.mmp", "tendrilight.mmp")


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest().upper()


def archive_members(data: bytes) -> dict[str, bytes]:
    if len(data) < 16:
        raise ValueError("RES header is truncated")
    signature, count, table_offset, names_size = struct.unpack_from("<4I", data)
    if signature != RES_SIGNATURE:
        raise ValueError("not an EI RES archive")
    entry_size = struct.calcsize("<iIIIHI")
    table_end = table_offset + count * entry_size
    names_end = table_end + names_size
    if table_offset < 16 or names_end > len(data):
        raise ValueError("RES member table is out of bounds")
    names = data[table_end:names_end]
    result: dict[str, bytes] = {}
    for index in range(count):
        _next, size, offset, _timestamp, name_length, name_offset = struct.unpack_from(
            "<iIIIHI", data, table_offset + index * entry_size)
        if name_offset + name_length > len(names) or offset + size > table_offset:
            raise ValueError(f"RES member {index} is out of bounds")
        name = names[name_offset:name_offset + name_length].decode("cp1251")
        result[name.lower()] = data[offset:offset + size]
    return result


def mmp_report(name: str, data: bytes) -> dict[str, Any]:
    if len(data) < 20:
        raise ValueError(f"{name}: MMP header is truncated")
    signature, width, height, mip_count, texture_format = struct.unpack_from("<5I", data)
    if signature != MMP_SIGNATURE:
        raise ValueError(f"{name}: not an MMP texture")
    return {
        "name": name,
        "size": len(data),
        "sha256": sha256(data),
        "width": width,
        "height": height,
        "mipCount": mip_count,
        "textureFormat": f"0x{texture_format:08X}",
        "atlas": {"columns": 4, "rows": 4, "phaseCount": 16},
    }


def report(path: Path, names: tuple[str, ...], required_sha256: str | None = None) -> dict[str, Any]:
    data = path.read_bytes()
    fingerprint = sha256(data)
    if required_sha256 and fingerprint != required_sha256.upper():
        raise ValueError(
            f"texture archive SHA-256 {fingerprint} does not match required {required_sha256.upper()}")
    members = archive_members(data)
    textures = []
    for requested in names:
        key = requested.lower()
        if key not in members:
            raise ValueError(f"texture archive does not contain {requested}")
        textures.append(mmp_report(key, members[key]))
    return {
        "format": "ei-particle-texture-report-v1",
        "source": {"path": str(path.resolve()), "size": len(data), "sha256": fingerprint},
        "textures": textures,
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("textures_res", type=Path)
    parser.add_argument("--name", action="append", dest="names")
    parser.add_argument("--require-jmv-reference", action="store_true")
    parser.add_argument("--require-base-reference", action="store_true")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args(argv)
    try:
        if args.require_jmv_reference and args.require_base_reference:
            raise ValueError("choose only one reference texture archive")
        required = (REFERENCE_JMV_TEXTURES_SHA256 if args.require_jmv_reference else
                    REFERENCE_BASE_TEXTURES_SHA256 if args.require_base_reference else None)
        result = report(args.textures_res, tuple(args.names or DEFAULT_NAMES), required)
        encoded = json.dumps(result, indent=2, ensure_ascii=False, sort_keys=True) + "\n"
        if args.output:
            args.output.write_text(encoded, encoding="utf-8", newline="\n")
        else:
            sys.stdout.write(encoded)
    except (OSError, ValueError, struct.error) as error:
        parser.error(str(error))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
