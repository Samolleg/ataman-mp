"""Render a deterministic material preview from an opened staged EI .blend."""

from __future__ import annotations

import os
from pathlib import Path

import bpy
from mathutils import Vector


output_value = os.environ.get("EI_BLEND_RENDER_OUTPUT")
if not output_value:
    raise RuntimeError("EI_BLEND_RENDER_OUTPUT is required")
output = Path(output_value).resolve()
if output.suffix.lower() != ".png":
    raise RuntimeError("EI_BLEND_RENDER_OUTPUT must be a PNG")
output.parent.mkdir(parents=True, exist_ok=True)

visible = [
    obj for obj in bpy.context.scene.objects
    if obj.type == "MESH" and not obj.hide_get() and not obj.hide_render
    and all(not collection.hide_render for collection in obj.users_collection)
]
if not visible:
    raise RuntimeError("Scene has no visible mesh objects")
corners = [obj.matrix_world @ Vector(corner) for obj in visible for corner in obj.bound_box]
minimum = Vector((min(v.x for v in corners), min(v.y for v in corners), min(v.z for v in corners)))
maximum = Vector((max(v.x for v in corners), max(v.y for v in corners), max(v.z for v in corners)))
center = (minimum + maximum) * 0.5
extent = maximum - minimum
radius = max(extent.x, extent.y, extent.z) * 0.62

camera_data = bpy.data.cameras.new("EI preview camera")
camera = bpy.data.objects.new("EI preview camera", camera_data)
bpy.context.scene.collection.objects.link(camera)
camera.location = center + Vector((0.0, -max(2.5, radius * 3.2), extent.z * 0.06))
camera.rotation_euler = (center - camera.location).to_track_quat("-Z", "Y").to_euler()
camera_data.type = "ORTHO"
camera_data.ortho_scale = max(extent.z * 1.12, extent.x * 1.4, 1.0)
bpy.context.scene.camera = camera

for x, y, energy, size in ((-2.0, -2.5, 900.0, 4.0), (2.5, -1.0, 650.0, 3.0)):
    light_data = bpy.data.lights.new(f"EI preview light {x}", "AREA")
    light_data.energy = energy
    light_data.shape = "DISK"
    light_data.size = size
    light = bpy.data.objects.new(light_data.name, light_data)
    bpy.context.scene.collection.objects.link(light)
    light.location = center + Vector((x, y, max(1.5, extent.z)))
    light.rotation_euler = (center - light.location).to_track_quat("-Z", "Y").to_euler()

scene = bpy.context.scene
scene.render.engine = "BLENDER_EEVEE_NEXT"
scene.render.resolution_x = 1024
scene.render.resolution_y = 1024
scene.render.resolution_percentage = 100
scene.render.image_settings.file_format = "PNG"
scene.render.image_settings.color_mode = "RGBA"
scene.render.film_transparent = False
scene.render.filepath = str(output)
scene.render.image_settings.color_depth = "8"
scene.view_settings.look = "AgX - Medium High Contrast"
scene.world.color = (0.035, 0.035, 0.035)
bpy.ops.render.render(write_still=True)
print("EI_CHARACTER_BLEND_RENDER", output)
