# Increment 29 — Acquisition geometry

## Purpose

Complete reusable receiver geometry editing for the active single shot while
preserving the existing receiver-ordered forward and SEG-Y contracts.

## Scope

- Represent one acquisition as a surface rectangular array, a surface line, or
  explicit ordered coordinates imported from CSV.
- Use strict CSV columns `x_m,y_m,z_m`; preserve row order and require `z=0 m`.
- Apply a finite X/Y translation after geometry generation and before bounds
  and duplicate checks.
- Reject empty, excessive, non-finite, out-of-domain, non-surface, and exact
  duplicate receiver sets. Keep the 1,100,000-receiver safety limit.
- Store complete explicit coordinates in the experiment draft so a run never
  depends on the original CSV file.
- Save and load standalone JSON acquisition templates through explicit file
  paths. A template contains geometry and translation but no model, shot,
  source, or run state.
- Advance experiment drafts to schema version 4 while reading versions 1–3.
- Enable line/CSV modes and template/translation controls in the desktop.

## Exclusions

Multi-shot work, per-receiver component masks, buried receivers, spreadsheet
dialects, geographic coordinates, geometry editing on the 3-D canvas, and
changes to propagation or SEG-Y layout are outside this increment.

## Acceptance

1. Geometry tests cover rectangle ordering, diagonal line endpoints, CSV row
   order, translation, exact duplicate rejection, surface/bounds validation,
   and count limits.
2. Draft tests cover schema-v4 round trip and versions 1–3 migration.
3. Template tests cover atomic round trip and malformed/unsafe data rejection.
4. Desktop tests cover all three enabled modes, CSV import, templates,
   translation, receiver count/overlay updates, and persistence.
5. The focused desktop experiment and shell tests pass in the complete desktop
   build and one reduced optional-feature build. No Overthrust run, screenshot,
   CUDA regression, or full suite is required because downstream receiver and
   propagation contracts do not change.

## Result

Verified on 2026-09-21. All three acquisition modes, strict CSV import,
schema-v4 migration, translation, duplicate/surface/bounds/count checks,
standalone atomic templates, UI persistence, and overlay count updates satisfy
the declared scope. The two focused tests passed in both the complete desktop
build and the CUDA/YAML/SEG-Y-off boundary build.
