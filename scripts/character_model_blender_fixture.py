"""Deterministic ei_figer model import/export fixture for headless Blender.

Required environment variables:
  EI_FIXTURE_RES       copied, immutable input RES
  EI_FIXTURE_MODEL     model/member stem to import
  EI_FIXTURE_REPORT    JSON output outside the input tree

Optional:
  EI_FIXTURE_PROFILE   BASE or JMV; deterministically selects the import mode
  EI_FIXTURE_MODE      explicit VANILLA or JABAIS_VOUX override/provenance
  EI_FIXTURE_MESH_MASK comma-separated modern-addon mesh include list
  EI_FIXTURE_VISIBLE_COLLECTION isolate one morph collection in saved scenes
  EI_FIXTURE_HIDE_OBJECTS comma-separated local object names to hide
  EI_FIXTURE_BLEND     save the imported inspection scene
  EI_FIXTURE_EXPORT    pre-created candidate RES; never the input path

The caller must copy the candidate before requesting export. This script will
not create a RES archive or write to the source archive implicitly.
"""

from __future__ import annotations

import hashlib
import importlib
import json
import math
import os
from pathlib import Path
import struct
import traceback

import bpy


def required_path(name: str) -> Path:
    value = os.environ.get(name)
    if not value:
        raise RuntimeError(f"{name} is required")
    return Path(value).resolve()


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def uv_bounds(mesh: bpy.types.Mesh) -> list[float] | None:
    layer = mesh.uv_layers.active
    if layer is None or not layer.data:
        return None
    us = [loop.uv.x for loop in layer.data]
    vs = [loop.uv.y for loop in layer.data]
    return [min(us), max(us), min(vs), max(vs)]


def packed_hash(format_code: str, rows: list[tuple[object, ...]]) -> str:
    digest = hashlib.sha256()
    packer = struct.Struct("<" + format_code)
    for row in rows:
        digest.update(packer.pack(*row))
    return digest.hexdigest()


def mesh_record(obj: bpy.types.Object) -> dict[str, object]:
    mesh = obj.data
    if hasattr(mesh, "calc_normals_split"):
        mesh.calc_normals_split()
    loop_normals = [tuple(loop.normal) for loop in mesh.loops]
    record: dict[str, object] = {
        "name": obj.name,
        "parent": obj.parent.name if obj.parent else None,
        "location": list(obj.location),
        "rotation_mode": obj.rotation_mode,
        "rotation_quaternion": list(obj.rotation_quaternion),
        "scale": list(obj.scale),
        "vertices": len(mesh.vertices),
        "vertex_position_sha256": packed_hash(
            "fff", [tuple(vertex.co) for vertex in mesh.vertices]
        ),
        "edges": len(mesh.edges),
        "polygons": len(mesh.polygons),
        "loops": len(mesh.loops),
        "loop_vertex_index_sha256": packed_hash(
            "I", [(loop.vertex_index,) for loop in mesh.loops]
        ),
        "loop_normal_sha256": packed_hash("fff", loop_normals),
        "loop_normal_sample": [list(value) for value in loop_normals[:8]],
        "uv_layers": [layer.name for layer in mesh.uv_layers],
        "active_uv_bounds": uv_bounds(mesh),
        "materials": [slot.name if slot else None for slot in mesh.materials],
        "collections": sorted(collection.name for collection in obj.users_collection),
    }
    active_uv = mesh.uv_layers.active
    record["loop_uv_sha256"] = (
        packed_hash("ff", [tuple(loop.uv) for loop in active_uv.data]) if active_uv else None
    )
    if hasattr(obj, "imported_item_group"):
        record["imported_item_group"] = obj.imported_item_group
    return record


def add_vector(value: object, delta: list[float]) -> list[float]:
    if len(delta) != len(value):
        raise RuntimeError(f"Delta length {len(delta)} does not match target length {len(value)}")
    before = [float(component) for component in value]
    for index, component in enumerate(delta):
        value[index] += float(component)
    return before


