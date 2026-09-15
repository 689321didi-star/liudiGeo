# Wave3D Desktop Development Review

**Status:** Proposed; awaiting user review before implementation.

## Product boundary

The desktop application is a scientific experiment workbench around the
qualified Wave3D solver. Its first release shall configure, validate, run, and
inspect one 3-D isotropic elastic forward experiment. It shall provide a
volume view and three orthogonal slices from the same synchronized wavefield
frame, source and receiver editing, run progress and control, final SEG-Y
inspection, and a reproducible run directory.

Wavefield-snapshot storage has a visible reserved command and interface but no
writer in the first release. RTM is represented only by an extensible task
type and result boundary. RTM propagation, checkpointing, imaging conditions,
and image post-processing are later research increments.

## Repository and build

Keep the application in this repository so the GUI and solver share the same
validated types and do not develop an ABI or configuration-version mismatch.
Add an optional `WAVE3D_BUILD_DESKTOP` CMake target named `wave3d_studio`.
Solver, I/O, and tests must remain buildable without Qt or OpenGL.

The proposed implementation uses Qt 6 Widgets for the main window, docking,
forms, tables, and task control, with modern OpenGL for 3-D textures, volume
ray casting, and orthogonal slice rendering. Linux/NVIDIA is the first
qualified platform. A custom renderer keeps CUDA/OpenGL interoperation under
project control; adding VTK is deferred unless later requirements outweigh its
dependency and data-transfer cost.

## Modules

1. **Project and run manager** — creates or opens a workspace, stores model
   references and experiment YAML, creates immutable run manifests, and keeps
   logs, SEG-Y, reports, and future snapshot products in separate directories.
2. **Model and crop editor** — imports supported model data, shows dimensions,
   spacing, units, property ranges, and three model sections, and selects a
   physical crop before preparation.
3. **Workspace editor** — edits grid spacing, time step, duration/sample count,
   halo, CPML thickness, free surface, output location, and device selection.
4. **Source editor** — edits position, wavelet, dominant frequency, delay,
   amplitude, and six independent moment-tensor terms; includes named presets
   and shows the source in all four views.
5. **Acquisition editor** — creates regular surface arrays or imports explicit
   receiver coordinates, validates bounds and count, and overlays receivers in
   the views.
6. **Preflight service** — validates model/configuration consistency, CFL and
   stencil/boundary constraints, source/receiver bounds, output size, writable
   paths, and live VRAM. Persistent render volumes are included through
   `ForwardMemoryPlanRequest::workspace_bytes`.
7. **Simulation controller** — owns a `CudaForwardSession` on a worker thread,
   advances positive whole-step batches, reports progress/time, and implements
   cooperative pause, resume, and stop at batch boundaries.
8. **Visualization pipeline** — extracts one selected physical scalar on the
   GPU and publishes a complete `(field, step, time, volume)` frame. The 3-D
   view and XY/XZ/YZ views sample the same frame and therefore cannot show
   different time steps.
9. **Rendering and interaction** — provides volume transfer functions,
   signed/unsigned colour maps, clipping, opacity, camera control, slice
   indices, linked crosshairs, source/receiver overlays, and displayed-value
   inspection. Display settings never modify scientific data.
10. **Results and SEG-Y viewer** — opens the produced receiver-major VX/VY/VZ
    SEG-Y, displays gathers and headers, selects receiver/component ranges,
    and exports figures separately from the original record.
11. **Diagnostics** — records configuration, solver/device information,
    timings, warnings, failures, cancellation state, and output checksums in
    each run.

## Runtime ownership and data flow

The GUI thread owns widgets and presentation. The simulation worker exclusively
owns the CUDA session. It advances a configurable small batch, synchronizes at
the accepted whole-step boundary, optionally extracts the selected display
field, and publishes only a complete frame. Stop and pause requests are atomic
commands consumed between batches; the application never terminates the worker
thread forcibly.

