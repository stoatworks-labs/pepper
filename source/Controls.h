#pragma once

/**
    Host parameters are 0..1; these are what they mean.

    Every ranged parameter either plugin declares is a plain FF_TYPE_STANDARD
    float in 0..1, including the ones that stand for stops of light, an angle
    in degrees, a refractive index or a distance in rig sizes. That is not a
    style preference: `SetParamInfo` clamps a standard default into 0..1
    *before* returning, and `SetParamRange` can only be called afterwards --
    so a parameter declared in degrees cannot declare a default in degrees.
    The conversions live here, in one file both plugins and the harness use,
    so there is only ever one answer to what a slider position means, and
    SW Pepper Feed's Centre is SW Pepper's Centre bit for bit.

    Options carry their element INDEX as the element value (0, 1, 2 ...), and
    `OptionIndex` rounds whatever arrives back to one: an option's range reads
    back 0..1 whatever its element count, so nothing here trusts the range.

    **The rig size** is the unit of every length: the ghost's height for a
    pane (the source screen is one rig size deep, and at 45 degrees its
    mirror image stands one rig size tall), and 0.6 of the top's width for a
    pyramid (about its apparent height from a level seat). See Rig.h.
*/
namespace pepper
{

inline constexpr double kPi = 3.14159265358979323846;

int OptionIndex( float value, int count );

//---------------------------------------------------------------------------
// Light
//---------------------------------------------------------------------------

/// Source Level: the hidden screen's brightness against the stage, in stops,
/// -3 to +6, linear in stops. A real ghost needs its source several times
/// brighter than the stage because the sheet reflects about a tenth of it;
/// +2.5 (x5.7) puts a white source on PET foil at 45 degrees at 0.73 of white.
inline constexpr double kSourceStopsMin     = -3.0;
inline constexpr double kSourceStopsMax     = 6.0;
inline constexpr double kSourceStopsDefault = 2.5;

/// Stage Level: the stage (Dest) seen through the sheet, in stops, -4 to +2.
inline constexpr double kStageStopsMin     = -4.0;
inline constexpr double kStageStopsMax     = 2.0;
inline constexpr double kStageStopsDefault = 0.0;

/// Source Black: the hidden screen's black, as a fraction of its white, 0 to
/// 5 %, linear. An LED floor is near 0; a projection screen in a lit pit is
/// a few percent, and it is what shows as a grey rectangle on the stage.
inline constexpr double kSourceBlackMax = 0.05;

/// Spill: the luminance of whatever the sheet reflects other than the screen
/// -- the pit floor around a pane's screen, the room around a pyramid -- as a
/// fraction of white, 0 to 0.5, linear.
inline constexpr double kSpillMax = 0.5;

/// The smallest contribution a ray is followed for, in linear light: under
/// half an 8-bit code even at black, where sRGB is steepest (1/8192 x 12.92
/// x 255 = 0.40 codes). A ray is followed only while the most it could
/// still deliver -- its weight, Q's larger eigenvalue, in its brightest
/// channel -- exceeds this over the brightest light in the scene: Source
/// Level's gain (NOT x Opacity), Spill or Stage Level's gain.
inline constexpr double kPruneLight = 1.0 / 8192.0;

enum class Polarisation : int
{
	None,      ///< an LED wall, a projector: unpolarised
	Horizontal,///< E along the screen picture's x (an LCD turned on its side)
	Vertical,  ///< E along the screen picture's y (most LCD TVs, upright)
	Deg45,     ///< E at 45 degrees anticlockwise from the picture's x (many monitors and phones)
	Deg135,    ///< E at 135 degrees: the other diagonal
	Count
};

/// The angle of a polarised source's E field from the screen picture's x
/// axis, anticlockwise as the picture is seen from in front; meaningless for
/// None.
double PolarisationAngle( Polarisation kind );

/// Viewing Angle: the full angle, either side of the screen's normal, at
/// which its luminance has fallen to half, 40 to 180 degrees, linear. 180 is
/// a Lambertian emitter (no falloff); an LED tile is typically 120 to 160.
/// The model is the generalised Lambertian, L proportional to cos^p of the
/// angle off the normal, with p chosen to halve L at half this angle.
inline constexpr double kViewAngleMinDeg     = 40.0;
inline constexpr double kViewAngleMaxDeg     = 180.0;
inline constexpr double kViewAngleDefaultDeg = 180.0;

double ViewAngleFromParam( float value );///< radians, the full angle
float ParamForViewAngle( double radians );
/// p in L proportional to cos^p, for a full half-luminance angle; 0 at 180 degrees.
double FalloffPower( double fullAngle );

double SourceGainFromParam( float value );///< linear multiplier, 2^stops
double StageGainFromParam( float value ); ///< linear multiplier, 2^stops
double SourceBlackFromParam( float value );
double SpillFromParam( float value );
float ParamForSourceStops( double stops );
float ParamForStageStops( double stops );
float ParamForSourceBlack( double fraction );
float ParamForSpill( double fraction );

//---------------------------------------------------------------------------
// Rig
//---------------------------------------------------------------------------

enum class RigKind : int
{
	Pane,   ///< one sheet over a floor screen: the theatre, Musion
	Pyramid,///< three or four sheets standing on a screen: the phone toy
	Count
};

/// Where the hidden screen is. Above is the whole rig turned upside down: a
/// display case with its monitor in the lid, a pyramid standing apex up
/// under a monitor that faces down. Rig.h says which way up each picture is.
enum class MountKind : int
{
	Below,///< the screen on the floor or the table, facing up
	Above,///< the screen overhead, facing down
	Count
};

/// Sheet Angle: the sheet's angle to the source screen, 25 to 65 degrees,
/// linear. 45 is the textbook rig: the ghost stands upright.
inline constexpr double kSheetAngleMinDeg     = 25.0;
inline constexpr double kSheetAngleMaxDeg     = 65.0;
inline constexpr double kSheetAngleDefaultDeg = 45.0;

/// Sides: element 0 is a four-sided pyramid, 1 three.
int SidesFromParam( float value );

/// Centre: the pyramid's small end -- and the feed's centre square -- as its
/// apothem's diameter over the screen's short side, 0.1 to 0.6, linear.
inline constexpr double kCentreMin     = 0.1;
inline constexpr double kCentreMax     = 0.6;
inline constexpr double kCentreDefault = 0.3;

enum class ScreenKind : int
{
	Feed, ///< the pyramid's screen shows SW Pepper Feed's layout of Src
	AsIs, ///< the screen shows Src as it is (a ready-made cross)
	Count
};

double SheetAngleFromParam( float value );///< radians
double CentreFromParam( float value );    ///< of the screen's short side
float ParamForSheetAngle( double radians );
float ParamForCentre( double fraction );

//---------------------------------------------------------------------------
// Sheet
//---------------------------------------------------------------------------

enum class SheetKind : int
{
	PetFoil,      ///< polyester film: Musion's foil, Mylar. n 1.65
	FloatGlass,   ///< the Victorian plate. n 1.52
	Acrylic,      ///< PMMA, most pyramids. n 1.49
	Polycarbonate,///< n 1.585
	Count
};

/// Each channel stands for one wavelength: the dominant wavelength of its
/// BT.709 primary against D65, from the CIE 1931 observer. Red, green, blue.
inline constexpr double kChannelNm[ 3 ] = { 611.3, 549.1, 464.3 };
/// The channel whose index bends the rays: green. Rays are not traced per
/// channel, which would triple a thick sheet's back images. The colour fringe
/// this leaves out is 0.7 % (glass) to 1.7 % (polycarbonate, PET) of a back
/// image's offset, and nothing for a film (AGENTS.md, "Dispersion").
inline constexpr int kRayChannel = 1;

/// A sheet's refractive index at a wavelength, from its published
/// dispersion (Controls.cpp cites each).
double IndexAt( SheetKind kind, double nanometres );
/// At the d line, 587.6 nm: the number a datasheet quotes.
double IndexOf( SheetKind kind );
/// At channel c's wavelength.
double ChannelIndex( SheetKind kind, int channel );
const char* NameOf( SheetKind kind );

/// A thin coat on the sheet's screen side -- the side the hidden screen and
/// the audience are on.
enum class CoatingKind : int
{
	None,
	Aluminium,      ///< a metal partial mirror: a beam splitter, a metallised foil
	TitaniumDioxide,///< a high-index dielectric layer: reflects more, absorbs nothing
	Count
};
const char* NameOf( CoatingKind kind );

/// Coat Thickness: for aluminium 0 to 10 nm, linear (at 5 nm a film reflects
/// and transmits about 40 % each and absorbs the rest); for titanium dioxide
/// 0 to 2 quarter waves at the green channel's wavelength, linear (one
/// quarter wave, 56 nm, is the textbook high reflector; two is a half wave,
/// which green does not see at all face on).
inline constexpr double kAluminiumMaxNm      = 10.0;
inline constexpr double kTitaniaMaxQuarters  = 2.0;
inline constexpr double kCoatThicknessDefault = 0.5;

double CoatThicknessNm( CoatingKind kind, float value );
float ParamForCoatThickness( CoatingKind kind, double nanometres );
/// The coat's complex index n + i k at channel c's wavelength.
void CoatIndex( CoatingKind kind, int channel, double& n, double& k );

/// Thickness: 0 to 4 % of the rig size, linear. 0 is a film: every image of
/// the sheet's two surfaces lands in the same place. 12 mm plate under a
/// 1.8 m ghost is 0.67 %; a 0.5 mm acrylic pyramid 60 mm across is 1.4 %.
inline constexpr double kThicknessMax = 0.04;

/// Ripple: the foil's steepest slope anywhere, 0 to 3 degrees, linear.
inline constexpr double kRippleMaxDeg     = 3.0;
inline constexpr double kRippleDefaultDeg = 0.2;

/// Ripple Speed: the frame's fundamental (the (1,1) mode), 0.05 to 4 Hz,
/// geometric.
inline constexpr double kRippleHzMin     = 0.05;
inline constexpr double kRippleHzMax     = 4.0;
inline constexpr double kRippleHzDefault = 0.5;

double ThicknessFromParam( float value );///< rig sizes
double RippleFromParam( float value );   ///< radians of slope
double RippleHzFromParam( float value );
float ParamForThickness( double rigSizes );
float ParamForRipple( double radians );
float ParamForRippleHz( double hertz );

//---------------------------------------------------------------------------
// View
//---------------------------------------------------------------------------

/// Distance: the camera from the rig's centre, 1 to 12 rig sizes, geometric.
/// At 2.5 the pane's ghost fills three quarters of the frame's height.
inline constexpr double kDistanceMin     = 1.0;
inline constexpr double kDistanceMax     = 12.0;
inline constexpr double kDistanceDefault = 2.5;

/// Elevation: the camera above (+) or below (-) the rig's centre, as an
/// angle, -20 to +60 degrees, linear.
inline constexpr double kElevationMinDeg = -20.0;
inline constexpr double kElevationMaxDeg = 60.0;

/// Azimuth: the camera around the rig, -180 to +180 degrees, linear; 0 is
/// square on, from the audience.
inline constexpr double kAzimuthMaxDeg = 180.0;

/// The camera's vertical field of view: 30 degrees, about a 45 mm lens on a
/// full-frame camera. Fixed; Distance frames the rig.
inline constexpr double kFieldOfViewDeg = 30.0;

double DistanceFromParam( float value ); ///< rig sizes
double ElevationFromParam( float value );///< radians
double AzimuthFromParam( float value );  ///< radians
float ParamForDistance( double rigSizes );
float ParamForElevation( double radians );
float ParamForAzimuth( double radians );

} // namespace pepper