def apply_edit_spec(path: Path) -> dict[str, object]:
    spec = json.loads(path.read_text(encoding="utf-8"))
    if spec.get("schema") != 1 or not isinstance(spec.get("operations"), list):
        raise RuntimeError("Edit spec must have schema 1 and an operations array")
    applied: list[dict[str, object]] = []
    for operation in spec["operations"]:
        kind = operation.get("type")
        object_name = operation.get("object")
        obj = bpy.data.objects.get(object_name)
        if obj is None or obj.type != "MESH":
            raise RuntimeError(f"Edit target is not a mesh object: {object_name}")
        delta = operation.get("delta")
        if kind in {"vertex_delta", "vertex_group_delta", "uv_delta", "location_delta"} and not isinstance(delta, list):
            raise RuntimeError(f"Edit operation requires a numeric delta: {operation}")
        result: dict[str, object] = {"type": kind, "object": object_name}
        if delta is not None:
            result["delta"] = delta
        if kind == "vertex_delta":
            index = int(operation["index"])
            if index < 0 or index >= len(obj.data.vertices):
                raise RuntimeError(f"Vertex index out of range for {object_name}: {index}")
            target = obj.data.vertices[index].co
            result.update({"index": index, "before": add_vector(target, delta), "after": list(target)})
            obj.data.update()
        elif kind == "vertex_group_delta":
            indices = operation.get("indices")
            if (not isinstance(indices, list) or not indices
                    or any(not isinstance(index, int) for index in indices)
                    or len(indices) != len(set(indices))):
                raise RuntimeError("vertex_group_delta requires unique integer indices")
            changes = []
            for index in indices:
                if index < 0 or index >= len(obj.data.vertices):
                    raise RuntimeError(f"Vertex index out of range for {object_name}: {index}")
                target = obj.data.vertices[index].co
                changes.append({
                    "index": index,
                    "before": add_vector(target, delta),
                    "after": list(target),
                })
            result.update({"indices": indices, "changes": changes})
            obj.data.update()
        elif kind == "uv_delta":
            layer = obj.data.uv_layers.active
            index = int(operation["index"])
            if layer is None or index < 0 or index >= len(layer.data):
                raise RuntimeError(f"UV loop index out of range for {object_name}: {index}")
            target = layer.data[index].uv
            result.update({"index": index, "before": add_vector(target, delta), "after": list(target)})
            obj.data.update()
        elif kind == "location_delta":
            target = obj.location
            result.update({"before": add_vector(target, delta), "after": list(target)})
        elif kind == "mesh_scale":
            factors = operation.get("factors")
            pivot = operation.get("pivot", [0.0, 0.0, 0.0])
            if (not isinstance(factors, list) or len(factors) != 3 or
                    not isinstance(pivot, list) or len(pivot) != 3):
                raise RuntimeError("mesh_scale requires three factors and a three-value pivot")
            factors = [float(value) for value in factors]
            pivot = [float(value) for value in pivot]
            if (not all(math.isfinite(value) and value > 0.0 for value in factors) or
                    not all(math.isfinite(value) for value in pivot)):
                raise RuntimeError("mesh_scale factors must be finite/positive and pivot finite")
            bounds_before = [
                [min(vertex.co[axis] for vertex in obj.data.vertices),
                 max(vertex.co[axis] for vertex in obj.data.vertices)]
                for axis in range(3)
            ]
            for vertex in obj.data.vertices:
                for axis in range(3):
                    vertex.co[axis] = pivot[axis] + (
                        vertex.co[axis] - pivot[axis]
                    ) * factors[axis]
            obj.data.update()
            bounds_after = [
                [min(vertex.co[axis] for vertex in obj.data.vertices),
                 max(vertex.co[axis] for vertex in obj.data.vertices)]
                for axis in range(3)
            ]
            result.update({
                "factors": factors,
                "pivot": pivot,
                "vertex_count": len(obj.data.vertices),
                "bounds_before": bounds_before,
                "bounds_after": bounds_after,
            })
        elif kind == "mesh_axis_taper_scale":
            axis = int(operation.get("axis", 0))
            direction = float(operation.get("direction", 1.0))
            factors = operation.get("factors")
            pivot = operation.get("pivot", [0.0, 0.0, 0.0])
            preserve_fraction = float(operation.get("preserve_fraction", 0.2))
            full_fraction = float(operation.get("full_fraction", 0.55))
            if (axis not in {0, 1, 2} or direction not in {-1.0, 1.0} or
                    not isinstance(factors, list) or len(factors) != 3 or
                    not isinstance(pivot, list) or len(pivot) != 3):
                raise RuntimeError(
                    "mesh_axis_taper_scale requires axis 0..2, direction +/-1, "
                    "three factors, and a three-value pivot"
                )
            factors = [float(value) for value in factors]
            pivot = [float(value) for value in pivot]
            if (not all(math.isfinite(value) and value > 0.0 for value in factors) or
                    not all(math.isfinite(value) for value in pivot) or
                    not (0.0 <= preserve_fraction < full_fraction <= 1.0)):
                raise RuntimeError(
                    "mesh_axis_taper_scale values must be finite/positive and "
                    "satisfy 0 <= preserve_fraction < full_fraction <= 1"
                )
            directed = [direction * float(vertex.co[axis]) for vertex in obj.data.vertices]
            low = min(directed)
            high = max(directed)
            span = high - low
            if not math.isfinite(span) or span <= 0.0:
                raise RuntimeError(f"Degenerate taper axis for {object_name}")
            anchor = direction * low
            unchanged_count = 0
            full_count = 0
            transition_count = 0
            for vertex, longitudinal in zip(obj.data.vertices, directed):
                fraction = (longitudinal - low) / span
                if fraction <= preserve_fraction:
                    weight = 0.0
                    unchanged_count += 1
                elif fraction >= full_fraction:
                    weight = 1.0
                    full_count += 1
                else:
                    linear = ((fraction - preserve_fraction) /
                              (full_fraction - preserve_fraction))
                    weight = linear * linear * (3.0 - 2.0 * linear)
                    transition_count += 1
                effective = [1.0 + (factor - 1.0) * weight for factor in factors]
                for component in range(3):
                    component_pivot = anchor if component == axis else pivot[component]
                    vertex.co[component] = component_pivot + (
                        vertex.co[component] - component_pivot
                    ) * effective[component]
            obj.data.update()
            result.update({
                "axis": axis,
                "direction": direction,
                "factors": factors,
                "pivot": pivot,
                "preserve_fraction": preserve_fraction,
                "full_fraction": full_fraction,
                "directed_min": low,
                "directed_max": high,
                "axis_anchor": anchor,
                "vertex_count": len(obj.data.vertices),
                "unchanged_vertex_count": unchanged_count,
                "transition_vertex_count": transition_count,
                "full_scale_vertex_count": full_count,
            })
        elif kind == "mesh_axis_profile_scale":
            axis = int(operation.get("axis", 0))
            direction = float(operation.get("direction", 1.0))
            pivot = operation.get("pivot", [0.0, 0.0, 0.0])
            profile = operation.get("profile")
            if (axis not in {0, 1, 2} or direction not in {-1.0, 1.0} or
                    not isinstance(pivot, list) or len(pivot) != 3 or
                    not isinstance(profile, list) or len(profile) < 2):
                raise RuntimeError(
                    "mesh_axis_profile_scale requires axis 0..2, direction +/-1, "
                    "a three-value pivot, and at least two profile knots"
                )
            pivot = [float(value) for value in pivot]
            knots = []
            for knot in profile:
                if (not isinstance(knot, dict) or
                        not isinstance(knot.get("factors"), list) or
                        len(knot["factors"]) != 3):
                    raise RuntimeError("Each profile knot requires fraction and three factors")
                fraction = float(knot["fraction"])
                factors = [float(value) for value in knot["factors"]]
                if (not math.isfinite(fraction) or
                        not all(math.isfinite(value) and value > 0.0 for value in factors)):
                    raise RuntimeError("Profile fractions/factors must be finite and factors positive")
                knots.append((fraction, factors))
            if (knots[0][0] != 0.0 or knots[-1][0] != 1.0 or
                    any(left[0] >= right[0] for left, right in zip(knots, knots[1:])) or
                    not all(math.isfinite(value) for value in pivot)):
                raise RuntimeError("Profile knots must increase strictly from fraction 0 to 1")
            directed = [direction * float(vertex.co[axis]) for vertex in obj.data.vertices]
            low = min(directed)
            high = max(directed)
            span = high - low
            if not math.isfinite(span) or span <= 0.0:
                raise RuntimeError(f"Degenerate profile axis for {object_name}")
            knot_counts = [0 for _ in knots]
            for vertex, longitudinal in zip(obj.data.vertices, directed):
                fraction = min(1.0, max(0.0, (longitudinal - low) / span))
                segment = next(
                    index for index in range(len(knots) - 1)
                    if fraction <= knots[index + 1][0]
                )
                left_fraction, left_factors = knots[segment]
                right_fraction, right_factors = knots[segment + 1]
                linear = (fraction - left_fraction) / (right_fraction - left_fraction)
                weight = linear * linear * (3.0 - 2.0 * linear)
                factors = [
                    left + (right - left) * weight
                    for left, right in zip(left_factors, right_factors)
                ]
                for component in range(3):
                    vertex.co[component] = pivot[component] + (
                        vertex.co[component] - pivot[component]
                    ) * factors[component]
                knot_counts[segment] += 1
            obj.data.update()
            result.update({
                "axis": axis,
                "direction": direction,
                "pivot": pivot,
                "profile": [
                    {"fraction": fraction, "factors": factors}
                    for fraction, factors in knots
                ],
                "directed_min": low,
                "directed_max": high,
                "vertex_count": len(obj.data.vertices),
                "segment_vertex_counts": knot_counts[:-1],
            })
        elif kind == "mesh_bilateral_shoulder_smooth":
            lateral_axis = int(operation.get("lateral_axis", 0))
            vertical_axis = int(operation.get("vertical_axis", 2))
            lateral_start = float(operation.get("lateral_start_fraction", 0.45))
            vertical_start = float(operation.get("vertical_start_fraction", 0.58))
            vertical_full = float(operation.get("vertical_full_fraction", 0.80))
            max_drop_fraction = float(operation.get("max_drop_fraction", 0.04))
            if (lateral_axis not in {0, 1, 2} or vertical_axis not in {0, 1, 2} or
                    lateral_axis == vertical_axis or
                    not (0.0 <= lateral_start < 1.0) or
                    not (0.0 <= vertical_start < vertical_full <= 1.0) or
                    not math.isfinite(max_drop_fraction) or max_drop_fraction <= 0.0):
                raise RuntimeError(
                    "mesh_bilateral_shoulder_smooth requires distinct valid axes, "
                    "0 <= lateral_start < 1, 0 <= vertical_start < vertical_full <= 1, "
                    "and a positive finite max_drop_fraction"
                )
            lateral_extent = max(abs(float(vertex.co[lateral_axis])) for vertex in obj.data.vertices)
            vertical_min = min(float(vertex.co[vertical_axis]) for vertex in obj.data.vertices)
            vertical_max = max(float(vertex.co[vertical_axis]) for vertex in obj.data.vertices)
            vertical_span = vertical_max - vertical_min
            if lateral_extent <= 0.0 or vertical_span <= 0.0:
                raise RuntimeError(f"Degenerate shoulder bounds for {object_name}")
            max_drop = vertical_span * max_drop_fraction
            changed = 0
            maximum_applied = 0.0
            for vertex in obj.data.vertices:
                lateral_fraction = abs(float(vertex.co[lateral_axis])) / lateral_extent
                vertical_fraction = (
                    (float(vertex.co[vertical_axis]) - vertical_min) / vertical_span
                )
                if lateral_fraction <= lateral_start or vertical_fraction <= vertical_start:
                    continue
                lateral_linear = (lateral_fraction - lateral_start) / (1.0 - lateral_start)
                lateral_weight = lateral_linear * lateral_linear * (3.0 - 2.0 * lateral_linear)
                if vertical_fraction >= vertical_full:
                    vertical_weight = 1.0
                else:
                    vertical_linear = (
                        (vertical_fraction - vertical_start) /
                        (vertical_full - vertical_start)
                    )
                    vertical_weight = vertical_linear * vertical_linear * (3.0 - 2.0 * vertical_linear)
                drop = max_drop * lateral_weight * vertical_weight
                vertex.co[vertical_axis] -= drop
                changed += 1
                maximum_applied = max(maximum_applied, drop)
            obj.data.update()
            result.update({
                "lateral_axis": lateral_axis,
                "vertical_axis": vertical_axis,
                "lateral_start_fraction": lateral_start,
                "vertical_start_fraction": vertical_start,
                "vertical_full_fraction": vertical_full,
                "max_drop_fraction": max_drop_fraction,
                "max_drop": max_drop,
                "maximum_applied_drop": maximum_applied,
                "vertex_count": len(obj.data.vertices),
                "changed_vertex_count": changed,
            })
        elif kind == "mesh_proximal_cap_smooth":
            axis = int(operation.get("axis", 0))
            direction = float(operation.get("direction", 1.0))
            vertical_axis = int(operation.get("vertical_axis", 2))
            preserve = float(operation.get("preserve_fraction", 0.03))
            full_start = float(operation.get("full_start_fraction", 0.12))
            full_end = float(operation.get("full_end_fraction", 0.20))
            end = float(operation.get("end_fraction", 0.42))
            vertical_start = float(operation.get("vertical_start_fraction", 0.58))
            vertical_full = float(operation.get("vertical_full_fraction", 0.85))
            max_drop_fraction = float(operation.get("max_drop_fraction", 0.11))
            if (axis not in {0, 1, 2} or vertical_axis not in {0, 1, 2} or
                    axis == vertical_axis or direction not in {-1.0, 1.0} or
                    not (0.0 <= preserve < full_start <= full_end < end <= 1.0) or
                    not (0.0 <= vertical_start < vertical_full <= 1.0) or
                    not math.isfinite(max_drop_fraction) or max_drop_fraction <= 0.0):
                raise RuntimeError(
                    "mesh_proximal_cap_smooth requires distinct valid axes, direction +/-1, "
                    "0 <= preserve < full_start <= full_end < end <= 1, "
                    "0 <= vertical_start < vertical_full <= 1, and a positive finite drop"
                )
            directed = [direction * float(vertex.co[axis]) for vertex in obj.data.vertices]
            low, high = min(directed), max(directed)
            span = high - low
            vertical_min = min(float(vertex.co[vertical_axis]) for vertex in obj.data.vertices)
            vertical_max = max(float(vertex.co[vertical_axis]) for vertex in obj.data.vertices)
            vertical_span = vertical_max - vertical_min
            if span <= 0.0 or vertical_span <= 0.0:
                raise RuntimeError(f"Degenerate proximal-cap bounds for {object_name}")
            max_drop = vertical_span * max_drop_fraction
            changed = 0
            maximum_applied = 0.0
            for vertex, value in zip(obj.data.vertices, directed):
                axial_fraction = (value - low) / span
                if axial_fraction <= preserve or axial_fraction >= end:
                    continue
                if axial_fraction < full_start:
                    linear = (axial_fraction - preserve) / (full_start - preserve)
                    axial_weight = linear * linear * (3.0 - 2.0 * linear)
                elif axial_fraction <= full_end:
                    axial_weight = 1.0
                else:
                    linear = (end - axial_fraction) / (end - full_end)
                    axial_weight = linear * linear * (3.0 - 2.0 * linear)
                vertical_fraction = (
                    (float(vertex.co[vertical_axis]) - vertical_min) / vertical_span
                )
                if vertical_fraction <= vertical_start:
                    continue
                if vertical_fraction >= vertical_full:
                    vertical_weight = 1.0
                else:
                    linear = (
                        (vertical_fraction - vertical_start) /
                        (vertical_full - vertical_start)
                    )
                    vertical_weight = linear * linear * (3.0 - 2.0 * linear)
                drop = max_drop * axial_weight * vertical_weight
                vertex.co[vertical_axis] -= drop
                changed += 1
                maximum_applied = max(maximum_applied, drop)
            obj.data.update()
            result.update({
                "axis": axis,
                "direction": direction,
                "vertical_axis": vertical_axis,
                "preserve_fraction": preserve,
                "full_start_fraction": full_start,
                "full_end_fraction": full_end,
                "end_fraction": end,
                "vertical_start_fraction": vertical_start,
                "vertical_full_fraction": vertical_full,
                "max_drop_fraction": max_drop_fraction,
                "max_drop": max_drop,
                "maximum_applied_drop": maximum_applied,
                "vertex_count": len(obj.data.vertices),
                "changed_vertex_count": changed,
            })
        else:
            raise RuntimeError(f"Unsupported edit operation: {kind}")
        applied.append(result)
    return {
        "path": str(path),
        "sha256": sha256(path),
        "operations": applied,
    }


