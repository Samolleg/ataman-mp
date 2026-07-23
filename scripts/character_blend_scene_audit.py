"""Audit a saved EI Blender scene after a fresh reopen.

Run Blender with the target .blend as its first argument and this script via
``--python``. Set ``EI_BLEND_AUDIT_REPORT`` to an explicit staged JSON path.
The script is read-only with respect to the opened .blend.
"""

from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import struct

import bpy


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def packed_hash(format_code: str, rows: list[tuple[object, ...]]) -> str:
    digest = hashlib.sha256()
    packer = struct.Struct("<" + format_code)
    for row in rows:
        digest.update(packer.pack(*row))
    return digest.hexdigest()


def mesh_record(obj: bpy.types.Object) -> dict[str, object]:
    mesh = obj.data
    active_uv = mesh.uv_layers.active
    uv_rows = [tuple(loop.uv) for loop in active_uv.data] if active_uv else []
    uv_bounds = None
    if uv_rows:
        uv_bounds = [
            min(value[0] for value in uv_rows),
            max(value[0] for value in uv_rows),
            min(value[1] for value in uv_rows),
            max(value[1] for value in uv_rows),
        ]
    return {
        "name": obj.name,
        "parent": obj.parent.name if obj.parent else None,
        "collections": sorted(collection.name for collection in obj.users_collection),
        "hidden_viewport": obj.hide_get(),
        "hidden_render": obj.hide_render,
        "vertices": len(mesh.vertices),
        "loops": len(mesh.loops),
        "polygons": len(mesh.polygons),
        "vertex_position_sha256": packed_hash(
            "fff", [tuple(vertex.co) for vertex in mesh.vertices]
        ),
        "loop_vertex_index_sha256": packed_hash(
            "I", [(loop.vertex_index,) for loop in mesh.loops]
        ),
        "loop_normal_sha256": packed_hash(
            "fff", [tuple(loop.normal) for loop in mesh.loops]
        ),
        "uv_layers": [layer.name for layer in mesh.uv_layers],
        "active_uv_bounds": uv_bounds,
        "loop_uv_sha256": packed_hash("ff", uv_rows) if uv_rows else None,
        "materials": [slot.name if slot else None for slot in mesh.materials],
    }


report_value = os.environ.get("EI_BLEND_AUDIT_REPORT")
if not report_value:
    raise RuntimeError("EI_BLEND_AUDIT_REPORT is required")
report_path = Path(report_value).resolve()
report_path.parent.mkdir(parents=True, exist_ok=True)

blend_path = Path(bpy.data.filepath).resolve()
if not blend_path.is_file():
    raise RuntimeError("Open a saved .blend before running the audit")

provenance_text = bpy.context.scene.get("ei_pipeline_provenance_json")
if not isinstance(provenance_text, str):
    raise RuntimeError("Scene has no ei_pipeline_provenance_json")
provenance = json.loads(provenance_text)
if provenance.get("source_profile") == "JMV" and provenance.get("import_mode") != "JABAIS_VOUX":
    raise RuntimeError("JMV scene provenance does not use JABAIS_VOUX")

meshes = sorted(
    (mesh_record(obj) for obj in bpy.data.objects if obj.type == "MESH"),
    key=lambda item: item["name"],
)
report = {
    "schema": 1,
    "status": "ok",
    "blend": {
        "path": str(blend_path),
        "size": blend_path.stat().st_size,
        "sha256": sha256(blend_path),
    },
    "blender": {
        "version": bpy.app.version_string,
        "version_tuple": list(bpy.app.version),
    },
    "provenance": provenance,
    "scene_mode": getattr(bpy.context.scene, "item_container_set", None),
    "collections": [
        {
            "name": collection.name,
            "hide_viewport": collection.hide_viewport,
            "hide_render": collection.hide_render,
        }
        for collection in sorted(bpy.data.collections, key=lambda item: item.name)
    ],
    "mesh_count": len(meshes),
    "material_count": len(bpy.data.materials),
    "image_count": len(bpy.data.images),
    "meshes": meshes,
}
report_path.write_text(json.dumps(report, indent=2, sort_keys=True), encoding="utf-8")
print("EI_CHARACTER_BLEND_AUDIT", report_path, report["status"])
