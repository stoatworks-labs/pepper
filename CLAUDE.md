# pepper

Pepper's ghost for Resolume Arena/Avenue: a clear sheet at an angle reflecting
a hidden screen over the stage behind it, traced per pixel in linear light. It
ships as two FFGL plugins from one core, written in C++/GLSL, built with CMake
as MODULEs into two universal `.bundle`s (macOS). MIT. **Local only,
v0.1.0:** no GitHub repository, no release, **never loaded into Resolume**.

| | id | display name | bundle | type |
| --- | --- | --- | --- | --- |
| the pane, the foil and the pyramid, seen | `PG01` | `SW Pepper` | `Pepper.bundle`, `com.stoatworks.ffgl.pepper` | **mixer** |
| the cross layout a real pyramid is fed | `PG02` | `SW Pepper Feed` | `PepperFeed.bundle`, `com.stoatworks.ffgl.pepperfeed` | effect |

Read `AGENTS.md` before changing the optics, the harness's model or any
tolerance. Read `~/Projects/resolume/genlock/AGENTS.md`,
`~/Projects/resolume/relay/AGENTS.md` ("What Relay showed in Arena") and
lenticular's before touching `ProcessOpenGL`. The spec is
`~/Projects/resolume/specs/SPEC-pepper.md`.

## Commands (CMake)
- Configure: `cmake -B build -DCMAKE_BUILD_TYPE=Release`
- Fast dev build: add `-DCMAKE_OSX_ARCHITECTURES=arm64`
- Build: `cmake --build build -j8`
- Install into Arena: `cmake --install build`, which installs both bundles to
  `~/Documents/Resolume Arena/Extra Effects`. Mixers load from there too. Never
  run this from a shared session.
- Render a frame offline: `./build/pgtest --out /tmp/f.png --size 1280x720`
  - `--plugin feed` renders the feed.
  - `--time 1.25` is the foil mid-ripple.
