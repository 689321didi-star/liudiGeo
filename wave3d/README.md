# Wave3D

Wave3D is an incremental single-GPU 3D isotropic elastic forward-modeling
project. The legacy Zhang Wei RTM source is kept unchanged outside this
directory and is used only as a numerical reference.

## Current status

The elastic forward roadmap through the derived-Overthrust benchmark and the
desktop preparation increments is implemented and verified:

- physical and allocated grid dimensions;
- the `[z][y][x]` storage convention with contiguous `x`;
- absorbing-boundary and finite-difference halo sizes;
- simulation time and material extrema;
- conservative pre-run validation and overflow checks;
- optional CUDA discovery and error translation;
- move-only RAII device memory;
- a field-by-field elastic forward memory budget;
- a trivial CUDA allocation/copy/fill round trip;
- distinct physical, physical-grid, and padded-storage coordinates;
- validated homogeneous and horizontal-layer elastic models;
- a dimensionally defined Ricker moment-rate function and symmetric
  moment-tensor source;
- deterministic regular or irregular surface receivers for `vx`, `vy`, and
  `vz`;
- exact rational constants for the standard radius-six, 12th-order staggered
  derivative and its spectral maximum;
- padded `float32` elastic `lambda`, `mu`, `K`, face-buoyancy, and edge-`mu`
  coefficient preparation with constant material extension;
- the accepted coefficient-aware CFL limit and explicit design-band spatial
  and temporal dispersion gates.
- a checked CPU radius-six staggered derivative for `I->H` and `H->I` mappings
  along all three storage axes.
- move-only, zero-initialized, nine-component padded `float32` elastic
  wavefield ownership;
- transparent CPU and CUDA stress/velocity propagation;
- replaceable sponge, six-side CPML, and the production traction-free top plus
  five-side CPML boundary;
- typed optional YAML, CSV, HDF5, and single-file three-component SEG-Y
  production I/O;
- a general YAML/HDF5-to-CUDA-to-single-SEG-Y production command;
- a reproducible reduced SEG/EAGE 3-D Overthrust Vp-to-Vp/Vs/rho converter;
- a qualified `200^3`, 4000-step RTX 5060 envelope;
- const wavefield views, observer/factory/receiver interfaces, and a removable
  default-off checkpoint interface for a possible future RTM phase.
- a batched CUDA forward-session interface and six physical-volume display
  quantities for a future live renderer;
- an optional Qt 6.8/OpenGL modern four-view desktop shell.
- a versioned desktop project store with standardized scientific directories
  and non-overwriting run manifests.

The accepted equation contract is in `docs/ELASTIC_NUMERICAL_SPEC.md`; the
target measurements are in `docs/RTX5060_QUALIFICATION.md`. Viscoelasticity,
RTM, imaging, reverse propagation, and P/S decomposition are intentionally not
implemented.

## Build

CPU-only requirements are CMake 3.24 or newer and a C++17 compiler.

```text
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DWAVE3D_ENABLE_CUDA=OFF
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

CUDA support is opt-in and currently targets the RTX 5060 (`sm_120`):

```text
cmake -S . -B build-cuda -DCMAKE_BUILD_TYPE=Release \
  -DWAVE3D_ENABLE_CUDA=ON -DWAVE3D_BUILD_QUALIFICATION_TOOLS=ON
cmake --build build-cuda --config Release
ctest --test-dir build-cuda -C Release --output-on-failure
./build-cuda/wave3d_cuda_info
compute-sanitizer --tool memcheck --error-exitcode 1 \
  ./build-cuda/wave3d_cuda_tests
```

Keeping CUDA optional preserves independently testable CPU configuration and
memory-planning rules.

Production I/O and RTM-ready checkpoint interfaces are independently opt-in:

```text
-DWAVE3D_ENABLE_YAML=ON
-DWAVE3D_ENABLE_HDF5=ON
-DWAVE3D_ENABLE_SEGY=ON
-DWAVE3D_ENABLE_RTM=ON
```

`WAVE3D_BUILD_QUALIFICATION_TOOLS=ON` adds the one-off CUDA information and
performance qualification programs. It is off by default and is not required
by the production runner or desktop application.

`WAVE3D_ENABLE_RTM` adds interfaces and mock tests only; it does not build an
RTM executable.

The desktop shell is independently opt-in and currently contains no model
loader or solver controller:

```text
cmake -S . -B build-desktop -DCMAKE_BUILD_TYPE=Release \
  -DWAVE3D_ENABLE_CUDA=OFF -DWAVE3D_BUILD_DESKTOP=ON \
  -DCMAKE_PREFIX_PATH=/path/to/qt
cmake --build build-desktop --parallel
QT_QPA_PLATFORM=xcb ./build-desktop/wave3d_studio
```

Native Linux is the release platform. The XCB override is needed by the
current WSLg development environment; a native desktop session normally does
not need it. Install a Chinese-capable font such as Noto Sans CJK SC.

## Prepare the derived Overthrust benchmark

The optional converter requires MatIO, HDF5, and yaml-cpp. It accepts the
audited MATLAB v5 Overthrust volume and creates a canonical 200 x 200 x 187
Wave3D elastic model plus its validated production configuration:

```text
cmake -S . -B build-overthrust -DCMAKE_BUILD_TYPE=Release \
  -DWAVE3D_ENABLE_CUDA=OFF \
  -DWAVE3D_ENABLE_YAML=ON \
  -DWAVE3D_ENABLE_HDF5=ON \
  -DWAVE3D_ENABLE_OVERTHRUST=ON
cmake --build build-overthrust --target wave3d_prepare_overthrust --parallel
./build-overthrust/wave3d_prepare_overthrust \
  path/to/overthrust_3d_vp.mat \
  path/to/overthrust_small.h5 \
  path/to/overthrust_small.yaml
