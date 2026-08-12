# Resource Browser r3 test build

## What changed

- Replaced fixed model snapshots with an interactive preview: drag to orbit,
  right/middle drag to pan, wheel to zoom, and **Reset view** to reframe.
- Corrected the default 3D camera to view models from above the ground plane.
- MOD assemblies expose a searchable checkable part list with **Normal**,
  **All**, and **None** presets. Normal selects established humanoid anatomy and
  sensibly shows all parts for models without that anatomy.
- MOD and ANM previews resolve provider ADB actions and matching ANM/MOD/BON
  resources, then use the editor's existing 15 Hz clip interpolation and
  hierarchy propagation. Choose an action from the list and play or pause it.
- A standalone external ANM without provider ADB can still preview: action
  names and loop lengths are inferred from its bounded closing-loop samples and
  explicitly identified as fallback semantics.
- MMP previews now reuse minimap-style wheel zoom, drag-to-pan, Fit, and 100%
  controls.
- WAV/MP3/OGG members have play/pause, seek, and volume controls through the
  deployed Qt Multimedia backend. Unsupported codecs fail visibly and remain
  exportable.

## Manual checks

1. Preview a known upright humanoid MOD. Confirm it starts above ground, then
   orbit, pan, zoom, and reset the view.
2. Search and toggle individual humanoid parts. Check **Normal**, **All**, and
   **None**, including a non-humanoid model.
3. Select a matching ANM or MOD from the active profile. Choose several actions
   and verify play/pause changes the displayed pose without blocking browsing.
4. Open an isolated copied ANM and verify its fallback timing is clearly noted.
5. Preview an MMP and test wheel zoom, drag pan, Fit, and 100%.
6. Preview representative WAV and compressed audio members. Verify seek/volume;
   a missing system codec should produce an actionable fallback message.

## Known limitations

- The interactive model pane is still an untextured CPU renderer. It reuses the
  editor's animation math but not its OpenGL material/atlas renderer.
- Animation playback is designed for MOD assemblies. A standalone FIG has no
  hierarchy to receive multipart ANM channels.
- Compressed audio support depends on codecs available to the packaged Qt 5.12
  Windows multimedia backend; WAV is the primary supported control.
- Nested RES navigation and external-editor save watching remain future work.
