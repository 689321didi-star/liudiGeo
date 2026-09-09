# Increment 12 Single-File SEG-Y Validation Plan

Status: passed on 2026-09-09. Fixed before implementation; no output contract
or acceptance criterion was relaxed after testing began.

## Goal

Replace the three per-component SEG-Y outputs and external JSON sidecars with
one self-describing SEG-Y Revision 1 file containing the complete synthetic
three-component receiver record.  This changes only the optional SEG-Y adapter;
the propagator, canonical HDF5 representation, and SEG-Y model reader remain
unchanged.

## Output contract

- The writer emits exactly the requested `.sgy` path and no component files or
  sidecars.
- The file is big-endian SEG-Y Revision 1 with a 3200-byte ASCII textual header,
  a 400-byte binary header, no extended textual headers, 240-byte trace headers,
  and IEEE `float32` samples (format code 5).
- The file is one common-source ensemble.  Traces are receiver-major, with VX,
  VY, then VZ for every receiver.
- The synthetic survey axes are declared explicitly: x/east is in-line and
  y/north is cross-line.  Trace identification codes are 14 for VX/in-line, 13
  for VY/cross-line, and 12 for VZ/vertical.
- `SCALCO=-1000` and `SCALEL=-1000`; horizontal coordinates, receiver elevation,
  and source depth are stored in millimetres.  z is positive downward, so
  receiver elevation is `-z` and source depth is `+z` relative to the z=0
  surface.
- The textual header records SI velocity units, lack of normalization, trace
  ordering, axis/component mapping, source metadata, and the `(n+1)*dt` first-
  sample convention.
- Revision 1 cannot represent a fractional-microsecond sample interval without
  losing information.  The writer therefore rejects such intervals rather than
  rounding silently.  Its existing 65535-sample fixed-trace limit remains.

## Acceptance

The focused test must independently parse raw bytes and verify the complete
file size, textual declarations, binary revision/format/fixed-length fields,
trace count and ordering, trace sequence numbers, component codes, both
coordinate scalars, source/receiver coordinates, receiver elevation, source
depth, sample count, interval, and every sample value.  It must also prove that
no legacy component or JSON files are produced and that fractional-microsecond
sampling and truncated input fail explicitly.

After the focused test passes, the enabled-I/O Release suite and focused
ASan/UBSan suite must pass.  The optional-I/O-off CPU suite must also pass to
show that the SEG-Y change did not enter the propagation dependency graph.

## Observed result

- The focused raw-byte parser verified one file, nine traces for three test
  receivers, receiver-major VX/VY/VZ sample order, component codes 14/13/12,
  all declared binary and trace fields, and exact IEEE `float32` values.
- Fractional-microsecond sampling failed before file creation; truncation was
  rejected; no component files or JSON sidecars were produced.
- A one-step target RTX 5060 run produced one 10,188-byte file with 27 traces
  for nine receivers. An independent script read Revision `0x0100`, format 5,
  fixed-length flag 1, zero extended headers, and `14,13,12` repeated nine
  times.
- The all-options Release suite passed 25/25, focused I/O ASan/UBSan passed
  4/4 with leak detection disabled, and optional-I/O-off CPU Release passed
  16/16.
- The target executable compiled with no output adapter, HDF5 only, SEG-Y only,
  and both adapters enabled.
