# Legacy 3D Source Audit

Audit date: 2026-09-08. This is a read-only provenance and risk record. No
legacy file was modified or copied into Wave3D.

## Provenance and licensing boundary

The supplied archive is located outside the active project at

```text
../2D_and_3D_elastic_reverse_time_migration-master/
  2D_and_3D_elastic_reverse_time_migration-master/3D_elastic_RTM/
```

It corresponds to the public
[`GeophysicsLab/2D_and_3D_elastic_reverse_time_migration`](https://github.com/GeophysicsLab/2D_and_3D_elastic_reverse_time_migration)
repository. The inspected upstream `master` revision is
[`1b6f4c7`](https://github.com/GeophysicsLab/2D_and_3D_elastic_reverse_time_migration/commit/1b6f4c784492b99c2cafd5298b5b9ee893f45acd).
Neither the supplied archive nor the upstream repository exposes a license
file or GitHub-detected license. Publication on GitHub alone does not grant
copying or redistribution rights. Wave3D may use observable scientific ideas
as leads, but must independently derive its implementation and must not copy
legacy code or comments.

Relevant supplied-file SHA-256 values are:

| File | SHA-256 |
| --- | --- |
| `3D_elastic_modeling_parameter` | `519ae391a914a3755cf1fc3418e785eb583c27c777a000764ea117ca19464b37` |
| `3D_elastic_modeling_parameter_sh` | `287e08dde54e8ba675180e6ade414c50649e6810aadff07fd7ab951798481298` |
| `3D_elastic_modeling_typedef_struct` | `a9d2f314e02bdcfb3db617937498525b0e9b088b9361305ba4ebfeb60d8f517d` |
| `3D_forward_or_back_together.cu` | `c6bd401b4fda518857c543f3a502511d82548f421673d193cd3a087097c6ca45` |
| `3D_zzzzz` | `1fcc6ef4c33091d9d6dd08565faa2e036275247217369c990e70b881e6a3e0c1` |
| `makefile` | `2e30503714fc3395dd8c6a7df49397863eaf54716b8850cadb651d9cc2eaba8f` |
| `zzzzz` | `748827918dc7b8712549466f7d53333e5007e24ff377efcb1eff313c24a064b2` |

These hashes identify the inspected evidence; they are not a dependency or a
request to commit the archive.

## Supplied file index

| File | Observable role | Audit result |
| --- | --- | --- |
| `3D_elastic_modeling_parameter` | global variables, `radius=6`, dimensions, model/acquisition/boundary state | declaration fragment, not a translation unit |
| `3D_elastic_modeling_parameter_sh` | Seismic Unix `getpar` parsing | no range, unit, CFL, or memory validation |
| `3D_elastic_modeling_typedef_struct` | monolithic host/device allocation and zeroing | combines forward, reverse, P/S, imaging, I/O, and multi-GPU state |
| `3D_forward_or_back_together.cu` | forward/backward launch order | calls absent kernel definitions; tightly coupled to RTM |
| `3D_output_file.cu` | wavefield and image output | full-volume staging and global state |
| `3D_zzzzz` | 3D raw I/O and model extension helpers | confirms `[z][y][x]` address expression with x contiguous |
| `zzzzz` | mixed 2D/3D utilities, Ricker functions, FD coefficients | contains the six radius-six decimals but many unrelated utilities |
| `makefile` | expected build | requires absent main source, CUDA 5.5 samples, Seismic Unix, CUBLAS |
| `smooth_3d` | binary or placeholder artifact | not reviewable source |
| `log_old.txt` | historical runtime text | not a reproducible test record |

The file named `zzzzz` mixes allocation, signal processing, image processing,
and numerical helpers; its name and contents provide no stable module boundary.

## Deleted upstream files

The supplied and current upstream trees omit the makefile target source
`3D_elastic_modeling.cu` and the included `3D_elastic_modeling_kernel.cu`.
Upstream history shows explicit deletions:

- [`3D_elastic_modeling.cu` deletion](https://github.com/GeophysicsLab/2D_and_3D_elastic_reverse_time_migration/commit/8a40a499b311d4bbbcd6a4ccf29065daa148c6ee)
  at commit `8a40a49`.
- [`3D_elastic_modeling_kernel.cu` deletion](https://github.com/GeophysicsLab/2D_and_3D_elastic_reverse_time_migration/commit/62c905aee77ae7f62cab5bc3fced0e6d54404e8a)
  at commit `62c905a`.

For audit only, the preceding upstream revision `e731991` was inspected through
GitHub's immutable raw URLs. The historical main file has Git blob
`d6acc0afeb3725f9a04b5e4aa27e0703629df9b6`, and the kernel file has Git blob
`ad173f4709a527028e3420a1ffa297829e7cf991`. They confirm the missing definitions
once existed, but they are not part of the user-supplied current source and are
not copied into Wave3D.

## What the legacy evidence confirms

The current fragments plus the historical revision establish only these facts:

- A complete staggered-grid style velocity-stress implementation was intended.
- `radius` is fixed at six.
- The first-derivative helper stores
  `1.2213363647`, `-0.096931457519`, `0.017447662353`,
  `-0.0029672895159`, `0.00035900539822`, and
  `-0.00002184781161`.
- Independent moment matching in `ELASTIC_NUMERICAL_SPEC.md` shows these are
  rounded standard 12th-order Taylor staggered coefficients.
- The historical driver treats input `dt` as milliseconds, constructs a Ricker
  pulse using `dt/1000`, and sets derivative-update multipliers to
  `dt/(1000*h)`.
- The historical forward order is source addition to the three normal stresses,
  stress update, velocity update, receiver sampling, and buffer exchange.
- The explosion-like source adds the same unscaled sample to `Txx`, `Tyy`, and
  `Tzz`.
- Receiver output stores three arrays corresponding to `Vx`, `Vy`, and `Vz`,
  with x fastest in the receiver index.
- The volume address form is `z*nx*ny + y*nx + x`, matching Wave3D's chosen
  `[z][y][x]` layout.

These observations corroborate broad intent. They do not establish physical
normalization, a validated sign convention, interface accuracy, or a complete
reproducible 3D method.

## Important limitations and apparent defects

The following prevent direct reuse or a reproduction claim:

1. **Current build is incomplete.** The makefile requires the deleted main
   file, and the launch fragment calls kernel definitions absent from the
   supplied tree.
2. **No license is supplied.** Code copying is excluded independently of
   technical quality.
3. **Source units are undefined.** A dimensionless Ricker sample is added
   directly to three stress arrays with no moment scale, cell-volume factor,
   `dt`, interpolation, or polarity contract.
4. **Component staggering is sampled incorrectly for a physical receiver.**
   Historical receiver code reads `Vx`, `Vy`, and `Vz` at one identical array
   index even though the update stencils imply different logical locations.
5. **Heterogeneous coefficients are not prepared at staggered locations.**
   The historical velocity updates use density from one common index, and
   shear updates use `rho*Vs^2` from one common index rather than documented
   face/edge averages.
6. **Attenuation is fused into every update.** The `att` factor implements a
   sponge-like damping formula inside physics kernels, preventing clean elastic
   and boundary verification.
7. **Ownership is unsafe and monolithic.** One structure contains duplicated
   forward/backward fields, decomposed fields, images, excitation fields,
   receiver staging, model arrays, and multi-GPU exchange state.
8. **Some CUDA initialization calls pass pointer addresses rather than target
   buffers.** Examples use `cudaMemsetAsync(&mgdevice[i].att_h,...)` and the
   same pattern for several host wavefield pointers after allocation. This can
   overwrite structure state rather than initialize the allocation.
9. **Pinned host memory is treated as device memory in initialization paths.**
   The code uses CUDA memset calls without an explicit, checked ownership and
   accessibility contract.
10. **Launch errors are generally detected only by broad synchronizations.**
    There is no local kernel-launch context comparable to Wave3D's error layer.
11. **Input validation is absent.** Dimensions, positions, receiver extents,
    material positivity, CFL, dispersion, integer products, and VRAM budget are
    not rejected before allocation or launch.
12. **Time units are implicit.** Input `dt` is silently divided by 1000 inside
    several locations, whereas Wave3D public APIs use seconds.
13. **The alternative `make_ricker_better` helper appears dimensionally
    inconsistent.** It multiplies an expression already containing seconds by
    `sdt` a second time. The active historical path uses another helper, so no
    behavior is inferred from it.
14. **Source and multi-GPU z mapping use modulo arithmetic and a hard-coded
    halo adjustment.** Boundary cases are not demonstrated by tests.
15. **The repository contains no committed analytical, convergence, CPU/GPU,
    energy, or boundary-reflection test suite.** Runtime images and logs cannot
    replace these checks.

## Wave3D reuse decision

Wave3D independently accepts only the following high-level choices: a
radius-six staggered derivative, `[z][y][x]` storage, an explosion preset,
Ricker excitation, and three-component acquisition. Exact coefficients are
regenerated from documented moment equations. All continuum equations,
stagger locations, material averages, time levels, source sign/units, CFL, and
tests are specified independently in `ELASTIC_NUMERICAL_SPEC.md` and traced to
primary literature where accessible.

No legacy source file is compiled, linked, vendored, or required at runtime.
