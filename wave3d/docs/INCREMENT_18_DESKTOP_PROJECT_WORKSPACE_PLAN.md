# Increment 18 Predeclared Desktop Project Workspace Plan

## Purpose

Give the verified desktop shell a durable project identity and the standard
scientific workspace layout. This increment persists project metadata and
prepares immutable run directories, but does not load model samples, parse or
edit a forward experiment, create a CUDA session, or render scientific data.

## Fixed scope

- Add a Qt Core project-domain library inside the default-off desktop build
  boundary; it must not introduce Qt into the solver, CLI, or existing I/O
  targets.
- Define versioned `wave3d.desktop.project.v1` JSON containing a stable project
  ID, display name, creation time, optional model reference, queue preference,
  shared display field, and one or more uniquely identified shots.
- Create the standardized `source/`, `models/`, `figures/model/`, `runs/`, and
  `manifests/` directories beside `project.wave3d.json` without overwriting an
  unrelated non-empty directory.
- Load, validate, and atomically save project metadata. Reject unknown schemas,
  malformed identifiers/timestamps, duplicate shots, unsupported display
  fields, unsafe relative model references, and missing workspace directories.
- Prepare a run once under `runs/<run_id>/`, atomically retain the supplied
  resolved `config.yaml`, create `output/`, `figures/`, `logs/`, and `reports/`,
  and write an immutable manifest containing the project/shot identity and
  SHA-256 of the exact configuration bytes. Refuse an existing run ID.
- Connect New Project and Open Project actions to the project store, expose the
  active project name/path/shot count in the shell, and keep preflight/run
  actions disabled because experiment editing is not implemented yet.
- Persist window geometry, dock state, and last successfully opened project
  with Qt settings.

## Acceptance

1. Focused project tests cover create/load/save round trips, the standard
   directory tree, schema and invariant rejection, safe model references,
   exact configuration hashing, run layout, and refusal to overwrite projects
   or runs.
2. Focused window tests open and create a project without dialogs, verify the
   summary/state changes, and verify that scientific run actions remain gated.
3. The Qt-enabled desktop/CPU suite passes and the WSLg shell/OpenGL smoke
   still succeeds.
4. The default desktop-off build exposes no project or Qt target.
5. The full CUDA/YAML/HDF5/SEG-Y suite remains green.

## Deferred work

Forward-YAML editing and parsing, model import and inspection, derived-model
operations, acquisition/source editors, run queue execution, worker threads,
scientific rendering, SEG-Y viewing, snapshots, and RTM belong to later
increments.

## Result

Verified on 2026-09-15. Project and run-workspace tests cover the declared
schema, standard tree, atomic round trip, unsafe-path and malformed-type
rejection, shot identity, exact configuration retention and hashing, and
non-overwrite rules. Window tests cover direct create/open, project summary,
last-project reopening, display preference persistence, error reporting, and
continued run gating. A fresh Release desktop build passed 19/19, the existing
CUDA/YAML/HDF5/SEG-Y build passed 28/28, the desktop-off build exposed no
desktop executable, and WSLg created all four OpenGL contexts.
