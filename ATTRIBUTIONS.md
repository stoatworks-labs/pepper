# Attributions

Pepper is built on other people's work. This file lists what that work is, who did
it, and what it is doing here.

It is normally generated — the master lists live in the `stoatworks-backend` repo and
are pushed out by `scripts/sync-attributions.py`. Pepper is not in those lists yet,
so this is a provisional hand copy in the same shape; the first sync replaces it.

## Code we derived from other people's work

Someone else solved this first, and this project would not exist in its current form without their work.

### Mixer mechanics, two-input harness and pipe — Stoatworks genlock, wipe, relay and lenticular

<https://github.com/stoatworks-labs/lenticular>  
Licence: MIT  
Copyright: Stoatworks Labs

The mixer mechanics of ProcessOpenGL are genlock's, wipe's, relay's and lenticular's, adapted:

- the guard on the input count;
- the guard on each pointer;
- one MaxUV per input;
- interleaved scoped bindings.

So are the two-input harness rig, its --mixer check, the two-input --pipe with its cue sheet, the software-renderer pass and the CMake shape.

### Host clock — Stoatworks relay

<https://github.com/stoatworks-labs/relay>  
Licence: MIT  
Copyright: Stoatworks Labs

source/Timing.* is relay's clock: the host's unit settled by voting, the epoch at the first reading.

### Two bundles from one core — Stoatworks vectrix

<https://github.com/stoatworks-labs/vectrix>  
Licence: MIT  
Copyright: Stoatworks Labs

An OBJECT core library with each plugin's registration in its own entry file, compiled only into its own bundle.

### Mutation script, glslc extraction and reserved-word check — Stoatworks knockout

<https://github.com/stoatworks-labs/knockout>  
Licence: MIT  
Copyright: Stoatworks Labs

These are knockout's, adapted:

- tools/mutate.sh (one copy, one build, one recompiled file per mutant);
- tools/glslc.sh (assembled shaders through glslc);
- the GLSL reserved-word check in tools/verify.sh.

### Diagnostics log — Stoatworks tinsel

<https://github.com/stoatworks-labs/tinsel>  
Licence: MIT  
Copyright: Stoatworks Labs

source/Diag.* is tinsel's log, by way of relay and lenticular, renamed into this namespace.

### Sweep and verify shape — Stoatworks graticule

<https://github.com/stoatworks-labs/graticule>  
Licence: MIT  
Copyright: Stoatworks Labs

tools/sweep.py and the release-job-locally checks in tools/verify.sh follow genlock's, wipe's, relay's, lenticular's and graticule's.

## Third-party code this project uses

Libraries, SDKs and frameworks the project is built on or bundles.

### Resolume FFGL SDK

<https://github.com/resolume/ffgl>  
Licence: BSD-3-Clause  
Copyright: FreeFrame

Vendored as a git submodule at external/ffgl.

The plugin ABI itself. An FFGL effect or source is defined by this SDK's headers — there is no other way to be loadable by Resolume Arena and Avenue.

### GLEW — the OpenGL Extension Wrangler Library

<https://github.com/nigels-com/glew>  
Licence: BSD-3-Clause (with Mesa 3-D and Khronos components)  
Copyright: Milan Ikits, Marcelo E. Magallon and Lev Povalahev

Arrives inside the FFGL submodule at external/ffgl/deps/glew-2.1.0. Not fetched separately.

Resolves OpenGL entry points on Windows, where the system headers stop at OpenGL 1.1.

### libpng

<http://www.libpng.org/pub/png/libpng.html>  
Licence: PNG Reference Library License (libpng)  
Copyright: the PNG Reference Library authors

Arrives inside the FFGL submodule, under the SDK's CustomThumbnail sample.

Part of the upstream SDK tree rather than something these plugins call directly — listed because it is present in the checkout.

## Inspirations

What this set out to be. No code, assets or binaries from any of these were used or examined — the debt is to the idea.

### Pepper's ghost

Implemented from textbook optics:

- Fresnel's equations for a smooth dielectric surface, and Snell's law;
- the multiple-reflection series of a slab;
- Brewster's angle;
- Abelès' identity R_p = R_s² at 45°;
- the mirror image in a plane;
- the vibration modes of a tensioned rectangular membrane;
- the IEC 61966-2-1 sRGB transfer function.

The rigs it models are the general shape of the Victorian stage illusion (Dircks and Pepper, 1862), the angled-foil stage systems that followed it, and the four-sided pyramid toys used with phones. No particular product, foil, rig drawing, app or video was used, and no reflectance of any commercial foil is claimed.

## Getting this wrong

If your work is here and the description is inaccurate, the licence is wrong, or you would rather not be listed — open an issue and it will be fixed.
