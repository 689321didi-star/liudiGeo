# Increment 25 — Live wavefield display

## Goal

Display completed CUDA wavefield steps in the existing 3-D volume and linked
XY/XZ/YZ sections while preserving the verified forward-run lifecycle. Every
presented four-view update must refer to one immutable frame identity.

## Scope

- Add a move-only CUDA pinned-host buffer owner and a direct physical-volume
  download overload without exposing raw allocation outside the CUDA layer.
- Extend `CudaForwardJob` with read-only grid/time metadata and reusable
  extraction of Vx, Vy, Vz, velocity magnitude, divergence, or curl magnitude
  through the existing verified CUDA visualization kernel.
- Give the desktop worker two persistent pinned staging buffers. After selected
  completed batches it extracts, downloads, and explicitly normalizes one
  presentation frame. If both host buffers are still in use, it drops only the
  obsolete display opportunity; propagation and receiver sampling continue.
- Publish immutable frame metadata containing sequence, completed step,
  physical time, field, physical display range, dimensions, and shared pinned
  storage. Field changes and display interval changes apply at later batch
  boundaries.
- Composite the live scalar over the static model in the OpenGL volume and the
  three sections. Signed fields use a zero-centred diverging map; magnitudes use
  a nonnegative sequential map. Source, receivers, crop, slice indices, and
  physical orientation remain synchronized.
- Add live opacity, threshold, display interval, frame/time/range diagnostics,
  and one shared field selector. Clear the live layer and restore the static
  scene after completion, cancellation, or failure.

## Invariants

- CUDA setup, propagation, visualization extraction, device-to-host transfer,
  and pinned-buffer allocation remain on the worker thread. Shared frame
  ownership keeps storage alive until the GUI releases the last reader.
- The GUI consumes only completed immutable frames and never reads a buffer
  while CUDA or the worker can write it.
- A four-view presentation uses one frame sequence. The renderer may skip a
  superseded display frame but may never skip a solver step or receiver sample.
- Normalization is presentation-only and reversible from the stored physical
  range. SEG-Y and scientific solver arrays retain SI values.
- Live rendering errors fail the display layer visibly without corrupting the
  solver or its final SEG-Y publication.
- CUDA/OpenGL registered-buffer interoperation remains outside this increment;
  pinned staging is the compatibility baseline to measure first.

## Acceptance criteria

1. Pinned-buffer tests prove ownership, size checks, move behavior, and exact
   CUDA device-to-host transfer.
2. Production-job tests extract more than one field after a completed batch and
   preserve grid, step, time, and source-wavefield behavior.
3. Worker tests prove frame identity, normalization/range metadata, field
   switching, interval selection, buffer reuse, and nonblocking frame drops.
4. CPU presentation tests prove XY/XZ/YZ orientation, zero-centred signed
   colour mapping, magnitude mapping, and one-frame synchronization metadata.
5. Desktop tests run a real small CUDA job and observe live volume plus all
   three live sections before successful SEG-Y/result publication.
6. A real Overthrust smoke run measures one-step propagation plus extraction,
   pinned transfer, normalization, and OpenGL upload sufficiently to document
   the first display-interval recommendation.
7. Combined and optional build regressions pass and are recorded in
   `docs/HANDOFF.md`.

## Expected limitation

The first implementation uploads completed pinned frames to OpenGL with
`glTexSubImage3D`. It establishes correctness and timing without CUDA/OpenGL
resource registration. Direct device-to-OpenGL transfer is considered only
after the measured staging cost justifies the additional platform-specific
synchronization complexity.

## Result

Verified on 2026-09-21. The complete build passed 35/35 tests, including exact
pinned transfer, multi-field production extraction, double-buffer/drop policy,
three-section orientation and colour mapping, and a desktop-driven real CUDA
run that observed one synchronized frame identity before successful SEG-Y
publication.

Three real five-step Overthrust display benchmark repetitions reported these
observed ranges:

```text
grid=200x200x187
physical_cells=7480000
setup_ms=8364.115..10165.305
propagation_step_mean_ms=67.146..67.886
extraction_mean_ms=2.292..2.338
pinned_transfer_mean_ms=1.747..2.067
normalization_mean_ms=40.277..56.498
opengl_upload_ms=15.437..21.597
display_pipeline_mean_ms=60.032..82.135
recommended_interval_steps_10pct=9,10,13
```

The UI defaults to 15 steps, rounding above the worst observed 13-step result.
It remains configurable from 1 through 100 steps. CUDA-off, SEG-Y-off, YAML-off,
HDF5-off, and desktop-off build boundaries also compiled and passed their
affected tests.
