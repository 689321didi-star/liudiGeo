# Increment execution policy

**Status:** Active from 2026-09-21.

This policy keeps the remaining desktop work small, reviewable, and economical
without weakening scientific or release checks. `AGENTS.md` remains the
authoritative working agreement.

## Active product path

The current desktop milestone is a complete **single-shot** forward workflow.
Multi-shot editing, shot CSV import, and sequential queue execution are
postponed. Existing shot identities, per-shot draft paths, SEG-Y shot headers,
and queue preference fields remain readable so this work can be resumed
without a schema rewrite.

The remaining active increments are:

1. **Increment 28 — source mechanisms (complete):** double-couple presets and
   the documented strike/dip/rake conversion for the active shot.
2. **Increment 29 — acquisition geometry (complete):** receiver lines,
   explicit CSV coordinates, reusable templates, translation, and duplicate
   validation for the active shot.
3. **Increment 30 — SEG-Y model conversion (complete):** convert the supported
   regular IEEE-float property-volume subset into canonical Wave3D HDF5 with
   explicit dimensions, spacing, units, and provenance.
4. **Increment 31 — single-shot release qualification (candidate verified;
   native Linux gate open):** audit the disabled
   snapshot command and RTM task/result seams, then qualify the complete native
   Linux single-shot workflow and documentation.

Multi-shot management and sequential queue execution stay in the deferred
backlog and are not acceptance gates for these four increments.

## Minimal increment packet

Each increment contains only what is needed to make one behavior reviewable:

- one short plan stating scope, exclusions, and acceptance checks;
- the smallest implementation change that satisfies the plan;
- focused tests for new behavior and the nearest integration boundary;
- one concise `HANDOFF.md` record with the exact commands and results;
- a decision or architecture update only when an interface, scientific
  convention, data format, ownership boundary, or accepted limitation changes;
- one clean commit after the declared checks pass.

Do not create screenshots, generated scientific products, benchmark reports,
or a full Overthrust run unless the increment changes rendering, propagation,
performance, or a milestone explicitly requires that evidence.

## Proportionate validation

Use the narrowest row that covers the actual change:

| Change type | Required checks |
| --- | --- |
| Documentation only | Link/path and diff inspection; no rebuild |
| Isolated pure logic | Focused unit test target plus its nearest integration test |
| Desktop state or widget behavior | Affected desktop tests plus one optional-feature boundary if relevant |
| I/O schema or scientific convention | Focused byte/numerical tests, round trip, and affected integration tests |
| Solver/CUDA behavior | Focused CPU/GPU or analytical comparison, affected CUDA integration, then the required scientific ladder step |
| Release milestone or broad build change | Complete configured suite and all affected optional-build boundaries |

A failing check, shared interface change, or unexpectedly broad dependency is a
reason to widen validation. Passing focused checks alone is never presented as
full release qualification.

## Efficient working rules

- Inspect only the files and symbols related to the increment before widening
  the search.
- Reuse existing fixtures and production adapters; do not add duplicate test
  data or parallel abstractions.
- Build the affected targets first. Run the complete suite once at the release
  milestone, or earlier only when the change crosses broad boundaries.
- Avoid repeated configure/build/test commands after an unchanged passing
  result.
- Keep progress reports and handoff entries factual and short: outcome,
  validation, and remaining limitation.
- Stop scope growth at the increment boundary and record unrelated findings in
  the backlog.
