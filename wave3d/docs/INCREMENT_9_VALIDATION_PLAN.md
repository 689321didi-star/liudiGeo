# Increment 9 Predeclared Production-I/O Validation Plan

Status: passed on 2026-09-09. Fixed before the first production-I/O round
trip; no threshold or contract was relaxed after testing began.

All adapters are outside propagation and controlled through independent CMake
options.  The existing CPU and CUDA suites must build with every optional I/O
dependency disabled.

## Canonical contracts

- YAML resolves grid/boundary/time/numerical values, homogeneous material,
  all six source-tensor entries, wavelet metadata, physical receiver
  coordinates, model path, and output path. Loading then emitting and loading
  again must preserve every typed value.
- CSV irregular geometry has the exact header `x_m,y_m,z_m`; a round trip must
  preserve each binary64 coordinate and receiver order.
- HDF5 model datasets `/vp`, `/vs`, `/rho` have shape `[nz,ny,nx]`, `float32`
  values, SI-unit attributes, and complete grid metadata. Three-component
  trace datasets have shape `[1,nreceiver,nt]`, exact binary64 `dt`, receiver
  coordinates, source metadata, axes, units, and orientations. Sparse velocity
  snapshots store step, exact time, storage indices, and three components.
- SEG-Y writes one Rev-1 file per VX/VY/VZ component with IEEE `float32`
  samples, big-endian binary/trace headers, receiver coordinates, sample count,
  the rounded microsecond interval, and a JSON sidecar carrying exact `dt`, SI
  coordinates, source metadata, component orientation, axes, and the explicit
  fact that samples are unnormalized. A focused reader validates emitted
  headers and values. The optional model converter accepts only fixed-length
  IEEE-float SEG-Y volumes whose trace/sample counts match the declared grid.

## Acceptance

Round trips use a non-cubic layered model, irregular off-grid receivers, a
general moment tensor, nontrivial three-component traces, and a sparse
snapshot. Shapes, values, units, `[z][y][x]` axes, receiver-major traces,
exact `dt`, coordinates, source metadata, and component identities must be
unchanged. Truncated, malformed, non-finite, dimensionally inconsistent, or
unsupported-format inputs must fail explicitly.

The enabled-I/O Release and ASan/UBSan tests must pass, followed by the
unchanged CPU-only and CUDA Release suites with I/O off, before Increment 10.

## Observed result

- Enabled YAML/HDF5/SEG-Y Release: 18/18 tests passed.
- Enabled-I/O ASan/UBSan: 4/4 focused I/O tests passed.
- All optional I/O disabled: CPU Release 15/15 and CUDA Release 20/20 passed.
- The first YAML fixture was rejected because its declared design frequency
  gave fewer than five shear-wave points per wavelength along z. The fixture
  frequency was corrected from 25 Hz to the valid 20 Hz; serialization code
  and acceptance criteria were unchanged.
- Round trips preserved binary32 model/trace values, binary64 coordinates and
  `dt`, source metadata, receiver order, component identity, axes, units, and
  sparse snapshot indices. Malformed CSV/YAML/HDF5 and truncated SEG-Y were
  rejected.
