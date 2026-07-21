"""Compare one FIG member semantically through the selected ei_figer decoder."""

from __future__ import annotations

import hashlib
import importlib
import json
import os
from pathlib import Path

import bpy
import numpy as np


def required(name: str) -> str:
    value = os.environ.get(name)
    if not value:
        raise RuntimeError(f"{name} is required")
    return value


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def numeric_diff(left: object, right: object) -> dict[str, object]:
    a = np.asarray(left)
    b = np.asarray(right)
    result: dict[str, object] = {"left_shape": list(a.shape), "right_shape": list(b.shape)}
    if a.size <= 64 and b.size <= 64:
        result["left_values"] = a.tolist()
        result["right_values"] = b.tolist()
    if a.shape != b.shape:
        result.update({"equal": False, "mismatch_count": None, "max_abs_delta": None})
        return result
    equal = np.array_equal(a, b)
    mismatch = np.count_nonzero(a != b)
    result.update({"equal": bool(equal), "mismatch_count": int(mismatch)})
    if mismatch:
        samples = []
        for coordinate in np.argwhere(a != b)[:8]:
            key = tuple(int(value) for value in coordinate)
            samples.append({
                "index": list(key),
                "left": a[key].item(),
                "right": b[key].item(),
            })
        result["mismatch_samples"] = samples
    if a.size and np.issubdtype(a.dtype, np.number) and np.issubdtype(b.dtype, np.number):
        absolute_delta = np.abs(a.astype(np.float64) - b.astype(np.float64))
        result["max_abs_delta"] = float(np.max(absolute_delta))
        result["abs_delta_counts"] = {
            str(threshold): int(np.count_nonzero(absolute_delta > threshold))
            for threshold in (1.0e-7, 1.0e-6, 1.0e-5, 1.0e-4, 1.0e-3)
        }
    else:
        result["max_abs_delta"] = None
    return result


left_path = Path(required("EI_FIG_LEFT_RES")).resolve()
right_path = Path(required("EI_FIG_RIGHT_RES")).resolve()
member = required("EI_FIG_MEMBER")
mode = os.environ.get("EI_FIG_MODE", "VANILLA")
report_path = Path(required("EI_FIG_REPORT")).resolve()
report_path.parent.mkdir(parents=True, exist_ok=True)

addon = importlib.import_module("ei_figer")
addon.register()
if hasattr(bpy.context.scene, "item_container_set"):
    bpy.context.scene.item_container_set = mode

resfile = importlib.import_module("ei_figer.resfile")
figure = importlib.import_module("ei_figer.figure")


def read(path: Path):
    archive = resfile.ResFile(str(path))
    with archive.open(member, "r") as stream:
        raw = stream.read()
    parsed = figure.CFigure()
    parsed.read_fig(member, raw)
    return raw, parsed


left_raw, left = read(left_path)
right_raw, right = read(right_path)


def expanded_corners(parsed, complete_triangles_only=False, renderable_only=False):
    indices = np.asarray(parsed.indicies)
    if complete_triangles_only or renderable_only:
        indices = indices[: len(indices) - len(indices) % 3]
    if renderable_only and len(indices):
        triangle_indices = indices.reshape((-1, 3))
        triangle_components = np.asarray(parsed.v_c)[triangle_indices]
        triangle_vertices = triangle_components[:, :, 0]
        keep = ((triangle_vertices[:, 0] != triangle_vertices[:, 1]) &
                (triangle_vertices[:, 0] != triangle_vertices[:, 2]) &
                (triangle_vertices[:, 1] != triangle_vertices[:, 2]))
        indices = triangle_indices[keep].reshape(-1)
    components = np.asarray(parsed.v_c)[indices]
    vertex_indices = components[:, 0]
    normal_indices = components[:, 1]
    uv_indices = components[:, 2]
    return {
        "vertex_indices": vertex_indices,
        "positions": np.asarray(parsed.verts)[:, vertex_indices, :],
        "normals": np.asarray(parsed.normals)[normal_indices],
        "uv": np.asarray(parsed.t_coords)[uv_indices],
    }


