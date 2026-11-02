#include "Rig.h"

#include "Controls.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace pepper::rig
{

Vec3 operator+( Vec3 a, Vec3 b )
{
	return { a.x + b.x, a.y + b.y, a.z + b.z };
}

Vec3 operator-( Vec3 a, Vec3 b )
{
	return { a.x - b.x, a.y - b.y, a.z - b.z };
}

Vec3 operator*( Vec3 a, double k )
{
	return { a.x * k, a.y * k, a.z * k };
}

double dot( Vec3 a, Vec3 b )
{
	return a.x * b.x + a.y * b.y + a.z * b.z;
}

Vec3 cross( Vec3 a, Vec3 b )
{
	return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
}

Vec3 normalize( Vec3 a )
{
	const double length = std::sqrt( dot( a, a ) );
	return length > 0.0 ? a * ( 1.0 / length ) : a;
}

namespace
{
/// PCG's output permutation: a fixed, exact scramble for the ripple's
/// phases and signs. It runs on the CPU only, so its job is to be the same
/// on every machine, which integer arithmetic is.
uint32_t hash32( uint32_t v )
{
	uint32_t state = v * 747796405u + 2891336453u;
	uint32_t word  = ( ( state >> ( ( state >> 28u ) + 4u ) ) ^ state ) * 277803737u;
	return ( word >> 22u ) ^ word;
}

double unit( uint32_t v )
{
	return static_cast< double >( hash32( v ) ) / 4294967296.0;
}

/// Picture coordinates (x right, y up) to world (x, -z) on the screen.
Vec3 fromPicture( double px, double py )
{
	return { px, 0.0, -py };
}

/// The membrane modes of one sheet's frame. A tensioned rectangle with fixed
/// edges rings at f_mn proportional to sqrt( (m/W)^2 + (n/L)^2 ), and its
/// slope in mode (m, n) is at most A pi sqrt( (m/W)^2 + (n/L)^2 ). The
/// amplitudes fall as 1 / (m^2 + n^2) with a hashed sign, and are scaled so
/// that the sum of every mode's steepest slope is tan( Ripple ): the most the
/// foil can tilt anywhere, should every mode line up.
void ringModes( Face& f, int faceIndex, const Inputs& in )
{
	double weight[ kModeCount ], slope[ kModeCount ];
	double sum = 0.0;
	for( int k = 0; k < kModeCount; ++k )
	{
		const double m = kModeM[ k ], n = kModeN[ k ];
		const uint32_t seed = static_cast< uint32_t >( faceIndex * 97 + k * 13 + 1 );
		const double sign   = ( hash32( seed ) & 1u ) ? 1.0 : -1.0;
		weight[ k ]         = sign / ( m * m + n * n );
		slope[ k ]          = kPi * std::sqrt( ( m / f.width ) * ( m / f.width ) + ( n / f.length ) * ( n / f.length ) );
		sum += std::fabs( weight[ k ] ) * slope[ k ];
	}

	const double steepest = std::tan( in.ripple );
	for( int k = 0; k < kModeCount; ++k )
	{
		f.amplitude[ k ] = sum > 0.0 ? steepest * weight[ k ] / sum : 0.0;
		f.hertz[ k ]     = in.modesLocked ? in.rippleHz : in.rippleHz * slope[ k ] / slope[ 0 ];
		f.phase[ k ]     = 2.0 * kPi * unit( static_cast< uint32_t >( faceIndex * 97 + k * 13 + 50 ) );

		//The phase in double, reduced to one turn BEFORE it meets the phase
		//offset, so a session hours long is as exact as its first second.
		const double turns = std::fmod( f.hertz[ k ] * in.seconds, 1.0 );
		f.height[ k ]      = f.amplitude[ k ] * std::cos( 2.0 * kPi * turns + f.phase[ k ] );
	}
}
} // namespace

void PictureAxes( ScreenMap map, Vec3& px, Vec3& py )
{
	switch( map )
	{
	case ScreenMap::PaneFloor:
		px = { 1.0, 0.0, 0.0 };
		py = { 0.0, 0.0, 1.0 };
		break;
	case ScreenMap::AsIsMirrored:
	case ScreenMap::FeedMirrored:
		px = { -1.0, 0.0, 0.0 };
		py = { 0.0, 0.0, -1.0 };
		break;
	default:
		px = { 1.0, 0.0, 0.0 };
		py = { 0.0, 0.0, -1.0 };
		break;
	}
}

void FeedArm( int sides, double centre, double aspect, double& w, double& d )
{
	const double r0 = 0.5 * centre;
	w               = std::min( centre * std::tan( kPi / sides ), aspect * ( 0.5 - r0 ) );
	d               = w / aspect;
}

void ArmAxes( int sides, int j, double& ax, double& ay, double& tx, double& ty )
{
	const double theta = 2.0 * kPi * j / sides;
	//rot( theta ) applied to (0, -1), and that turned a further 90 degrees.
	ax = std::sin( theta );
	ay = -std::cos( theta );
	tx = -ay;
	ty = ax;
}

Setup Build( const Inputs& in )
{
	Setup s;
	s.rig        = in.rig;
	s.sheetAngle = in.angle;
	for( int c = 0; c < 3; ++c )
		s.eta[ c ] = ChannelIndex( static_cast< SheetKind >( in.sheet ), c );
	s.etaRay  = s.eta[ kRayChannel ];
	s.coating = in.coating;
	s.coatNm  = in.coating == static_cast< int >( CoatingKind::None ) ? 0.0 : in.coatNm;
	s.mount   = in.mount;
	s.falloff = FalloffPower( in.viewAngle );
	const bool above = in.mount == static_cast< int >( MountKind::Above ) && !in.aboveAsBelow;
	for( int c = 0; c < 3; ++c )
		s.tint[ c ] = std::clamp( in.tint[ c ], 0.0, 1.0 );
	s.sourceGain  = in.sourceGain;
	s.sourceBlack = in.sourceBlack;
	s.spill       = in.spill;
	s.stageGain   = in.stageGain;
	s.linearLight = in.linearLight;
	s.sides       = in.sides;
	s.centre      = in.centre;
	s.aspect      = in.outAspect;

	const double cosA = std::cos( in.angle ), sinA = std::sin( in.angle );
	const double A    = in.srcAspect;

	if( in.rig == static_cast< int >( RigKind::Pane ) )
	{
		s.rigSize = 1.0;
		s.target  = { 0.0, 0.5, 0.0 };

		s.faces  = 1;
		Face& f  = s.face[ 0 ];
		f.origin = { 0.0, 0.0, 0.0 };
		f.edge   = { 1.0, 0.0, 0.0 };
		f.up     = { 0.0, sinA, cosA };
		f.normal = cross( f.edge, f.up );
		f.half   = 0.5 * A + 1.0;
		f.spread = 0.0;
		f.length = 1.5 / cosA;
		f.width  = 2.0 * f.half;
		ringModes( f, 0, in );

		//Overhead, the picture's top goes away from the audience.
		s.map  = above ? ScreenMap::AsIs : ScreenMap::PaneFloor;
		s.xMin = -0.5 * A;
		s.xMax = 0.5 * A;
		s.zMin = 0.0;
		s.zMax = 1.0;
	}
	else
	{
		const int n      = in.sides;
		const double r0  = 0.5 * in.centre;
		const double tn  = std::tan( kPi / n );
		const double top = 2.0 * 0.5 * tn;//the top edge's length
		s.rigSize        = 0.6 * top;
		s.pyramidH       = ( 0.5 - r0 ) * std::tan( in.angle );
		s.target         = { 0.0, 0.5 * s.pyramidH, 0.0 };

		s.faces = n;
		for( int j = 0; j < n; ++j )
		{
			double ax, ay, tx, ty;
			ArmAxes( n, j, ax, ay, tx, ty );
			const Vec3 outward = fromPicture( ax, ay );
			Face& f            = s.face[ j ];
			f.origin           = outward * r0;
			f.edge             = fromPicture( tx, ty );
			f.up               = outward * cosA + Vec3{ 0.0, sinA, 0.0 };
			f.normal           = cross( f.edge, f.up );
			f.half             = r0 * tn;
			f.spread           = cosA * tn;
			f.length           = ( 0.5 - r0 ) / cosA;
			f.width            = top;
			ringModes( f, j, in );
		}

		const bool asIs = in.screen == static_cast< int >( ScreenKind::AsIs );
		if( above )
			s.map = asIs ? ScreenMap::AsIsMirrored : ScreenMap::FeedMirrored;
		else
			s.map = asIs ? ScreenMap::AsIs : ScreenMap::Feed;
		s.feedFlip = above;
		//The picture's short side is the unit.
		const double halfX = A >= 1.0 ? 0.5 * A : 0.5;
		const double halfZ = A >= 1.0 ? 0.5 : 0.5 / A;
		s.xMin             = -halfX;
		s.xMax             = halfX;
		s.zMin             = -halfZ;
		s.zMax             = halfZ;
	}

	if( in.polarisation != static_cast< int >( Polarisation::None ) )
	{
		Vec3 px, py;
		PictureAxes( s.map, px, py );
		const double chi = PolarisationAngle( static_cast< Polarisation >( in.polarisation ) ) * ( in.clockwise ? -1.0 : 1.0 );
		s.polarisation   = px * std::cos( chi ) + py * std::sin( chi );
	}

	FeedArm( in.sides, in.centre, A, s.armW, s.armD );
	s.thickness = in.thickness * s.rigSize;

	//The camera, on an orbit round the rig's centre.
	const double D  = in.distance * s.rigSize;
	const double ce = std::cos( in.elevation );
	s.eye           = s.target + Vec3{ D * ce * std::sin( in.azimuth ), D * std::sin( in.elevation ), D * ce * std::cos( in.azimuth ) };
	s.forward       = normalize( s.target - s.eye );
	s.right         = normalize( cross( s.forward, Vec3{ 0.0, 1.0, 0.0 } ) );
	s.up            = cross( s.right, s.forward );
	s.tanHalfY      = std::tan( 0.5 * kFieldOfViewDeg * kPi / 180.0 );

	if( in.mount == static_cast< int >( MountKind::Above ) )
	{
		//The camera, reflected into the Below rig (Rig.h): y negated about
		//the rig's centre. right x up then points AWAY from the eye -- a
		//mirror's image -- which is what makes every pixel's ray the
		//reflection of the Above rig's.
		s.eye.y     = 2.0 * s.target.y - s.eye.y;
		s.forward.y = -s.forward.y;
		s.right.y   = -s.right.y;
		s.up.y      = -s.up.y;
	}
	return s;
}

} // namespace pepper::rig
