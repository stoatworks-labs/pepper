#pragma once

#include <FFGLSDK.h>

//AFTER the SDK, as in Pepper.h.
#include "StoatworksAboutParams.h"

#include <string>

namespace pepper
{

/**
    SW Pepper Feed -- the layout a pyramid hologram is fed, as an FFGL
    **effect**: the clip below, three or four times round a centre square,
    each arm's top outward and each arm a mirror image, over black.

    An effect and not a mixer because it is a picture made from one picture:
    it belongs on a clip, a layer or the composition, wherever the feed for a
    real pyramid is cut. SW Pepper (Rig = Pyramid, Screen = Feed) builds the
    same layout from the same GLSL (kCommon's feedLookup) and looks into it,
    so what is sent to the phone is what the simulator showed.

    No clock, no buffers, no state across frames.
*/
class Feed : public CFFGLPlugin
{
public:
	Feed();

	FFResult InitGL( const FFGLViewportStruct* vp ) override;
	FFResult ProcessOpenGL( ProcessOpenGLStruct* pGL ) override;
	FFResult DeInitGL() override;

	FFResult SetFloatParameter( unsigned int index, float value ) override;
	float GetFloatParameter( unsigned int index ) override;

	void SetHostInfo( const char* hostname, const char* version ) override;

	char* GetTextParameter( unsigned int index ) override;
	/// See Pepper.h: the About line must accept its own default.
	FFResult SetTextParameter( unsigned int index, const char* value ) override;

	const char* GetShortName() override
	{
		return "PepF";
	}

	/// The negative controls, as in Pepper.h. Only kFaultRotatedArms means
	/// anything here.
	void SetFaultForTest( int mask )
	{
		fault = mask;
	}

	enum ParamID : FFUInt32
	{
		PT_SIDES,
		PT_CENTRE,

		PT_ABOUT_FIRST,
		PT_COUNT = PT_ABOUT_FIRST + stoatworks::about::kParamCount
	};

private:
	ffglex::FFGLShader shader;
	ffglex::FFGLScreenQuad quad;

	float params[ PT_COUNT ] = {};
	int fault                = 0;

	unsigned long frames  = 0;
	unsigned guardsLogged = 0;
	void LogGuard( unsigned bit, const char* what );

	std::string aboutText;
};

} // namespace pepper
