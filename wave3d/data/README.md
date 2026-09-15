# Wave3D data directory

`data/` contains local scientific inputs and generated artifacts. Large files
remain ignored by Git. Every dataset uses the same directory contract:

```text
data/<dataset>/
├── project.wave3d.json      optional desktop project metadata
├── source/                  immutable downloaded source volumes
├── models/                  prepared Wave3D HDF5 models and preparation logs
├── figures/model/           figures derived from source or prepared models
├── runs/<run_id>/
│   ├── config.yaml          complete run configuration
│   ├── manifest.json        optional immutable desktop run manifest
│   ├── output/              primary numerical output, such as record.sgy
│   ├── figures/             plots derived from this run
│   ├── logs/                execution and timing logs
│   └── reports/             validation results and numerical summaries
└── manifests/               checksums and artifact inventories
```

Names use lowercase `snake_case`. Scientific formats retain their native
extensions: MATLAB `.mat` for downloaded source volumes, HDF5 `.h5` for
canonical `[z,y,x]` models, YAML `.yaml` for run configurations, SEG-Y `.sgy`
for seismic traces, and SVG/PNG for figures.

Build trees and helper executables belong under the repository-level `build/`
directory. They must not be stored in `data/`.

When Wave3D Studio creates a managed project, `project.wave3d.json` uses the
versioned `wave3d.desktop.project.v1` schema and the directories above are
created together. Existing command-line datasets may omit the project file.

Run configurations use paths relative to their own directory. Moving the
repository therefore does not invalidate their model or output paths. A run
must keep the exact configuration that produced its output, while logs and
derived plots stay beside that output under the fixed subdirectories above.

For the current Overthrust dataset, the dense result is located at:

```text
data/overthrust/runs/forward_101x101/
```

Verify large artifacts against the dataset manifest with:

```text
cd data/overthrust
sha256sum --check manifests/SHA256SUMS
```
