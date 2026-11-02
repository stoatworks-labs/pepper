# Pepper

> **AI-assisted project.** This codebase was created with [Claude](https://claude.com/claude-code)
> (Anthropic), directed and reviewed by a human author. The optics are not
> asserted but measured. An offline harness drives the real plugin classes in a
> headless GL context and reads each claim back out of the picture:
>
> - a black source adds **exactly nothing**, to the bit;
> - a clear sheet reflects what it does not transmit, R + T = 1, to **3e-7**;
> - a polarised source vanishes at **Brewster's angle**, its null landing
>   **0.001 px** from where atan n puts it;
> - at 45° the p reflection is the s reflection squared (Abelès) to **6e-8**;
> - a thick pane's second image lands **0.02 px** from Snell's 2 t tan θt;
> - the foil's wobble peaks at **six of six** membrane mode frequencies,
>   measured by FFT.
>
> See [Status](#status). It has **never been loaded into Resolume**, on any
> platform. Check it in your own rig before trusting it in a show.

Pepper's ghost — a sheet of glass or foil reflecting a hidden screen over the
stage behind it — as an FFGL **mixer** for [Resolume](https://resolume.com)
Arena and Avenue, plus an **effect** that lays out a clip for a real pyramid
hologram. The same optics cover the Victorian pane, a Musion-style foil and the
plastic pyramid on a phone.

![A ghost on a foil at 45 degrees, over a lit stage](docs/hero.png)

<sub>The repo's two generated cards through SW Pepper at its defaults: a PET foil
at 45°, the figure card on the hidden floor screen at +2.5 stops, the stage card
behind. Rendered by `pgtest`, the offline harness, not captured from Resolume.
The ghost only adds light: the orange beam behind the figure's legs comes
through it, and the stage is dimmed to about 0.87 by the foil even where there
is no ghost.</sub>

| | id | display name | bundle | type |
| --- | --- | --- | --- | --- |
| the pane, the foil and the pyramid, seen | `PG01` | `SW Pepper` | `Pepper.bundle`, `com.stoatworks.ffgl.pepper` | **mixer** |
| the cross layout a real pyramid is fed | `PG02` | `SW Pepper Feed` | `PepperFeed.bundle`, `com.stoatworks.ffgl.pepperfeed` | effect |

## Pepper's ghost is one sheet of glass

A clear sheet at an angle splits every ray into a reflected part and a
transmitted part, in the proportions Fresnel gives for that angle. Put a
brightly lit picture where the reflected rays land, keep it out of direct
view, and the eye sees that picture floating behind the sheet, over whatever
the transmitted rays reach. That is the whole trick, from the Royal Polytechnic
in 1862 to a foil on an arena stage. SW Pepper traces exactly that, per pixel,
in linear light, and these fall out of it:

- **The ghost is additive.** Black in the source contributes nothing and
  disappears; nothing can be darker than the stage behind the sheet; a ghost
  over a white wall cannot be seen. An alpha-over would let a dark source darken
  the stage, and that is one of the harness's negative controls.
- **The stage is dimmed by the sheet**, (1 − R)² τ / (1 − R² τ²) for its two
  surfaces — about 0.87 for PET at 45° — whether there is a ghost or not.
  Fading the source out leaves the glass in place, as dimming the lamp on the
  Victorian actor did.
- **Reflectance depends on the angle and the polarisation.** At 45°, one
  surface of PET reflects 12.7 % of s-polarised light and 1.6 % of p. An LCD
  whose light is polarised in the plane of incidence **vanishes at Brewster's
  angle**, which is why an LCD laid on the floor for a ghost can show nothing.
  LED walls and projection are unpolarised.
- **A thick pane gives a double image**, and a fainter third: the back surface's
  reflection comes out parallel to the front's, displaced along the pane by
  2 t tan θt. This is why a foil is used instead of glass: at thickness 0 the
  images coincide.
- **A tinted sheet** tints the stage and the back-surface images, which cross it
  twice, but not the front-surface image.
- **A foil ripples.** A tensioned film rings in its frame at the membrane's mode
  frequencies, and a tilt of δ turns the reflected ray by 2δ. So **the ghost
  swims and the stage behind does not**: a thin film does not deviate the light
  passing through it.
- **Keystone.** The ghost is the screen's mirror image in the sheet's plane. It
  stands upright at 45°, and at any other angle a level audience sees it as a
  keystone.
- **The pyramid** is the same sheet three or four times over a phone. Seen from
  one side, the near face shows its arm of the screen floating inside. At a
  corner the image splits across two faces. From above you see the screen itself.

![PET foil against thick, tinted float glass](docs/glass.png)

<sub>Left: a PET foil, thickness 0, one image. Right: 4 % float glass tinted
(0.8, 0.95, 0.85). The back surface's image stands a little higher than the
front's — 2 t tan θt further along the pane — and it is tinted twice. Ripple
off in both.</sub>

![The sheet at 38, 45 and 55 degrees](docs/angles.png)

<sub>The generated graphic card in a sheet at 38°, 45° and 55° from a level seat.
The ghost is wider at the top at 38°, a rectangle at 45° and narrower at the top
at 55°. `--geometry` measures the ratio at 40° and 50°: 1.0442 and 0.9605, as
the mirror predicts.</sub>

![Brewster's angle, with an unpolarised, a p- and an s-polarised source](docs/brewster.png)

<sub>A white screen in a PET sheet at 33°, so the level ray meets it at 57°, near
Brewster's 58.8°. Left: unpolarised. Middle: polarised along the picture's
vertical, which is p for this rig — the ghost vanishes where the incidence is
Brewster's and the stage shows through. Right: along the horizontal, s, which
does not vanish.</sub>

![The pyramid from four seats](docs/pyramid.png)

<sub>`Rig = Pyramid`, four acrylic faces at 45° over a phone showing the feed of
the figure card. Clockwise from top left: square on to a face from 10° up;
on a corner, where the figure is in two faces at once; from 60° up, where the
screen's own feed is seen directly through the faces; three sides. The bright
figures lying flat are the phone's screen seen directly: it is roughly ten
times brighter than its reflection, as it is in life.</sub>

![SW Pepper Feed, four and three sides](docs/feed.png)

<sub>SW Pepper Feed on the figure card. Each arm holds the clip top outward as a
**mirror image**, because each face of the pyramid mirrors it back the right way
round. The common layout of four rotated copies reads mirrored in a pyramid,
and is the harness's negative control.</sub>

## Effect or mixer

**The rig is a mixer and the feed is an effect.** They are two bundles built from one core.

- The pane needs two pictures that take different light paths: the stage is
  **dimmed** by T, and the screen is **added** at R, per pixel, in linear light.
  An effect has one input, and no host blend mode does both. An alpha layer
  would need alpha 1 − T and colour R·L / (1 − T), which is the screen's own
  light L. That is over white whenever the screen is brighter than the stage,
  as it must be for a ghost to show. On top of that, a host blends in gamma
  space, with one alpha for three channels, where a tinted sheet dims each
  channel differently. So **SW Pepper is the hologram layer's Blend Mode**. The layer below
  is the stage (Dest) and this layer is the hidden screen (Src). This is how an
  operator already composites a ghost, usually with Add, which is this with
  T = 1 and R = 1, in gamma space.
- Resolume binds a mixer parameter named `Opacity` to the layer's opacity fader
  (measured on genlock, wipe, relay and lenticular). Here **Opacity is the
  source's light**: fade the layer up and the ghost appears; fade it down and
  the glass is still there.
- The pyramid **viewer** is the same mixer under `Rig = Pyramid`: the phone's
  picture (Src) and the room behind it (Dest).
- The pyramid **feed** has one input. It must work on a clip, a layer or the
  whole composition, which is how a dedicated pyramid output is fed, and a mixer
  can do none of those. So it is an effect, built from the same layout GLSL the
  mixer uses under `Screen = Feed`. **The feed you send to a real pyramid is the
  one the simulator looked into.**

## The controls

**SW Pepper**

- **Light**
  - **Linear Light** (on; off adds in gamma space, for comparison with Resolume's Add)
  - **Opacity** (the source's light, on the layer fader)
  - Source Level (−3 to +6 stops, default +2.5)
  - Source Black (the screen's own black, up to 5 % of white)
  - Polarisation (None, Horizontal, Vertical, relative to the screen's picture)
  - Stage Level (−4 to +2 stops)
  - Spill (what the sheet reflects besides the screen — the pit, the room — up to 50 %)

  Linear Light is first on purpose: Arena does not show a mixer's first
  parameter, so index 0 holds a control whose default is right if nobody can
  ever reach it.
- **Rig**: Rig (Pane, Pyramid), Sheet Angle (25° to 65°, 45°), Sides (Four,
  Three), Centre (the pyramid's small end, 0.1 to 0.6 of the screen's short
  side), Screen (Feed lays Src out as SW Pepper Feed would; As Is takes Src as
  an already laid-out feed).
- **Sheet**: Sheet (PET Foil 1.65, Float Glass 1.52, Acrylic 1.49,
  Polycarbonate 1.585), Thickness (0 to 4 % of the rig; 0 is a film), Tint (the
  sheet's colour face-on), Ripple (the foil's steepest slope, 0° to 3°, 0.2°),
  Ripple Speed (the frame's fundamental, 0.05 to 4 Hz).
- **View**: Distance (1 to 12 rig sizes: the ghost's height for the pane, 0.6
  of the top's width for the pyramid), Elevation (−20° to 60°), Azimuth
  (−180° to 180°). The camera is a 30° lens orbiting the ghost. The stage (Dest)
  is what that camera sees behind the glass, so the view moves the glass and the
  ghost, not the stage.

**SW Pepper Feed**: Sides and Centre, with the same mappings as the mixer's.

## Build

Needs CMake and the Resolume FFGL SDK, which is a submodule.

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
cmake --install build    # → ~/Documents/Resolume Arena/Extra Effects
```

macOS builds universal (arm64 + x86_64) by default. Add
`-DCMAKE_OSX_ARCHITECTURES=arm64` for a faster development build. Both bundles
install to **Extra Effects**: Resolume has one FFGL folder, and mixers load
from it too (measured on genlock, wipe and relay).

## Building and testing

The harness renders the real plugin classes headlessly. The mixer gets two
inputs: `--input-a` is Dest, the stage; `--input-b` is Src, the hidden screen.
They may be different sizes, with different hardware padding, rendered to a
third size.

    ./build/pgtest --out /tmp/f.png        a picture through SW Pepper (--plugin feed for the feed)
    ./build/pgtest --list                  every parameter, kind and default
    ./build/pgtest --names                 names, ids, parameter 0, Opacity
    ./build/pgtest --mixer                 two inputs, two MaxUVs, and the guards
    ./build/pgtest --additive              the ghost only adds light, in linear light
    ./build/pgtest --energy                a clear sheet: R + T = 1, per pixel, every sheet
    ./build/pgtest --fresnel               Brewster's null and Abelès' identity, out of the picture
    ./build/pgtest --double                a thick pane's images: their weights and where they land
    ./build/pgtest --geometry              dots land where the mirror puts them; the keystone
    ./build/pgtest --ripple                the foil rings at its frame's mode frequencies, by FFT
    ./build/pgtest --opacity               Opacity is the source's light; at 0 the glass stays
    ./build/pgtest --pyramid               each face shows the clip the right way round
    ./build/pgtest --feed                  the feed's arms are mirror images, where stated
    ./build/pgtest --mutation              one character of the shipped GLSL fails --double
    ./build/pgtest --bench                 720p through 4K, pane and pyramid
    ./build/pgtest --pipe                  raw RGBA frames in, frames out (filming, not a check)
    python3 tools/sweep.py                 no control of either plugin is silently dead
    tools/mutate.sh                        thirteen one-character mutants, each caught
    python3 tools/figures.py               the README's pictures, remade
    tools/verify.sh                        all of it, on a fresh universal build

Every numeric check runs at **640×360 and 320×180**, on this Mac's GPU **and**
on Apple's software renderer. Every check carries a **negative control**: a
fault switch in the shipped plugin that builds the wrong answer into the real
shader, and that the check must reject. Every tolerance is derived from
something stated rather than from the number this machine printed first.
[AGENTS.md](AGENTS.md) lists every number and where it comes from.

## Status

**v0.1.0, local only, not released.** Verified by measurement on an Apple M4
Max, macOS 26.4.1, 2026-11-02, at 640×360 **and** 320×180 unless stated, on
the GPU and on Apple's software renderer:

| Check | Result |
| --- | --- |
| Two inputs, two sizes, two MaxUVs | Dest 200×120 of 256×256, Src 96×70 of 128×128, out 320×200 and 320×180: **0 padding pixels** reached the picture through the glass or in it, every quadrant within **1.3e-7** of flat, both markers where predicted |
| The guards | a null input array, zero inputs, one input, a null Dest, a null Src and a zero-sized Dest return `FF_FAIL`; so do the feed's null and missing input |
| Additive | a black Src adds **0 bytes**; under it the stage is dimmed by exactly the film's T at each pixel's incidence (**2.7e-7**); no pixel is darker than that for any Src; L(stage, screen) = L(stage, 0) + L(0, screen) to **2.5e-7**; the stage alone is T × Stage Level × its sRGB-decoded light to **2e-7** |
| Energy | R + T = 1 at every pixel for all four sheets, worst **3.1e-7**; a tinted sheet falls short by exactly its absorption |
| Fresnel | the p ghost's null at row **180.001** against Brewster's 180.000 for all four sheets, p/s **3e-6** there; at 45° R_p = R_s² to **6e-8** (PET: R_s 0.12714, R_p 0.016164); s rises monotonically with incidence |
| Thick pane | W1 = (1 − W0)² W0 τ² to **1.5e-7** relative and W2 = (1 − W0)² W0³ τ⁴ to **4.5e-5**; the second image's edge at row **187.98**, Snell predicts 187.96 |
| Geometry | six views (sheet 40° to 55°, elevation to 15°, azimuth to 25°): every dot within **0.05** of its tolerance of the mirror image; at 45° the ghost is a rectangle (1.0000), at 40° and 50° a keystone of **1.0442 and 0.9605** against 1.0441 and 0.9605 |
| Ripple | a Dest marker through the rippling foil moves **0.0002 px**, the ghost of a dot 7.7 px; the dot's track peaks at **6 of 6** mode frequencies and nowhere else above 5 % |
| Opacity | Opacity 0 is the black-Src picture to the bit, 8-bit and float; the ghost's light is linear in Opacity to **2.3e-7** |
| Pyramid | four sides from azimuth 0, 90, 180, −90 and three from 0, 120, −120: the near face shows the clip the right way round, its dots within **0.06** of tolerance (0.30 at 320×180); from 60° up the screen's 12 dots are where they are, each arm mirrored |
| Feed | every dot within **0.19 px** of its predicted place (tolerance 0.375), every arm a mirror image (determinant −1), black outside the arms, alpha 255 everywhere; the mixer's own pyramid screen shows the feed's dots where the feed drew them |
| Negative controls | every one fails its check: folded MaxUV, alpha-over, gamma-space add, one surface, r_p with cosines swapped, s and p swapped, one pass through the thick pane, the tint once, a fixed 45° mirror, a mirrored screen, locked modes, Opacity as a crossfade, rotated arms |
| Mutation | the shipped `--mutation` (the tint's path from the incidence, not the refraction) fails 3 `--double` assertions; `tools/mutate.sh` catches **13 of 13** one-character mutants across the GLSL and the C++ |
| No dead controls | all **22** of SW Pepper's controls and both of the feed's change the picture at 480×270; the rest are the About block |
| Host metadata | `oxbow probe` reads **SW Pepper / PG01 / mixer / inputs 2..2**, parameter 0 **Linear Light**, and **SW Pepper Feed / PG02 / effect / 1..1**; oxbow's selftest renders the feed and is refused by the mixer (one input) |
| Render cost | one 30-frame run at shipped defaults (the PET foil, rippling), `glFinish` both sides, on a shared machine: the pane **0.08 / 0.15 / 0.20 ms** a frame at 720p / 1080p / 4K, the pyramid **0.10 / 0.18 / 0.21 ms** — 1.3 % of a 60 fps frame at most. Not asserted; another GPU will differ |
| macOS binary | universal (`x86_64 arm64`), both bundles export `plugMain` and ad-hoc sign |

Run `tools/verify.sh` before believing any of it.

**What is not done.**

- Pepper has **never been loaded into Resolume**, on any platform. Nothing here
  shows Arena accepting it, hiding Linear Light, or driving Opacity from the
  fader on this plugin. Those are the fleet's measurements on other mixers.
- It is not released: there is no GitHub repository, no download and no
  Windows build.
- The harness has run on two rasterisers, this Mac's GPU and Apple's software
  renderer. No other has run it.

It also leaves out, on purpose:

- dispersion, so there is no colour fringe from the index varying with
  wavelength;
- scattering, haze and dust on the foil;
- the screen's own angular falloff;
- display-case rigs with the screen above, and pyramids standing apex up.

[AGENTS.md](AGENTS.md) lists everything assumed rather than measured, and the
decisions taken without asking.

<!-- attributions:start -->
This project is built on other people's work — see [ATTRIBUTIONS.md](ATTRIBUTIONS.md).
<!-- attributions:end -->

## Licence

MIT — see [LICENSE](LICENSE).