source_res = required_path("EI_FIXTURE_RES")
report_path = required_path("EI_FIXTURE_REPORT")
model_name = os.environ.get("EI_FIXTURE_MODEL")
if not model_name:
    raise RuntimeError("EI_FIXTURE_MODEL is required")
source_profile = os.environ.get("EI_FIXTURE_PROFILE")
if source_profile is not None:
    source_profile = source_profile.upper()
if source_profile not in {None, "BASE", "JMV"}:
    raise RuntimeError(f"Unsupported EI_FIXTURE_PROFILE: {source_profile}")
explicit_mode = os.environ.get("EI_FIXTURE_MODE")
if explicit_mode not in {None, "VANILLA", "JABAIS_VOUX"}:
    raise RuntimeError(f"Unsupported EI_FIXTURE_MODE: {explicit_mode}")
profile_mode = {"BASE": "VANILLA", "JMV": "JABAIS_VOUX"}.get(source_profile)
if explicit_mode and profile_mode and explicit_mode != profile_mode:
    raise RuntimeError(
        f"EI_FIXTURE_PROFILE={source_profile} requires {profile_mode}, not {explicit_mode}"
    )
mode = explicit_mode or profile_mode
if mode is None:
    raise RuntimeError("Set EI_FIXTURE_PROFILE to BASE/JMV or set EI_FIXTURE_MODE explicitly")
