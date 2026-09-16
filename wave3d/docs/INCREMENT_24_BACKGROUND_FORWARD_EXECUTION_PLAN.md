# Increment 24 — Background CUDA forward execution

## Goal

Run one immutable, preflighted desktop configuration on a worker thread while
the Qt interface remains responsive. The run advances at bounded CUDA batch
boundaries, supports cooperative pause/resume/stop, and publishes a validated
SEG-Y product and terminal result record only after successful completion.

Live wavefield transfer and rendering are outside this increment. They will
consume the same bounded-batch lifecycle in the following increment.

## Scope

- Refactor the production CUDA task into a reusable incremental job with
  explicit preparation, bounded advance, progress, and finalization phases.
- Preserve `run_cuda_forward_from_yaml` and the command-line runner by
  composing the incremental job to completion.
- Write SEG-Y to a temporary path, verify the expected file shape and readable
  essential metadata, and atomically publish `output/record.sgy`.
- Add a Qt worker which creates and destroys the CUDA job on its own thread,
  consumes pause/resume/stop requests only between completed batches, and
  exposes a thread-safe lifecycle snapshot.
- Retain the prepared run in the main window, enable execution only for the
  complete CUDA/HDF5/YAML/SEG-Y build, lock experiment editing during a run,
  and connect progress plus all four run controls.
- Atomically write one terminal `result.json` beside the immutable run
  manifest. Successful records include the SEG-Y relative path, byte count,
  SHA-256, trace/sample shape, device, and timing report. Cancelled and failed
  records include their terminal state and diagnostic without claiming an
  output product.

## Invariants

- The worker exclusively owns CUDA setup, session advance, trace download,
  and teardown. GUI code never calls a CUDA solver operation.
- Configuration bytes and `manifest.json` remain immutable after preflight.
- Pause and stop never interrupt a CUDA kernel or force-terminate a thread;
  requests take effect after the current bounded batch synchronizes.
- An interrupted or failed run cannot leave `record.sgy` presented as a
  completed product. Temporary output is removed on failure.
- CPU-only, desktop-off, and desktop builds lacking any production I/O adapter
  retain their current behavior and dependencies.

## Acceptance criteria

1. Incremental production-job tests prove multi-batch progress, reject early
   finalization, and produce the same readable three-component SEG-Y contract
   as the existing one-shot entry point.
2. Worker lifecycle tests with a deterministic injected job prove running,
   paused, resumed, cancelled, completed, and failed transitions without
   blocking the GUI thread.
3. Workspace tests prove terminal result records are atomic, schema checked,
   path safe, checksum shaped correctly, and cannot be overwritten.
4. Desktop shell tests prove preflight enables start only in a complete
   production build and that running-state controls/edit locks are coherent.
5. A combined Qt/CUDA/HDF5/YAML/SEG-Y build compiles and its focused desktop
   and CUDA pipeline tests pass on the active GPU.
6. Existing optional-build matrices and the complete CUDA regression suite
   pass. Commands and observed results are recorded in `docs/HANDOFF.md`.

## Expected limitation

Progress advances only at CUDA batch boundaries. The initial batch size is
one time step, matching the accepted design review for the current Overthrust
timing; performance and latency measurements may justify a later configurable
batch size. Closing the application requests cooperative stop and waits for
the active batch to finish.
