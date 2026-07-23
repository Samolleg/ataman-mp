#!/usr/bin/env python3
"""Read-only, deterministic report for authored EI MOB particle/torch records."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
import struct
import sys
from typing import Any, Iterator


TAGS = {
    0xA000: "OBJECT_DB_FILE",
    0xABD0: "WORLD_SET",
    0xABD1: "WS_WIND_DIR",
    0xABD2: "WS_WIND_STR",
    0xABD3: "WS_TIME",
    0xABD4: "WS_AMBIENT",
    0xABD5: "WS_SUN_LIGHT",
    0xB000: "OBJECT_SECTION",
    0xB001: "OBJECT",
    0xB002: "NID",
    0xB003: "OBJ_TYPE",
    0xB004: "OBJ_NAME",
    0xB005: "OBJ_INDEX",
    0xB006: "OBJ_TEMPLATE",
    0xB007: "OBJ_PRIM_TXTR",
    0xB008: "OBJ_SEC_TXTR",
    0xB009: "OBJ_POSITION",
    0xB00A: "OBJ_ROTATION",
    0xB00B: "OBJ_TEXTURE",
    0xB00C: "OBJ_COMPLECTION",
    0xB00D: "OBJ_BODYPARTS",
    0xB00E: "PARENT_TEMPLATE",
    0xB00F: "OBJ_COMMENTS",
    0xB010: "OBJ_DEF_LOGIC",
    0xB011: "OBJ_PLAYER",
    0xB012: "OBJ_PARENT_ID",
    0xB013: "OBJ_USE_IN_SCRIPT",
    0xB014: "OBJ_IS_SHADOW",
    0xB015: "OBJ_R",
    0xB016: "OBJ_QUEST_INFO",
    0xDD01: "PARTICL",
    0xDD02: "PARTICL_ID",
    0xDD03: "PARTICL_POSITION",
    0xDD04: "PARTICL_COMMENTS",
    0xDD05: "PARTICL_NAME",
    0xDD06: "PARTICL_TYPE",
    0xDD07: "PARTICL_SCALE",
    0xBBBF0000: "TORCH",
    0xBBBF0001: "TORCH_STRENGHT",
    0xBBBF0002: "TORCH_PTLINK",
    0xBBBF0003: "TORCH_SOUND",
}

WORLD_SET_TAG = 0xABD0
CONTAINERS = {0xA000, WORLD_SET_TAG, 0xB000, 0xB001, 0xDD01, 0xBBBF0000}
RECORD_TYPES = {0xDD01: "particle", 0xBBBF0000: "torch"}
DWORD_TAGS = {0xB002, 0xB003, 0xB012, 0xDD02, 0xDD06}
FLOAT_TAGS = {0xABD2, 0xABD3, 0xABD4, 0xABD5, 0xDD07, 0xBBBF0001}
PLOT_TAGS = {0xABD1, 0xB009, 0xB00C, 0xDD03, 0xBBBF0002}
QUAT_TAGS = {0xB00A}
BYTE_TAGS = {0xB011, 0xB013, 0xB014}
STRING_TAGS = {
    0xB004, 0xB006, 0xB007, 0xB008, 0xB00E, 0xB00F, 0xB016,
    0xDD04, 0xDD05, 0xBBBF0003,
}


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest().upper()


def decode_string(data: bytes) -> str:
    return data.removesuffix(b"\0").decode("cp1251", errors="replace")


def iter_nodes(data: bytes, start: int, end: int) -> Iterator[tuple[int, int, int, int]]:
    offset = start
    while offset < end:
        if offset + 8 > end:
            raise ValueError(f"truncated MOB header at 0x{offset:X}")
        tag, length = struct.unpack_from("<II", data, offset)
        if length < 8 or offset + length > end:
            raise ValueError(
                f"invalid MOB node at 0x{offset:X}: tag=0x{tag:08X} length={length} boundary=0x{end:X}"
            )
        yield tag, offset, offset + 8, offset + length
        offset += length
    if offset != end:
        raise ValueError(f"MOB node sequence ends at 0x{offset:X}, expected 0x{end:X}")


def decode_leaf(tag: int, payload: bytes) -> Any:
    if tag in DWORD_TAGS and len(payload) == 4:
        return struct.unpack("<I", payload)[0]
    if tag in FLOAT_TAGS and len(payload) == 4:
        return struct.unpack("<f", payload)[0]
    if tag in PLOT_TAGS and len(payload) == 12:
        return list(struct.unpack("<3f", payload))
    if tag in QUAT_TAGS and len(payload) == 16:
        return list(struct.unpack("<4f", payload))
    if tag in BYTE_TAGS and len(payload) == 1:
        return payload[0]
    if tag in STRING_TAGS:
        return decode_string(payload)
    return {"payload_hex": payload.hex().upper()}


def parse_record(data: bytes, tag: int, offset: int, payload_start: int, end: int,
                 include_raw: bool = False) -> dict[str, Any]:
    fields: list[dict[str, Any]] = []
    for child_tag, child_offset, child_payload, child_end in iter_nodes(data, payload_start, end):
        payload = data[child_payload:child_end]
        fields.append({
            "tag": TAGS.get(child_tag, f"UNKNOWN_0x{child_tag:08X}"),
            "tag_id": f"0x{child_tag:08X}",
            "offset": child_offset,
            "length": child_end - child_offset,
            "value": decode_leaf(child_tag, payload),
        })
    record_bytes = data[offset:end]
    result: dict[str, Any] = {
        "record_type": RECORD_TYPES[tag],
        "offset": offset,
        "length": end - offset,
        "sha256": sha256(record_bytes),
        "fields": fields,
    }
    if include_raw:
        result["raw_record_hex"] = record_bytes.hex().upper()
    return result


def collect_records(data: bytes, include_raw: bool = False) -> list[dict[str, Any]]:
    records: list[dict[str, Any]] = []

    def walk(start: int, end: int) -> None:
        for tag, offset, payload_start, node_end in iter_nodes(data, start, end):
            if tag in RECORD_TYPES:
                records.append(parse_record(data, tag, offset, payload_start, node_end, include_raw))
            elif tag in CONTAINERS:
                walk(payload_start, node_end)

    walk(0, len(data))
    return records


def collect_world_settings(data: bytes, include_raw: bool = False) -> list[dict[str, Any]]:
    settings: list[dict[str, Any]] = []

    def walk(start: int, end: int) -> None:
        for tag, offset, payload_start, node_end in iter_nodes(data, start, end):
            if tag == WORLD_SET_TAG:
                fields = []
                for child_tag, child_offset, child_payload, child_end in iter_nodes(
                        data, payload_start, node_end):
                    fields.append({
                        "tag": TAGS.get(child_tag, f"UNKNOWN_0x{child_tag:08X}"),
                        "tag_id": f"0x{child_tag:08X}",
                        "offset": child_offset,
                        "length": child_end - child_offset,
                        "value": decode_leaf(child_tag, data[child_payload:child_end]),
                    })
                record_bytes = data[offset:node_end]
                result: dict[str, Any] = {
                    "record_type": "world_settings",
                    "offset": offset,
                    "length": node_end - offset,
                    "sha256": sha256(record_bytes),
                    "fields": fields,
                }
                if include_raw:
                    result["raw_record_hex"] = record_bytes.hex().upper()
                settings.append(result)
            elif tag in CONTAINERS:
                walk(payload_start, node_end)

    walk(0, len(data))
    return settings


def field_map(record: dict[str, Any]) -> dict[str, Any]:
    return {field["tag"]: field["value"] for field in record["fields"]}


def add_associations(records: list[dict[str, Any]]) -> list[dict[str, Any]]:
    particles = [(record, field_map(record)) for record in records if record["record_type"] == "particle"]
    associations = []
    for torch in (record for record in records if record["record_type"] == "torch"):
        fields = field_map(torch)
        position = fields.get("OBJ_POSITION")
        if not isinstance(position, list):
            continue
        ranked = []
        for particle, particle_fields in particles:
            particle_position = particle_fields.get("PARTICL_POSITION")
            if not isinstance(particle_position, list):
                continue
            distance = math.dist(position, particle_position)
            ranked.append({
                "distance": distance,
                "particle_offset": particle["offset"],
                "particle_id": particle_fields.get("PARTICL_ID"),
                "particle_name": particle_fields.get("PARTICL_NAME"),
                "particle_type": particle_fields.get("PARTICL_TYPE"),
            })
        ranked.sort(key=lambda item: (item["distance"], item["particle_offset"]))
        associations.append({
            "torch_offset": torch["offset"],
            "torch_id": fields.get("NID"),
            "torch_name": fields.get("OBJ_NAME"),
            "nearest_particles": ranked[:5],
        })
    return associations


def record_id(record: dict[str, Any]) -> int | None:
    fields = field_map(record)
    value = fields.get("PARTICL_ID" if record["record_type"] == "particle" else "NID")
    return value if isinstance(value, int) else None


def report(path: Path, include_raw: bool = False,
           selected_ids: set[int] | None = None) -> dict[str, Any]:
    data = path.read_bytes()
    records = collect_records(data, include_raw)
    world_settings = collect_world_settings(data, include_raw)
    all_counts = {
        "particle": sum(record["record_type"] == "particle" for record in records),
        "torch": sum(record["record_type"] == "torch" for record in records),
    }
    if selected_ids is not None:
        records = [record for record in records if record_id(record) in selected_ids]
    return {
        "format": "ei-particle-mob-report-v2",
        "source": {"path": str(path.resolve()), "size": len(data), "sha256": sha256(data)},
        "counts": all_counts,
        "selection": sorted(selected_ids) if selected_ids is not None else None,
        "records": records,
        "world_settings": world_settings,
        "torch_particle_proximity": add_associations(records),
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mob", type=Path)
    parser.add_argument("--output", type=Path, help="JSON destination; stdout when omitted")
    parser.add_argument("--raw-hex", action="store_true", help="include exact complete record bytes")
    parser.add_argument("--record-id", type=int, action="append",
                        help="include only selected authored NID/PARTICL_ID; repeatable")
    args = parser.parse_args(argv)
    try:
        result = report(args.mob, args.raw_hex, set(args.record_id) if args.record_id else None)
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
