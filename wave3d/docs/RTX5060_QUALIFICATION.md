# RTX 5060 Elastic Forward Qualification

Qualified on 2026-09-09 with CUDA 13.2.78, driver 595.84, and an NVIDIA
GeForce RTX 5060 (compute capability 12.0). This report covers the current
accuracy-first, single-GPU, isotropic elastic forward path. It does not cover
attenuation, RTM, multi-GPU execution, or full-volume output at every step.

## Reproduction

```text
cmake -S . -B build-qualify -DCMAKE_BUILD_TYPE=Release \
  -DWAVE3D_ENABLE_CUDA=ON -DWAVE3D_ENABLE_HDF5=ON \
  -DWAVE3D_BUILD_QUALIFICATION_TOOLS=ON
cmake --build build-qualify --parallel
./build-qualify/wave3d_qualify_rtx5060 4000 /tmp/wave3d_traces.h5
compute-sanitizer --tool memcheck --error-exitcode 1 \
  ./build-qualify/wave3d_qualify_rtx5060 1
```

The qualification executable validates the existing CFL/design-band rules,
queries free memory immediately before planning, rejects a plan above 80% of
that free memory after reserve, owns all device state before timing, performs
no per-step allocation, and downloads every final field for finite-value and
surface-traction checks.

## Qualified case and result

| Quantity | Result |
|---|---:|
| Physical / allocated grid | `200^3` / `252 x 252 x 232` |
| Allocated cells | 14,732,928 |
| Boundary | free top + five-side 20-cell CPML |
| Time sampling | 4000 x 0.5 ms = 2 s |
| Setup | 674.045 ms |
| Propagation | 292,785.148 ms |
| Time per step | 73.196 ms |
| Throughput | 201.280 million allocated-cell steps/s |
| Owned device memory | 2023.693 MiB |
| Observed free-memory reduction | 2090.250 MiB |
| Plan including 512 MiB reserve | 2704.294 MiB |
| 80% contemporaneous budget | 5885.850 MiB |
| Trace download | 0.091 ms |
| HDF5 output | 0.645 ms / 440,408 bytes |
| Host peak RSS during final validation | 2,219,816 KiB |
| Final surface traction | exactly 0 Pa |
| Wavefield/CPML/trace finite scan | passed |
| Target-size Compute Sanitizer memcheck | 0 errors |

Final regressions were CPU Release 15/15, CUDA Release 20/20, and
ASan/UBSan 15/15. Free-surface memcheck/initcheck passed the full physical
case; racecheck/synccheck passed the focused projection kernels with zero
hazards/errors.

Device free memory is time-dependent; a production invocation must plan again
rather than rely on these sampled values. The planner is conservative because
it includes three padded model volumes, while this executable prepares
coefficients on the host and retains only coefficients on the device.

## Profile finding

Nsight Systems 2025.6.3 measured ten target-size steps:

- CPML stress kernel: 51.7% of GPU kernel time, 38.113 ms average.
- CPML velocity kernel: 48.1%, 35.408 ms average.
- Free-surface, source, and receiver kernels combined: less than 0.2%.

Nsight Compute 2026.1.1 measured one stress launch at 36.08 ms. It reached
87.54% compute throughput but only 13.41% memory throughput, with 56 registers
per thread, 66.67% theoretical occupancy, and 60.16% achieved occupancy. FP64
was the dominant pipeline because the accepted kernels accumulate the
radius-six derivative and constitutive update in double precision before
storing float32 state.

The measured optimization target is therefore the numerical main kernels,
especially FP64 work/register pressure—not I/O, source sampling, or the free
surface. This gate deliberately retains the clear CPU-matching kernels.
Changing accumulator precision, fusing kernels, or changing CPML state layout
requires a separate predeclared numerical/performance increment with complete
CPU/GPU and physical-regression evidence.