```

The fixed crop, derivation, acquisition, and numerical settings are not CLI
knobs. The command rejects a source that differs from the audited dimensions,
spacing, precision, or derived extrema, then rereads both outputs and requires
exact round trips. Source and generated data stay outside Git; provenance and
expected dataset hashes are in
[`docs/OVERTHRUST_SOURCE_AUDIT.md`](docs/OVERTHRUST_SOURCE_AUDIT.md).
The completed CUDA/SEG-Y evidence is in
[`docs/OVERTHRUST_FORWARD_QUALIFICATION.md`](docs/OVERTHRUST_FORWARD_QUALIFICATION.md).
Local datasets and runs follow the fixed directory contract in
[`data/README.md`](data/README.md).

For the fixed 64-cubed, eight-step CUDA memory-check input, add `--smoke`
before the MAT path. This is a validation profile, not the production model.
The fixed `--refinement-coarse` and `--refinement-fine` profiles use the full
model for 0.8 s at 1 ms and 0.5 ms, respectively; they exist only to reproduce
the Increment 14 time-step convergence gate. Compare their `record.sgy` files
at exact common `(n+1)dt` times with:

```text
python3 tools/verify_overthrust_refinement.py COARSE.sgy FINE.sgy
```

## HDF5 model to one SEG-Y record

Build the general GPU runner with all four required adapters:

```text
cmake -S . -B build-run -DCMAKE_BUILD_TYPE=Release \
  -DWAVE3D_ENABLE_CUDA=ON \
  -DWAVE3D_ENABLE_YAML=ON \
  -DWAVE3D_ENABLE_HDF5=ON \
  -DWAVE3D_ENABLE_SEGY=ON
cmake --build build-run --parallel
```

Prepare a `wave3d.forward.v2` YAML file using
[`configs/forward.example.yaml`](configs/forward.example.yaml) as the starting
point, then run:

```text
./build-run/wave3d_run path/to/run.yaml
```

Relative `model_hdf5_path` and `output_directory` values are resolved from the
YAML file's directory. The HDF5 input must use the Wave3D model schema with
`/vp`, `/vs`, and `/rho` volumes, and its grid and calculated material extrema
must exactly match the YAML declarations. The program validates numerical and
GPU-memory limits before propagation and writes exactly one file per component:

```text
<output_directory>/record_vx.sgy
<output_directory>/record_vy.sgy
<output_directory>/record_vz.sgy
```

For the fixed production Overthrust configuration, independently check the
three-file contract, samples, acquisition headers, and conservative travel
window with:

```text
python3 tools/verify_overthrust_record.py path/to/overthrust_output
```

Render any Wave3D three-component IEEE-float SEG-Y triplet as labeled SVG or
plain PNG receiver gathers with the dependency-free plotting tool:

```text
python3 tools/plot_segy.py path/to/output_directory record_gathers.svg
```

The panels are ordered VX/east, VY/north, and VZ/down. Time increases from top
to bottom, receivers increase from left to right, and blue/white/red encode
negative/zero/positive particle velocity. Each component is independently
clipped at the 99.5th absolute-amplitude percentile by default; use
`--clip-percentile` to change the display scale. The SVG opens directly in a
web browser and embeds all raster data, so it remains a standalone file. For a
square surface array, thin vertical lines separate each x-fastest receiver row
after the two-dimensional geometry is flattened onto the gather axis. Add
`--shared-scale` when the color strength must be comparable across VX, VY, and
VZ; the default independent scales expose weaker component structure more
clearly.

For the audited Overthrust MAT source and its prepared HDF5 model, generate the
original three views with the crop marked, the cropped Vp three views, and the
prepared Vp/Vs/density section with:

```text
python3 tools/plot_overthrust_model.py \
  data/overthrust/source/overthrust_3d_vp.mat \
  data/overthrust/models/elastic_200x200x187.h5 \
  data/overthrust/figures/model \
  --h5dump /path/to/h5dump
```

The tool reads the original compressed MATLAB volume and the actual HDF5
datasets. Its temporary decoded volume is removed after rendering.

For a large square receiver array, select a single center line for a readable
three-component gather and stream-verify the complete file while rendering
surface diagnostic maps:

```text
python3 tools/plot_segy.py --receiver-row 50 --shared-scale \
  data/overthrust/runs/forward_101x101/output \
  data/overthrust/runs/forward_101x101/figures/center_xline_gather.svg
python3 tools/verify_and_plot_segy_grid.py \
  data/overthrust/runs/forward_101x101/output \
  data/overthrust/runs/forward_101x101/figures/receiver_maps.svg \
  --report data/overthrust/runs/forward_101x101/reports/segy_verification.txt \
  --side 101 --spacing 40
```

The dense-grid verifier checks every trace header, coordinate, sample, travel
window, and component before writing peak-amplitude and first-arrival maps.

This is the general HDF5-driven production command. The qualification command
below remains useful only for its fixed, documented target case.

With CUDA and SEG-Y enabled, the qualified target case can write its complete
three-component record using a common filename prefix:

```text
./build-all/wave3d_qualify_rtx5060 4000 record.sgy
```

This creates `record_vx.sgy`, `record_vy.sgy`, and `record_vz.sgy`. Each is a
big-endian SEG-Y Revision 1 file with IEEE `float32` samples, one trace per
receiver, fixed-length traces, and no extended textual headers. Trace
identification codes are 13 (VX/in-line), 14 (VY/cross-line), and 12
(VZ/vertical). This qualification executable uses its fixed documented model
and geometry; use `wave3d_run` above for general HDF5 models.
