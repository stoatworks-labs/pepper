#pragma once

#include <string>

/**
    The GLSL, in pieces, and how each pass is assembled from them.

    A fragment shader is `kVersion + kCommon + its body`; the vertex shader is
    `kVersion + kVertex`. `kCommon` holds the two things both plugins must do
    identically: sRGB to light and back, and the feed's arm lookup. SW Pepper
    Feed draws the layout with it, and SW Pepper's pyramid screen shows the
    same layout through it under Screen = Feed, so the feed sent to a real
    pyramid is the feed the simulator looks into, character for character.

    `tools/glslc.sh` assembles the same way (its ASSEMBLED table) and
    compiles every pass through glslc.
*/
namespace pepper
{

extern const char* const kVersion;
extern const char* const kVertex;
extern const char* const kCommon;
extern const char* const kPepperFragment;
extern const char* const kFeedFragment;

/// kVersion + kCommon + body, as the plugins compile it.
std::string AssembleFragment( const char* body );
std::string AssembleVertex();

/// The negative controls' bitmask, shared with the CPU side. The shipped
/// plugins always carry 0; only the harness sets any of these. Each builds
/// the wrong answer to one check into the real shader or the real
/// ProcessOpenGL, so the check is shown to reject the plugin's own wrong
/// version rather than an imitation of it.
enum Fault : int
{
	kFaultNone         = 0,
	kFaultFoldedMaxUV  = 1 << 0, ///< Dest's MaxUV folded into the vertex shader (the genlock trap) -- --mixer
	kFaultAlphaOver    = 1 << 1, ///< the ghost laid OVER the stage, occluding it -- --additive
	kFaultOneSurface   = 1 << 2, ///< transmission through one surface, 1 - R -- --energy
	kFaultRpCosSwap    = 1 << 3, ///< r_p with cos(i) and cos(t) swapped: no Brewster null -- --fresnel
	kFaultSwapSP       = 1 << 4, ///< s and p swapped -- --fresnel
	kFaultOnePass      = 1 << 5, ///< the back image traced through half the sheet -- --double
	kFaultTintOnce     = 1 << 6, ///< a back image tinted once, not twice -- --double
	kFaultFixed45      = 1 << 7, ///< the reflection about a 45-degree pane whatever Sheet Angle says -- --geometry
	kFaultMirrored     = 1 << 8, ///< the screen's picture mirrored left to right -- --geometry
	kFaultModesLocked  = 1 << 9, ///< every ripple mode at the fundamental (CPU) -- --ripple
	kFaultCrossfade    = 1 << 10,///< Opacity as a crossfade to the bare stage -- --opacity
	kFaultRotatedArms  = 1 << 11,///< the feed's arms rotated copies, not reflections -- --pyramid, --feed
};

} // namespace pepper
