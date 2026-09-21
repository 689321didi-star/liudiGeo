# Increment 26 — Three-file three-component SEG-Y output

**Status:** Verified on 2026-09-21.

## Goal

Replace the interleaved Vx/Vy/Vz `record.sgy` product with three independently
readable and atomically published SEG-Y files: `record_vx.sgy`,
`record_vy.sgy`, and `record_vz.sgy`.

## Format contract

- Retain SEG-Y Revision 1 for broad processing/viewer compatibility: 3200-byte
  ASCII textual header, 400-byte binary header, 240-byte trace headers,
  big-endian IEEE float32 sample format code 5, fixed-length traces, and no
  extended textual headers.
- Each file contains exactly one component and `receiver_count` traces in
  receiver order. Trace samples remain particle velocity in m/s with no
  display normalization.
- Trace identification codes follow the standard mapping: 13 for
  Vx/in-line/east, 14 for Vy/cross-line/north, and 12 for Vz/vertical/down.
  Textual headers state the
  component, coordinate convention, units, sampling, and first-sample time.
- Source/receiver coordinates, scalars, sample interval/count, ensemble fields,
  and physical time convention remain unchanged.

## Scope

- Add a component-specific SEG-Y writer and exact component-layout validator.
- Finalize all three temporary files, validate each, then publish all three;
  any failure removes every temporary and newly published member so a partial
  three-file product is never reported as complete.
- Replace the production report's single path with three component paths.
- Version the desktop terminal result schema and store a three-member product
  list with component, relative path, checksum, and byte count.
- Update the CLI report, desktop lifecycle, estimates, independent verification
  and plotting tools, tests, and current documentation.
- Preserve the original receiver traces, numerical solver, sample values,
  coordinate convention, and display behavior.

## Acceptance criteria

1. Byte-level I/O tests verify each file's textual/binary/trace headers,
   component code, receiver order, exact size, and exact samples.
2. The production CUDA pipeline publishes exactly three final files and no
   temporary or legacy `record.sgy`; forced publication failure leaves no
   completed member.
3. Desktop completion records three checksummed products and rejects missing,
   duplicated, mislabeled, modified, or extra files.
4. The actual small desktop CUDA run completes and registers all three files.
5. The plotting and Overthrust verification tools accept the new triplet.
6. Complete and optional build regressions pass and are recorded in
   `docs/HANDOFF.md`.

## Accepted limitation

The files remain SEG-Y Revision 1 rather than Revision 2.1 because the current
product needs broad viewer/processing compatibility and does not need extended
trace-header mappings or XML metadata. This choice can be revisited as a
separate exchange-format increment.