- Choose the two inputs: `--input-a stage --input-b figure`.
  - a is **Dest**, the stage behind the glass.
  - b is **Src**, the hidden screen (and the feed's input).
  - The other cards are `video`, `graphic`, `quads-a`, `quads-b`, `bars`,
    `dots`, `black`, `white` and `flat`.
- Set anything by name, in host units (0..1) or by an option's element name:
  `--set "Rig=Pyramid" --set "Sheet=Float Glass" --set "Thickness=1"`.
  Unparsable values are refused.
- List parameters: `./build/pgtest --list` (`--plugin feed` for the feed's).
- Film through it:

  ```
  … | ./build/pgtest --pipe --size WxH [--pipe-src FILE_OR_FIFO] [--src-size WxH] [--fps N] [--script cues.txt] | …
  ```

  - stdin is Dest; `--pipe-src` is Src (without it, `--input-b` is held).
  - The clock runs at index / fps.
  - Cues are `frame Name value`. Standard parameters ramp; booleans and
    options step.
  - SIGPIPE is ignored: a closed stdout is exit 1.
- Remake the README's figures: `python3 tools/figures.py` (into `docs/`).

## Verify
- Everything: `tools/verify.sh`. It does a fresh universal build and runs every
  check below on the GPU **and** the software renderer, then the sweep, the
  pipe, `nm`, `lipo`, the plists, ad-hoc signing, oxbow and the source greps.
  - It takes minutes, most of them the software renderer.
  - `OXBOW=` points at an oxbow binary when
    `~/Projects/resolume/oxbow/build/oxbow` isn't built. Build one out of
    source, into a scratch dir.
- Names, ids, index 0 is Linear Light, the source's light is Opacity:
  `./build/pgtest --names`
- Two inputs, two sizes, two MaxUVs, the guards, no texture unit left bound:
  `--mixer`
- The ghost only adds light; the stage alone is T × Stage Level × its decoded
  light: `--additive`
- R + T = 1 per pixel, every sheet; a tint loses exactly its absorption:
  `--energy`
- Brewster's null where atan n puts it, each channel at its own index;
  R_p = R_s² at 45°; a source polarised at 45° and 135°: `--fresnel`
- A thick pane's images, weights and positions (Snell), and the colour fringe
  dispersion would add: `--double`
- A coat's interference against an independent thin-film model, from either
  side: `--coating`
- The screen's angular falloff: `--falloff`
- A polarised source through every face of the pyramid, against a 3D trace:
  `--polarised`
- Dots where the mirror puts them, and the keystone, screen below and above:
  `--geometry`
- The foil rings at its frame's mode frequencies, and the stage does not move:
  `--ripple`
- Opacity 0 is the black-Src picture to the bit; the ghost is linear in it:
  `--opacity`
- Each pyramid face shows the clip the right way round, apex down and apex
  up; from above, the feed: `--pyramid`
- The feed's arms are mirror images, where stated; the mixer agrees, below and
  above: `--feed`
- One character of the shipped GLSL fails `--double`: `--mutation`
- 22 one-character mutants of the GLSL and the C++, each caught:
  `tools/mutate.sh`
- No dead control in either plugin: `python3 tools/sweep.py`
- The shaders through glslc: `tools/glslc.sh`
- ms/frame: `./build/pgtest --bench`. Never loop it at 4K on this shared
  machine.
- `PGTEST_RENDERER=software ./build/pgtest --ripple` runs any check on Apple's
  software renderer.
- The harness writes the plugin's log. Point `PEPPER_LOG_DIR` elsewhere before
  running it around an Arena session (verify.sh does).

Every numeric check runs at 640x360 and 320x180 and carries its own negative
control (`SetFaultForTest`).

## Notes
- **SW Pepper is an `FF_MIXER`; SW Pepper Feed is an `FF_EFFECT`.** The type is
  the eighth argument of `CFFGLPluginInfo`, in `source/MixerEntry.cpp` and
  `source/FeedEntry.cpp`.
  - Each entry file is compiled into **its own bundle only**. `pepper_core` is
    an OBJECT library without them (vectrix's pattern). A registration in the
    core puts both plugins in both bundles.
  - `SetMinInputs`/`SetMaxInputs` are a separate declaration from the type.
- **`inputTextures[0]` is Dest (the stage), `[1]` is Src (the hidden screen).**
  - Each input has its own `MaxUV` and half-texel inset, applied once, in its
    own fetch.
  - The vertex shader passes UV through unscaled — never fold MaxUV into it.
  - The scoped bindings are declared interleaved.
- **Every position comes from `floor( uv * OutSize )`**, never from the
  interpolated `uv`.
- **Opacity is the source's light, and the name is load-bearing**: Arena binds
  a mixer parameter named Opacity to the layer fader.
  - Prune (the smallest ray worth tracing) comes from Source Level, **not**
    Source Level × Opacity. Otherwise Opacity 0 would not be the black-Src
    picture bit for bit.
- **Parameter 0 is sacrificial**: Arena hides a mixer's first parameter. It is
  `Linear Light`, on.
- **Dest is sampled once, at the pixel's centre.** A thin sheet does not deviate
  what is seen through it, so the view moves the glass, never the stage.
- **No time reaches the GPU.** Ripple phases are reduced in double on the CPU
  (`rig::Build`). The clock is relay's `Timing.h`: Arena's `SetTime` is in ms,
  and the unit is settled by voting.
- **A coat is a table on texture unit 2**, built in double by `Coating.cpp`.
  It is bound by hand around the draw and unbound after, because the scoped
  bindings only clear units 0 and 1. `--mixer` asserts every unit is clean.
- **Screen above is the Below rig seen in a mirror** (`rig::Build` reflects the
  camera). The harness predicts it in real space instead, never through the
  reflection.
- **Prune is `kPruneLight` (`Controls.h`) over the brightest light**, and the
  harness prunes each image the same way. A pixel within 1e-4 of the prune is
  not predicted.
- **Predictions are made from what the harness ASKED** (`asked()`, through
  `Controls.h`), never from `SetupForTest()`. The harness's optical model in
  `Harness.h` is its own, written from the spec, not from `Rig.cpp`.
- `SetParamInfo` clamps a STANDARD default into 0..1, so every ranged
  parameter is 0..1 and the mappings live in `Controls.cpp`.
  - An option's element value is its index; use `OptionIndex`.
- Override `SetTextParameter` to return `FF_SUCCESS` for the About block, or no
  host can instantiate the plugin.
- macOS build must be universal. Verify with `lipo`, never the build log.
- `kPepperFragment` is three adjacent raw strings, because MSVC caps one literal
  near 16 KB. `tools/glslc.sh` joins them as the compiler does.
- GLSL reserved words to avoid: `patch sample input output filter common active
  half layout flat packed smooth round`, `noise1`–`noise4`, and the rest of GLSL
  4.10's list. `verify.sh` greps declarations. MSVC: no `M_PI` (use `kPi`), and
  never `near`/`far`.
- **Provisional, normally generated:** `source/StoatworksAbout.h` and
  `ATTRIBUTIONS.md` are hand copies in the shape the backend's sync scripts
  write. They get overwritten once Pepper is in the backend's lists.

## Not done yet
- Never loaded into Resolume, on either platform. No Windows build. No GitHub
  repository, release, website page, demo or video. **Releasing needs Allan's
  go-ahead.**
- No CI, on purpose: a new repo is private, and private repos get no workflows.
- Not modelled (AGENTS.md says why):
  - haze, scattering and dust: it needs a blurred copy of each input, a pass
    the plugin does not have;
  - birefringent PET;
  - per-channel ray bending: a thick sheet's red and blue back images land
    together. The fringe this leaves out is printed by `--double`.

## Diagnostics

`source/Diag.{h,cpp}` is a log file only, with no crash handler (this runs
inside Resolume). It records:

- the host;
- the GL renderer;
- the first frame's input sizes;
- each guard, once;
- the first 400 changes of Opacity, which show what drove it.

    ~/Library/Logs/pepper/pepper.YYYY-MM-DD.log
