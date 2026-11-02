#pragma once

/**
    The rig, as numbers: what the shader is handed, worked out on the CPU in
    double from the parameters, once a frame.

    Pepper models one thing -- **a clear sheet at an angle over an emitting
    screen** -- and a rig is just where the sheets and the screen are and
    where the eye is. World space is right-handed, `y` up, metres-free: the
    unit is the RIG SIZE (Controls.h). The viewer at Azimuth 0 is on `+z`
    looking `-z`.

    **Pane.** The source screen lies on the floor, `y = 0`, over
    `x in [-A/2, A/2]`, `z in [0, 1]`, A being Src's aspect; its picture's top
    is at `z = 1`, toward the audience, which is how a floor LED is rigged for
    an upright ghost. The pane leans toward the audience from its base line
    (`y = 0, z = 0`) at Sheet Angle phi: up-slope `(0, sin phi, cos phi)`, so
    its normal is `(0, -cos phi, sin phi)`. A floor point `(x, 0, s)` has its
    mirror image at `(x, s sin 2phi, s cos 2phi)`: at 45 degrees the ghost
    stands vertical on the base line, one rig size tall. The pane's frame
    covers the screen and one rig size either side, up to `z = 1.5`; past it
    there is no sheet. The screen is hidden from direct view, as a stage
    front hides it in every real rig: a ray that was never reflected sees
    Dest, the stage.

    **Pyramid.** The screen lies at `y = 0`, the shape of Src, in units of its
    SHORT side, picture top away from the viewer at Azimuth 0 (picture
    `(x, y)` is world `(x, -z)`). An apex-down pyramid of n faces stands on
    it: small end at apothem c/2, top at apothem 1/2, every face at Sheet
    Angle to the screen. Face j leans over arm j of the feed. The rig size is
    0.6 of the top's width.

    **Feed.** The layout SW Pepper Feed makes and the pyramid's screen shows
    under Screen = Feed. Arm j points along `a_j = rot( 2 pi j / n ) (0, -1)`
    (arm 0 toward the viewer, at the bottom of the picture), `t_j` is a_j
    turned 90 degrees anticlockwise, and the arm holds the clip in
    `r in [c/2, c/2 + d]` along a_j, `|e| <= w/2` along t_j, with
    `w = min( c tan( pi/n ), A ( 1/2 - c/2 ) )` and `d = w / A`: its top
    outward and its right along t_j. That is a REFLECTION of the clip
    (determinant -1), which the face's mirror undoes. The shader does the
    lookup (kCommon in Shaders.cpp); this computes w and d.

    **Camera.** A pinhole, 30 degrees vertical, orbiting the rig's centre at
    Distance rig sizes, Elevation and Azimuth. `right x up` points at the
    viewer.

    **Mount = Above.** The rig turned upside down: the screen overhead
    facing down, the pane hanging from its top edge, the pyramid standing on
    its wide end with its small end at the screen. It is built as the mirror
    image, in the horizontal plane through the rig's centre, of the Below
    rig above -- the same world, so the same shader -- and the CAMERA is
    reflected into it instead: its eye, forward, right and up each have y
    negated about the centre. Mirror symmetry is exact for these optics
    (nothing here is chiral), so the picture is the Above rig's own.
    Which way up the screen's picture is, as the audience would rig it:
      - Pane: the picture's top AWAY from the audience (map AsIs on the
        pane's rectangle), which is what stands the ghost upright when the
        screen is overhead.
      - Pyramid: the screen turned face down about the viewer's line of
        sight, so arm 0 still points at the viewer and the picture is
        mirrored left to right in world x (maps AsIsMirrored and
        FeedMirrored). The feed for an apex-up pyramid has each arm's clip
        turned through 180 degrees in place, top INWARD (FeedFlip); with
        the apex-down layout every face shows the clip upside down.

    **Polarisation** is the screen's own: an E field along the picture's x,
    its y or a diagonal, mapped into the world through the screen's map.

    **Dispersion.** The sheet's index is per channel (eta[ 3 ], Controls.h's
    wavelengths): it sets each channel's Fresnel weights and the coat's
    table. Rays are bent with the green channel's index alone (etaRay).
*/
namespace pepper::rig
{

struct Vec3
{
	double x = 0.0, y = 0.0, z = 0.0;
};

Vec3 operator+( Vec3 a, Vec3 b );
Vec3 operator-( Vec3 a, Vec3 b );
Vec3 operator*( Vec3 a, double k );
double dot( Vec3 a, Vec3 b );
Vec3 cross( Vec3 a, Vec3 b );
Vec3 normalize( Vec3 a );

/// How many membrane modes ring on each sheet, and which: (m, n) over the
/// frame's width and slope. Shared with the shader, which hard-codes the
/// same table (kModes in Shaders.cpp); `pgtest --ripple` would see a
/// mismatch as a spectrum with the wrong peaks.
inline constexpr int kModeCount = 6;
inline constexpr int kModeM[ kModeCount ] = { 1, 2, 1, 2, 3, 1 };
inline constexpr int kModeN[ kModeCount ] = { 1, 1, 2, 2, 1, 3 };

inline constexpr int kMaxFaces = 4;

/// One sheet: a plane through `origin` spanned by `edge` (along its bottom
/// edge) and `up` (up its slope), `normal = edge x up`. It is bounded to
/// `s in [0, length]` up the slope and `|e| <= half + spread s` along the
/// edge -- a rectangle for a pane, a trapezoid for a pyramid's face. Its
/// ripple modes live on `e in [-width/2, width/2]`, `s in [0, length]`.
struct Face
{
	Vec3 origin, edge, up, normal;
	double half   = 0.0;
	double spread = 0.0;
	double length = 0.0;
	double width  = 0.0;

