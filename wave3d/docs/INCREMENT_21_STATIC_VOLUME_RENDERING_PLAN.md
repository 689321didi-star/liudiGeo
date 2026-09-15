# Increment 21 Predeclared Static Volume Rendering Plan

## Purpose

Replace the large static-XY preview with a real interactive 3-D rendering of
the validated model. This increment renders a stationary Vp/Vs/density volume;
it does not connect CUDA propagation, live wavefields, source/receiver editing,
or RTM.

## Fixed scope

- Add a reusable desktop volume-texture preparation contract. It preserves
  canonical `[z][y][x]` order, normalizes only a display copy with the selected
  property's full-volume extrema, reports physical x/y/z aspect, and leaves
  all physical arrays unchanged.
- Replace only `volumeViewport` with an OpenGL 3.3 core renderer. Upload one
  `GL_R32F` 3-D texture and use front-to-back fragment-shader ray marching
  through a physical-aspect box with the existing sequential colour map.
- Keep the existing CPU slice viewports unchanged. The material selector
  reloads Vp/Vs/density into the volume renderer and all three slices together.
- Map the current inclusive crop controls to normalized texture clipping bounds
  and show the same selected subvolume in 3-D. Crop definition remains a
  presentation operation until the user explicitly creates a derived model.
- Add mouse orbit, wheel zoom, opacity, lower-threshold, and reset-camera
  controls. Clamp every control and show renderer failure explicitly.
- Retain the screenshot capture path and expose diagnostic properties for
  OpenGL context, shader, texture dimensions, uploaded property, and frame
  readiness.

## Acceptance

1. CPU tests verify exact normalization endpoints/midpoint, constant-volume
   behavior, canonical sample order, physical aspect, crop-bound conversion,
   source immutability, and invalid-input rejection.
2. Window tests verify the dedicated volume widget and transfer controls,
   model/property/crop synchronization, correct texture dimensions, and state
   clearing across projects.
3. WSLg/XCB compiles the production shader, uploads the real
   `200 x 200 x 187` Overthrust texture, produces a nonempty ray-cast frame,
   exercises orbit/zoom, and captures a review image.
4. HDF5-on, HDF5-off, default desktop-off, and full
   CUDA/YAML/HDF5/SEG-Y suites pass.

## Deferred work

CUDA/OpenGL interoperation, live wavefield frames, signed/diverging transfer
functions, source/receiver overlays, arbitrary clipping planes, lighting,
isosurfaces, experiment editors, SEG-Y viewing, and RTM remain outside this
increment.

## Result

Verified on 2026-09-15. `StaticModelScene` now prepares an independent
normalized `float32` display volume in canonical `[z][y][x]` order and reports
physical axis proportions plus normalized crop bounds. `VolumeViewport`
uploads it as `GL_R32F` and performs front-to-back OpenGL 3.3 ray marching.
The renderer keeps model depth positive downward, clips against the shared
crop, and provides bounded orbit, zoom, opacity, threshold, and reset state.

The actual `200 x 200 x 187` Overthrust model compiled the production shader,
uploaded successfully, and produced a nonempty WSLg/XCB frame at a changed
camera angle and distance. The frame was inspected against all three
orthogonal sections; layer positions, depth orientation, crop proportions,
colour mapping, and text were consistent. HDF5-on passed 22/22 tests,
HDF5-off passed 19/19, desktop-off passed 17/17, and the complete
CUDA/YAML/HDF5/SEG-Y build passed 28/28.
