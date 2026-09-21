# Increment 31 — Single-shot release qualification

## Goal

Audit the reserved snapshot and RTM seams, then qualify the complete native
Linux single-shot workflow and its removable optional-feature boundaries.

## Scope

- Keep wavefield snapshot storage visibly reserved and disabled. Verify that
  its existing HDF5 sparse-snapshot adapter remains independent of the desktop
  run path.
- Verify that RTM-on adds only checkpoint and task/result interfaces plus their
  tests, while RTM-off contains no checkpoint, imaging, reverse-propagation, or
  RTM product target.
- Build and run the complete CUDA/YAML/HDF5/SEG-Y/desktop Release suite, the
  dependency-free core suite, the HDF5-only desktop boundary, and an RTM-on
  interface build.
- Run the accepted dense single-shot Overthrust command-line workflow and
  independent three-file SEG-Y verifier without retaining a duplicate result.
- Exercise the real desktop/OpenGL smoke on native Linux and record GPU,
  driver, toolkit, test, numerical, graphics, and packaging evidence.

## Exclusions

Snapshot persistence, RTM imaging, reverse propagation, P/S decomposition,
multi-shot editing, run queues, solver optimization, and product installers are
not implemented in this increment.

## Acceptance checks

1. Snapshot remains disabled with an explicit reserved-state explanation; RTM
   interfaces stay optional, const-view based, and absent from forward targets.
2. Every configured Release suite passes, including RTM-on and RTM-off
   boundaries.
3. The dense 101 by 101 Overthrust run produces three valid component SEG-Y
   files whose independent scientific verification passes and whose hashes are
   recorded.
4. Native Linux creates all four OpenGL contexts, completes a single-shot
   desktop run, opens the result, and captures the required release screenshot.
5. Qualification documents distinguish measured evidence from gates that could
   not be executed; the milestone is complete only when all four checks pass.

## Environment finding

The active host reports a Microsoft WSL2 kernel. It can supply development,
CUDA, numerical, adapter, and WSLg smoke evidence, but project decision D048
and `DESKTOP_ENVIRONMENT_QUALIFICATION.md` prohibit reporting it as final
native-Linux graphics qualification. The native gate therefore remains open
until the same commit is exercised on the fixed product platform.

## Result

**Release-candidate result:** all checks available on the current WSL2 host
passed on 2026-09-21. The complete desktop suite passed 37/37, the RTM-on suite
30/30, the dependency-free suite 17/17, and the HDF5-only desktop suite 23/23.
The dense Overthrust run completed 3,000 steps with 10,201 receivers and the
independent verifier passed all three SEG-Y components. WSLg created all four
OpenGL contexts and produced a reviewed 1440 by 900 screenshot.

Acceptance check 4 remains open because this is not native Linux. The release
milestone and this increment are therefore not marked complete. Exact measured
evidence and the remaining command are recorded in
`SINGLE_SHOT_RELEASE_QUALIFICATION.md`.
