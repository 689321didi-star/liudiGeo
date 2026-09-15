# Desktop Development Environment Qualification

**Date:** 2026-09-15 Asia/Shanghai

## Verdict

The current hardware is sufficient for first-release Wave3D forward desktop
development and live four-view visualization. The active WSL2/WSLg instance is
accepted for development checks only. Final packaging, graphics behavior, and
end-to-end qualification must run on native Linux, which is the fixed product
platform.

## Observed environment

- GPU: NVIDIA GeForce RTX 5060 Laptop GPU, compute capability 12.0
- GPU memory: 8,151 MiB total, 7,810 MiB free during inspection
- Driver/runtime view: driver 577.05, CUDA 12.9
- CUDA compiler: nvcc 12.9.86
- CPU compiler: conda-forge GCC/G++ 14.4.0
- CMake: 4.4.3
- System memory: 7.5 GiB total, 6.0 GiB available during inspection
- Swap: 2.0 GiB
- Workspace filesystem: 950 GiB available
- Development OS: Ubuntu 26.04 under WSL2
- Display transport: WSLg X11 and Wayland sockets present
- Desktop toolkit: Qt 6.8.4 with Core, Gui, Widgets, OpenGL, and OpenGLWidgets
- Chinese UI font: Noto Sans CJK SC, installed in the user font directory

## Capacity assessment

The accepted dense Overthrust forward plan required 3,073,674,920 bytes under
the existing conservative memory calculation. Three target-size physical
display volumes add 89,760,000 bytes. This combined planned allocation plus
reserve remains well below 80% of the observed free GPU memory. The desktop
controller must repeat the live memory query and include every persistent
render buffer before each run.

System memory is adequate for one forward session, Qt, the current model, and
the pinned-host visualization fallback. It is not a basis for concurrent full
runs or future RTM sizing. The optional run queue therefore executes one GPU
task at a time. RTM hardware requirements will be qualified separately after
checkpoint/recomputation policy is defined.

## Release limitations

WSLg can exercise Qt event handling and OpenGL context creation, but it adds a
Windows display/driver translation layer. Native-Linux testing remains
mandatory for CUDA/OpenGL interoperation, frame pacing, font/input behavior,
packaging, and long-running stability. No WSL result may be reported as final
native-Linux graphics qualification.

## Development smoke result

The Increment 17 application built against Qt 6.8.4. Under WSLg's XCB path,
all four `QOpenGLWidget` viewports created valid OpenGL contexts and the process
exited successfully. A 1440 x 900 composited review image rendered the Chinese
interface correctly after the Noto CJK installation. The offscreen platform is
used only for widget-contract tests because it does not support
`QOpenGLWidget`.

For a fresh build with the local Conda compiler, explicitly retain Release
flags with `-DCMAKE_CXX_FLAGS_RELEASE="-O3 -DNDEBUG"`. In this environment the
first explicit-compiler configure initialized that cache entry as empty, which
made the CPU physics test run unoptimized. Reconfiguration with the fixed flags
restored the expected test duration; this was a build-configuration issue, not
a solver regression.
