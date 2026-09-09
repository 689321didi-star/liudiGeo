# Increment 13 HDF5-to-SEG-Y Production Pipeline Plan

Status: passed on 2026-09-09. Fixed before implementation; no contract or
acceptance criterion was relaxed after testing began.

## Goal

Add a real production command that loads a typed YAML run configuration and a
canonical Wave3D HDF5 elastic model, propagates the configured single-source
three-component experiment on the selected CUDA device, and writes exactly one
standard `record.sgy` file.

The fixed qualification executable remains a qualification tool. It must not
be presented as a general HDF5-driven application.

## Increment 13a — Model-bound configuration

The existing YAML `homogeneous_material` value cannot safely validate a
heterogeneous HDF5 model. Replace it with six explicit `material_extrema`
values already represented by `SimulationConfig`. The YAML schema becomes
`wave3d.forward.v2`; unsupported or missing schemas fail. Round-trip and
malformed-input tests must pass before building the runner.

At execution, the declared extrema must match extrema calculated from every
cell of the loaded HDF5 model. This prevents CFL and dispersion checks from
using optimistic metadata.

Status: passed on 2026-09-09. The focused Release round trip preserved six
distinct extrema and rejected malformed and v1 inputs.

## Increment 13b — CUDA production runner

Add `wave3d_run CONFIG.yaml`, built only when CUDA, YAML, HDF5, and SEG-Y are
all enabled. Relative model and output paths are resolved against the YAML
file's directory. The output path is always
`<output_directory>/record.sgy`.

The runner must:

1. load and validate YAML and HDF5 inputs;
2. require exact grid and declared/actual material-extrema agreement;
3. validate the SEG-Y Revision 1 sample axis before GPU allocation;
4. plan against current free GPU memory with the accepted 80% plus 512 MiB
   reserve rule;
5. prepare coefficients, source/receiver interpolation, CPML, and the optional
   traction-free top;
6. execute every configured time step with the accepted CUDA ordering;
7. download receiver traces and write one combined SEG-Y file;
8. report resolved paths, shape, device/memory values, and timings.

CPML retains the accepted parameters: target reflection `1e-3`, polynomial
power 2, and `kappa_max=1`. Its frequency is the configured source Ricker
frequency and its velocity is the actual model maximum Vp. A free top disables
only z-min CPML; an absorbing top enables all six sides.

## Acceptance

- YAML v2 round-trip preserves all six material extrema and rejects old,
  missing, or malformed schemas.
- A small heterogeneous HDF5 model completes through the public production
  pipeline on the RTX 5060 and produces exactly one `record.sgy`.
- Independent reads verify the expected `3*nreceiver` traces, sample count,
  component sequence, finite unnormalized values, and a nonzero propagated
  signal. No component files or JSON sidecars appear.
- Grid or extrema mismatches fail before propagation and leave no SEG-Y file.
- The all-options Release suite, focused I/O sanitizer suite, optional-I/O-off
  CPU suite, and supported build-option combinations remain green.

## Observed result

- YAML v2 round-trip and schema rejection passed before runner work began.
- The end-to-end test loaded a two-layer `9 x 9 x 9` canonical HDF5 model,
  resolved relative paths, ran 40 free-surface/five-side-CPML CUDA steps, and
  wrote one SEG-Y file for three receivers. Its nine traces were finite,
  contained a nonzero propagated signal, and repeated codes 14/13/12.
- Deliberate grid and material-extrema mismatches failed before propagation and
  left no SEG-Y file.
- The all-options Release suite passed 26/26. Compute Sanitizer memcheck of the
  complete pipeline reported zero errors. Focused I/O ASan/UBSan passed 4/4
  with leak detection disabled, and optional-I/O-off CPU Release passed 16/16.
- CUDA builds without adapters, with HDF5 only, and with SEG-Y only remained
  successful; the production runner is present only when all four required
  features are enabled.
