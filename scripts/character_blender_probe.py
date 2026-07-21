"""Read-only Blender/ei_figer capability probe.

Run through launch_character_blender.ps1 with EI_PROBE_OUTPUT set to an explicit
path outside all input assets. The probe registers the staged add-on in a factory
startup process and records capabilities; it never opens or writes a RES file.
"""

from __future__ import annotations

import hashlib
import importlib
import json
import os
from pathlib import Path
import platform
import traceback

import bpy


def hash_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def enum_identifiers(rna: object, property_name: str) -> list[str]:
    prop = rna.properties.get(property_name)
    if prop is None or not hasattr(prop, "enum_items"):
        return []
    return [item.identifier for item in prop.enum_items]


def operator_exists(path: str) -> bool:
    namespace, name = path.split(".", 1)
    try:
        getattr(getattr(bpy.ops, namespace), name).get_rna_type()
    except (AttributeError, KeyError, RuntimeError):
        return False
    return True


output_value = os.environ.get("EI_PROBE_OUTPUT")
if not output_value:
    raise RuntimeError("EI_PROBE_OUTPUT must name an explicit JSON report path")
output = Path(output_value).resolve()
output.parent.mkdir(parents=True, exist_ok=True)

report: dict[str, object] = {
    "schema": 1,
    "status": "failed",
    "blender": {
        "version": bpy.app.version_string,
        "version_tuple": list(bpy.app.version),
        "python": platform.python_version(),
        "background": bpy.app.background,
    },
}

try:
    addon = importlib.import_module("ei_figer")
    addon_path = Path(addon.__file__).resolve()
    info = dict(addon.bl_info)
    addon.register()
    scene = bpy.context.scene
    scene_rna = bpy.types.Scene.bl_rna
    operators = {
        "model_import": operator_exists("object.model_import"),
        "model_export": operator_exists("object.model_export"),
        "animation_import": operator_exists("object.animation_import"),
        "animation_export": operator_exists("object.animation_export"),
        "modern_res_repack": operator_exists("object.repack_resfile"),
    }
    report.update(
        {
            "status": "ok",
            "addon": {
                "module": str(addon_path),
                "module_sha256": hash_file(addon_path),
                "name": info.get("name"),
                "author": info.get("author"),
                "version": list(info.get("version", ())),
                "declared_blender": list(info.get("blender", ())),
            },
            "scene_properties": {
                "res_file": scene_rna.properties.get("res_file") is not None,
                "figmodel_name": scene_rna.properties.get("figmodel_name") is not None,
                "item_container_set": scene_rna.properties.get("item_container_set") is not None,
                "item_container_choices": enum_identifiers(scene_rna, "item_container_set"),
                "auto_fix": scene_rna.properties.get("auto_fix") is not None,
            },
            "operators": operators,
        }
    )
except Exception as exc:  # Blender must still leave an actionable JSON report.
    report["error"] = f"{type(exc).__name__}: {exc}"
    report["traceback"] = traceback.format_exc()

output.write_text(json.dumps(report, indent=2, sort_keys=True), encoding="utf-8")
print("EI_CHARACTER_BLENDER_PROBE", output, report["status"])
if report["status"] != "ok":
    raise SystemExit(2)
