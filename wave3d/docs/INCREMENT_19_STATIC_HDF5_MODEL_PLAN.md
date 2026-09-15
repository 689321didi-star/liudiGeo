# Increment 19 Predeclared Static HDF5 Model Plan

## Purpose

Load a real validated Wave3D HDF5 model into the desktop application and show
scientifically indexed static orthogonal sections. This increment establishes
the model-to-display contract without starting the CUDA solver or claiming a
finished 3-D volume renderer.

## Fixed scope

- Keep the desktop shell buildable without HDF5. Add the model feature only
  when both `WAVE3D_BUILD_DESKTOP` and `WAVE3D_ENABLE_HDF5` are enabled.
- Reuse `read_hdf5_model`; do not add a second HDF5 parser or weaken the
  `wave3d.model.v1` validation path.
- Own the loaded physical model in a desktop model-scene object and expose its
  grid, physical extents, Vp/Vs/density extrema, and central x/y/z indices.
- Generate XY, XZ, and YZ images directly from canonical `[z][y][x]` arrays.
  XY places increasing y upward; XZ and YZ place positive depth downward.
- Use one documented perceptual sequential colour map and one global range per
  selected model property. Permit Vp, Vs, and density selection.
- Require an open project before import. Copy an external HDF5 file once into
  the project's `models/` directory, refuse an existing destination, retain
  the source unchanged, then atomically persist its safe relative reference.
- Add an HDF5 import action, model-information dock, property selector, and
  real synchronized central sections. The 3-D viewport must clearly label its
  current content as a static model preview rather than volume ray casting.
- Permit explicit project/model command-line paths for repeatable WSLg capture
  without changing the interactive file-dialog workflow.

## Acceptance

1. Focused tests write a small heterogeneous model with the existing HDF5
   adapter, reload it through the desktop scene, and verify metadata, extrema,
   section dimensions/orientation, property switching, and exact endpoint
   colours.
2. Window tests import a model into a project, verify immutable copy/reference
   behavior, populated metadata, enabled property selection, real section
   images, and refusal to overwrite an existing project model.
3. An HDF5-enabled clean desktop build and its full test suite pass; the
   HDF5-off desktop build still succeeds with model import gated.
4. WSLg creates all four OpenGL contexts and captures the static model shell.
5. The full CUDA/YAML/HDF5/SEG-Y suite remains green.

## Deferred work

Interactive slice indices and crosshairs, model crop editing, SEG-Y model
import, source/receiver overlays, GPU 3-D texture upload, volume ray casting,
transfer-function editing, solver control, live wavefield frames, and RTM are
outside this increment.

## Result

Verified on 2026-09-15. The HDF5-conditional desktop scene loads through the
existing validated adapter, reports grid/extents/property extrema, and renders
the three central sections with the declared axis directions and global
property scales. External imports are copied byte-for-byte through an atomic
publish step, never overwrite an existing project model, and persist a safe
relative reference. Project reopening restores the model, while switching to
an empty project clears every image and metadata field.

The final HDF5-enabled desktop build passed 21/21 tests, the HDF5-off desktop
build passed 19/19, and the default desktop-off build passed 17/17 with no
desktop target. The CUDA/YAML/HDF5/SEG-Y regression passed 28/28.
WSLg/XCB created all four OpenGL contexts and captured the real
`200 x 200 x 187` Overthrust Vp model. Visual inspection found the model
metadata, section orientation, aspect ratios, and expected structural patterns
consistent; the large viewport is explicitly identified as a static center-XY
preview because GPU volume ray casting remains deferred.
