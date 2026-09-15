# Increment 17 Predeclared Desktop Shell Plan

## Purpose

Establish the removable Qt/OpenGL desktop build boundary and the accepted
four-view application shell. This increment does not load a model, launch a
forward session, upload a volume texture, write output, or implement project
state.

## Fixed scope

- Add default-off `WAVE3D_BUILD_DESKTOP`.
- Find Qt 6 Core, Gui, Widgets, OpenGL, and OpenGLWidgets only when enabled.
- Build `wave3d_studio` without adding Qt to any solver, I/O, test, or CLI
  target.
- Create one main window with a larger 3-D viewport and stacked XY/XZ/YZ
  viewports, plus module navigation, display-field selection, run controls,
  progress, log dock, and reserved snapshot action.
- Default the shared field selector to velocity magnitude and expose the six
  accepted display choices.
- Keep snapshot storage visibly reserved and disabled.
- Apply one centralized modern dark theme with clear hierarchy, compact
  scientific controls, and consistent spacing.
- Provide deterministic shell inspection and review-image capture modes plus a
  focused Qt test.

## Acceptance

1. A default CPU-only build neither searches for Qt nor exposes the desktop
   target.
2. Enabling the desktop option with Qt 6.8 builds the application and focused
   test.
3. The test verifies exactly four named viewports, a 3-D-left/three-slice-right
   layout, the six shared display choices, velocity-magnitude default, disabled
   snapshot action, initial idle run state, and required module navigation.
4. A WSLg smoke launch creates the Qt window and an OpenGL context, then exits
   automatically; this is development evidence only.
5. The existing CUDA/YAML/HDF5/SEG-Y suite remains green.
6. CPU-only and RTM-off build boundaries remain intact.

## Deferred work

Project persistence, model loading, SEG-Y model import, real rendering,
source/acquisition editors, queues, worker threads, CUDA/OpenGL interoperation,
and SEG-Y result viewing belong to later increments.

## Result

Verified on 2026-09-15. The Qt-enabled CPU/desktop suite passed 18/18 and the
full CUDA/YAML/HDF5/SEG-Y suite passed 28/28. `--inspect-shell` reported four
viewports, six display fields, and `speed` as the default. The WSLg XCB smoke
created all four OpenGL contexts. The 1440 x 900 composited capture was
visually inspected after installing Noto Sans CJK; Chinese text, control
spacing, and the 2:1 volume-to-slice layout rendered correctly.
