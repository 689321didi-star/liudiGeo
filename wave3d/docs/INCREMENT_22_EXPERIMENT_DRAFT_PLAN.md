# Increment 22 Predeclared Workspace and Source Draft Plan

## Purpose

Turn the existing placeholder workspace/source navigation into a persistent,
scientifically validated editor for one active shot. This increment prepares a
draft only; it does not create a runnable YAML, configure receivers, allocate
CUDA state, or start propagation.

## Fixed scope

- Add a versioned `wave3d.desktop.experiment.v1` JSON draft under
  `source/<shot-id>.experiment.json`. Saving is atomic, refuses unsafe shot
  identifiers and invalid values, and never changes a prepared run.
- Treat physical dimensions, spacing, halo, absorbing widths, and top boundary
  as read-only properties of the active HDF5 model. Edit time step, total time,
  CFL safety factor, and design frequency without silently changing model grid
  metadata.
- Edit one active shot's physical x/y/z location, origin time, Ricker dominant
  frequency, peak delay, peak rate, and either an isotropic explosion scalar
  moment or six explicit symmetric moment-tensor components. Keep a visible
  disabled double-couple option for its later separately verified conversion.
- Resolve drafts through the existing `SimulationConfig`, radius-six CFL and
  dispersion validation, and `prepare_moment_tensor_source`. Report maximum
  stable time step, CFL fraction, minimum S-wave points per wavelength, time
  samples per period, and coordinate failures in the editor.
- Show the valid draft source location in the three orthogonal model sections
  and the static 3-D viewport. Unsaved edits may update presentation, while only
  the explicit save action changes the draft file.
- Keep experiment preflight and run controls disabled because receiver geometry
  and complete `wave3d.forward.v2` YAML do not exist yet.

## Acceptance

1. Qt Core tests cover defaults, explosion/manual resolution, numerical and
   coordinate rejection, exact JSON round trip, unsafe identity rejection,
   atomic replacement, and isolation from project/run files.
2. Window tests cover disabled-without-model state, model-derived read-only
   grid metadata, editing and saving, reopening, validation diagnostics,
   source-mode switching, and synchronized source-marker state.
3. WSLg/XCB opens the real Overthrust project, loads or creates the active-shot
   draft, renders a valid source marker in all four views, and captures a review
   image without weakening the Increment 21 renderer gates.
4. HDF5-on, HDF5-off, desktop-off, and full CUDA/YAML/HDF5/SEG-Y suites pass.

## Deferred work

Strike/dip/rake conversion, multiple-shot table editing, acquisition geometry,
complete YAML generation and round trip, storage/VRAM preflight, worker-thread
execution, live wavefields, SEG-Y results, snapshots, and RTM remain outside
this increment.

## Result

Verified on 2026-09-15. The desktop now owns an atomic, per-shot
`wave3d.desktop.experiment.v1` draft with model binding, time and numerical
settings, physical source coordinates, Ricker parameters, and explosion or
manual symmetric-moment values. Defaults and edits resolve through the
existing grid, source, CFL, and dispersion contracts before saving.

The real Overthrust project reported its immutable `200 x 200 x 187`, 25 m,
halo/CPML/free-surface metadata and produced the expected 3000-step default.
Its 1 ms step uses 65.5% of the 1.5270 ms safety-adjusted CFL limit; the 9 Hz
design band retains 6.28 minimum S-wave points per wavelength and 111.1 time
samples per period. The `(2500,2500,1150) m` source marker was inspected at the
matching `x=100`, `y=100`, `z=46` intersection in all four views.

HDF5-on passed 23/23 tests, HDF5-off passed 20/20, desktop-off passed 17/17,
and CUDA/YAML/HDF5/SEG-Y passed 28/28. Both 1440 x 1062 WSLg/XCB editor
captures passed the existing context/shader/texture/frame gates and the new
3-D source-marker gate.
