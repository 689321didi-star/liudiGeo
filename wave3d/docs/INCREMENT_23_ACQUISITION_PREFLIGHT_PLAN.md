# Increment 23 — Acquisition geometry and forward preflight

## Goal

Complete one active shot from a validated model/workspace/source draft into a
reviewable forward-run configuration, without starting CUDA work. The desktop
must configure, validate, persist, and display a surface receiver array and
must publish an immutable preflight run only after the existing production
YAML adapter round-trips the complete configuration.

## Scope

- Extend the per-shot experiment draft with a versioned surface rectangular
  receiver grid. Its physical coordinates are metres, z is positive downward,
  and generated order is y outer / x fastest.
- Default to `101 x 101` receivers spanning the model's complete x/y node
  extents at `z = 0 m`.
- Validate finite bounds, positive counts, checked receiver/sample products,
  coordinates against the model grid, and non-empty three-component output.
- Report receiver count, sample count, raw three-component float32 trace bytes,
  and deterministic SEG-Y Revision 1 file bytes. The float32 number is labelled
  as a raw trace payload rather than an HDF5 file size because container
  metadata and compression are variable.
- Add an acquisition editor with array counts, physical bounds, depth,
  validation, estimates, and visible reserved entries for line/CSV geometry.
- Overlay receivers in the 3-D model view and all orthogonal sections. Dense
  arrays may be visually decimated, while diagnostics retain the exact count.
- Assemble the existing `ForwardRunConfiguration` from the active HDF5 model,
  resolved source/workspace, receiver coordinates, and project run paths.
- Enable experiment preflight only in desktop builds with HDF5, YAML, and
  SEG-Y, because the current production runner always writes SEG-Y Revision 1.
  Preflight writes `runs/<run-id>/config.yaml` and `manifest.json` through the
  existing immutable project-run API, then reloads YAML and verifies the
  complete resolved configuration.
- Keep CUDA execution, pause/resume/cancel, receiver CSV import, multiple-shot
  editing, snapshots, and trace rendering outside this increment.

## Acceptance criteria

1. Defaults generate exactly 10,201 unique, in-domain surface receivers with
   endpoints at the model x/y extents and x-fastest order.
2. Invalid counts, overflow, reversed/degenerate bounds, non-surface depth, or
   out-of-domain coordinates cannot be saved or preflighted.
3. Draft JSON round-trips acquisition values atomically; version-1 drafts load
   with a missing-acquisition marker and receive model-derived defaults in the
   desktop before use.
4. Trace and SEG-Y estimates match checked analytical byte counts for the
   receiver/sample shape.
5. A valid preflight produces an immutable run, its YAML round-trips through
   `load_yaml_run_configuration`, and all model/grid/source/receiver/output
   values match the resolved draft.
6. Receiver markers use the documented x/y/z convention in the 3-D and XY/XZ/YZ
   views; tests expose the exact receiver count and displayed-marker count.
7. HDF5-off, YAML-off, SEG-Y-off, and desktop-off builds remain valid and do
   not expose a runnable preflight action.
8. The HDF5+YAML desktop suite, the complete CUDA/YAML/HDF5/SEG-Y suite, and a
   real Overthrust WSLg/XCB screenshot pass before the increment is committed.

## Result

Verified on 2026-09-15. The version-2 draft, version-1 migration path,
`101 x 101` defaults, checked estimates, acquisition editor, four-view receiver
overlays, SEG-Y sample-axis gate, and immutable YAML preflight all passed their
focused tests. The complete HDF5/YAML/SEG-Y desktop build passed 25/25; the
SEG-Y-off build passed 24/24, HDF5-on/YAML-off passed 23/23, HDF5-off passed
20/20, desktop-off passed 17/17, and the complete CUDA build passed 28/28.

The 1440 x 1062 WSLg/XCB Overthrust capture passed real OpenGL context, shader,
3-D texture, frame, source-marker, and receiver-marker gates. Visual inspection
confirmed a uniform surface array over `0–4975 m` on x/y, consistent XY and 3-D
placement, positive depth downward in XZ/YZ, readable controls, and no clipping,
transpose, or mirror defect. CUDA execution remains intentionally disconnected.
