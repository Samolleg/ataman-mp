"""Deterministic ei_figer model import/export fixture for headless Blender.

Required environment variables:
  EI_FIXTURE_RES       copied, immutable input RES
  EI_FIXTURE_MODEL     model/member stem to import
  EI_FIXTURE_REPORT    JSON output outside the input tree

Optional:
  EI_FIXTURE_MODE      VANILLA or JABAIS_VOUX (used when the add-on exposes it)
  EI_FIXTURE_BLEND     save the imported inspection scene
  EI_FIXTURE_EXPORT    pre-created candidate RES; never the input path

The caller must copy the candidate before requesting export. This script will
not create a RES archive or write to the source archive implicitly.
"""

from __future__ import annotations

import hashlib
import importlib
import json
import os
from pathlib import Path
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


def mesh_record(obj: bpy.types.Object) -> dict[str, object]:
    mesh = obj.data
    record: dict[str, object] = {
        "name": obj.name,
        "parent": obj.parent.name if obj.parent else None,
        "location": list(obj.location),
        "rotation_mode": obj.rotation_mode,
        "rotation_quaternion": list(obj.rotation_quaternion),
        "scale": list(obj.scale),
        "vertices": len(mesh.vertices),
        "edges": len(mesh.edges),
        "polygons": len(mesh.polygons),
        "loops": len(mesh.loops),
        "uv_layers": [layer.name for layer in mesh.uv_layers],
        "active_uv_bounds": uv_bounds(mesh),
        "materials": [slot.name if slot else None for slot in mesh.materials],
        "collections": sorted(collection.name for collection in obj.users_collection),
    }
    if hasattr(obj, "imported_item_group"):
        record["imported_item_group"] = obj.imported_item_group
    return record


source_res = required_path("EI_FIXTURE_RES")
report_path = required_path("EI_FIXTURE_REPORT")
model_name = os.environ.get("EI_FIXTURE_MODEL")
if not model_name:
    raise RuntimeError("EI_FIXTURE_MODEL is required")
mode = os.environ.get("EI_FIXTURE_MODE", "VANILLA")
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

    result = bpy.ops.object.model_import()
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
    }
    if "FINISHED" not in result or not meshes:
        raise RuntimeError(f"Model import did not produce meshes: {sorted(result)}")

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
