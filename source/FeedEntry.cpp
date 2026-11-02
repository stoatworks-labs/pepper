/**
	The FF_EFFECT registration: SW Pepper Feed. See MixerEntry.cpp.
*/
#include "Feed.h"
#include "Names.h"

static CFFGLPluginInfo PluginInfo(
	PluginFactory< pepper::Feed >,// Create method
	pepper::names::kFeedId,       // Plugin unique ID of maximum length 4
	pepper::names::kFeedName,     // Plugin name
	2,                            // API major version number
	1,                            // API minor version number
	0,                            // Plugin major version number
	1,                            // Plugin minor version number
	FF_EFFECT,                    // Plugin type
	"The layout a pyramid hologram is fed: the clip three or four times round a centre square, each arm's top outward, each arm a mirror image, over black.\n\nThe mirror is on purpose. Each face of the pyramid reflects its arm, and a reflection of a reflection reads the right way round.\n\nSides and Centre match SW Pepper's, so the feed sent to a real pyramid is the one the simulator looked into.",// Plugin description
	"Pepper Feed FFGL effect"     // About
);

extern "C" const char* PepperFeedBuildStamp()
{
	return "pepper " PEPPER_VERSION " feed, built " __DATE__ " " __TIME__;
}
