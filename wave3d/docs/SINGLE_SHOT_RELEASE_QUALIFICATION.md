# Single-shot release qualification

**Candidate date:** 2026-09-21 Asia/Shanghai

**Candidate branch:** `feat/desktop-increment-31-release-qualification`

**Verdict:** Candidate checks pass; native Linux graphics gate remains open.

## Environment

- Development OS: Ubuntu 26.04 under Microsoft WSL2/WSLg
- GPU: NVIDIA GeForce RTX 5060 Laptop GPU, 8,151 MiB
- Driver: 577.05
- CUDA compiler: 12.9.86
- Product graphics platform required by D048: native Linux

WSL2 is valid for development and numerical evidence. It is not accepted as
the final product graphics platform, so the evidence below does not complete
the release milestone.

## Build and test matrix

| Boundary | Configuration | Result |
| --- | --- | --- |
| Complete desktop | Release, CUDA/YAML/HDF5/SEG-Y/desktop on, RTM off | 37/37 passed |
| RTM interface | Release, CUDA/YAML/HDF5/SEG-Y/RTM on, desktop off | 30/30 passed |
| Dependency-free core | Release, all optional features off | 17/17 passed |
| HDF5 desktop | Release, desktop/HDF5 on, CUDA/YAML/SEG-Y/RTM off | 23/23 passed |

The complete and RTM builds use explicit `-O3 -DNDEBUG` C++ Release flags.
RTM-on adds only the header-only checkpoint and task/result interface tests.
RTM-off contains no RTM, reverse-propagation, imaging, or decomposition target.

## Dense Overthrust evidence

The accepted single-shot configuration used the canonical 200 by 200 by 187
Overthrust model, 1 ms sampling, 3 s duration, and a 101 by 101 surface array.

```text
physical cells:       7,480,000
allocated cells:     13,907,376
receivers:               10,201
samples/component:        3,000
planned bytes:    3,073,674,920
propagation:        200,296.174 ms
wall time:                 3:25.01
maximum RSS:             724,204 KiB
```

The independent verifier reported 10,201 traces in each of three Revision 1,
big-endian IEEE-float SEG-Y files, 3,000 samples per trace, 1,000 microseconds
sample interval, 91,809,000 finite samples, and 86,284,671 nonzero samples.
Every receiver arrival was inside 0.294–0.857 s; the expected Mxy polarity
matched 9,895 of 10,000 non-axis receivers. Result: `PASS`.

```text
Vx cb3a9cea8f81e0aa101e4a144c2110c353bed47ace774e71ee91f38009583fe3
Vy 6adfb7d65dcfac1cd1a74f034e102e0709e4838128750cd59272ea4b852cbfaf
Vz 3652dd13253977f5601ad72075f2f8653d8be629435b76bf62f0b070d8a87f90
```

The duplicate 374,591,520-byte SEG-Y product was deleted after verification.
The canonical model checksum was unchanged.

## Desktop evidence

Under WSLg/XCB, both an empty shell and the real Overthrust review project
completed the OpenGL smoke. All four `QOpenGLWidget` contexts, the volume
shader, static texture, volume frame, source marker, and receiver markers were
valid. The reviewed 1440 by 900 image shows coherent Overthrust layering,
aligned XY/XZ/YZ sections, the source, and the dense receiver array. Scrollable
side panels and a hidden-until-selected results dock keep the accepted viewport
size without discarding controls.

## Remaining native Linux gate

On a native Linux NVIDIA host, configure the complete desktop build from this
commit, run its full `ctest`, then execute:

```bash
build/desktop24/wave3d_studio \
  --project build/desktop19/review_project \
  --module workspace --slice-indices 100,100,93 \
  --volume-camera 0.75,-0.45,2.2 --smoke-test

build/desktop24/wave3d_studio \
  --project build/desktop19/review_project \
  --module workspace --slice-indices 100,100,93 \
  --volume-camera 0.75,-0.45,2.2 \
  --capture-shell native-linux-release.png
```

Open the completed result workspace once and verify font/input behavior and
stable frame pacing during a desktop run. Record GPU, driver, test totals, and
the screenshot checksum here. Only then may Increment 31 and the single-shot
release milestone be marked complete.
