# SEG/EAGE 3-D Overthrust Source Audit

Audit date: 2026-09-09

## Scientific authority and license

The scientific source is:

- F. Aminzadeh, J. Brac, and T. Kunz, *SEG/EAGE 3-D Salt and Overthrust
  Models*, SEG/EAGE 3-D Modeling Series No. 1, 1997.
- SEG Open Data catalog:
  `https://wiki.seg.org/wiki/SEG/EAGE_Salt_and_Overthrust_Models`
- Archived dataset DOI: `https://doi.org/10.5281/zenodo.4252588`
- License: Creative Commons Attribution 4.0. Copyright 1997 Society of
  Exploration Geophysicists. The SEG notice requires the attribution and
  license to remain associated with the model.

Independent published descriptions characterize the input as a constant-
density acoustic P-wave velocity macro model occupying
`20 km x 20 km x 4.65 km`, sampled every 25 m on
`801 x 801 x 187` nodes, with discontinuous velocities from approximately
`2.179 km/s` to `6.000 km/s`.

## Access result and transport source

The official URL advertised by the SEG catalog was checked directly:

```text
https://s3.amazonaws.com/open.source.geoscience/open_data/
seg_eage_models_cd/Overthrust_3D_CD1.tar.gz
```

On 2026-09-09 both HEAD and ranged GET returned:

```text
HTTP 403 AccessDenied
```

The decoded volume used for this increment was transported through the mirror
linked by the MIT-licensed `leileely/FDwave3D` repository, whose README points
back to the SEG model page:

```text
https://drive.usercontent.google.com/download?
id=1vTemFS0poXAUMhHfea-nVGRSvuLPNa5K&export=download&confirm=t
```

The transport file is not treated as scientific authority. Its geometry and
values were accepted only after the cross-checks below.

## Container audit

```text
file name: overthrust_3d_vp.mat
byte count: 149938918
SHA-256: 251fd1fbd2e1d9ac6aa227960c348f27e08f38325ff7d6d650aa69d77ef105f6
container: MATLAB 5.0 MAT-file, little endian
container creation string: MACI64, Wed May 2 16:09:05 2012
```

Variables:

```text
d       shape (1,3),         MATLAB double, values [25,25,25]
n       shape (1,3),         MATLAB double, values [187,801,801]
data    shape (187,801,801), MATLAB double
```

The companion plotting and simulation scripts identify the source axes as
`[z,y,x]`. This is also the Wave3D canonical axis order. The first axis is
unambiguously depth because it has 187 samples; the two 801-sample horizontal
axes follow the companion script's y/x interpretation.

Decoded checks:

```text
cell count:              119979387
decoded byte count:      959835096
finite cell count:       119979387
minimum Vp:              2178.83447265625 m/s
maximum Vp:              6000.0 m/s
mean Vp:                 4479.918830902926 m/s
first sample:            3166.66650390625 m/s
last sample:             6000.0 m/s
all values exact after float64 -> float32 -> float64: yes
full canonical float32 SHA-256:
8ef272ee6e5c640070464851de102a4bf3e1364c0290409f5662742bbe787cbc
```

The shape, 25 m spacing, physical extent, and `2.179-6.000 km/s` extrema agree
with the independent published descriptions. The MATLAB container promotes
the source binary32 values to double but does not add precision or change any
value.

## Fixed reduced-model window

A sliding `200 x 200` horizontal window was ranked by the depth-integrated
absolute x/y Vp gradient. The maximum score selected the zero-based source
window:

```text
z = [0,187)
y = [153,353)
x = [154,354)
```

The source-to-Wave3D mapping is therefore a direct crop with no permutation,
interpolation, decimation, or smoothing. The output has canonical shape
`[187,200,200]`, physical `(x,y,z)` node extent
`(4975 m,4975 m,4650 m)`, and 25 m spacing.

Orthogonal full-volume and crop slices were inspected locally. The selected
window contains curved shallow layers, dipping contacts, a strong overthrust
discontinuity, laterally changing basement structure, and depth-dependent
layering. The initially considered central `[300:500,300:500]` window had much
less horizontal variation and was rejected before implementation.

## Precomputed expected derived values

Using the approved binary64 calculations followed by one binary32 conversion:

```text
property  minimum             maximum        canonical float32 SHA-256
Vp        2445.75927734375     6000.0         5f30736af9dfdba8ca9e68bd7e29444534a9cf0736cc02af6251820acc26a7ee
Vs        1412.059814453125    3464.1015625   86ad455e471a824df75ad1c9685e34ff81986b9d57f9c4a7775d41e2e718921f
rho       2180.043212890625    2728.346435546875 ba34f51ca8c63683511c847e11fe4f2de7e2e43befa574da3ab3d92fad8dbb06
```

Every derived value is finite and positive, and every cell passes
`Vp^2 > (4/3)Vs^2`. These hashes and extrema are acceptance oracles for the
production converter; they were calculated before that converter was written.

## Canonical conversion verification

The production converter ran on the audited source on 2026-09-09 and wrote an
ignored 89,768,192-byte `overthrust_small.h5`. Its own immediate HDF5 reread
preserved grid geometry and every binary32 cell exactly. An independent
`h5dump -b LE` extraction produced three 29,920,000-byte raw arrays whose
SHA-256 values exactly matched the precomputed Vp, Vs, and density oracles
above.

Independent array inspection reported:

```text
property  cells    finite   mean
Vp        7480000  7480000  4614.87442134 m/s
Vs        7480000  7480000  2664.39897970 m/s
rho       7480000  7480000  2544.17252773 kg/m3
```

Recomputing the approved formulas from the extracted Vp matched every Vs and
density float exactly, and all 7,480,000 cells had positive bulk modulus.
Horizontal, x-z, and y-z slices of all three properties were inspected: the
same folds, dipping contacts, discontinuities, and basement appear at the same
coordinates, with z increasing downward and no transposition or reversal.

The matching ignored 3,373-byte YAML has 121 surface receivers, 3000 samples
at 1 ms, and exact HDF5 extrema. Its write/read round trip passed the existing
run validator. The numerical report gives a 0.00152702393047 s CFL limit, a
0.654868584601 CFL fraction, and 6.27582139757 minimum shear points per design
wavelength.
