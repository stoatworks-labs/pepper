# pepper — orientation for another LLM (or a newcomer)

**What it is:** Pepper's ghost — a clear sheet at an angle reflecting a hidden
screen over the stage behind it — as two FFGL 2.1 plugins for Resolume
Arena/Avenue, built from one core. They are written in C++17 and GLSL 4.10,
built with CMake into two universal macOS `.bundle`s. MIT.

| | id | display name | bundle | type |
| --- | --- | --- | --- | --- |
| the pane, the foil and the pyramid, seen | `PG01` | `SW Pepper` | `Pepper.bundle` | **mixer**, 2 inputs |
| the cross layout a real pyramid is fed | `PG02` | `SW Pepper Feed` | `PepperFeed.bundle` | effect, 1 input |

**Status, 2026-11-02:** v0.1.0, released on Allan's go-ahead ("Yes, release
Pepper"), public at https://github.com/stoatworks-labs/pepper.

- It has **never been loaded into Resolume on macOS**.
- It started in tranche four, from IDEAS.md. The spec is
  `~/Projects/resolume/specs/SPEC-pepper.md`.

`CLAUDE.md` is the command reference. This file is the *why*:

- the decision between effect and mixer;
- every number in the harness and where it comes from;
- the traps this build actually hit;
- what is verified and what is assumed;
- the decisions taken without asking.

For how an FFGL mixer behaves at the ABI and in Arena, read genlock's
`AGENTS.md`, relay's "What Relay showed in Arena" and lenticular's "What
Lenticular showed in Arena". That covers:

- the type is one argument, and the input count is a separate declaration;
- index 0 is hidden;
- `Opacity` is bound to the layer fader and ramped by transitions.

Nothing here contradicts them, and **none of it was re-measured on this plugin**.

---

## The one idea

A clear sheet at an angle splits every ray into a reflected and a transmitted
part, in the proportions Fresnel gives for that angle. SW Pepper traces that,
per pixel, in linear light:

1. A camera ray meets a sheet.
2. Its transmitted part goes on, to the stage (Dest) or to the next sheet.
3. Its reflected part goes to the hidden screen (Src), or to the dark around it.

Everything the README lists — the ghost is additive, the stage is dimmed,
Brewster's null, the double image of a thick pane, the tint applied twice, a
coat's interference, the foil's swim, the keystone, the pyramid's faces, the
screen above or below — falls out of that one model.
None of it is a separate effect. The spec has the geometry in full, and
`source/Rig.h` has it as code.

## Effect or mixer — decided

**The rig is a mixer; the feed is an effect; two bundles, one core.**

- The pane needs two pictures on two light paths: the stage **dimmed** by T and
  the screen **added** at R, per pixel, in linear light.
  - An effect has one input.
  - No host blend of an effect's output does both. An alpha layer would need
    alpha 1 − T and colour R·L / (1 − T). For a clear sheet R + T = 1, so that
    colour is the screen's own light L, which is over white whenever the screen
    is brighter than the stage — as it must be for a ghost to read (Source
    Level +2.5 stops by default).
  - A host also blends in gamma space, with one alpha for three channels,
    where a tinted sheet dims each channel differently.

  So SW Pepper is the hologram layer's **Blend Mode**: Dest is the stage below,
  Src is the hidden screen. Resolume's own Add is this with T = 1 and R = 1, in
  gamma space, and `Linear Light` off gives the gamma half of that for
  comparison.
- **Opacity is the source's light**, not a crossfade. Arena binds a mixer
  parameter named `Opacity` to the layer fader, so fading the layer up is
  bringing the lamp up on the Victorian actor. At 0 the glass is still there,
  dimming the stage, and `--opacity` asserts that to the bit.
- The pyramid **viewer** is the same mixer under `Rig = Pyramid`.
- The pyramid **feed** has one input and must work on a clip, a layer or the
  composition — whatever a pyramid output is fed from. A mixer can be none of
  those, so it is an effect. It is built from **the same GLSL function**
  (`feedLookup` in `kCommon`) that the mixer's screen uses under
  `Screen = Feed`, so the feed sent to a real pyramid is the one the simulator
  looked into. `--feed` asserts the two agree.

## How the plugins are wired

