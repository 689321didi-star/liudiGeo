# Increment 20 Predeclared Interactive Slice and Crop Plan

## Purpose

Turn the verified static HDF5 scene into an interactive model-inspection and
immutable crop workflow. This increment changes model presentation and creates
derived model artifacts; it does not add 3-D ray casting, edit imported voxel
values, configure a forward experiment, or launch CUDA.

## Fixed scope

- Represent slice positions as zero-based x/y/z physical-grid indices and crop
  bounds as validated half-open ranges `[begin,end)` on each axis.
- Extend the desktop scene with arbitrary-index XY/XZ/YZ extraction and exact
  canonical `[z][y][x]` crop extraction for Vp, Vs, and density.
- Keep grid spacing, halo, and absorbing-boundary metadata in a crop. The new
  local model coordinate origin is `(0,0,0)`; record the source-model origin
  offset in the derivation manifest so the mapping is reversible.
- Add compact x/y/z slice controls, six inclusive crop-endpoint controls,
  linked crosshairs/crop outlines, selected crop dimensions/physical size, and
  a create-crop action. All controls remain disabled without a valid model.
- Write every crop to a new safe `.h5` name under `models/` and its immutable
  `wave3d.desktop.model_derivation.v1` JSON under `manifests/models/`. Record
  the operation, source/output relative paths and SHA-256 values, half-open
  indices, origin offset, dimensions, spacing, and UTC creation time.
- Publish the HDF5 and manifest through temporary files, refuse any existing
  output or manifest, preserve the source bytes, and update the project model
  reference only after both derived artifacts validate successfully.

## Acceptance

1. Focused scene tests verify arbitrary slices, out-of-range rejection, exact
   three-property crop mapping, dimensions, spacing/storage metadata, and
   unchanged source arrays.
2. Focused derivation tests verify the HDF5 values, reversible bounds/origin,
   both SHA-256 values, manifest schema, safe naming, and non-overwrite/cleanup
   behavior.
3. Window tests verify control ranges/defaults, synchronized section updates,
   invalid crop gating, successful project reference replacement, provenance
   creation, and clearing controls when another empty project opens.
4. HDF5-on and HDF5-off desktop builds pass, the default desktop-off boundary
   remains intact, and the CUDA/YAML/HDF5/SEG-Y regression remains green.
5. WSLg/XCB captures and visually checks a noncentral Overthrust slice with a
   visible linked crosshair and crop outline.

## Deferred work

GPU 3-D textures and ray casting, transfer functions, crop resampling or
smoothing, Vp-to-Vs/density derivation, SEG-Y model import, source/receiver
overlays, experiment editing, solver control, live wavefields, and RTM remain
outside this increment.

## Result

Verified on 2026-09-15. Arbitrary x/y/z sections, linked crosshairs, inclusive
UI crop endpoints, and half-open scientific crop bounds now share one physical
index state. Exact Vp/Vs/density extraction preserves spacing and numerical
storage metadata, resets the derived local coordinate origin to zero, and
records the reversible source offset.

Crop creation writes and rereads a temporary HDF5 before atomic publication,
then writes a versioned manifest with source/output paths, SHA-256 values,
bounds, offsets, dimensions, spacing, and UTC time. Existing or unsafe names
are rejected, failed work is cleaned up, source bytes remain unchanged, and
the project switches references only after successful publication.

The HDF5-enabled desktop suite passed 22/22, HDF5-off passed 19/19, default
desktop-off passed 17/17, and CUDA/YAML/HDF5/SEG-Y passed 28/28. WSLg/XCB
captured the real Overthrust model at noncentral `(70,120,80)` with crop
`x=40..160`, `y=30..170`, `z=10..150`. Visual inspection confirmed the same
crosshair and crop volume in all sections, correct y-up/depth-down orientation,
readable controls, and a practical right-dock layout.
