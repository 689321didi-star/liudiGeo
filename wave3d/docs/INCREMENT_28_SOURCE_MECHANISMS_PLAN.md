# Increment 28 — Source mechanisms

## Purpose

Complete the active-shot source editor with a scientifically explicit
double-couple source defined by scalar moment and strike/dip/rake.

## Fixed convention

- Public Wave3D coordinates remain `x=east`, `y=north`, `z=down`.
- Strike is clockwise from north in `[0, 360)` degrees.
- Dip is downward from horizontal in `[0, 90]` degrees.
- Rake is measured in the fault plane from strike toward down-dip and lies in
  `[-180, 180]` degrees.
- Scalar moment is finite, positive, and expressed in N·m.
- The Aki–Richards double-couple equations are evaluated in north-east-down
  order, then mapped to Wave3D `Mxx,Myy,Mzz,Mxy,Mxz,Myz` east-north-down order.

The component equations and NED convention are documented by the GFZ
Information Sheet IS 3.9, equations (4) and (6):
<https://gfzpublic.gfz.de/pubman/item/item_272892/component/file_541895/IS_3.9.pdf>.
The accepted angle ranges and NED component ordering are independently exposed
by Pyrocko:
<https://pyrocko.org/docs/current/library/reference/pyrocko.moment_tensor.html>.

## Scope

- Add a core strike/dip/rake-to-moment-tensor conversion with finite/range
  validation.
- Add a persisted double-couple source mode and parameters to experiment draft
  schema version 3 while reading versions 1 and 2.
- Enable the existing desktop double-couple choice, add scalar moment and angle
  controls, and show the resolved six components.
- Keep explosion and manual symmetric-tensor behavior unchanged.

## Exclusions

Multi-shot editing, magnitude-to-moment conversion, focal-sphere graphics,
auxiliary-plane calculation, acquisition changes, solver changes, and a new
forward run are outside this increment.

## Acceptance

1. Core tests cover canonical strike-slip, normal, and reverse mechanisms;
   trace-free symmetry; scalar-moment norm; angle boundaries; and invalid input
   rejection.
2. Experiment tests cover double-couple resolution, schema-v3 round trip, and
   version-1/version-2 compatibility.
3. Desktop shell tests cover the enabled mode, parameter editing, resolved
   component preview, and persistence.
4. The focused acquisition, desktop experiment, and desktop shell tests pass.
   No screenshot, Overthrust run, CUDA regression, or full suite is required
   because propagation and rendering contracts do not change.

## Result

Verified on 2026-09-21. Core conversion, schema-v3 persistence/migration, the
enabled editor mode, and the resolved-component preview satisfy the declared
scope. The three focused tests passed in the complete desktop build; experiment
and shell tests also passed in the CUDA/YAML/SEG-Y-off boundary build.
