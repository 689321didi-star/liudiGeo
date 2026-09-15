# Derived SEG/EAGE 3-D Overthrust Forward Qualification

Qualification date: 2026-09-09

## Qualified case

The qualified input is the derived isotropic solid elastic benchmark defined
in `OVERTHRUST_SOURCE_AUDIT.md`:

```text
physical shape [z,y,x]: 187 x 200 x 200
spacing:                25 x 25 x 25 m
physical cells:         7,480,000
allocated cells:        13,907,376
boundary:               traction-free z-min; 20-point CPML elsewhere
source:                 Mxy=1e12 N*m at (2500,2500,1100) m
receivers:              11 x 11 surface grid, 121 three-component stations
time axis:              3000 samples at 1 ms
```

The run used an NVIDIA GeForce RTX 5060 with 8,151 MiB total memory and driver
595.84. Large source, HDF5, YAML-run, SEG-Y, memcheck, and plot artifacts stay
under ignored `data/overthrust/`.

## Canonical-model gate

`wave3d_prepare_overthrust` decoded only the audited MAT hyperslab, performed
the accepted elastic derivation, wrote canonical HDF5 and
`wave3d.forward.v2`, and reread both exactly. Independent `h5dump -b LE`
extraction matched all preimplementation dataset SHA-256 oracles:

```text
Vp   5f30736af9dfdba8ca9e68bd7e29444534a9cf0736cc02af6251820acc26a7ee
Vs   86ad455e471a824df75ad1c9685e34ff81986b9d57f9c4a7775d41e2e718921f
rho  ba34f51ca8c63683511c847e11fe4f2de7e2e43befa574da3ab3d92fad8dbb06
```

All 7,480,000 values of each property were finite. Independent formula
recalculation matched every derived Vs/rho float, every cell had positive bulk
modulus, and horizontal/x-z/y-z slices retained consistent complex structure
and depth direction. The numerical report gave:

```text
CFL dt limit:                           0.00152702393047 s
configured CFL fraction:                0.654868584601
minimum S-wave points/design wavelength: 6.27582139757
```

The real preparation path passed ASan/UBSan.

## CUDA smoke and time refinement

The fixed 64-cubed, eight-step Overthrust profile completed through the
production runner. Compute Sanitizer memcheck reported zero errors. Its only
output was one 10,944-byte SEG-Y with 27 receiver-major 14/13/12 traces and
216/216 finite samples.

The full model then ran for 800 samples at 1 ms and 1600 samples at 0.5 ms.
`tools/verify_overthrust_refinement.py` aligned coarse sample `n` with fine
sample `2n+1` at common `(n+1)dt` times and reported:

```text
normalized L2, all traces: 0.000433639742266
normalized L2, VX:         0.000454674475987
normalized L2, VY:         0.000432879948454
normalized L2, VZ:         0.000387158120674
acceptance threshold:      0.05
result:                    PASS
```

Propagation took 55.99 s and 111.63 s respectively. The production 1 ms time
step therefore remained unchanged.

## Full production run

The command was:

```text
build-all/wave3d_run data/overthrust/runs/forward_11x11/config.yaml
```

It completed all 3000 steps without a CUDA error:

```text
planned required bytes: 2,710,794,920
live allowed budget:    6,078,123,212
input load:             62.410 ms
GPU setup:              596.676 ms
propagation:            207,772.727 ms
trace download:         0.387 ms
SEG-Y write:            7.967 ms
wall time:              208.68 s
maximum host RSS:       728,976 KiB
```

The output directory contained exactly `record.sgy`. The independent
`tools/verify_overthrust_record.py` parser reported:

```text
file bytes:               4,446,720
SEG-Y:                    Revision 1, big-endian, IEEE float32, fixed traces
traces:                   363, receiver-major 14/13/12 component order
samples per trace / dt:   3000 / 1000 us
finite / total samples:   1,089,000 / 1,089,000
nonzero samples:          1,015,337
maximum absolute sample:  6.44325045869e-05 m/s
VX energy:                9.25727935258e-06
VY energy:                1.00884045397e-05
VZ energy:                3.09450894860e-06
observed SHA-256:          1d506a86f63587b06fb3171853eecc40b755517d6dda877970511613e2aa0241
```

Using a per-receiver threshold of `1e-4` times its three-component peak, first
significant samples ranged from 0.321 to 0.857 s. Every receiver lay between
its conservative direct bound `distance/max(Vp)-dt` and its late bound
`Ricker peak delay+distance/min(Vs)+dt`; the aggregate bound ranges were
0.1823-0.5048 s and 1.1133-2.4835 s.

## Regression matrix

```text
all options Release:                 28/28 passed
focused I/O ASan/UBSan:               5/5 passed
optional-I/O-off CPU Release:        17/17 passed
CUDA no-I/O compile:                  passed
CUDA HDF5-only compile:               passed
CUDA SEG-Y-only compile:              passed
Overthrust+all-adapters compile/test: passed
```

Increment 14 is complete. This qualification does not authorize or claim
attenuation, anisotropy, fluids, RTM, imaging, or another geological model.

## 2026-09-15 pre-optimization reproducibility check

The unchanged 101 x 101 receiver experiment was repeated before solver
optimization. All 26 repository tests passed first. The full run exited zero
and produced a 374,584,320-byte SEG-Y whose SHA-256 was
`3854ec1776b3fbfa5ed3000869c99341c7aa67a03980295a04fe14e8087914c9`.
It was byte-for-byte identical to the accepted canonical dense output.

The independent verifier scanned all 30,603 headers and 91,809,000 samples;
all samples were finite, all 10,201 arrivals satisfied their bounds, and the
Mxy polarity test matched 9,895/10,000 off-axis receivers. Propagation took
241,562.642 ms, 20.54% above the preceding 200,400.441 ms measurement, so
performance changes will use controlled repeated runs. The duplicate SEG-Y
and the temporary validation plot were removed after verification.
