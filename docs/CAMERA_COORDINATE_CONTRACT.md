# Camera coordinate contract

## The legacy-camera landmine

`CCamera::getLocation()` exposes the editor's persisted orbit-controller state:

- `pos`: stored translation/orbit anchor;
- `pivot`: orbit pivot;
- `rot`: pitch and yaw controls.

Despite its historical name, `pos` is **not guaranteed to be the rendered
world-space camera eye**. The view transform is assembled as:

```text
T(pivot) * T(-pos) * Rx(pitch) * Rz(yaw) * T(-pivot)
```

The pivot translations and rotations occur after the stored position has been
introduced. Treating `pos` as the ray origin therefore disagrees with the
camera that actually renders the viewport whenever the orbit transform is
non-trivial.

This caused the July 2026 shadow-cascade defect: inverse-projected points came
from the real view/projection transform, but cascade rays were started at the
stored orbit anchor. The mixed coordinate systems produced angle-dependent
receiver bounds, empty near cascades, disappearing contact shadows, and abrupt
quality changes while panning or orbiting.

## Required APIs

Use the view matrix as the source of truth for renderer calculations.

```cpp
const QMatrix4x4 view = camera.viewMatrix();
bool ok = false;
const QVector3D eye = ei::cameraEyeFromView(view, &ok);
```

The rendered eye is the world-space point that the view matrix maps to the
camera-space origin, equivalently:

```text
inverse(view) * (0, 0, 0, 1)
```

For frustum slices, use `ei::cameraFrustumSliceCorners()`. Its near/far inputs
are positive view-space depths (`-viewPosition.z`), matching the color shaders'
`v_viewDepth` convention.

## Which representation to use

| Task | Required representation |
| --- | --- |
| GPU view transform, picking, frustum extraction | `viewMatrix()` |
| World-space rendered camera eye | `cameraEyeFromView(viewMatrix())` |
| Cascade receiver corners | `cameraFrustumSliceCorners()` |
| Distance culling, render LOD, light sorting | Rendered eye |
| Saving/restoring editor camera controls | `getLocation()` / `setLocation()` |
| Changing the orbit center without moving the view | `setOrbitPivotPreservingView()` |

Do not combine inverse-projected geometry with `getLocation().pos`. Do not
rename the stored field to `eye` in local variables. Use `cameraAnchor` or
`storedOrbitAnchor` when serializing or diagnosing it.

## Review and test requirements

Any new camera-dependent renderer feature must be checked while:

1. panning without changing rotation;
2. orbiting around a non-origin pivot;
3. changing pitch and yaw;
4. dollying close and far;
5. restoring a serialized camera state.

Coverage, LOD, visibility, shadow quality, and picking must not change merely
because two code paths used different interpretations of the camera position.
The focused camera test verifies that reconstructed frustum corners map back to
their requested positive view depths and that the derived eye maps to the
camera-space origin.
