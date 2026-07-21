"""Assign staged character-atlas PNGs to an audited EI Blender scene.

Open the source .blend in Blender, then run this script with explicit environment
variables. It always saves to a different staged .blend and packs the images.
"""

from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import re

import bpy


def required_path(name: str) -> Path:
    value = os.environ.get(name)
    if not value:
        raise RuntimeError(f"{name} is required")
    path = Path(value).resolve()
    if not path.is_file():
        raise FileNotFoundError(path)
    return path


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest().upper()


def material(name: str, image: bpy.types.Image) -> bpy.types.Material:
    result = bpy.data.materials.new(name)
    result.use_nodes = True
    nodes = result.node_tree.nodes
    nodes.clear()
    output = nodes.new("ShaderNodeOutputMaterial")
    shader = nodes.new("ShaderNodeBsdfPrincipled")
    texture = nodes.new("ShaderNodeTexImage")
    texture.image = image
    texture.interpolation = "Closest"
    texture.extension = "REPEAT"
    result.node_tree.links.new(texture.outputs["Color"], shader.inputs["Base Color"])
    result.node_tree.links.new(texture.outputs["Alpha"], shader.inputs["Alpha"])
    result.node_tree.links.new(shader.outputs["BSDF"], output.inputs["Surface"])
    if hasattr(result, "surface_render_method"):
        result.surface_render_method = "DITHERED"
    return result


source_blend = Path(bpy.data.filepath).resolve()
if not source_blend.is_file():
    raise RuntimeError("Open an audited source .blend before assigning an atlas")
output_value = os.environ.get("EI_BLEND_ATLAS_OUTPUT")
if not output_value:
    raise RuntimeError("EI_BLEND_ATLAS_OUTPUT is required")
output_blend = Path(output_value).resolve()
if output_blend == source_blend:
    raise RuntimeError("EI_BLEND_ATLAS_OUTPUT must be a different staged .blend")
composed_path = required_path("EI_BLEND_ATLAS_COMPOSED")
skin_path = required_path("EI_BLEND_ATLAS_SKIN")
report_value = os.environ.get("EI_BLEND_ATLAS_REPORT")
if not report_value:
    raise RuntimeError("EI_BLEND_ATLAS_REPORT is required")
report_path = Path(report_value).resolve()

scene = bpy.context.scene
provenance_text = scene.get("ei_pipeline_provenance_json")
if not isinstance(provenance_text, str):
    raise RuntimeError("Source scene has no EI pipeline provenance")
provenance = json.loads(provenance_text)
if provenance.get("source_profile") == "JMV" and provenance.get("import_mode") != "JABAIS_VOUX":
    raise RuntimeError("Refusing to texture a JMV scene not imported as JABAIS_VOUX")

composed_image = bpy.data.images.load(str(composed_path), check_existing=False)
composed_image.name = "EI composed character atlas"
composed_image.pack()
skin_image = bpy.data.images.load(str(skin_path), check_existing=False)
skin_image.name = "EI unlayered skin atlas"
skin_image.pack()
composed_material = material("EI character atlas (composed)", composed_image)
skin_material = material("EI character atlas (skin only)", skin_image)

assignments: dict[str, str] = {}
for obj in sorted((item for item in bpy.data.objects if item.type == "MESH"), key=lambda item: item.name):
    local_name = obj.name.split("~", 1)[-1]
    use_skin = local_name == "hd" or re.fullmatch(r"hr\.\d+", local_name) is not None
    selected = skin_material if use_skin else composed_material
    obj.data.materials.clear()
    obj.data.materials.append(selected)
    assignments[obj.name] = selected.name

atlas_provenance = {
    "schema": 1,
    "source_blend": str(source_blend),
    "source_blend_sha256": sha256(source_blend),
    "composed_png": str(composed_path),
    "composed_png_sha256": sha256(composed_path),
    "skin_png": str(skin_path),
    "skin_png_sha256": sha256(skin_path),
    "unlayered_rule": "hd and hr.NN use skin; other body/equipment parts use composed atlas",
}
scene["ei_atlas_provenance_json"] = json.dumps(atlas_provenance, sort_keys=True)
text = bpy.data.texts.get("EI_ATLAS_README") or bpy.data.texts.new("EI_ATLAS_README")
text.clear()
text.write(json.dumps(atlas_provenance, indent=2, sort_keys=True))

output_blend.parent.mkdir(parents=True, exist_ok=True)
report_path.parent.mkdir(parents=True, exist_ok=True)
bpy.ops.wm.save_as_mainfile(filepath=str(output_blend), check_existing=False)
report = {
    "schema": 1,
    "status": "ok",
    "blender": bpy.app.version_string,
    "scene_provenance": provenance,
    "atlas_provenance": atlas_provenance,
    "assignments": assignments,
    "assignment_count": len(assignments),
    "output": {
        "path": str(output_blend),
        "size": output_blend.stat().st_size,
        "sha256": sha256(output_blend),
    },
}
report_path.write_text(json.dumps(report, indent=2, sort_keys=True), encoding="utf-8")
print("EI_CHARACTER_BLEND_ATLAS", report_path, report["status"])
