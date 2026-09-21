# Increment 30 — SEG-Y model conversion

## Goal

Add a project workflow that converts three regular IEEE-float SEG-Y property
volumes (`Vp`, `Vs`, and density) to the canonical Wave3D HDF5 model and records
enough provenance to audit the conversion.

## Contract

- The user declares dimensions, metre spacing, halo, and six absorbing-boundary
  widths; SEG-Y headers are not treated as a complete 3-D geometry description.
- Each input contains exactly `nx * ny` fixed-length traces of `nz` big-endian
  IEEE float samples. Trace order is `x` fastest then `y`; samples increase in
  `z`. Values use `m/s`, `m/s`, and `kg/m3` respectively.
- Conversion validates all three layouts and the resulting physical model,
  writes HDF5 through a temporary file, verifies its round trip, and refuses to
  overwrite an existing model or manifest.
- `manifests/models/<name>.json` records geometry, conventions, source paths,
  byte counts and SHA-256 values, plus the output SHA-256.
- A successful desktop conversion activates the new model. Builds without both
  HDF5 and SEG-Y expose no enabled conversion command.

## Exclusions

Variable-length traces, IBM floats, extended textual headers, header-derived
3-D binning, coordinate origins, resampling, unit conversion, and multi-property
single-file layouts remain outside this increment.

## Acceptance checks

1. A small three-file fixture converts with exact `[z][y][x]` values and grid
   metadata, and its manifest contains matching checksums and conventions.
2. Mismatched/truncated inputs and output collisions fail without publishing a
   partial model or manifest.
3. The desktop shell activates a converted model in the full build; the
   HDF5-only boundary keeps the command unavailable.

## Result

Implemented and verified on 2026-09-21. Exact value/geometry conversion,
provenance, collision and mismatched-layout cleanup, desktop activation, and
the HDF5-only feature boundary passed their focused tests.