left_corners = expanded_corners(left)
right_corners = expanded_corners(right)
left_drawable = expanded_corners(left, complete_triangles_only=True)
right_drawable = expanded_corners(right, complete_triangles_only=True)
left_renderable = expanded_corners(left, renderable_only=True)
right_renderable = expanded_corners(right, renderable_only=True)
fields = {
    "header": numeric_diff(left.header.get_array(), right.header.get_array()),
    "center": numeric_diff(left.center, right.center),
    "fmin": numeric_diff(left.fmin, right.fmin),
    "fmax": numeric_diff(left.fmax, right.fmax),
    "radius": numeric_diff(left.radius, right.radius),
    "vertices": numeric_diff(left.verts, right.verts),
    "normals": numeric_diff(left.normals, right.normals),
    "uv": numeric_diff(left.t_coords, right.t_coords),
    "indices": numeric_diff(left.indicies, right.indicies),
    "vertex_components": numeric_diff(left.v_c, right.v_c),
    "morph_components": numeric_diff(left.m_c, right.m_c),
    "corner_vertex_indices": numeric_diff(left_corners["vertex_indices"], right_corners["vertex_indices"]),
    "corner_positions": numeric_diff(left_corners["positions"], right_corners["positions"]),
    "corner_normals": numeric_diff(left_corners["normals"], right_corners["normals"]),
    "corner_uv": numeric_diff(left_corners["uv"], right_corners["uv"]),
    "drawable_corner_vertex_indices": numeric_diff(
        left_drawable["vertex_indices"], right_drawable["vertex_indices"]
    ),
    "drawable_corner_positions": numeric_diff(
        left_drawable["positions"], right_drawable["positions"]
    ),
    "drawable_corner_normals": numeric_diff(
        left_drawable["normals"], right_drawable["normals"]
    ),
    "drawable_corner_uv": numeric_diff(left_drawable["uv"], right_drawable["uv"]),
    "renderable_corner_vertex_indices": numeric_diff(
        left_renderable["vertex_indices"], right_renderable["vertex_indices"]
    ),
    "renderable_corner_positions": numeric_diff(
        left_renderable["positions"], right_renderable["positions"]
    ),
    "renderable_corner_normals": numeric_diff(
        left_renderable["normals"], right_renderable["normals"]
    ),
    "renderable_corner_uv": numeric_diff(left_renderable["uv"], right_renderable["uv"]),
}
semantic_fields = (
    "header",
    "center",
    "fmin",
    "fmax",
    "radius",
    "morph_components",
    "corner_vertex_indices",
    "corner_positions",
    "corner_normals",
    "corner_uv",
)
semantic_equal = all(fields[name]["equal"] for name in semantic_fields)
drawable_semantic_fields = (
    "center",
    "fmin",
    "fmax",
    "radius",
    "drawable_corner_vertex_indices",
    "drawable_corner_positions",
    "drawable_corner_normals",
    "drawable_corner_uv",
)
drawable_semantic_equal = all(fields[name]["equal"] for name in drawable_semantic_fields)
renderable_semantic_fields = (
    "center",
    "fmin",
    "fmax",
    "radius",
    "renderable_corner_vertex_indices",
    "renderable_corner_positions",
    "renderable_corner_normals",
    "renderable_corner_uv",
)
renderable_semantic_equal = all(fields[name]["equal"] for name in renderable_semantic_fields)
report = {
    "schema": 1,
    "status": "ok",
    "member": member,
    "mode": mode,
    "left": {"res": str(left_path), "size": len(left_raw), "sha256": digest(left_raw)},
    "right": {"res": str(right_path), "size": len(right_raw), "sha256": digest(right_raw)},
    "byte_equal": left_raw == right_raw,
    "semantic_equal": semantic_equal,
    "drawable_semantic_equal": drawable_semantic_equal,
    "renderable_semantic_equal": renderable_semantic_equal,
    "index_tail": {
        "left": np.asarray(left.indicies)[len(left.indicies) - len(left.indicies) % 3:].tolist()
        if len(left.indicies) % 3 else [],
        "right": np.asarray(right.indicies)[len(right.indicies) - len(right.indicies) % 3:].tolist()
        if len(right.indicies) % 3 else [],
    },
    "fields": fields,
}
report_path.write_text(json.dumps(report, indent=2, sort_keys=True), encoding="utf-8")
print("EI_FIG_SEMANTIC_DIFF", report_path, semantic_equal)