- **One OBJECT core, two entry files** (vectrix's pattern).
  - `pepper_core` holds everything except the registrations.
  - `source/MixerEntry.cpp` and `source/FeedEntry.cpp` each hold one
    `CFFGLPluginInfo` and are compiled only into their own bundle.
  - A registration in the core would put both plugins in both bundles. The
    oxbow step of `verify.sh` asserts each bundle reads as its own.
  - `pgtest` links the core without either entry file and instantiates both
    classes directly.
- **Mixer inputs:** `inputTextures[0]` is Dest and `[1]` is Src.
  - Each has its own `MaxUV` and half-texel inset, applied once, in its own
    fetch in the fragment shader.
  - The vertex shader passes UV through unscaled. `FoldMaxUV` exists only for
    the genlock-trap negative control.
  - The scoped bindings are declared interleaved — activate(0), bind(0),
    activate(1), bind(1) — because each one clears to 0 on exit rather than
    restoring.
- **Guards** return `FF_FAIL` for each of these:
  - a null array;
  - fewer than two inputs;
  - a null Dest or a null Src;
  - a zero-sized input;
  - a zero viewport.

  Each input guard is logged once; a zero viewport is refused silently. The
  Feed guards a missing or null input.
- **The CPU does the geometry in double, once a frame** (`rig::Build`):
  - the faces, the camera, the feed's arm size;
  - the ripple's six mode heights per face, with phases reduced to one turn in
    double (`fmod`) before they meet the phase offset. No time reaches the GPU,
    so an hours-long session is as exact as its first second.
- **The clock** is relay's `Timing.h`:
  - `SetTime` arrives in ms in Arena; the unit is settled by voting;
  - the epoch is the first reading;
  - a host that never calls `SetTime` gets a steady clock.
- **The shader is a small ray tree.** Per pixel, four rotated-grid subsamples
  each trace a camera ray **backwards from the eye** through an explicit stack:
  16 deep, at most 64 rays.
  - At each sheet the ray splits into one transmitted ray and the reflected
    images. A film has one; a thick pane has three plus the lumped rest of the
    series.
  - Each ray carries **Q**, per channel a 2×2 Hermitian matrix: how the eye
    sees a field arriving down it, E^H Q E. It starts as the identity. Each
    interaction turns Q into its own s and p (p = s × k, Born & Wolf, both
    sides) and applies Q' = J^H Q J, J = diag(x_s, x_p), where x is a summed
    Coef: |x_s|², |x_p|² and conj(x_s) x_p, which carries the phase. At the
    screen a polarised source's field E is seen as E^H Q E, an unpolarised one
    as tr Q / 2. So a polarisation is carried through every face, not just the
    last (v0.1.0's approximation, now `kFaultLastOnly`).
  - A ray is pushed only if Q's larger eigenvalue — the most it could still
    contribute, whatever the light it meets — exceeds `Prune` =
    `kPruneLight` (1/8192, `Controls.h`) divided by the brightest light in the
    scene: max(Source Level's gain, Spill, Stage Level). That is computed from
    Source Level, **not** Source Level × Opacity: otherwise the set of rays
    followed would change with the fader, and Opacity 0 would not be the
    black-Src picture bit for bit.
- **A coat is a table.** `Coating.cpp` sums the layer's Airy series in double,
  per channel, over 1024 values of cos(incidence): R and the s-p product from
  the air, the same from inside the sheet, and T, 12 rows in all. It is
  uploaded as RGBA32F when Sheet, Coating or Coat Thickness changes, bound by
  hand on texture unit 2 around the draw, and unbound after: the scoped
  bindings clear units 0 and 1 only. `--mixer` asserts all three units are
  empty, and unit 0 active, after a coated frame.
- **Screen above is the Below rig seen in a mirror.** `rig::Build` reflects the
  camera about the rig's centre height and flips the screen picture's mapping,
  so the shader has one geometry. The harness does not: it predicts Above in
  real space, the screen overhead facing down (`worldFor`).
- **Each channel has its own index** (`kChannelNm`: 611.3, 549.1, 464.3 nm,
  the BT.709 primaries' dominant wavelengths) for Fresnel, the coat and the
  tint's path. The rays themselves bend by green's (`kRayChannel`); see
  "Dispersion" below.
- **Dest is sampled once, at the pixel's own centre**, and multiplied by the
  summed weight of the rays that reached the stage. A thin sheet does not
  deviate what is seen through it, and that is what makes the stage stand
  still while the foil ripples.
  - The consequence, decided: the stage is **what the camera sees**. Distance,
    Elevation and Azimuth move the glass, the screen and the ghost, not the
    stage picture.
- **Output alpha:** `A = max(α_dest, max(shown))` and rgb = shown / A. That is
  opaque wherever the stage is, and over black it is exactly the shown signal.
  The Feed is opaque everywhere, black outside the arms.

## Every number in the harness

Every numeric check runs at **640×360 and 320×180**, on this Mac's GPU **and**
on Apple's software renderer (`PGTEST_RENDERER=software`). Light levels are
read through a float framebuffer. Predictions come from the harness's own
double-precision model in `tools/pgtest/Harness.h`:

- `worldFor`, `paneRay`, `mirror`, `project`, `fresnelAt`, `filmR`/`filmT` and
  `feedPoint`;
- for a coat, a Macleod characteristic matrix (`macleodAt`), in Macleod's own
  sign convention (N = n − ik), carried into Born & Wolf's for comparison:
  conjugated, r_p → −conj(r_p), t_p from tangential to full fields, and t
  normalised by the power each side carries;
- the slab's incoherent series (`slabR`, `slabT`) in Coefs of their own;
- a 3D coherency tracer through the pyramid (`pyramidLight`): world-space 3×3
  matrices, M' = J^H M J with J = x_s s sᵀ + x_p p_out p_inᵀ, pruned as the
  plugin prunes, so it shares no basis and no code with the shader's 2×2 Q;
- written from the spec's geometry, not from `rig::Build` or the shader;
- fed what the harness **asked for**, through `asked()`, which reads the
  parameters back through `Controls.h`. They are never fed `SetupForTest()`,
  which would follow a fault instead of catching it.

### The tolerance-source list

| Check | The number | Where it comes from |
|---|---|---|
| light levels, everywhere | **1e-4** in linear light (`kLightTol`) | A stated budget of 128 float operations at 3 ULP each — GLSL lets division and `sqrt` be that loose — is 2.3e-5 (`kLightBound`), and the tolerance is asserted (`static_assert`) to be at least three times it. A budget, not a count; measured worst everywhere is 1e-7 to 3e-7, two orders under. |
| `--mixer` sentinel | **135** in summed channel difference | genlock's: half the distance from the magenta padding to the nearest card colour. Each picture is first divided by its own all-white render, so the glass's Fresnel weights cancel and what is left is what was sampled. |
| `--mixer` quadrants | `kLightTol` | Only pixels whose every subsample, through the harness's own pane model, lands two texels clear of any boundary. |
| `--mixer` markers | **one Dest texel**; for Src, **one Src texel's mirrored projection + 0.375 px** | The sampling's own blur; 0.375 is three times Euler–Maclaurin's 1/8 px bound on a sampled shape's centroid. |
| `--additive` | **0 bytes** on the black half; `kLightTol` elsewhere | A black Src adds exactly 0 light — a branch-free sum of zero weights — so the left half, over black, must be bitwise the black-Src picture. |
| `--energy` | `kLightTol` | R and T from two renders each (white Src over black Dest, and the reverse), divided out per pixel. |
| `--fresnel` Brewster row | **0.1 px** | A stated margin, not a derived bound. Near the null, p is quadratic in the angle, so a parabola through three rows is exact but for the cubic term. Measured 0.001 px. |
| `--fresnel` p/s at the null | **1e-4**, asserted ≥ 3 × the model's own p/s for that pixel | The harness's model of the same pixel, with its subsamples up to 7/8 px off the null and its two columns off centre, gives the depth a real null can reach. That is 3e-6, and the measured value is 3e-6. |
| `--fresnel` Abelès | **3e-5**, asserted ≥ 3 × the float bound | Each film value is good to `kLightBound` relative; that is propagated through R = R_f / (2 − R_f) and R_s². Bisected so that pixel (W/2, H/2) meets the sheet at exactly 45°. |
| `--double` weights | **1e-3 relative** | The subsample spread is second order (< 1e-4); W2, the smallest, is a difference of two floats near 0.1 known to 1e-7, about 1e-4 relative. Measured 1.5e-7 and 8.3e-5. |
| `--double` edges | **0.5 px** | The step's half-level crossing: the pixel whose centre ray puts image k on the Src edge, bracketed on pixel centres and bisected in the model. Then the measured crossing, linearly interpolated between two rows. Measured 0.02 px. |
| `--geometry`, `--pyramid` dots | **0.375 px + 2 × the projective shift** per dot | The centroid of a sampled trapezoid is within 1/8 px of the true one (Euler–Maclaurin), three times that, plus twice how far the projective map moves a dot's centroid off its projected centre (computed per dot from its corners). Every dot the model puts in view must be found. |
| `--geometry` keystone | the ratio's own propagated dot tolerances | The four corners' tolerances, carried through the ratio of top to bottom width. |
| `--ripple` Dest marker | **0.01 px** | An argued margin: the marker's centroid can move only as T varies across it, which is smooth and a fraction of a percent of its light. Measured 0.0002–0.0004 px. |
| `--ripple` spectrum | ≥ 4 of 6 modes found; the strongest within a bin of a mode; nothing else ≥ 5 % | 256 frames at 8 fps is 1/32 Hz a bin. The closest modes (1.708, 1.811 Hz) are three bins apart, and the Hann window's sidelobes are under 3 %. |
| `--opacity` | **0 bytes, 0 floats** at Opacity 0; `kLightTol` for linearity | Opacity multiplies the source's gain and nothing else, Prune included (above). |
| `--feed` dots | **0.375 px** | As for the dots above, in the feed's own pixels. The arms are axis-aligned at four sides, so there is no projective shift. |
| `--feed` mixer agreement | the feed's 0.375 px, seen through the mixer's camera | The mixer's pyramid screen from 60° up, against the feed's **measured** points, not its predicted ones. |
| `--fresnel` per channel | as above, for each of red, green and blue at its own index | Each channel's null sits where atan n for that channel puts it: 4 px apart, red to blue, for PET at 640×360. |
| `--fresnel` 45° and 135° | `kLightTol` per pixel; the mirror pair to `kLightTol` | R for the field at that angle, projected onto each pixel's own s and p. |
| `--coating` model | **1e-12** for identities; **1e-4** for titanium dioxide's R + T | A zero-thickness layer is the bare surface; reciprocity; quarter and half waves; bulk aluminium; the plugin's Airy sum against the harness's matrix and its cross terms. All exact in double, 1e-15 measured. Titanium dioxide is not lossless: Siefke's k is 5.4e-6 in blue, which makes R + T short by 5e-5. |
| `--coating` picture | `kLightTol`; the absorption **2 × `kLightTol`** | Absorption is 1 − R − T, two measurements. Pixels whose image weight is within 1e-4 of the prune are not predicted (below). |
| `--coating` table | measured lerp error ≤ **a third of the tolerance** | The plugin's 1024-entry table against the exact coat, at every predicted pixel's incidence: 1e-7 worst. |
| `--falloff` | `kLightTol` | The film's R × cos^p off the screen's normal, with p from the Viewing Angle through `Controls.h`. |
| `--polarised` | `kLightTol` | An 80×45 grid of pixels over the frame, against the 3D tracer, for None, Horizontal, Vertical, 45 and 135. A pixel is skipped, and counted, when a subsample passes within 1e-4 rig sizes (about 0.05 px) of a face's edge, or an image's weight is within 1e-4 of the prune: float and double may disagree there. |
| `--mixer` units | **0** | Texture units 0, 1 and 2 bound to nothing, unit 0 active. |
| `--bench` | not asserted | There is no threshold worth asserting on a shared machine. |

**Negative controls live in the shipping class.**
`Pepper::SetFaultForTest` takes a bitmask of `pepper::Fault`, as does
`Feed::SetFaultForTest`. The shipped plugins carry 0, and nothing but the
harness sets it. Each fault builds the wrong answer into the real shader or the
real `rig::Build`:

| Fault | Check | What it does |
|---|---|---|
| `kFaultFoldedMaxUV` | `--mixer` | Dest's MaxUV folded into the varying (the genlock trap). Fails 4 assertions. |
| `kFaultAlphaOver` | `--additive` | The ghost laid over the stage, so a dark source darkens it. Fails 3. |
| *(Linear Light off)* | `--additive` | The add done in gamma space. Fails superposition and the decoded stage, 2. |
| `kFaultOneSurface` | `--energy` | Transmission through one surface, 1 − R. |
| `kFaultRpCosSwap` | `--fresnel` | r_p with cos i and cos t swapped: no null where Brewster says. Fails 8. |
| `kFaultSwapSP` | `--fresnel` | s and p exchanged. Fails 8. |
| `kFaultOnePass` | `--double` | The back image traced through half the sheet. Fails 4. |
| `kFaultTintOnce` | `--double` | A back image tinted once, not twice. Fails 3. |
| `kFaultFixed45` | `--geometry` | The mirror at 45° whatever Sheet Angle says. Fails 2 (the keystones). |
| `kFaultMirrored` | `--geometry` | The screen's picture mirrored left to right. Fails 1. |
| `kFaultModesLocked` | `--ripple` | Every mode at the fundamental's frequency (CPU). Fails 1. |
| `kFaultCrossfade` | `--opacity` | Opacity as a crossfade to the bare stage. Fails 2. |
| `kFaultRotatedArms` | `--pyramid`, `--feed` | The common layout: arms as rotated copies, not reflections. Fails 17 and 9. |
| `kFaultLastOnly` | `--polarised` | The polarisation forgotten at every interaction but the last (v0.1.0). Fails 2. |
| `kFaultCoatSymmetric` | `--coating` | A metal coat reflecting the same from inside as from the air. Fails 2. |
| `kFaultCoatIncoherent` | `--coating` | The coat's two faces added in intensity, not amplitude (CPU, the table). Fails 2. |
| `kFaultOneIndex` | `--fresnel` | One index for all three channels: no dispersion (CPU). Fails 10. |
| `kFaultFalloffOff` | `--falloff` | Every screen Lambertian, whatever Viewing Angle says. Fails 2. |
| `kFaultAboveAsBelow` | `--geometry`, `--pyramid`, `--feed` | Screen Above with Below's picture maps and feed (CPU). Fails 1, 9 and 5. |
| `kFaultClockwise` | `--fresnel` | The polarisation angle taken clockwise: 45 and 135 swapped (CPU). Fails 2. |
| `kFaultLeaveBound` | `--mixer` | The coat's table left bound on unit 2, and unit 2 left active (CPU). Fails 1. |

### Would this hold on another rasteriser, at another raster?

- **Light levels** (`--additive`, `--energy`, `--opacity`, `--mixer`'s
  quadrants): yes. Every position comes from `floor( uv * OutSize )`, an integer
  pixel, never from the interpolated `uv`. Every weight is arithmetic on
  uniforms the CPU computed in double, and the tolerance is a float budget, not
  a raster count. The software renderer gives the same numbers to the printed
  digit.
- **`--additive`'s bitwise half**: yes on any rasteriser. A black Src
  contributes `0 × weight`, and the two renders run the same shader on the same
  uniforms.
- **`--fresnel`**: the Brewster row is the centre row at both rasters by
  construction (the sheet angle is chosen so that the level ray meets it at
  atan n). The parabola is fitted in rows, so it scales.
- **`--double`**: the edges are bracketed on this raster's own pixel centres.
  The 0.5 px margin is in this raster's pixels at both.
- **`--geometry`, `--pyramid`, `--feed`**: each tolerance is computed per dot,
  from that raster's projection. At 320×180 the pyramid's dots are within 0.30
  of tolerance, against 0.06 at 640×360. Smaller dots, same bound.
- **`--ripple`**: no raster dependence in the frequencies. The ghost's swing
  halves with the raster (7.7 to 3.8 px), and its floor, 0.5 px, is under both.
- Two rasterisers have run all of it: this Mac's GPU and Apple's software
  renderer, at both rasters. llvmpipe and other GPUs have not.

### The mutation

**Shipped (`--mutation`, run by `verify.sh`):** in the shader,
`vec3 tau  = pow( Tint, vec3( 1.0 / max( cost, 1e-4 ) ) );` → `cosi`. One
character: the tint's path length taken from the angle of incidence instead of
refraction.

- It fails **3** `--double` assertions.
- The shipped text, compiled through the same hook, renders the default path's
  bytes.

**`tools/mutate.sh`** copies the tree once and builds arm64-only. Then, for
each of **22** one-character mutants, it recompiles the one file and runs the
named check. **22 of 22 are caught:**

| Mutant | Check that catches it |
|---|---|
| r_p's numerator `-` → `+` | `--fresnel` |
| a polarised source seen with Q's s-p cross term negated | `--polarised` |
| a sheet's internal echoes summed as an alternating series, `( 1.0 - b.s )` → `+` | `--energy` |
| Q turned into the next face's s and p with its new cross term signed wrong, `( qp - qs )` → `+` | `--polarised` |
| the screen's falloff measured off its back, `-r.d.y` → `+` | `--falloff` |
| the coat's table interpolated toward the wrong neighbour, `i + 1` → `i - 1` | `--coating` |
| the back image walked the wrong way along the pane | `--double` |
| the pane's screen picture shifted, `P.x - r.x` → `+` | `--geometry` |
| every feed arm sampled half a clip to one side | `--feed` |
| sRGB decoded at exponent 2.5 | `--additive` |
| the pyramid camera aimed at the wrong height (C++) | `--pyramid` |
| every mode's frequency over the wrong fundamental (C++) | `--ripple` |
| a feed arm's depth `w / aspect` → `w * aspect` (C++) | `--feed` |
| arm 0 pointing up the picture (C++) | `--feed` |
| the Above rig's camera not reflected about the rig's centre (C++) | `--geometry` |
| every channel given green's index, `c` → `1` (C++) | `--fresnel` |
| the polarisation angle taken clockwise, `+ py` → `- py` (C++) | `--fresnel` |
| Airy's denominator for s with the echo signed wrong (C++, `Coating.cpp`) | `--coating` |
| the coat seen from inside without r' = −r (C++, `Coating.cpp`) | `--coating` |
| Src's MaxUV square, `maxSrc.t` → `maxSrc.s` (C++) | `--mixer` |
| unit 2 left active after the coat's table is unbound, `+ 1` → `+ 2` (C++) | `--mixer` |
| Opacity dividing the source's light (C++) | `--opacity` |

**A blind spot, measured.** Q's turn into a new face's s and p has two
cross terms in its new diagonal, ±2 c sn qr. qr is zero until a ray has turned
between two planes of incidence, so those terms reach the picture only at a
**third** face. Flipping one moved no measured pixel by as much as 3e-7, in
any `--polarised` rig. No check can see them, and `mutate.sh` carries none.
They are the rotation as derived, and the harness's tracer has them in its
own form.

`Controls.cpp` has no mutants, on purpose. The harness reads what it asked for
back through `Controls.h`, so a wrong mapping moves the plugin and the
prediction together. The mappings' ends and inverses are pinned by `--names`
and by the sweep.

---

## The traps

These are the ones this build actually hit, in the order they were found.

**The sRGB decode was pinned by nothing.** `--additive`'s superposition test
decodes the plugin's own output with the harness's decode. That holds for **any**
decode the plugin uses, as long as its encode inverts it: L(stage, screen) =
L(stage, 0) + L(0, screen) whatever the curve is.

- The physics suites run with Linear Light off (`physics()`), so none of them
  saw it either.
- `tools/mutate.sh`'s exponent mutant (2.4 → 2.5) survived every check.
- Fixed by predicting the stage alone, in light, from the card:
  T × Stage Level × decode(card). The gamma-space negative control now fails
  that assertion too.

**`--geometry`'s mirrored-screen negative control passed.**

- The four corner dots are symmetric under a left–right mirror, so a mirrored
  screen puts four dots where four dots were predicted.
- The fifth, asymmetric dot went missing, and a missing dot was simply not
  counted.
- Every dot the model puts in view must now be **found** as well as placed.

**Four sides from a level seat failed `--pyramid` at every azimuth**, by 4 to 6
times the tolerance. Three sides passed.

- From a level seat, the far arm of the screen is seen **directly through the
  pyramid**, squashed, across the lower part of the near face's ghost.
- That is real, and the figure shows it. But it sits under the bottom dots'
  windows.
- The pyramid's dots are now in the clip's upper part (`kHighTriangle`),
  clear of it. The direct view is documented, not hidden.

**The plugin clock's epoch is its first reading.**

- So a render at a fixed `--time` was always at elapsed 0, and the foil never
  moved in a still.
- `MixRig::Render` now primes the clock at 0 before the first real time.

**Prune depended on Opacity.**

- The smallest ray weight worth following was computed from Source Level ×
  Opacity, so dimming the fader changed which rays were traced.
- As a result, Opacity 0 was not bitwise the black-Src picture.
- It now comes from Source Level alone.

**`--mixer`'s Src marker was counted in the wrong place.**

- Its colour, (A0, FF, 00), is within half the marker-to-quadrant-0 distance
  of Src's quadrant 1 (B4, B4, 00).
- So quadrant 1 was counted as marker, and the centroid sat 100 px off.
- Each pixel is now classified by the nearest colour.

**Abelès' tolerance was first written against the absolute light bound.**

- The residual is a difference of R_p and R_s², both near 0.01–0.02, inverted
  from film values.
- Its float bound has to be propagated through that inversion, which is what
  `floatBound` does.

**`--double`'s step search crossed rows that miss the pane.**

- Bisecting over the whole column found a "step" where the frame ended.
- It now brackets on pixel centres inside the pane first.

**The software renderer is slow on a ray tracer.**

- `--ripple`'s 256 full frames per raster took over 20 minutes there.
- The Dest marker now sits beside the ghost dot, and each frame is scissored to
  the two measurement windows. Fragments inside are shaded exactly as before.
- 36 s.
- `verify.sh` still takes minutes, most of them `--fresnel` (bisection) and
  `--additive` on the software renderer.

**The thick coated pane was off by 2–7e-5, which looked like a model error.**

- It was the prune. A back image of aluminium-coated glass, seen from
  inside, falls near 1/8192 of the brightest light, and the plugin rightly
  stopped following it.
- `kPruneLight` moved to `Controls.h`, and the harness prunes each image as
  the plugin does: the image's weight, at its brightest channel and
  polarisation, against the prune.
- A pixel whose image is within 1e-4 of the prune either way is not
  predicted: float and double may fall on different sides of it.

**Titanium dioxide is not lossless.** The self-check that R + T = 1 for a
clear coat failed by 5e-5. Siefke's k is 5.4e-6 in blue, and over a quarter
wave that is real absorption. The bound is now 1e-4 with that reason, and
the picture compares R + T against the model's own absorption.

**The apex-up pyramid failed from azimuth ±90 and with three sides.** Two
harness errors, not the plugin's:

- Overhead, face j reflects arm (n − j) mod n, not arm j: the mirror is the
  other way up (`armUnder`).
- The far arm, seen directly through the pyramid, overlapped the upper dots
  as the lower ones overlap it apex down. Apex up, the dots are in the
  clip's lower part (`kLowTriangle`).
- From 15° below, the overhead screen is seen directly through the faces,
  across the ghost. The view is from 10° up instead.

**`kPepperFragment` outgrew two raw strings.** Each piece is under MSVC's
16 KB literal cap; there are now three, and `glslc.sh` joins any number.

**Azimuth looked dead to the sweep.** −180° and +180° are the same seat. The
sweep moves it from 0° to 90°. Coat Thickness is dead with no coat, so it is
swept with Coating = Aluminium.

**oxbow's selftest cannot drive a mixer.** It is an effect host, so it hands
the plugin one input. That makes it a useful assertion: the mixer must
**refuse** it (`FF_FAIL` on frame 0). The Feed is selftested normally.

---

## Shape of the code

| File | What |
|---|---|
| `source/Pepper.{h,cpp}` | the mixer: parameters, guards, the clock, `rig::Build`, uniforms |
| `source/Feed.{h,cpp}` | the feed effect |
| `source/MixerEntry.cpp`, `source/FeedEntry.cpp` | one registration each, compiled into their own bundle only |
| `source/Names.h` | ids and display names, shared with the harness |
| `source/Rig.{h,cpp}` | the geometry, in double: faces, screen, camera, feed arms, ripple modes |
| `source/Controls.{h,cpp}` | every host 0..1 ↔ physical mapping, the sheets' and coats' indices per channel, option names |
| `source/Coating.{h,cpp}` | the coat: one layer's Airy sum, in double, and the table the shader reads |
| `source/Shaders.{h,cpp}` | `kVersion`, `kVertex`, `kCommon` (sRGB, the feed lookup, fault bits), `kFeedFragment`, `kPepperFragment` (three adjacent raw strings — MSVC's 16 KB literal cap) |
| `source/Timing.{h,cpp}` | relay's host clock |
| `source/Diag.{h,cpp}` | the log, `~/Library/Logs/pepper/`, or `PEPPER_LOG_DIR` |
| `source/StoatworksAbout*.h` | the About block (generated by the backend's `sync-about.py`) |
| `tools/pgtest/` | the harness: `Harness.h` (rigs, cards, the optical model, measurement) and `main.cpp` (the checks) |
| `tools/verify.sh`, `glslc.sh`, `mutate.sh`, `sweep.py`, `figures.py` | see CLAUDE.md |

## Decisions taken without asking

- **Two plugins, a mixer and an effect**, for the reasons above. Plugin ids
  `PG01` and `PG02`, checked free across every `*/source` on 2026-11-02 and
  recorded in the session notes. Bundle ids `com.stoatworks.ffgl.pepper` and
  `…pepperfeed`.
- **Index 0 is `Linear Light`**, a boolean, on. Arena hides a mixer's first
  parameter, so index 0 holds a control whose default is right for ever. Off
  is the gamma-space add, for comparison with Resolume's Add.
- **The pane's screen is hidden from direct view.** A ray that was never
  reflected sees the stage, as a stage front hides the screen in every real
  rig.
- **The pyramid's screen is an object in the room.**
  - It occludes the stage behind it.
  - It is seen directly, bright, from any seat that can see it.
  - Source Level +2.5 stops makes the ghost read well and makes the directly
    seen screen clip. That is roughly a phone at full brightness, and it is
    what a real pyramid looks like from a seat a little above.
- **Polarisation is carried through every interaction** (above). The source
  is None, Horizontal, Vertical, 45 Degrees or 135 Degrees, relative to the
  screen's picture, counter-clockwise from its horizontal as the picture is
  seen face on.
  - For the pane, Vertical is p and vanishes at Brewster.
  - At 45° and 135° each pixel's s and p shares differ across the ghost, so
    each is lopsided (up to 0.169 at 640×360) and the two are mirror images.
  - In the pyramid, light reaching the eye through two faces has turned
    between two planes of incidence, and Horizontal and Vertical differ by
    0.18 to 0.51 there. v0.1.0's last-interaction approximation had that
    wrong; it is the negative control.
- **The sheets' indices** are dispersion fits, evaluated at each channel's
  wavelength. At the d line (587.6 nm) they are PET 1.65, float glass 1.523,
  acrylic 1.491 and polycarbonate 1.585. The sources are in `Controls.cpp`.
  At green's 549 nm, which the rays bend by, they are 1.655, 1.525, 1.493
  and 1.589.
- **Dispersion.** Fresnel, the coat and the tint's path use each channel's
  index. The rays bend by green's, so a thick sheet's back images land in one
  place for all three channels. Tracing each channel's rays would triple
  them. What that leaves out is measured, not argued: `--double` prints it.
  The red and blue back images would be **0.05 px** apart at 640×360 for 4 %
  float glass, 0.7 % of the image's 8 px offset. For polycarbonate and PET the
  ratio is 1.7 %. It scales with the raster: at 4K, full thickness, about
  0.3 px for glass and 0.8 px for polycarbonate. A film has no fringe: its
  images coincide. A real 12 mm plate under a 1.8 m ghost is a sixth of the
  full thickness.
- **Coatings** are one layer on the side the declared normal points to — the
  screen's side, the audience's — interfering (Airy), because a coat is thin
  next to the light's coherence length. The sheet under it stays incoherent.
  - **Aluminium**, 0 to 10 nm, from Rakić's bulk fit. 5 nm reflects about
    0.47 from the air, 0.39 from inside, and absorbs 0.14–0.20. A real
    evaporated film a few nm thick, with its oxide skin, differs. Treat the
    thickness as the control it is, not a recipe.
  - **Titanium dioxide**, 0 to 2 quarter waves at 549 nm (0.5 is one, 56.3 nm),
    from Siefke's ALD film. Clear, so it reflects the same from either side.
  - Musion-style foils are coated, and the coat is what makes the ghost bright
    enough: a bare PET film reflects 13 % at 45°.
- **The screen's falloff** is the generalised Lambertian: L ∝ cos^p off its
  normal, with p chosen so L halves at half the Viewing Angle. 180° is
  Lambertian (p = 0), the default. An LED tile is typically 120°–160°. A
  real panel's curve is not a power of cos; this is the one-number model.
- **Screen above** hangs the pane from the screen, or stands the pyramid apex
  up under it. Seen from the audience the picture is a mirror image of the
  Below case: the overhead screen's picture is laid out mirrored, top away
  from the audience, and the feed's arms point top inward. `--geometry`,
  `--pyramid` and `--feed` predict all of it in real space. The keystone
  inverts: at 40° the ghost is narrower at the top (0.9577), where Below it
  is wider (1.0442).
- **Ripple is a membrane**, rather than noise. Each face's bounding rectangle,
  fixed at its edges, rings in six modes: (1,1), (2,1), (1,2), (2,2), (3,1) and
  (1,3).
  - Amplitudes fall as 1/(m² + n²), with a hashed sign.
  - They are scaled so the sum of every mode's steepest slope is tan(Ripple).
  - Frequencies are in proportion to √((m/W)² + (n/L)²).
  - The pyramid's faces are trapezoids; the rectangle is an approximation there.
- **Spill** is a constant: the light a reflected ray finds when it misses the
  screen (the pit, the room).
- **The camera** is a 30° pinhole (about a 45 mm lens on full frame) orbiting
  the ghost's centre. The rig size is the ghost's height for the pane, and
  0.6 of the top's width for the pyramid.
- **The feed's arms are reflections of the clip**, not the common four rotated
  copies.
  - A face's mirror keeps left–right and turns the arm's outward direction into
    up.
  - So a rotated copy reads **mirrored** in a pyramid, and an arm drawn as a
    reflection reads the right way round.
  - The harness derives this independently (`feedPoint` and the face mirror),
    and the rotated layout is the negative control. Text in a rotated-copies
    feed should read backwards in a real pyramid. That is worth checking against
    one (Open questions).
- **Not modelled**, stated in the README, with why:
  - **Haze, scattering and dust.** Haze is light the sheet scatters forward
    and back into a wide cone. A fraction h of the light meeting the sheet
    (typically under 0.5 % for float glass, 0.5–3 % for PET film) becomes a veil, which is a
    blur of the screen and the stage over a large part of the frame. A
    per-pixel ray tracer cannot see its neighbours. It would need a blurred
    copy of each input: a mip chain or a separable blur in passes of its own.
    That is a second render target and a pipeline the plugin does not have.
    A host's own texture cannot be mipmapped in place, and a uniform veil from
    the mean picture would still need that mean. A follow-up, if wanted.
  - **Birefringent PET.** Drawn polyester is biaxial: about 1.64 to 1.67 in
    the plane, and its axes turn across a roll. A 12 µm film is then a
    waveplate of half a wave or more, varying with angle and position. That
    changes the polarisation of the back surface's light, and shows as
    coloured fringes only through a polariser. Modelling it needs the film's
    retardance and axis at every point, which nobody has for their foil. The
    in-plane index is used.
  - **A stage with depth**: Dest is a picture, not a 3D set.
- **CI runs only what needs no GL.** The macOS runner cannot make an
  accelerated context, so `ci.yml` runs `pgtest --list`, `--names` and glslc,
  and builds Windows. `verify.sh` is the release check, run locally.
  `tools/wincheck.ps1` (throw's, with a mixer kind and the input caps) loads
  each shipped DLL in the release workflow.
- **The Feed's `SetTimeSupported`** is left at the SDK default (true). It has no
  clock and ignores the time.

## What is genuinely verified, and what is assumed

**Verified by measurement**, on an Apple M4 Max, macOS 26.4.1, 2026-11-02,
at both rasters, on GPU and software, every check with its negative control.
These are the README's Status table, from `pgtest`, `tools/mutate.sh`,
`tools/sweep.py` and `tools/verify.sh`:

- the bundles' host metadata through `oxbow probe`;
- universality through `lipo`;
- ad-hoc signing;
- the Feed rendering in oxbow's selftest.

**Assumed, not measured here:**

- **Everything about Resolume.** That Arena:
  - loads both bundles from Extra Effects;
  - offers SW Pepper as a Blend Mode;
  - hides Linear Light;
  - drives Opacity from the fader.

  These are the fleet's measurements on genlock, wipe, relay and lenticular,
  not on this plugin.
- **The optics, as stated.** The harness proves that the plugin computes the
  model correctly, not that the model is the world:
  - the indices;
  - an isotropic sheet;
  - a coat as one uniform layer with bulk or ALD-film indices;
  - Fresnel for a smooth surface;
  - the membrane modes;
  - a screen's falloff as a power of cos.
- That an LCD's output is polarised along one of four directions in its
  picture: horizontal, vertical, 45° or 135°. Those cover the common panels;
  another angle cannot be expressed.
- That the generated cards stand for real footage. The figures are the
  harness's cards, not Resolume's demo media.

## Open questions

- **Does a rotated-copies feed read mirrored in a real pyramid?** The geometry
  says yes, and SW Pepper Feed draws reflections for that reason. A phone and a
  pyramid with some text in the clip would settle it in a minute.
- **What is in a real foil?** Coating now offers aluminium and titanium
  dioxide, from published indices. Does a real Musion-style foil reflect what
  either gives at some thickness? A measured reflectance at 45° would pin it.
- **Haze**: worth the second pass it would take (Not modelled, above)?

## Notes

- Bundle ids `com.stoatworks.ffgl.pepper` and `com.stoatworks.ffgl.pepperfeed`.
  Plugin ids `PG01` and `PG02`. Display names `SW Pepper` and `SW Pepper Feed`.
- **Generated.** `source/StoatworksAbout.h` and `ATTRIBUTIONS.md` are written
  by the backend's `sync-about.py` and `sync-attributions.py` (Pepper is in
  their lists since the release). Never hand-edit them: change the website's
  projects.json or the backend's `attributions/derived.json` and re-sync.
