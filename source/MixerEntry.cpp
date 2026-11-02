/**
	The FF_MIXER registration: SW Pepper.

	Listed directly in the Pepper bundle's target and never in pepper_core,
	because a registration in the shared library would put both plugins into
	both bundles (vectrix's two-bundle pattern; FeedEntry.cpp is the other).
*/
#include "Names.h"
#include "Pepper.h"

static CFFGLPluginInfo PluginInfo(
	PluginFactory< pepper::Pepper >,// Create method
	pepper::names::kMixerId,        // Plugin unique ID of maximum length 4
	pepper::names::kMixerName,      // Plugin name
	2,                              // API major version number
	1,                              // API minor version number
	0,                              // Plugin major version number
	1,                              // Plugin minor version number
	FF_MIXER,                       // Plugin type
	"Pepper's ghost: this layer is the hidden screen, the layer below is the stage seen through the glass.\n\nThe sheet reflects the screen by Fresnel's law and lets the stage through, so the ghost only ever adds light: its black is the stage, and nothing on it is darker than what is behind it. Tilt the sheet and the reflection follows the angle; a thick pane doubles it; foil ripples.\n\nRig = Pyramid is the phone toy: three or four sheets over a screen fed by SW Pepper Feed, looked at from wherever View puts you.\n\nOpacity is the source's light. Fade the layer up and the ghost appears; the glass stays.",// Plugin description
	"Pepper FFGL mixer"             // About
);

extern "C" const char* PepperBuildStamp()
{
	return "pepper " PEPPER_VERSION " mixer, built " __DATE__ " " __TIME__;
}
