# Increment 11 Predeclared RTM-Ready Interface Validation Plan

Status: passed on 2026-09-09. Fixed before interface implementation; no
contract or acceptance criterion was relaxed after implementation began.

This increment defines extension boundaries only. It must not add reverse
propagation, migration, imaging conditions, P/S decomposition, attenuation,
checkpoint persistence, or an RTM executable.

## Contracts

- A CPU elastic wavefield exposes nine non-owning, read-only float views with
  exact grid identity and bounds-checked element access. No interface consumer
  can obtain a mutable field pointer.
- A forward observer receives deterministic completed-step metadata and a
  read-only wavefield view after receiver sampling. Observers cannot alter
  propagation physics or ownership.
- The factory creates the current transparent CPU interior reference behind an
  `IForwardPropagator`. It owns its problem/state, advances exactly once per
  call, rejects overrun, preallocates receiver-major traces, and adds no
  allocation inside a step.
- `IReceiverData` exposes validated, read-only receiver/component/sample
  access independent of HDF5 or SEG-Y.
- `ICheckpointStore` accepts read-only wavefield views and restores only into
  an explicit mutable destination. Its time-level metadata distinguishes
  completed steps, integer velocity time, and half-step stress time.
- `WAVE3D_ENABLE_RTM` defaults off. Only the optional interface target and mock
  checkpoint test may refer to the optional RTM tree.

## Acceptance

- View size/grid/value checks pass; out-of-range access fails; compile-time
  type checks prove view data pointers are `const float*`.
- Factory output is bitwise equal to the established direct CPU stepping path.
  Attaching an observer does not change final fields or receiver records.
- A mock checkpoint observer saves known steps, reports deterministic metadata,
  restores every field bitwise, and rejects absent or mismatched checkpoints.
- Receiver data reads preserve component, receiver-major indexing, exact `dt`,
  coordinates, and source metadata; invalid access fails explicitly.
- The RTM-off CPU build succeeds with the optional RTM tree temporarily absent.
  The SHA-256 of the existing Release `wave3d_forward` before this increment is
  `f56399fb419e73648db3707cf408823f52e8a6c47ad9cf508e0836f761e106f1` and
  must remain unchanged after the RTM-off rebuild.
- RTM-on Release and ASan/UBSan mock suites, normal CPU Release, and CUDA
  Release pass before the project is declared complete.

No acceptance threshold will be relaxed after implementation begins.

## Observed result

- Compile-time checks prove field data is exposed only as `const float*` and
  indexed values as `const float&`; runtime bounds and grid checks passed.
- Four factory steps produced fields and receiver-major traces bitwise equal to
  direct CPU stepping. Observer metadata exactly identified integer velocity
  and half-step stress times. Instrumented factory stepping allocated zero
  dynamic objects.
- The mock observer saved steps 2 and 4. Two independent runs produced
  bitwise-identical metadata and all nine fields; restoration was bitwise exact
  and absent/mismatched restores failed explicitly.
- With `optional/rtm` temporarily moved completely outside the source tree,
  RTM-off configured, compiled, and passed the forward-interface test. The
  rebuilt forward executable retained SHA-256
  `f56399fb419e73648db3707cf408823f52e8a6c47ad9cf508e0836f761e106f1`.
- RTM-on Release passed 17/17 tests; RTM-on interface ASan/UBSan passed 2/2;
  final RTM-off CPU Release passed 16/16 and CUDA Release passed 21/21.
- A final all-options-on Release build passed 25/25 tests, covering CUDA,
  YAML, HDF5, SEG-Y, the core interfaces, and the removable checkpoint mock in
  one configuration.

The optional tree was restored after the removal test. No RTM or imaging
implementation was added.
