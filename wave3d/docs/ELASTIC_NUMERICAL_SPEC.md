# Wave3D Elastic Numerical Specification

Status: accepted scientific reference gate, 2026-09-08.

Implementation status: Increments 4a and 4b (exact constants, material
coefficients, CFL/design-band validation, and the checked CPU staggered
derivative) verified on 2026-09-09. Wavefield ownership and all time updates
remain unimplemented until their later sub-increments.

This document is the implementation contract for Increment 4. It fixes the
interior equations, signs, units, staggering, spatial coefficients, update
order, material placement, source normalization, stability bound, dispersion
checks, and CPU-reference acceptance tests before propagation code exists.
Changing any item below requires a dated architecture decision and new tests.

The scope is a 3D isotropic elastic **solid**. Attenuation, viscoelastic memory
variables, anisotropy, poroelasticity, RTM, P/S decomposition, CPML, and a
traction-free boundary are not part of Increment 4. Those boundary algorithms
require their own specifications before implementation.

## Evidence and derivation policy

The following primary publications establish the method family:

1. J. Virieux (1986), “P-SV wave propagation in heterogeneous media:
   velocity-stress finite-difference method,” *Geophysics* 51(4), 889–901,
   [doi:10.1190/1.1442147](https://doi.org/10.1190/1.1442147). This establishes
   the complete staggered velocity-stress approach and its behavior at strong
   material contrasts.
2. R. W. Graves (1996), “Simulating seismic wave propagation in 3D elastic
   media using staggered-grid finite differences,” *BSSA* 86(4), 1091–1106,
   [doi:10.1785/BSSA0860041091](https://doi.org/10.1785/BSSA0860041091). This
   establishes the 3D formulation and documents moment-tensor sources. Only
   publisher metadata and the abstract were accessible during this gate.
3. P. Moczo, J. Kristek, V. Vavryčuk, R. J. Archuleta, and L. Halada (2002),
   “3D heterogeneous staggered-grid finite-difference modeling of seismic
   motion with volume harmonic and arithmetic averaging of elastic moduli and
   densities,” *BSSA* 92(8), 3042–3066,
   [doi:10.1785/0120010167](https://doi.org/10.1785/0120010167). Its
   [publisher abstract](https://www.seismosoc.org/Publications/BSSA_html/bssa_92-8/01167.html)
   explicitly supports harmonic averaging of shear/bulk moduli and arithmetic
   averaging of density in heterogeneous staggered schemes.
4. B. Fornberg (1988), “Generation of finite difference formulas on
   arbitrarily spaced grids,” *Mathematics of Computation* 51(184), 699–706,
   [doi:10.1090/S0025-5718-1988-0935077-0](https://doi.org/10.1090/S0025-5718-1988-0935077-0).
   This supplies an independent general basis for finite-difference weights.
5. O. Holberg (1987), “Computational aspects of the choice of operator and
   sampling interval for numerical differentiation in large-scale simulation
   of wave phenomena,” *Geophysical Prospecting* 35(6), 629–655,
   [doi:10.1111/j.1365-2478.1987.tb00841.x](https://doi.org/10.1111/j.1365-2478.1987.tb00841.x).
   It motivates dispersion-optimized operators, but Wave3D does not select an
   inaccessible coefficient table from this paper.

The exact 12th-order coefficients, CFL expression, source sign conversion,
and numerical error thresholds below are independent Wave3D derivations. They
are written out so that the implementation can be reviewed without access to
paywalled equations. The supplied legacy material is corroborating evidence
only; see [LEGACY_AUDIT.md](LEGACY_AUDIT.md).

The user-supplied Zhang et al. paper is an RTM paper, not the authority for the
interior forward operator. Its role and access limits are recorded in
`HANDOFF.md`.

## Coordinates, stress sign, and units

Public coordinates remain `x=east`, `y=north`, `z=down`, in metres, with the
surface at `z=0`. This `END` ordering is left-handed. The divergence and
constitutive equations below remain valid under this orthogonal reflection,
but external moment tensors must be transformed explicitly; code must not use
an implicit cross-product convention.

Wave3D uses tension-positive Cauchy stress. Compression therefore has negative
normal stress. Reversing this convention would reverse source polarity and is
not an implementation detail.

| Symbol | Meaning | SI unit |
| --- | --- | --- |
| `x_i` | position | m |
| `t` | time | s |
| `v_i` | particle velocity | m/s |
| `rho` | mass density | kg/m^3 |
| `sigma_ij` | Cauchy stress | Pa |
| `lambda`, `mu`, `K` | Lamé parameters and bulk modulus | Pa |
| `f_i` | body-force density | N/m^3 |
| `M_ij` | constant moment-tensor scale | N*m |
| `s(t)` | dimensionless moment history | 1 |
| `q(t)=ds/dt` | moment-rate time function | 1/s |
| `delta(x-xs)` | three-dimensional Dirac delta | 1/m^3 |

Moment components supplied in a common `NED` basis must be permuted into
Wave3D `END` order:

```text
Mxx_END = Myy_NED    Myy_END = Mxx_NED    Mzz_END = Mzz_NED
Mxy_END = Mxy_NED    Mxz_END = Myz_NED    Myz_END = Mxz_NED
```

This mapping assumes both systems use positive down and symmetric tensors.

## Continuous equations

With repeated indices summed, conservation of linear momentum and isotropic
Hooke elasticity are

```text
rho * partial_t(v_i) = partial_j(sigma_ij) + f_i

partial_t(sigma_ij) =
    lambda * delta_ij * partial_k(v_k)
  + mu * (partial_j(v_i) + partial_i(v_j)).
```

Equivalently, the nine scalar equations are

```text
rho dvx/dt = d(sxx)/dx + d(sxy)/dy + d(sxz)/dz + fx
rho dvy/dt = d(sxy)/dx + d(syy)/dy + d(syz)/dz + fy
rho dvz/dt = d(sxz)/dx + d(syz)/dy + d(szz)/dz + fz

dsxx/dt = (lambda+2mu) dvx/dx + lambda (dvy/dy + dvz/dz)
dsyy/dt = (lambda+2mu) dvy/dy + lambda (dvx/dx + dvz/dz)
dszz/dt = (lambda+2mu) dvz/dz + lambda (dvx/dx + dvy/dy)
dsxy/dt = mu (dvx/dy + dvy/dx)
dsxz/dt = mu (dvx/dz + dvz/dx)
dsyz/dt = mu (dvy/dz + dvz/dy).
```

The physical input fields are converted in double precision before storage as
`float32` propagation coefficients:

```text
mu     = rho * Vs^2
K      = rho * (Vp^2 - (4/3) Vs^2)
lambda = K - (2/3) mu = rho * (Vp^2 - 2 Vs^2).
```

For a stable elastic solid, Wave3D requires finite `rho>0`, `Vp>0`, `Vs>0`,
`mu>0`, and `K>0`. Thus the cellwise velocity condition is
`Vp^2 > (4/3)Vs^2`. `lambda` itself may be negative; rejecting it solely for
that reason would be incorrect when `K` and `mu` remain positive.

## Spatial staggering

Let `(i,j,k)` denote the physical position `(i*dx,j*dy,k*dz)` before padded
storage offsets are added. All arrays retain `[z][y][x]` storage with x
contiguous. Logical locations are:

| Quantity | Offset `(x,y,z)` in grid cells | Time level |
| --- | --- | --- |
| `sxx, syy, szz` | `(0,0,0)` | half-integer |
| `lambda, mu, K` | `(0,0,0)` | static |
| `vx` | `(1/2,0,0)` | integer |
| `vy` | `(0,1/2,0)` | integer |
| `vz` | `(0,0,1/2)` | integer |
| `bx=1/rho_x` | `(1/2,0,0)` | static |
| `by=1/rho_y` | `(0,1/2,0)` | static |
| `bz=1/rho_z` | `(0,0,1/2)` | static |
| `sxy` | `(1/2,1/2,0)` | half-integer |
| `sxz` | `(1/2,0,1/2)` | half-integer |
| `syz` | `(0,1/2,1/2)` | half-integer |
| `mu_xy` | `(1/2,1/2,0)` | static |
| `mu_xz` | `(1/2,0,1/2)` | static |
| `mu_yz` | `(0,1/2,1/2)` | static |

The half offsets are logical; they do not alter the common SoA linear-index
formula. Each field's index zero represents its own listed location.

## Twelfth-order staggered derivative

Increment 4 uses a radius-six centered staggered derivative. For samples on
opposite half grids around target `x`, define `a_m=m-1/2` and

```text
D_h f(x) = (1/h) sum(m=1..6) c_m
           [f(x+a_m h) - f(x-a_m h)].
```

The coefficients are the unique standard Taylor coefficients satisfying

```text
2 sum c_m a_m       = 1
2 sum c_m a_m^(2r+1) = 0,  r=1,2,3,4,5.
```

| `m` | exact `c_m` | decimal |
| ---: | ---: | ---: |
| 1 | `160083/131072` | `1.22133636474609375` |
| 2 | `-12705/131072` | `-0.09693145751953125` |
| 3 | `22869/1310720` | `0.017447662353515625` |
| 4 | `-5445/1835008` | `-0.00296728951590401786` |
| 5 | `847/2359296` | `0.000359005398220486111` |
| 6 | `-63/2883584` | `-0.0000218478116122159091` |

The first omitted term is

```text
D_h f = f' - (231/54525952) h^12 f^(13) + O(h^14),
```

so the spatial derivative is 12th-order accurate for smooth fields. These
fractions were solved independently from the moment equations. Their decimals
match the six values found in the legacy helper, although that helper labels
them “optimized”; Wave3D treats them as standard Taylor coefficients.

For an integer-grid field `f[i]`, the derivative at half index `i+1/2` is

```text
D(I->H) f[i] = (1/h) sum c_m (f[i+m] - f[i-m+1]).
```

For a half-grid field whose array element `g[i]` lies at `i+1/2`, the
derivative at integer index `i` is

```text
D(H->I) g[i] = (1/h) sum c_m (g[i+m-1] - g[i-m]).
```

Every update must select `I->H` or `H->I` from the source and target lattice;
ad-hoc index shifts are forbidden.

## Leapfrog update contract

Initial state is `v^0=0` and `sigma^(-1/2)=0`. For step `n`, with
`t_n=n*dt`, perform exactly this order:

1. Update all six stresses from `n-1/2` to `n+1/2` using `v^n`.
2. Add the moment-rate source to the updated stress at the same `n+1/2`
   level, sampling `q(t_n)`.
3. Apply the stress-boundary hook. It is a no-op in the interior reference.
4. Update all three velocities from `n` to `n+1` using `sigma^(n+1/2)`.
5. Apply the velocity-boundary hook. It is a no-op in the interior reference.
6. Sample receivers from `v^(n+1)` and label the sample time `(n+1)dt`.
7. Notify diagnostics.

The discrete interior stress equations are the continuous scalar equations
above with the correct staggered `D(I->H)` or `D(H->I)` derivative and a factor
`dt`. Normal stresses use collocated `lambda` and `mu`; shear stresses use the
corresponding staggered `mu_xy`, `mu_xz`, or `mu_yz`.

The velocity equations are

```text
vx^(n+1) = vx^n + dt*bx * [Dx(sxx) + Dy(sxy) + Dz(sxz)]^(n+1/2)
vy^(n+1) = vy^n + dt*by * [Dx(sxy) + Dy(syy) + Dz(syz)]^(n+1/2)
vz^(n+1) = vz^n + dt*bz * [Dx(sxz) + Dy(syz) + Dz(szz)]^(n+1/2).
```

Increment 4 may execute the six stress equations in one transparent CPU loop
and the three velocity equations in another. Kernel fusion is not relevant to
the reference implementation.

## Heterogeneous material placement

Physical `Vp`, `Vs`, and `rho` samples are interpreted at the normal-stress
lattice. Derived `lambda`, `mu`, and `K` are collocated there. Face buoyancies
use arithmetic density averaging:

```text
bx(i+1/2,j,k) = 2 / [rho(i,j,k) + rho(i+1,j,k)],
```

with analogous formulas for y and z. Shear modulus at an edge is the harmonic
mean of the four neighboring normal-stress samples. For example,

```text
mu_xy(i+1/2,j+1/2,k) =
    4 / [1/mu(i,j,k) + 1/mu(i+1,j,k)
       + 1/mu(i,j+1,k) + 1/mu(i+1,j+1,k)].
```

`mu_xz` and `mu_yz` use the analogous four samples in their planes. These
choices follow the heterogeneous averaging direction supported by Moczo et
al. The first CPU reference qualifies homogeneous media and grid-aligned
interfaces only. Arbitrary subcell-interface accuracy is not claimed.

Before coefficient generation, physical edge values are extended constantly
through the nonphysical halo required by the stencil. Later boundary modules
may define additional coefficient preparation in absorbing zones without
changing the interior formulas.

## Moment-tensor source convention

Wave3D defines the distributional body-force representation

```text
f_i(x,t) = -M_ij * s(t) * partial_j delta(x-xs).
```

Move this term into total stress as

```text
sigma_total_ij = sigma_elastic_ij - M_ij*s(t)*delta(x-xs).
```

Differentiating gives the stress-rate source used by the leapfrog update:

```text
partial_t(sigma_source_ij) = -M_ij*q(t)*delta(x-xs),
q(t)=ds/dt.
```

This is an explicit Wave3D sign derivation. With tension-positive stress, a
positive isotropic tensor has `Mxx=Myy=Mzz>0` and produces a negative normal
stress increment while `q(t)>0`. The initial P-wave particle motion is outward,
which is the required positive-explosion polarity.

The Ricker function is a moment-rate function, not a dimensionless stress
increment:

```text
a(t) = pi*f0*(t - origin_time - peak_delay)
q(t) = A_rate * [1 - 2*a(t)^2] * exp[-a(t)^2],
```

where `A_rate` has units `s^-1`. A Ricker moment-rate pulse has zero net time
integral and therefore no permanent final moment. Physical runs should choose
a delay that makes the truncated pre-zero tail negligible; `1.5/f0` gives a
starting magnitude below `1e-8` of the peak when origin time is zero.

For a discrete component lattice with trilinear weights `w_a`, each component
update is

```text
sigma_ij[a] += -dt * M_ij * q(t_n) * w_a / (dx*dy*dz).
```

Weights are prepared separately for each staggered stress component, are
nonnegative for an interior point, and must sum to one within roundoff. This
preserves the integrated moment-rate normalization. No clipping or silent
renormalization at a boundary is allowed. Increment 4 must reject a source
whose interpolation support is unavailable.

The six tensor entries are components in Wave3D's `END` axes. The off-diagonal
entry is deposited once into its symmetric shear-stress field; it is not
multiplied by two.

## Receiver convention

Each receiver represents one physical point. Because `vx`, `vy`, and `vz` live
on different lattices, prepare a separate trilinear interpolation stencil for
each component. Each component's weights must sum to one. Sampling occurs
after the velocity update and records `(vx,vy,vz)` at `(n+1)dt` in m/s.

Increment 4 validation receivers are interior. Surface interpolation coupled
to a traction-free boundary is deferred to Increment 8 and must not be claimed
from the simple interior reference.

## Stability bound

For Fourier angle `theta=k*h`, the radius-six derivative symbol is

```text
k_tilde(theta) = (2/h) S(theta),
S(theta) = sum c_m sin[(m-1/2)theta].
```

Let `u=sin(theta/2)`. Independent symbolic reduction gives

```text
S = u*(19845u^10 + 26950u^8 + 39600u^6 + 66528u^4
       + 147840u^2 + 887040) / 887040.
```

Every coefficient of `dS/du` is positive on `0<=u<=1`, so the maximum occurs
at the Nyquist angle `theta=pi`:

```text
A = max |S| = 1187803/887040 = 1.33906362734487734.
```

Leapfrog von Neumann analysis for the fastest P mode gives the mathematical
interior stability limit

```text
dt <= 1 / [A * Vp_max * sqrt(dx^-2 + dy^-2 + dz^-2)].
```

Wave3D will apply a separate dimensionless safety factor `eta` with
`0<eta<1`; the initial default is `eta=0.9`. Thus the accepted runtime limit is

```text
dt_limit = eta / [A * Vp_max * sqrt(dx^-2 + dy^-2 + dz^-2)].
```

For the current `dx=dy=dz=10 m`, `Vp_max=4000 m/s` sample, `eta=0.9` gives
`dt_limit=0.000970109320535 s`; the configured `0.0005 s` is below it. This
bound applies to the interior elastic scheme. A future boundary formulation
may impose a stricter tested limit.

## Numerical-dispersion gate

A Ricker wavelet is not bandlimited, so every run must record an explicit
design frequency `f_design`. A recommended default is `3*f0`; the Ricker
amplitude there is approximately `0.302%` of its spectral peak. Accuracy claims
apply only through `f_design`.

For the selected operator, an axis-aligned wave with `N` points per wavelength
has spatial phase ratio

```text
v_phase_num/v = 2*S(2*pi/N)/(2*pi/N).
```

At `N=5`, the independently evaluated spatial phase error is `-0.00384%` and
the group-velocity error is `-0.04579%`; axis alignment is the worst direction
for the separable operator at the same physical wavelength. Wave3D therefore
requires, for the initial solid solver,

```text
Vs_min / (f_design*dx) >= 5,
Vs_min / (f_design*dy) >= 5,
Vs_min / (f_design*dz) >= 5.
```

It also requires at least 20 time samples per design-frequency period:

```text
f_design*dt <= 0.05.
```

For an otherwise exact spatial derivative this caps the second-order leapfrog
phase error at about `0.416%`. Both the selected `f_design` and measured points
per wavelength must appear in resolved metadata. These are accuracy checks in
addition to, not substitutes for, the CFL stability check.

## Increment 4 boundary limitation

The CPU reference will allocate a radius-six halo, extend material coefficients
constantly, initialize wavefields to zero, and update only points with complete
stencils. It will not claim an absorbing or free-surface boundary. Physical
tests must stop before a wave traveling at `Vp_max` can leave the validation
region, reach the unqualified edge, and return to a receiver.

The later sponge, CPML, and free-surface increments must each specify their own
equations, coefficient locations, stability implications, and reflection tests.

## Predeclared Increment 4 tests

Increment 4 is not complete until all applicable tests below pass:

1. **Coefficient identities:** compare all six doubles with the exact
   fractions; verify moments for powers 1 through 11 and the power-13 error
   coefficient.
2. **Derivative exactness:** constants give zero; polynomials through degree 12
   differentiate to roundoff away from boundaries.
3. **Derivative convergence:** a smooth sinusoid on successively refined grids
   exhibits the expected 12th-order regime before floating-point roundoff.
4. **Lattice/index test:** impulse and affine-field tests distinguish every
   `I->H` and `H->I` offset in x, y, and z.
5. **Material conversion:** verify `lambda`, `mu`, `K`, positive-solid
   rejection, face-density arithmetic means, and four-point shear-modulus
   harmonic means.
6. **Manufactured one-step update:** affine velocity fields produce all six
   analytical stress increments; affine stress fields produce all three
   analytical velocity increments, including every cross derivative.
7. **CFL and dispersion validation:** exact threshold values pass, values above
   fail, and the sample configuration reports its design-band margins.
8. **Source conservation:** interpolation weights sum to one and the volume
   integral of each stress increment equals `-dt*M_ij*q(t_n)` within roundoff.
9. **Source polarity:** a positive isotropic explosion produces outward first
   motion at symmetric receivers on the positive and negative coordinate axes.
10. **P arrival:** for a centered isotropic source in a homogeneous model, the
    Ricker peak feature arrives at `origin+peak_delay+r/Vp` within a tolerance
    declared from `dt` and spatial dispersion.
11. **S arrival:** an `Mxy` source observed on the x axis produces y-polarized
    shear motion whose peak feature arrives at
    `origin+peak_delay+r/Vs` within the same derived tolerance.
12. **Symmetry:** opposite-axis traces agree after the expected component sign
    reversal; isotropic-source transverse energy remains at roundoff scale
    before boundary contamination.
13. **Energy stability:** after source support ends, the discrete kinetic plus
    strain-energy envelope remains bounded during the pre-boundary window.
    Strain energy uses
    `sigma_dev:sigma_dev/(4mu) + trace(sigma)^2/(18K)`.
14. **Determinism and safety:** repeated CPU runs match, no allocation occurs in
    the time loop, and ASan/UBSan report no error.

Arrival tolerances must be calculated before running a case as at least one
time sample plus the predicted design-band phase error over the travel time;
they must not be widened after seeing results. Boundary-contaminated samples
must not be used to pass an interior-physics test.
