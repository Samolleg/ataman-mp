#!/usr/bin/env python3
"""Read-only Evil Islands character asset inventory and structural report.

The parser intentionally covers only fingerprinted container/envelope structures.
It never extracts or rewrites assets. JSON output must be an explicit path outside
all input files/directories.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import struct
import sys
from typing import Any, Iterable


RES_SIGNATURE = 0x019CE23C
MMP_SIGNATURE = 0x00504D4D
PNT3 = 0x33544E50


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest().upper()


def safe_name(raw: bytes) -> str:
    return raw.decode("cp1251", errors="replace")


def parse_res(data: bytes, depth: int = 0) -> list[dict[str, Any]]:
    if len(data) < 16:
        raise ValueError("RES header is truncated")
    magic, count, table_offset, names_size = struct.unpack_from("<4I", data)
    if magic != RES_SIGNATURE:
        raise ValueError("not an EI RES archive")
    entry_size = struct.calcsize("<iIIIHI")
    table_end = table_offset + count * entry_size
    names_end = table_end + names_size
    if table_offset < 16 or table_end > len(data) or names_end > len(data):
        raise ValueError("RES table or names block is out of bounds")
    names = data[table_end:names_end]
    result = []
    for index in range(count):
        next_index, size, offset, timestamp, name_len, name_offset = struct.unpack_from(
            "<iIIIHI", data, table_offset + index * entry_size
        )
        if name_offset + name_len > len(names) or offset + size > len(data):
            raise ValueError(f"RES member {index} is out of bounds")
        name = safe_name(names[name_offset:name_offset + name_len])
        payload = data[offset:offset + size]
        result.append({
            "index": index,
            "name": name,
            "size": size,
            "offset": offset,
            "timestamp": timestamp,
            "next_index": next_index,
            "sha256": sha256(payload),
            "structure": classify(payload, name, depth + 1),
        })
    return result


def parse_fig(data: bytes) -> dict[str, Any]:
    if len(data) < 40 or data[:4] != b"FIG8":
        raise ValueError("not a FIG8 figure")
    header = struct.unpack_from("<9I", data, 4)
    (vertex_blocks, normal_blocks, uv_count, index_count,
     component_count, morph_component_count) = header[:6]
    offset = 4 + 9 * 4 + 3 * 8 * 3 * 4 + 8 * 4
    vertex_offset = offset
    offset += vertex_blocks * 3 * 8 * 4 * 4
    normal_offset = offset
    offset += normal_blocks * 4 * 4 * 4
    uv_end = offset + uv_count * 8
    index_end = uv_end + index_count * 2
    component_end = index_end + component_count * 6
    morph_component_end = component_end + morph_component_count * 4
    if morph_component_end > len(data):
        raise ValueError("FIG8 arrays are truncated")
    uvs = [struct.unpack_from("<2f", data, offset + i * 8) for i in range(uv_count)]
    normal_floats = struct.unpack_from(f"<{normal_blocks * 16}f", data, normal_offset) \
        if normal_blocks else ()
    vertex_floats = struct.unpack_from(f"<{vertex_blocks * 96}f", data, vertex_offset) \
        if vertex_blocks else ()
    base_vertices = []
    for block in range(vertex_blocks):
        values = vertex_floats[block * 96:(block + 1) * 96]
        base_vertices.extend(tuple(values[axis * 32 + lane] for axis in range(3))
                             for lane in range(4))
    normals = []
    for block in range(normal_blocks):
        values = normal_floats[block * 16:(block + 1) * 16]
        normals.extend(tuple(values[axis * 4 + lane] for axis in range(4))
                       for lane in range(4))
    indices = list(struct.unpack_from(f"<{index_count}H", data, uv_end)) if index_count else []
    components = [
        struct.unpack_from("<3H", data, index_end + i * 6)
        for i in range(component_count)
    ]
    triangle_index_count = index_count - index_count % 3
    dangling_indices = indices[triangle_index_count:]
    invalid_component_indices = sorted({index for index in indices if index >= component_count})
    dangling_components = [
        list(components[index]) if index < component_count else None
        for index in dangling_indices
    ]
    repeated_component_triangles = 0
    repeated_vertex_triangles = 0
    zero_area_base_triangles = 0
    near_zero_area_base_triangles = 0
    for triangle_offset in range(0, triangle_index_count, 3):
        triangle_components = indices[triangle_offset:triangle_offset + 3]
        if len(set(triangle_components)) < 3:
            repeated_component_triangles += 1
        if all(index < component_count for index in triangle_components):
            triangle_vertices = [components[index][0] for index in triangle_components]
            if len(set(triangle_vertices)) < 3:
                repeated_vertex_triangles += 1
            a, b, c = (base_vertices[index] for index in triangle_vertices)
            ab = tuple(b[axis] - a[axis] for axis in range(3))
            ac = tuple(c[axis] - a[axis] for axis in range(3))
            cross = (ab[1] * ac[2] - ab[2] * ac[1],
                     ab[2] * ac[0] - ab[0] * ac[2],
                     ab[0] * ac[1] - ab[1] * ac[0])
            doubled_area = math.sqrt(sum(value * value for value in cross))
            if doubled_area == 0.0:
                zero_area_base_triangles += 1
            if doubled_area <= 1.0e-12:
                near_zero_area_base_triangles += 1
    component_limit_violations = []
    vertex_count = vertex_blocks * 4
    normal_count = normal_blocks * 4
    for index, (vertex, normal, uv) in enumerate(components):
        if vertex >= vertex_count or normal >= normal_count or uv >= uv_count:
            component_limit_violations.append({
                "component": index,
                "vertex": vertex,
                "normal": normal,
                "uv": uv,
            })
    nonfinite_uvs = sum(not (math.isfinite(u) and math.isfinite(v)) for u, v in uvs)
    nonfinite_normals = sum(not all(math.isfinite(value) for value in normal)
                            for normal in normals)
    normal_lengths = [math.sqrt(x * x + y * y + z * z)
                      for x, y, z, _ in normals
                      if math.isfinite(x) and math.isfinite(y) and math.isfinite(z)]
    zero_normals = sum(length <= 1.0e-12 for length in normal_lengths)
    referenced_normal_indices = [components[index][1]
                                 for index in indices[:triangle_index_count]
                                 if index < component_count and components[index][1] < len(normals)]
    referenced_zero_normals = sum(
        math.sqrt(sum(value * value for value in normals[index][:3])) <= 1.0e-12
        for index in referenced_normal_indices
    )
    diagnostics = []
    safe_repairs = []
    if dangling_indices:
        diagnostics.append({
            "code": "FIG_INDEX_TAIL_NOT_A_TRIANGLE",
            "severity": "warning",
            "message": (
                f"{len(dangling_indices)} trailing index value(s) cannot form a triangle; "
                "render/import policy must be explicit"
            ),
        })
        safe_repairs.append({
            "operation": "drop_trailing_non_triangle_indices",
            "count": len(dangling_indices),
            "reason": "Trailing values cannot form a triangle and are renderer-undrawable",
        })
    if repeated_vertex_triangles:
        diagnostics.append({
            "code": "FIG_REPEATED_VERTEX_TRIANGLE",
            "severity": "warning",
            "count": repeated_vertex_triangles,
            "message": "Triangles repeating a position index have zero raster area",
        })
        safe_repairs.append({
            "operation": "drop_repeated_vertex_triangles",
            "count": repeated_vertex_triangles,
            "reason": "Repeated position indices make these triangles renderer-undrawable",
        })
    if zero_area_base_triangles > repeated_vertex_triangles:
        diagnostics.append({
            "code": "FIG_OTHER_ZERO_AREA_BASE_TRIANGLE",
            "severity": "warning",
            "count": zero_area_base_triangles - repeated_vertex_triangles,
            "message": "Zero-area triangles without repeated position indices require review",
        })
    if invalid_component_indices:
        diagnostics.append({
            "code": "FIG_INDEX_COMPONENT_OUT_OF_RANGE",
            "severity": "error",
            "indices": invalid_component_indices,
        })
    if component_limit_violations:
        diagnostics.append({
            "code": "FIG_COMPONENT_VALUE_OUT_OF_RANGE",
            "severity": "error",
            "count": len(component_limit_violations),
        })
    if nonfinite_uvs:
        diagnostics.append({
            "code": "FIG_NONFINITE_UV",
            "severity": "error",
            "count": nonfinite_uvs,
        })
    if nonfinite_normals:
        diagnostics.append({
            "code": "FIG_NONFINITE_NORMAL",
            "severity": "error",
            "count": nonfinite_normals,
        })
    if zero_normals:
        diagnostics.append({
            "code": "FIG_ZERO_NORMAL",
            "severity": "warning",
            "count": zero_normals,
            "message": "Zero authored normals require an explicit importer policy",
        })
    return {
        "format": "FIG8",
        "vertex_blocks": vertex_blocks,
        "normal_blocks": normal_blocks,
        "uv_count": uv_count,
        "index_count": index_count,
        "triangle_index_count": triangle_index_count,
        "triangle_count": triangle_index_count // 3,
        "repeated_component_triangle_count": repeated_component_triangles,
        "repeated_vertex_triangle_count": repeated_vertex_triangles,
        "zero_area_base_triangle_count": zero_area_base_triangles,
        "near_zero_area_base_triangle_count": near_zero_area_base_triangles,
        "dangling_indices": dangling_indices,
        "dangling_components": dangling_components,
        "component_count": component_count,
        "invalid_component_indices": invalid_component_indices,
        "component_limit_violations": component_limit_violations,
        "morph_component_count": morph_component_count,
        "vertex_count_per_extreme": vertex_blocks * 4,
        "morph_extreme_count": 8,
        "texture_selector": header[8],
        "vertex_data_offset": vertex_offset,
        "normal_data_offset": normal_offset,
        "normal_stats": {
            "count": len(normals),
            "nonfinite_count": nonfinite_normals,
            "zero_count": zero_normals,
            "xyz_length_min": min(normal_lengths) if normal_lengths else None,
            "xyz_length_max": max(normal_lengths) if normal_lengths else None,
            "w_min": min((normal[3] for normal in normals), default=None),
            "w_max": max((normal[3] for normal in normals), default=None),
            "referenced_loop_count": len(referenced_normal_indices),
            "referenced_zero_count": referenced_zero_normals,
        },
        "nonfinite_uv_count": nonfinite_uvs,
        "uv_bounds": ([min(u for u, _ in uvs), max(u for u, _ in uvs),
                       min(v for _, v in uvs), max(v for _, v in uvs)] if uvs else None),
        "parsed_bytes": morph_component_end,
        "trailing_bytes": len(data) - morph_component_end,
        "diagnostics": diagnostics,
        "safe_repairs": safe_repairs,
    }


def read_i32(data: bytes, offset: int) -> tuple[int, int]:
    if offset + 4 > len(data):
        raise ValueError("integer is truncated")
    return struct.unpack_from("<i", data, offset)[0], offset + 4


def parse_lnk(data: bytes) -> dict[str, Any]:
    count, offset = read_i32(data, 0)
    if count < 0 or count > 1_000_000:
        raise ValueError("invalid LNK part count")
    links = []
    for _ in range(count):
        name_len, offset = read_i32(data, offset)
        if name_len <= 0 or offset + name_len > len(data):
            raise ValueError("invalid LNK child name")
        child = data[offset:offset + name_len].rstrip(b"\0").decode("cp1251", errors="replace")
        if not child or any(ord(char) < 32 for char in child):
            raise ValueError("invalid LNK child characters")
        offset += name_len
        parent_len, offset = read_i32(data, offset)
        if parent_len < 0 or offset + parent_len > len(data):
            raise ValueError("invalid LNK parent name")
        parent = None
        if parent_len:
            parent = data[offset:offset + parent_len].rstrip(b"\0").decode("cp1251", errors="replace")
            if not parent or any(ord(char) < 32 for char in parent):
                raise ValueError("invalid LNK parent characters")
            offset += parent_len
        links.append({"child": child, "parent": parent})
    roots = [item["child"] for item in links if item["parent"] is None]
    children = {item["child"] for item in links}
    parents = {item["child"]: item["parent"] for item in links}
    missing_parents = sorted({item["parent"] for item in links
                              if item["parent"] and item["parent"] not in children})
    cycles = []
    for child in sorted(children, key=str.casefold):
        chain = []
        positions = {}
        current = child
        while current in parents and parents[current] is not None:
            if current in positions:
                cycle = chain[positions[current]:] + [current]
                canonical = min((cycle[i:-1] + cycle[:i] + [cycle[i]])
                                for i in range(len(cycle) - 1))
                if canonical not in cycles:
                    cycles.append(canonical)
                break
            positions[current] = len(chain)
            chain.append(current)
            current = parents[current]
    return {
        "format": "LNK",
        "part_count": count,
        "roots": roots,
        "missing_parents": missing_parents,
        "cycles": sorted(cycles),
        "links": links,
        "parsed_bytes": offset,
        "trailing_bytes": len(data) - offset,
    }


def parse_anm(data: bytes) -> dict[str, Any]:
    offset = 0
    rotations, offset = read_i32(data, offset)
    if rotations < 0 or offset + rotations * 16 > len(data):
        raise ValueError("invalid ANM rotation channel")
    offset += rotations * 16
    translations, offset = read_i32(data, offset)
    if translations < 0 or offset + translations * 12 > len(data):
        raise ValueError("invalid ANM translation channel")
    offset += translations * 12
    morph_frames, offset = read_i32(data, offset)
    if morph_frames < 0:
        raise ValueError("invalid ANM morph frame count")
    morph_vertices = 0
    explicit_zero_morph_vertex_count = False
    if morph_frames:
        morph_vertices, offset = read_i32(data, offset)
        morph_bytes = morph_frames * morph_vertices * 12
        if morph_vertices < 0 or offset + morph_bytes > len(data):
            raise ValueError("invalid ANM morph channel")
        offset += morph_bytes
    elif len(data) - offset == 4 and struct.unpack_from("<i", data, offset)[0] == 0:
        explicit_zero_morph_vertex_count = True
        offset += 4
    return {
        "format": "ANM",
        "rotation_frames": rotations,
        "translation_frames": translations,
        "morph_frames": morph_frames,
        "morph_vertices": morph_vertices,
        "explicit_zero_morph_vertex_count": explicit_zero_morph_vertex_count,
        "logical_hz": 15,
        "parsed_bytes": offset,
        "trailing_bytes": len(data) - offset,
    }


def parse_mmp(data: bytes) -> dict[str, Any]:
    if len(data) < 20:
        raise ValueError("MMP header is truncated")
    signature, width, height, stored_size, pixel_format = struct.unpack_from("<5I", data)
    if signature != MMP_SIGNATURE:
        raise ValueError("not an MMP texture")
    return {
        "format": "MMP",
        "width": width,
        "height": height,
        "stored_size": stored_size,
        "pixel_format": f"0x{pixel_format:08X}",
        "pnt3": pixel_format == PNT3,
        "payload_within_file": 76 + stored_size <= len(data),
    }


def classify(data: bytes, logical_name: str, depth: int = 0) -> dict[str, Any]:
    lower = logical_name.lower()
    parsers = []
    if data[:4] == b"FIG8":
        parsers.append(parse_fig)
    if data[:4] == struct.pack("<I", MMP_SIGNATURE):
        parsers.append(parse_mmp)
    if data[:4] == struct.pack("<I", RES_SIGNATURE):
        if depth >= 3:
            return {"format": "RES", "note": "nested parsing depth limit reached"}
        return {"format": "RES", "members": parse_res(data, depth)}
    if lower.endswith(".lnk") or lower.endswith(".mod") or lower == "mod":
        parsers.append(parse_lnk)
    if lower.endswith(".anm") or ".anm/" in lower:
        parsers.append(parse_anm)
    for parser in parsers:
        try:
            return parser(data)
        except (ValueError, struct.error) as exc:
            return {"format": parser.__name__.removeprefix("parse_").upper(), "error": str(exc)}
    probes = []
    for probe in (parse_lnk, parse_anm):
        try:
            parsed = probe(data)
            if parsed.get("parsed_bytes") == len(data):
                parsed["heuristic_extensionless_probe"] = True
                probes.append(parsed)
        except (ValueError, struct.error, UnicodeError):
            pass
    if len(probes) == 1:
        return probes[0]
    if probes:
        return {"format": "ambiguous", "probes": probes}
    return {"format": "opaque"}


def iter_input_files(inputs: Iterable[Path]) -> Iterable[tuple[Path, Path]]:
    for source in inputs:
        resolved = source.resolve(strict=True)
        if resolved.is_file():
            yield resolved, resolved
        else:
            for path in sorted((p for p in resolved.rglob("*") if p.is_file()),
                               key=lambda p: str(p).casefold()):
                yield resolved, path


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("inputs", nargs="+", type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    inputs = [path.resolve(strict=True) for path in args.inputs]
    output = args.output.resolve()
    for source in inputs:
        if output == source or (source.is_dir() and source in output.parents):
            parser.error("--output must be outside every input path")
    records = []
    for root, path in iter_input_files(inputs):
        data = path.read_bytes()
        logical = path.name if root.is_file() else path.relative_to(root).as_posix()
        record = {
            "input_root": str(root),
            "path": str(path),
            "logical_name": logical,
            "size": len(data),
            "sha256": sha256(data),
            "structure": classify(data, logical),
        }
        records.append(record)
    report = {
        "schema": "evil-islands-character-asset-report/v1",
        "tool": str(Path(__file__).resolve()),
        "inputs": [str(path) for path in inputs],
        "records": records,
    }
    output.parent.mkdir(parents=True, exist_ok=True)
    encoded = (json.dumps(report, indent=2, ensure_ascii=False) + "\n").encode("utf-8")
    output.write_bytes(encoded)
    print(f"wrote {output} ({len(records)} records, SHA-256 {sha256(encoded)})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
