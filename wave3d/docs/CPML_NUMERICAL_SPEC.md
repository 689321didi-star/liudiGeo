# Wave3D CPML Numerical Specification

Status: accepted for Increment 7 on 2026-09-09.

## Sources and boundary of reuse

Wave3D follows the unsplit convolutional PML recurrence described by
Komatitsch and Martin, “An unsplit convolutional perfectly matched layer
improved at grazing incidence for the seismic wave equation,” *Geophysics*
72(5), SM155–SM167 (2007), DOI
[`10.1190/1.2757586`](https://doi.org/10.1190/1.2757586), and the authors'
GPL-3.0-or-later reference implementation
[`geodynamics/seismic_cpml`](https://github.com/geodynamics/seismic_cpml),
commit `0d89aa3132f2b9abdaba30345e3eb9455736d67d` inspected on 2026-09-09.
No reference source is copied: this document maps the published recurrence to
Wave3D's independently implemented radius-six operator and staggering.

## Stretch and recurrence

Each Cartesian derivative `D` is replaced independently by

```text
psi^(n+1) = b psi^n + a D
D_cpml    = D / kappa + psi^(n+1)
b         = exp(-(sigma/kappa + alpha) dt)
a         = sigma (b - 1) / (kappa (sigma + kappa alpha)).
```

Where `sigma=0`, Wave3D uses `a=0`, `b=1`, and `1/kappa=1` exactly.  Memory is
stored in `float32`; the raw radius-six derivative and recurrence arithmetic
are evaluated in double before conversion, matching the interior accuracy
policy.

For normalized depth `r` in an enabled PML side,

```text
sigma(r) = sigma_max r^m
kappa(r) = 1 + (kappa_max - 1) r^m
alpha(r) = alpha_max (1-r)
sigma_max = -(m+1) c_max log(R) / (2 L)
alpha_max = pi f0.
```

Increment 7 fixes `m=2`, `kappa_max=1`, and caller-declared target reflection
`R`.  `L` is that side's absorbing width times axis spacing.  Integer and
half-grid arrays are prepared separately from their actual target coordinate;
depth is clamped at one in halo-only storage beyond the PML.

## State mapping and update order

There is one memory array for each distinct derivative in the elastic system:
nine velocity derivatives used by the six stress updates and nine stress
derivatives used by the three velocity updates, 18 full padded `float32`
arrays total.  The six one-dimensional coefficient triplets (`a`, `b`, and
`1/kappa` for integer and half locations on x/y/z) are immutable.

At every step:

```text
CPML stress update -> moment source q(n dt) -> CPML velocity update
-> receiver sample at (n+1) dt.
```

Faces, edges, and corners require no special branch: every derivative uses
only the coefficient and memory associated with its own axis.  Increment 7
enables all six sides.  Increment 8 may disable only z-min CPML and compose a
traction-free top without changing this state interface.