	/// Each mode's amplitude (rig sizes), its frequency (Hz) and its phase,
	/// and this frame's displacement of it, `amplitude cos( 2 pi f t + phase )`.
	double amplitude[ kModeCount ] = {};
	double hertz[ kModeCount ]     = {};
	double phase[ kModeCount ]     = {};
	double height[ kModeCount ]    = {};
};

enum class ScreenMap : int
{
	PaneFloor    = 0,///< u along x, v along z (top toward the audience)
	AsIs         = 1,///< the picture as it is: u along x, v along -z
	Feed         = 2,///< SW Pepper Feed's layout, picture (x, y) at world (x, -z)
	AsIsMirrored = 3,///< u along -x, v along -z (a pyramid's screen overhead)
	FeedMirrored = 4,///< the feed, picture (x, y) at world (-x, -z)
};

/// The screen picture's own x and y axes, as world directions in the
/// screen's plane, for a map.
void PictureAxes( ScreenMap map, Vec3& px, Vec3& py );

struct Setup
{
	int rig = 0;///< RigKind

	//The camera.
	Vec3 eye, target, forward, right, up;
	double tanHalfY = 0.0;
	double aspect   = 1.0;///< output width / height

	//The sheets.
	int faces = 0;
	Face face[ kMaxFaces ];
	double eta[ 3 ]  = { 1.5, 1.5, 1.5 };///< per channel
	double etaRay    = 1.5;              ///< the index rays are bent with
	double thickness = 0.0;///< world units
	double tint[ 3 ] = { 1.0, 1.0, 1.0 };
	int coating      = 0;  ///< CoatingKind
	double coatNm    = 0.0;
	int mount        = 0;  ///< MountKind

	//The screen: a rectangle in y = 0, `xMin..xMax` by `zMin..zMax`.
	ScreenMap map = ScreenMap::PaneFloor;
	double xMin = 0.0, xMax = 0.0, zMin = 0.0, zMax = 0.0;
	/// The source's E field, in world space, or zero for unpolarised.
	Vec3 polarisation;
	/// The screen's luminance off its normal: proportional to cos^falloff.
	double falloff = 0.0;
	/// The feed's arms turned 180 degrees in place: an apex-up pyramid's.
	bool feedFlip = false;

	//The feed: n arms of w by d, starting at apothem r0 = c/2, in units of
	//the picture's short side.
	int sides     = 4;
	double centre = 0.3;
	double armW = 0.0, armD = 0.0;

	//Light, linear multipliers.
	double sourceGain  = 1.0;///< Source Level times Opacity
	double sourceBlack = 0.0;
	double spill       = 0.0;
	double stageGain   = 1.0;
	bool linearLight   = true;

	//The rig's own dimensions, for the record and the harness.
	double rigSize    = 1.0;///< world units per rig size
	double pyramidH   = 0.0;///< the pyramid's height
	double sheetAngle = 0.0;///< radians
};

/// The feed's arm, for a clip of aspect A (width / height) on n sides with
/// the centre c: fills w and d.
void FeedArm( int sides, double centre, double aspect, double& w, double& d );

/// The arm's axis a_j and its lateral t_j, in the PICTURE's own coordinates
/// (x right, y up).
void ArmAxes( int sides, int j, double& ax, double& ay, double& tx, double& ty );

struct Inputs
{
	int rig       = 0;
	double angle  = 0.785398163397448;
	int sides     = 4;
	double centre = 0.3;
	int screen    = 0;///< ScreenKind
	int sheet     = 0;///< SheetKind
	double thickness = 0.0;///< rig sizes
	double tint[ 3 ] = { 1.0, 1.0, 1.0 };
	double ripple    = 0.0;///< radians of slope
	double rippleHz  = 0.5;
	double distance  = 2.5;
	double elevation = 0.0;
	double azimuth   = 0.0;
	double sourceGain = 1.0, sourceBlack = 0.0, spill = 0.0, stageGain = 1.0;
	int polarisation  = 0;
	double viewAngle  = 3.14159265358979323846;///< the full half-luminance angle
	int mount         = 0;
	int coating       = 0;
	double coatNm     = 0.0;
	bool linearLight  = true;

	double srcAspect = 16.0 / 9.0;///< Src's width / height
	double outAspect = 16.0 / 9.0;
	double seconds   = 0.0;       ///< since the epoch, for the ripple

	/// The negative control: every mode at the fundamental's frequency.
	bool modesLocked = false;
	/// The negative control: Above with the Below picture maps and feed.
	bool aboveAsBelow = false;
	/// The negative control: the polarisation angle taken clockwise.
	bool clockwise = false;
};

Setup Build( const Inputs& in );

} // namespace pepper::rig