Use three persistent physical scalar volumes: one written by CUDA, one ready
for presentation, and one currently displayed. On the qualified
`200 x 200 x 187` model they consume 89,760,000 bytes (about 85.6 MiB). A
latest-complete-frame policy may discard an obsolete display frame when the
renderer falls behind; it may never skip solver steps or receiver sampling.

Start with pinned-host staging to establish correct threading, frame identity,
and rendering on all supported NVIDIA systems. Add CUDA/OpenGL registered
buffers as a measured follow-on optimization. Both transports implement the
same frame contract. The final implementation target is direct GPU transfer;
the staged path remains a compatibility and diagnostic fallback.

The default interactive batch should be one time step for the current
Overthrust model because one solver step already takes roughly 80 ms. Batch
size and display interval remain separate settings. Before freezing defaults,
measure one-step synchronization overhead and pause latency on the deployment
GPU.

## Run lifecycle

The controller state machine is:

`Draft -> Validated -> Ready -> Running <-> Paused -> Completed`

`Running` or `Paused` may enter `Stopping`, followed by `Cancelled` after the
current batch. Any setup, CUDA, I/O, or rendering transport error enters
`Failed` with a retained diagnostic log. Configuration becomes read-only once
a run starts; editing creates a new experiment or run revision.

Successful completion downloads traces, writes SEG-Y to a temporary file,
validates its expected size and essential headers, then atomically publishes
the final filename and checksum. Interrupted runs do not present a partial
file as a completed SEG-Y product.

## Development increments

1. **Desktop shell and project schema** — optional Qt build, workspace/run
   model, navigation, logging, settings persistence, and automated state tests.
2. **Static scientific scene** — HDF5 model loading, shared 3-D texture,
   volume view, three linked slices, crop/source/receiver overlays, and unit
   labels without launching propagation.
3. **Experiment editors and preflight** — workspace, source, acquisition, CFL,
   storage, and VRAM validation; generated YAML must round-trip through the
   existing parser.
4. **Controlled forward run** — worker ownership, one-step or short-batch
   advance, progress, pause/resume/stop, final SEG-Y publication, and failure
   recovery.
5. **Live wavefield display** — pinned staging first, synchronized four-view
   frames, display-field and transfer-function controls, then measured
   CUDA/OpenGL interoperation.
6. **Results workspace** — integrated SEG-Y gather/header inspection, run
   comparison metadata, figure export, and final end-to-end qualification on
   the accepted Overthrust case.
7. **Reserved research interfaces** — disabled snapshot command connected to a
   documented future writer contract, and RTM task/result interfaces with no
   RTM execution in this release.

Every increment must keep the headless command-line runner and CPU-only build
working. A small deterministic fixture covers UI/controller tests; the full
Overthrust run is reserved for milestone qualification rather than routine
test execution.

## Proposed first-release defaults to confirm

- Keep the desktop application in the `wave3d` repository as an optional
  target instead of creating a parallel product repository.
- Qualify Linux with NVIDIA CUDA first; postpone Windows packaging until the
  workflow is stable.
- Use a Chinese interface with scientific symbols, units, paths, and file
  metadata preserved exactly.
- Open with a four-view layout: 3-D volume on the left and XY/XZ/YZ slices on
  the right, with resizable docks for experiment controls, progress, and logs.
- Default the live scalar to signed `Vz`; provide Vx, Vy, speed, divergence,
  and curl magnitude without rerunning the simulation.
- Initially support the existing Wave3D HDF5 model and YAML experiment schema,
  regular receiver grids, and explicit receiver-coordinate import. Additional
  industry model formats are later adapters.
- Reserve the wavefield-snapshot button in a disabled state labelled as a
  future feature. Do not write large snapshot files in the first release.
- Preserve forward CLI behavior and SEG-Y format as the scientific baseline.

Desktop implementation must not start until these defaults and module
boundaries are reviewed and accepted or revised.
