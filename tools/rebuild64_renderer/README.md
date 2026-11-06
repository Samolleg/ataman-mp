# Rebuild64 display backend for Ataman 282

Only the Ataman host and flat C ABI are built in this repository. Renderer and
typed adapters are extracted from the commits in
`third_party/eirebuild64-renderer/source.lock.json`; they are never vendored here.
The local recipe reads Git objects without changing either source checkout.

```powershell
./tools/rebuild64_renderer/build_backend.ps1 `
  -SourceRepositoryPath <EIRebuild64-clone> `
  -RendererRepositoryPath <owned-renderer-clone> `
  -BuildRoot <new-directory-under-this-checkout-work>
```

Use an ASCII/8.3 path on this Windows machine. The optional `-ShaderStore` points
to an owned cache under `work/`; the upstream shader producer keys bytecode by
the preprocessed program. A fresh table is completed before the DLL is linked.
The resulting `bin/` contains the DLL, material data and original licenses and
notices. Stage them together with the MSVC 14 x64 runtime. No NGX, DLSS,
Streamline or NVIDIA SDK library is used. The replacement backend explicitly
reports DLSS unavailable and creates no SDK worker or writable NGX directory.

The caller loads `ataman_renderer_get_api` with QLibrary and ABI version 2.
All calls for a handle, including destruction, belong to one worker thread.
Inputs are borrowed only for each call and copied before returning. Captures
read the last committed renderer-owned texture. The renderer's internal
swapchain is invisible; the native widget receives a GPU copy only when the
host readiness observer accepts the current generation. It checks actual
material rows, atlas copies and height bake completion rather than RGB values.
There is no upstream general presentable-generation API in the pinned revision;
the observer is a temporary, explicitly pinned host contract, not such an API.

Reviewable patches provide typed terrain publication, function-level linking,
the no-NGX build selection, a read-only readiness observer, immutable memory
inputs to the loader, and configuration retirement between sessions. Generic
retained object parts receive their actual texture resource as material key.
No game classes, simulation, hooks, private engine headers or file save path
are part of this host.

OpenGL remains the default and tool owner. Settings / Map view switches without
restarting. The initial OpenGL path creates no renderer worker and does not
load the DLL. After first use, switching back pauses the warm renderer session;
its caches remain available for a subsequent switch. Documents are still owned
by CView and survive backend failure. Environment variable
`ATAMAN_REBUILD64_TEST_FAILURE` accepts `missing`, `load`, `no-d3d11`, `init`,
or `device-lost` for the isolated failure checks. Captures and validation must
follow the explicit task's offscreen and writable-path boundaries.

Renderer credit: Ilya Rudnev (Ilufus), evil-islands-owned-renderer; embedded
adapters from EIRebuild64. Original notices and third-party licenses travel with
the DLL. Author permission was settled by the owner on 2026-11-06.
