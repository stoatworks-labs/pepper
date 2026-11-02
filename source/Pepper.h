#pragma once

#include <FFGLSDK.h>

//AFTER the SDK: this header names FFUInt32 and does not pull the SDK in
//itself, so an include placed above it fails with "unknown type name" errors
//that point at the About block rather than at the include order.
#include "StoatworksAboutParams.h"
#include "Rig.h"
#include "Timing.h"

#include <string>

namespace pepper
{

/**
    SW Pepper -- Pepper's ghost, as an FFGL **mixer** for Resolume: a sheet of
    glass or foil at an angle reflects a hidden screen (this layer, Src) over
    the stage seen through it (the layer below, Dest). Or four sheets, as a
    pyramid on a phone.

    One mechanism: every camera ray that meets the sheet splits into a
    reflected and a transmitted part in the proportions Fresnel gives for
    that angle and polarisation; the reflected part goes on to the screen or
    the dark around it, the transmitted part to the stage. The additive
    ghost, the dimmed stage, the angle law, Brewster, the double image of a
    thick pane, the tint, the ripple, the keystone and the pyramid's seams
    fall out. See Rig.h for the geometry and AGENTS.md for the why.

    **This is an FF_MIXER**, the fleet's fifth. Read the mixer section of
    genlock's AGENTS.md and relay's "What Relay showed in Arena" before
    changing ProcessOpenGL.

    Its only state across frames is the clock, which drives the foil's
    ripple; there are no buffers, so a resize clears nothing.
*/
class Pepper : public CFFGLPlugin
{
public:
	Pepper();

	//CFFGLPlugin
	FFResult InitGL( const FFGLViewportStruct* vp ) override;
	FFResult ProcessOpenGL( ProcessOpenGLStruct* pGL ) override;
	FFResult DeInitGL() override;

	FFResult SetFloatParameter( unsigned int index, float value ) override;
	float GetFloatParameter( unsigned int index ) override;

	FFResult SetTime( double time ) override;

	/// Changes no pixel. It exists so ONE session in front of Resolume
	/// leaves a log that says which host and which bundle.
	void SetHostInfo( const char* hostname, const char* version ) override;

	char* GetTextParameter( unsigned int index ) override;

	/// Declared only so the About line can accept its own default.
	/// instantiateGL pushes every declared default back through the setters
	/// and deletes the whole instance if one fails, and CFFGLPlugin's
	/// SetTextParameter is a stub that returns exactly that failure.
	FFResult SetTextParameter( unsigned int index, const char* value ) override;

	/// Optional, not required -- see genlock's AGENTS.md.
	const char* GetShortName() override
	{
		return "Pepr";
	}

	/// What the last rendered frame used, for the harness: a check that
	/// disagrees with the picture can then say WHICH side is wrong.
	const rig::Setup& SetupForTest() const
	{
		return lastSetup;
	}

	/// The negative controls: a bitmask of pepper::Fault, 0 in the shipped
	/// plugin, reachable only from here.
	void SetFaultForTest( int mask )
	{
		fault = mask;
	}

	/// The mutation test: compile this fragment shader instead of the
	/// shipped one. Call before InitGL. Null restores the shipped text.
	void SetFragmentShaderForTest( const char* text )
	{
		fragmentOverride = text;
	}

	/// The harness knows what it sends SetTime: seconds.
	void SetClockScaleForTest( double scale )
	{
		clock.SetScaleForTest( scale );
	}

	/// In the order the host shows them.
	///
	/// **Index 0 is sacrificial.** Resolume Arena 7.27.1 does not expose a
	/// mixer's FIRST parameter at all -- measured on genlock, wipe, relay and
	/// lenticular -- so it is stuck at its default. Whatever sits here must
	/// be a control whose default is right if nobody can ever reach it:
	/// Linear Light, on, which is how light adds. Its other position is the
	/// gamma-space add Resolume's own Add blend mode does, for comparison.
	enum ParamID : FFUInt32
	{
		//Light
		PT_LINEAR_LIGHT,
		PT_OPACITY,///< the source's light: Arena binds it to the LAYER's opacity fader
		PT_SOURCE_LEVEL,
		PT_SOURCE_BLACK,
		PT_POLARISATION,
		PT_STAGE_LEVEL,
		PT_SPILL,

		//Rig
		PT_RIG,
		PT_SHEET_ANGLE,
		PT_SIDES,
		PT_CENTRE,
		PT_SCREEN,

		//Sheet
		PT_SHEET,
		PT_THICKNESS,
		PT_TINT_R,
		PT_TINT_G,
		PT_TINT_B,
		PT_RIPPLE,
		PT_RIPPLE_SPEED,

		//View
		PT_DISTANCE,
		PT_ELEVATION,
		PT_AZIMUTH,

		//About. Last in the enum so nothing before it ever moves.
		PT_ABOUT_FIRST,
		PT_COUNT = PT_ABOUT_FIRST + stoatworks::about::kParamCount
	};

private:
	ffglex::FFGLShader shader;
	ffglex::FFGLScreenQuad quad;

	timing::Clock clock;
	rig::Setup lastSetup;

	float params[ PT_COUNT ] = {};

	int fault                    = 0;
	const char* fragmentOverride = nullptr;
	std::string fragmentText;

	//The host-session log: what reached the mixer. Nothing renders from it.
	unsigned long frames  = 0;
	int instance          = 0;
	int opacityLines      = 0;
	float loggedOpacity   = -1.0f;
	unsigned guardsLogged = 0;
	void LogGuard( unsigned bit, const char* what );

	/// GetTextParameter hands the host a bare pointer, so the string has to
	/// outlive the call.
	std::string aboutText;
};

} // namespace pepper
