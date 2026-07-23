#!/usr/bin/env python3
"""Diagnose or safely repair renderer-undrawable records in one FIG8 member.

The default mode is read-only and writes only a JSON report. With
--apply-safe-repairs, --output must name a new staged FIG file. The tool never
updates RES archives and refuses in-place or pre-existing output files.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys
from typing import Any


RES_SIGNATURE = 0x019CE23C


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest().upper()


def read_res_member(data: bytes, requested_name: str) -> bytes:
    if len(data) < 16:
        raise ValueError("RES header is truncated")
    magic, count, table_offset, names_size = struct.unpack_from("<4I", data)
    if magic != RES_SIGNATURE:
        raise ValueError("--member requires an EI RES archive")
    entry_size = struct.calcsize("<iIIIHI")
    table_end = table_offset + count * entry_size
    names_end = table_end + names_size
    if table_offset < 16 or table_end > len(data) or names_end > len(data):
        raise ValueError("RES table or names block is out of bounds")
    names = data[table_end:names_end]
    matches = []
    for index in range(count):
        _, size, offset, _, name_len, name_offset = struct.unpack_from(
            "<iIIIHI", data, table_offset + index * entry_size
        )
        if name_offset + name_len > len(names) or offset + size > len(data):
            raise ValueError(f"RES member {index} is out of bounds")
        name = names[name_offset:name_offset + name_len].decode("cp1251", errors="strict")
        if name == requested_name:
            matches.append(data[offset:offset + size])
    if len(matches) != 1:
        raise ValueError(
            f"expected exactly one RES member named {requested_name!r}, found {len(matches)}"
        )
    return matches[0]


def repair_fig8(data: bytes) -> tuple[bytes, dict[str, Any]]:
    if len(data) < 40 or data[:4] != b"FIG8":
        raise ValueError("input is not a FIG8 member")
    header = list(struct.unpack_from("<9I", data, 4))
    vertex_blocks, normal_blocks, uv_count, index_count, component_count = header[:5]
    offset = 4 + 9 * 4 + 3 * 8 * 3 * 4 + 8 * 4
    offset += vertex_blocks * 3 * 8 * 4 * 4
    offset += normal_blocks * 4 * 4 * 4
    index_offset = offset + uv_count * 8
    index_end = index_offset + index_count * 2
    component_end = index_end + component_count * 6
    if component_end > len(data):
        raise ValueError("FIG8 index/component arrays are truncated")

    indices = list(struct.unpack_from(f"<{index_count}H", data, index_offset)) \
        if index_count else []
    components = [
        struct.unpack_from("<3H", data, index_end + index * 6)
        for index in range(component_count)
    ]
    invalid_component_refs = sorted({index for index in indices if index >= component_count})
    if invalid_component_refs:
        raise ValueError(
            "refusing repair: FIG indices reference missing components "
            + ",".join(str(index) for index in invalid_component_refs[:16])
        )
    vertex_count = vertex_blocks * 4
    invalid_vertex_components = [
        index for index, component in enumerate(components) if component[0] >= vertex_count
    ]
    if invalid_vertex_components:
        raise ValueError(
            "refusing repair: components reference missing vertices "
            + ",".join(str(index) for index in invalid_vertex_components[:16])
        )

    complete_count = index_count - index_count % 3
    trailing = indices[complete_count:]
    kept_indices: list[int] = []
    dropped_triangles: list[dict[str, Any]] = []
    for triangle in range(complete_count // 3):
        component_indices = indices[triangle * 3:triangle * 3 + 3]
        vertex_indices = [components[index][0] for index in component_indices]
        if len(set(vertex_indices)) < 3:
            dropped_triangles.append({
                "triangle": triangle,
                "component_indices": component_indices,
                "vertex_indices": vertex_indices,
            })
        else:
            kept_indices.extend(component_indices)

    operations = []
    if trailing:
        operations.append({
            "operation": "drop_trailing_non_triangle_indices",
            "count": len(trailing),
            "values": trailing,
            "reason": "Values cannot form a triangle and are renderer-undrawable",
        })
    if dropped_triangles:
        operations.append({
            "operation": "drop_repeated_vertex_triangles",
            "count": len(dropped_triangles),
            "triangles": dropped_triangles,
            "reason": "Repeated position indices make these triangles zero-area",
        })

    repaired = bytearray()
    repaired += data[:index_offset]
    if kept_indices:
        repaired += struct.pack(f"<{len(kept_indices)}H", *kept_indices)
    repaired += data[index_end:]
    struct.pack_into("<I", repaired, 4 + 3 * 4, len(kept_indices))
    result = {
        "format": "FIG8",
        "original_index_count": index_count,
        "repaired_index_count": len(kept_indices),
        "original_complete_triangle_count": complete_count // 3,
        "repaired_triangle_count": len(kept_indices) // 3,
        "operations": operations,
        "changed": bytes(repaired) != data,
        "preserved_prefix_bytes": index_offset,
        "preserved_suffix_bytes": len(data) - index_end,
    }
    return bytes(repaired), result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path, help="Copied standalone FIG8 member or RES archive")
    parser.add_argument("--member", help="Exact FIG8 member name when input is a RES archive")
    parser.add_argument("--report", required=True, type=Path)
    parser.add_argument("--output", type=Path, help="New staged FIG8 output")
    parser.add_argument("--apply-safe-repairs", action="store_true")
    args = parser.parse_args()

    source = args.input.resolve(strict=True)
    report_path = args.report.resolve()
    output = args.output.resolve() if args.output else None
    if not source.is_file():
        parser.error("input must be a file")
    if report_path == source or report_path.exists():
        parser.error("--report must be a new path distinct from input")
    if args.apply_safe_repairs != (output is not None):
        parser.error("--apply-safe-repairs and --output must be supplied together")
    if output is not None and (output == source or output.exists()):
        parser.error("--output must be a new path distinct from input")
    if output is not None and output == report_path:
        parser.error("--output and --report must be different paths")

    container_data = source.read_bytes()
    source_data = read_res_member(container_data, args.member) if args.member else container_data
    repaired_data, repair = repair_fig8(source_data)
    report: dict[str, Any] = {
        "schema": "evil-islands-fig-safe-repair/v1",
        "tool": str(Path(__file__).resolve()),
        "mode": "apply-safe-repairs" if output is not None else "diagnose-only",
        "input": {
            "path": str(source),
            "size": len(container_data),
            "sha256": sha256(container_data),
        },
        "repair": repair,
    }
    if args.member:
        report["member"] = {
            "name": args.member,
            "size": len(source_data),
            "sha256": sha256(source_data),
        }
    if output is not None:
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_bytes(repaired_data)
        report["output"] = {
            "path": str(output),
            "size": len(repaired_data),
            "sha256": sha256(repaired_data),
        }
    report_path.parent.mkdir(parents=True, exist_ok=True)
    encoded = (json.dumps(report, indent=2, ensure_ascii=False) + "\n").encode("utf-8")
    report_path.write_bytes(encoded)
    print(f"wrote {report_path} (SHA-256 {sha256(encoded)})")
    if output is not None:
        print(f"wrote {output} (SHA-256 {sha256(repaired_data)})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