if mode not in {"VANILLA", "JABAIS_VOUX"}:
    raise RuntimeError(f"Unsupported EI_FIXTURE_MODE: {mode}")
if not source_res.is_file():
    raise RuntimeError(f"Input RES does not exist: {source_res}")

report_path.parent.mkdir(parents=True, exist_ok=True)
report: dict[str, object] = {
    "schema": 1,
    "status": "failed",
    "input": {
        "res": str(source_res),
        "sha256": sha256(source_res),
        "size": source_res.stat().st_size,
        "model": model_name,
        "source_profile": source_profile,
        "mode": mode,
    },
    "blender": {"version": bpy.app.version_string, "version_tuple": list(bpy.app.version)},
}

try:
    # Factory startup contains the demonstration cube. Remove all startup data
    # so mesh counts and exports describe only the requested EI model.
    for obj in list(bpy.data.objects):
        bpy.data.objects.remove(obj, do_unlink=True)
    for collection in list(bpy.data.collections):
        bpy.data.collections.remove(collection)

    addon = importlib.import_module("ei_figer")
    addon.register()
    scene = bpy.context.scene
    scene.res_file = str(source_res)
    scene.figmodel_name = model_name
    if hasattr(scene, "item_container_set"):
        scene.item_container_set = mode

    mesh_mask = os.environ.get("EI_FIXTURE_MESH_MASK")
    import_args = {"mesh_mask": mesh_mask} if mesh_mask else {}
    result = bpy.ops.object.model_import(**import_args)
    visible_collection = os.environ.get("EI_FIXTURE_VISIBLE_COLLECTION")
    if visible_collection:
        if bpy.data.collections.get(visible_collection) is None:
            raise RuntimeError(f"Visible collection was not imported: {visible_collection}")
        for collection in bpy.data.collections:
            collection.hide_viewport = collection.name != visible_collection
            collection.hide_render = collection.name != visible_collection
    hidden_names = {
        name.strip() for name in os.environ.get("EI_FIXTURE_HIDE_OBJECTS", "").split(",")
        if name.strip()
    }
    for name in hidden_names:
        obj = bpy.data.objects.get(name)
        if obj is None:
            raise RuntimeError(f"Requested hidden object was not imported: {name}")
        obj.hide_set(True)
        obj.hide_render = True
    meshes = sorted(
        (mesh_record(obj) for obj in bpy.data.objects if obj.type == "MESH"),
        key=lambda item: item["name"],
    )
    report["import"] = {
        "operator_result": sorted(result),
        "meshes": meshes,
        "mesh_count": len(meshes),
        "collections": sorted(collection.name for collection in bpy.data.collections),
        "scene_mode": getattr(scene, "item_container_set", None),
        "mesh_mask": mesh_mask,
        "visible_collection": visible_collection,
        "hidden_objects": sorted(hidden_names),
    }
    if "FINISHED" not in result or not meshes:
        raise RuntimeError(f"Model import did not produce meshes: {sorted(result)}")

    scene_provenance = {
        "schema": 1,
        "source_res": str(source_res),
        "source_res_sha256": report["input"]["sha256"],
        "source_profile": source_profile,
        "import_mode": mode,
        "model": model_name,
        "blender": bpy.app.version_string,
        "warning": "Staged copied resources only; never export into a live game directory.",
    }
    scene["ei_pipeline_provenance_json"] = json.dumps(scene_provenance, sort_keys=True)
    readme = bpy.data.texts.get("EI_PIPELINE_README") or bpy.data.texts.new("EI_PIPELINE_README")
    readme.clear()
    readme.write(json.dumps(scene_provenance, indent=2, sort_keys=True))

    edit_value = os.environ.get("EI_FIXTURE_EDIT")
    if edit_value:
        edit_path = Path(edit_value).resolve()
        if not edit_path.is_file():
            raise RuntimeError(f"EI_FIXTURE_EDIT does not exist: {edit_path}")
        report["edit"] = apply_edit_spec(edit_path)
        report["edited_meshes"] = sorted(
            (mesh_record(obj) for obj in bpy.data.objects if obj.type == "MESH"),
            key=lambda item: item["name"],
        )

    blend_value = os.environ.get("EI_FIXTURE_BLEND")
    if blend_value:
        blend_path = Path(blend_value).resolve()
        blend_path.parent.mkdir(parents=True, exist_ok=True)
        bpy.ops.wm.save_as_mainfile(filepath=str(blend_path), check_existing=False)
        report["blend"] = {"path": str(blend_path), "sha256": sha256(blend_path)}

    candidate_value = os.environ.get("EI_FIXTURE_EXPORT")
    if candidate_value:
        candidate = Path(candidate_value).resolve()
        if candidate == source_res:
            raise RuntimeError("Refusing to export into the input RES")
        if not candidate.is_file():
            raise RuntimeError("EI_FIXTURE_EXPORT must be a pre-created candidate RES copy")
        before_hash = sha256(candidate)
        scene.res_file = str(candidate)
        export_result = bpy.ops.object.model_export()
        report["export"] = {
            "candidate": str(candidate),
            "operator_result": sorted(export_result),
            "before_sha256": before_hash,
            "after_sha256": sha256(candidate),
            "after_size": candidate.stat().st_size,
        }

    report["status"] = "ok"
except Exception as exc:
    report["error"] = f"{type(exc).__name__}: {exc}"
    report["traceback"] = traceback.format_exc()

report_path.write_text(json.dumps(report, indent=2, sort_keys=True), encoding="utf-8")
print("EI_CHARACTER_MODEL_FIXTURE", report_path, report["status"])
if report["status"] != "ok":
    raise SystemExit(2)
