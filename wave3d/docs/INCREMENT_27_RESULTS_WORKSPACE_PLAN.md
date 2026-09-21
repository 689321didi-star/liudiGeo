# Increment 27 — Desktop SEG-Y results workspace

**Status:** Verified on 2026-09-21.

## Goal

Add a read-only desktop results workspace that discovers completed project runs
and inspects the three component SEG-Y products without changing scientific
records.

## Scope

- Discover immutable `wave3d.desktop.run_result.v2` records below the active
  project's `runs/` directory and list run ID, completion time, receiver/sample
  shape, device, propagation time, and total SEG-Y bytes.
- Open only the declared `record_vx.sgy`, `record_vy.sgy`, and
  `record_vz.sgy` members after checking their paths, sizes, sample axes,
  format code, fixed-trace layout, component codes, and common geometry.
- Display one selectable Vx, Vy, or Vz receiver gather for a bounded contiguous
  receiver range. Use a reversible display-only percentile clip and retain
  particle velocity units in labels.
- Show the 40 textual-header cards, binary sample metadata, source location,
  and first/last selected receiver coordinates.
- Export the current gather as PNG only. Do not rewrite SEG-Y or add a video,
  snapshot, picking, processing, or normalization output path.
- Refresh the workspace after project activation and successful run result
  publication. Keep SEG-Y-off and desktop-off builds valid.

## Acceptance criteria

1. A deterministic three-file fixture is discovered through its immutable
   result record; incomplete, unsafe, mismatched, and non-component products
   are rejected.
2. Component selection and receiver-window selection read the exact requested
   traces and produce a non-empty gather image with correct sample/time labels.
3. Header inspection reports the file component, trace/sample counts, interval,
   source, selected receiver endpoints, and textual cards.
4. PNG export writes the displayed image without modifying any SEG-Y member or
   result record.
5. Navigation opens the results dock, project changes refresh it, and completed
   desktop runs appear without reopening the project.
6. Focused desktop/SEG-Y tests, the complete build, and optional build
   boundaries pass and are recorded in `docs/HANDOFF.md`.

## Accepted limits

The first viewer renders one component and at most 512 contiguous receiver
traces at once. It does not perform seismic processing, interpretation picks,
wiggle plotting, arbitrary external dialect import, or run-to-run amplitude
differencing. Those require separate scientific and interaction contracts.
