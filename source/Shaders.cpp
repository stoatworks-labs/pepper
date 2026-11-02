#include "Shaders.h"

namespace pepper
{

const char* const kVersion = R"(#version 410 core
)";

//---------------------------------------------------------------------------
// The vertex shader passes UV through UNSCALED. The SDK's mixer example
// folds MaxUV in here; that is only right for a plugin whose every fetch is
// at the fragment's own position, and this one fetches the hidden screen
// wherever a reflected ray lands. Each fetch applies its own input's MaxUV,
// once, in the fragment shader.
//
// FoldMaxUV is (1, 1) in the shipped plugins. The harness's --mixer
// negative control sets it to Dest's MaxUV -- the genlock trap, exactly.
//---------------------------------------------------------------------------
const char* const kVertex = R"(
layout( location = 0 ) in vec4 vPosition;
layout( location = 1 ) in vec2 vUV;

uniform vec2 FoldMaxUV;

out vec2 uv;

void main()
{
	gl_Position = vPosition;
	uv          = vUV * FoldMaxUV;
}
)";

//---------------------------------------------------------------------------
// What both plugins must do identically.
//---------------------------------------------------------------------------
const char* const kCommon = R"(
const float kPi = 3.14159265358979;

const int kFaultFoldedMaxUV = 1;
const int kFaultAlphaOver   = 2;
const int kFaultOneSurface  = 4;
const int kFaultRpCosSwap   = 8;
const int kFaultSwapSP      = 16;
const int kFaultOnePass     = 32;
const int kFaultTintOnce    = 64;
const int kFaultFixed45     = 128;
const int kFaultMirrored    = 256;
const int kFaultCrossfade   = 1024;
const int kFaultRotatedArms = 2048;
const int kFaultLastOnly    = 4096;
const int kFaultCoatSymmetric = 8192;
const int kFaultFalloffOff  = 65536;

uniform int Fault;

//sRGB, as IEC 61966-2-1 writes it. mix() with a bvec SELECTS, exactly, so
//neither branch leaks into the other at the joint.
vec3 decodeSrgb( vec3 c )
{
	c = clamp( c, 0.0, 1.0 );
	return mix( c / 12.92, pow( ( c + 0.055 ) / 1.055, vec3( 2.4 ) ), greaterThan( c, vec3( 0.04045 ) ) );
}

vec3 encodeSrgb( vec3 c )
{
	c = clamp( c, 0.0, 1.0 );
	return mix( c * 12.92, 1.055 * pow( c, vec3( 1.0 / 2.4 ) ) - 0.055, greaterThan( c, vec3( 0.0031308 ) ) );
}

//The feed. FeedAxis[ j ] is arm j's direction a_j and its lateral t_j in
//the picture's own coordinates (x right, y up), from the CPU in double:
//arm 0 points down the picture, toward the viewer. Each arm holds the clip
//in r in [FeedR0, FeedR0 + d] along a_j, |e| <= w/2 along t_j, its top
//outward and its right along t_j -- a REFLECTION of the clip, which the
//pyramid face's mirror undoes. The arms cannot overlap (w <= c tan(pi/n)),
//so the first that holds p is the only one. FeedFlip turns each arm's clip
//through 180 degrees in place, top INWARD: the layout an apex-up pyramid,
//under a screen facing down, is fed (Rig.h).
uniform int FeedSides;
uniform int FeedFlip;
uniform float FeedR0;
uniform vec2 FeedArm;      //w, d
uniform vec4 FeedAxis[ 4 ];//a_j.xy, t_j.xy

bool feedLookup( vec2 p, out vec2 cuv )
{
	for( int j = 0; j < 4; ++j )
	{
		if( j >= FeedSides )
			break;
		float r = dot( p, FeedAxis[ j ].xy ) - FeedR0;
		float e = dot( p, FeedAxis[ j ].zw );
		if( r >= 0.0 && r <= FeedArm.y && abs( e ) <= 0.5 * FeedArm.x )
		{
			cuv = vec2( e / FeedArm.x + 0.5, r / FeedArm.y );
			if( FeedFlip != 0 )
				cuv = 1.0 - cuv;
			//The negative control: the common layout, four ROTATED copies,
			//which read mirrored in a pyramid.
			if( ( Fault & kFaultRotatedArms ) != 0 )
				cuv.x = 1.0 - cuv.x;
			return true;
		}
	}
	cuv = vec2( 0.0 );
	return false;
}
)";

//---------------------------------------------------------------------------
// SW Pepper Feed: the layout, over black, opaque.
//---------------------------------------------------------------------------
const char* const kFeedFragment = R"(
uniform sampler2D InputTexture;
uniform vec2 MaxUV;
uniform vec2 HalfTexel;
uniform ivec2 OutSize;
uniform float ShortSide;//pixels

in vec2 uv;
out vec4 fragColor;

void main()
{
	//The pixel is an integer, and its centre in units of the short side
	//follows from it exactly; the interpolated uv is good to 1 in 10^5 only.
	ivec2 px = ivec2( floor( uv * vec2( OutSize ) ) );
	vec2 p   = ( vec2( px ) + 0.5 - 0.5 * vec2( OutSize ) ) / ShortSide;

	vec2 cuv;
	vec3 c = vec3( 0.0 );
	if( feedLookup( p, cuv ) )
	{
		vec4 t = textureLod( InputTexture, clamp( cuv, HalfTexel, 1.0 - HalfTexel ) * MaxUV, 0.0 );
		//The clip over black: a screen's transparent is unlit.
		c = t.rgb * t.a;
	}
	//Opaque everywhere. A real screen emits black; transparency here would
	//put the layers below on the pyramid's screen.
	fragColor = vec4( c, 1.0 );
}
)";

//---------------------------------------------------------------------------
// SW Pepper: the trace. See Rig.h for where everything is.
//
// Split into adjacent raw strings: MSVC caps one literal at about 16 KB
// (C2026), and tools/glslc.sh joins them as the compiler does.
//---------------------------------------------------------------------------
const char* const kPepperFragment = R"(
//inputTextures[0] -- Dest, the layer BELOW: the stage, or the room.
uniform sampler2D TextureDest;
//inputTextures[1] -- Src, THIS layer: the hidden screen's picture.
uniform sampler2D TextureSrc;
uniform vec2 MaxUVDest;
uniform vec2 MaxUVSrc;
uniform vec2 HalfTexelDest;
uniform vec2 HalfTexelSrc;
uniform ivec2 OutSize;

uniform int RigKind;//0 pane, 1 pyramid

//The camera: a pinhole at Eye, TanHalf = ( tan( fov/2 ) * aspect, tan( fov/2 ) ).
uniform vec3 Eye;
uniform vec3 Forward;
uniform vec3 CamRight;
uniform vec3 CamUp;
uniform vec2 TanHalf;

//The sheets. Face j is a plane through FaceOrigin spanned by FaceEdge and
//FaceUp, FaceNormal = edge x up, bounded to s in [0, length] and
//|e| <= half + spread s. FaceBounds = ( half, spread, length, width ).
uniform int FaceCount;
uniform vec3 FaceOrigin[ 4 ];
uniform vec3 FaceEdge[ 4 ];
uniform vec3 FaceUp[ 4 ];
uniform vec3 FaceNormal[ 4 ];
uniform vec4 FaceBounds[ 4 ];
//The ripple: face j's mode k displacement this frame, at [ 6 j + k ], in
//rig sizes. The phases are reduced in double on the CPU; no time reaches
//the GPU. Rippling 0 leaves every normal exactly as declared.
uniform int Rippling;
uniform float ModeHeight[ 24 ];

//The screen, in y = 0: ( xMin, xMax, zMin, zMax ) and how its picture maps.
uniform int ScreenMap;//0 pane floor, 1 as is, 2 feed, 3 as is mirrored, 4 feed mirrored
uniform vec4 ScreenRect;
uniform vec3 SourceE; //the source's E field in world space; zero if unpolarised
uniform float Falloff;//its luminance, proportional to cos^Falloff off its normal

uniform vec3 Eta;       //the sheet's index at each channel's wavelength
uniform float EtaRay;   //the index rays are bent with: the green channel's
uniform float Thickness;//world units
uniform vec3 Tint;      //face-on transmittance of the sheet, per channel
//A coat on the side FaceNormal points to -- the screen's side -- as a table
//over cos( incidence ), from Coating.cpp: row r of kCoatSize texels holds
//quantity r for the three channels.
uniform int Coated;
uniform sampler2D CoatTable;

uniform float SourceGain;//Source Level x Opacity
uniform float SourceBlack;
uniform float Spill;
uniform float StageGain;
uniform float Opacity;   //for the --opacity negative control only
uniform int LinearLight;
uniform float Prune;     //the smallest ray weight worth following

//The 45-degree pane's normal, for the --geometry negative control.
uniform vec3 Normal45;

in vec2 uv;
out vec4 fragColor;

const int kModeM[ 6 ] = int[ 6 ]( 1, 2, 1, 2, 3, 1 );
const int kModeN[ 6 ] = int[ 6 ]( 1, 1, 2, 2, 1, 3 );

const float kEps    = 1e-5;
const int kStack    = 16;
const int kMaxRays  = 64;
const int kCoatSize = 1024;

//A ray traced back from the eye, and how the eye sees whatever light comes
//down it: Q, a 2x2 Hermitian matrix per channel in the basis ( e1, d x e1 )
//across the ray -- the eye sees a field E as E^H Q E. qs and qp are its
//diagonal, qr + i qi its off-diagonal. e1 is zero while Q is still the
//identity times a weight, as it is until the first interaction. refl is 1
//once reflected.
struct Ray
{
	vec3 o;
	vec3 d;
	vec3 qs;
	vec3 qp;
	vec3 qr;
	vec3 qi;
	vec3 e1;
	int refl;
};

//What one interaction, or one set of paths through a sheet, does to light in
//its own s and p: the summed |x_s|^2 and |x_p|^2, and the summed
//conj( x_s ) x_p, which carries the phase between them.
struct Coef
{
	vec3 s;
	vec3 p;
	vec3 cr;
	vec3 ci;
};

Ray stack[ kStack ];
int sp = 0;

vec3 toLight( vec3 c )
{
	return LinearLight != 0 ? decodeSrgb( c ) : clamp( c, 0.0, 1.0 );
}

vec3 toSignal( vec3 c )
{
	return LinearLight != 0 ? encodeSrgb( c ) : clamp( c, 0.0, 1.0 );
}

float maxOf( vec3 v )
{
	return max( v.r, max( v.g, v.b ) );
}

Coef cMul( Coef a, Coef b )
{
	return Coef( a.s * b.s, a.p * b.p, a.cr * b.cr - a.ci * b.ci, a.cr * b.ci + a.ci * b.cr );
}

Coef cScale( Coef a, vec3 k )
{
	return Coef( a.s * k, a.p * k, a.cr * k, a.ci * k );
}

Coef cAdd( Coef a, Coef b )
{
	return Coef( a.s + b.s, a.p + b.p, a.cr + b.cr, a.ci + b.ci );
}

//a / ( 1 - b ): the sum of a geometric series, the phase term complex.
Coef cOver( Coef a, Coef b )
{
	vec3 dr = 1.0 - b.cr;
	vec3 di = -b.ci;
	vec3 m  = dr * dr + di * di;
	return Coef( a.s / ( 1.0 - b.s ), a.p / ( 1.0 - b.p ), ( a.cr * dr + a.ci * di ) / m, ( a.ci * dr - a.cr * di ) / m );
}

//The most a ray could still contribute, whatever the polarisation of the
//light it meets: Q's larger eigenvalue.
float reach( Ray r )
{
	vec3 h = 0.5 * ( r.qs + r.qp );
	vec3 g = 0.5 * ( r.qs - r.qp );
	return maxOf( h + sqrt( g * g + r.qr * r.qr + r.qi * r.qi ) );
}

void push( Ray r )
{
	if( sp < kStack && reach( r ) > Prune )
		stack[ sp++ ] = r;
}

//A ray carried on past an interaction with s direction s (the same on both
//sides; p = s x k either side, Born & Wolf's convention). Q is rotated into
//( s, p ) about the ray, then weighed by the interaction. The --pyramid
//negative control forgets the polarisation every interaction but the last
//leaves -- v0.1.0's approximation.
Ray interact( Ray r, vec3 o, vec3 d, vec3 s, Coef b, int refl )
{
	vec3 qs = r.qs, qp = r.qp, qr = r.qr, qi = r.qi;
	if( ( Fault & kFaultLastOnly ) != 0 )
	{
		qs = 0.5 * ( r.qs + r.qp );
		qp = qs;
		qr = vec3( 0.0 );
		qi = vec3( 0.0 );
	}
	float c = 1.0, sn = 0.0;
	if( dot( r.e1, r.e1 ) > 0.0 )
	{
		c  = dot( s, r.e1 );
		sn = dot( s, cross( r.d, r.e1 ) );
	}
	vec3 ss  = c * c * qs + 2.0 * c * sn * qr + sn * sn * qp;
	vec3 pp  = sn * sn * qs - 2.0 * c * sn * qr + c * c * qp;
	vec3 spr = c * sn * ( qp - qs ) + ( c * c - sn * sn ) * qr;
	vec3 spi = qi;
	return Ray( o, d, b.s * ss, b.p * pp, b.cr * spr - b.ci * spi, b.cr * spi + b.ci * spr, s, refl );
}

//The nearest sheet beyond the ray's origin, or -1.
int nearestFace( vec3 o, vec3 d, out float tHit, out vec2 local )
{
	int best = -1;
	tHit     = 1e30;
	local    = vec2( 0.0 );
	for( int j = 0; j < 4; ++j )
	{
		if( j >= FaceCount )
			break;
		float dn = dot( d, FaceNormal[ j ] );
		if( abs( dn ) < 1e-9 )
			continue;
		float t = dot( FaceOrigin[ j ] - o, FaceNormal[ j ] ) / dn;
		if( t <= kEps || t >= tHit )
			continue;
		vec3 q  = o + d * t - FaceOrigin[ j ];
		float e = dot( q, FaceEdge[ j ] );
		float s = dot( q, FaceUp[ j ] );
		vec4 b  = FaceBounds[ j ];
		if( s < 0.0 || s > b.z || abs( e ) > b.x + b.y * s )
			continue;
		best  = j;
		tHit  = t;
		local = vec2( e, s );
	}
	return best;
}

//The sheet's normal where the ray met it: the declared normal, tilted by
//the gradient of the membrane's displacement inside the frame.
vec3 sheetNormal( int j, vec2 local )
{
	vec3 n = FaceNormal[ j ];
	if( Rippling == 0 )
		return n;
	vec4 b  = FaceBounds[ j ];
	float u = local.x / b.w + 0.5;
	float v = local.y / b.z;
	if( u <= 0.0 || u >= 1.0 || v <= 0.0 || v >= 1.0 )
		return n;
	float ge = 0.0;
	float gs = 0.0;
	for( int k = 0; k < 6; ++k )
	{
		float h  = ModeHeight[ 6 * j + k ];
		float km = float( kModeM[ k ] ) * kPi;
		float kn = float( kModeN[ k ] ) * kPi;
		ge += h * ( km / b.w ) * cos( km * u ) * sin( kn * v );
		gs += h * ( kn / b.z ) * sin( km * u ) * cos( kn * v );
	}
	return normalize( n - ge * FaceEdge[ j ] - gs * FaceUp[ j ] );
}
)"
R"(
//A bare surface, from air at cos(i), per channel: R (the same from either
//side, r' = -r), T, and the refraction's cosine.
void bare( float cosi, out Coef R, out Coef T, out vec3 cost )
{
	vec3 sint2 = vec3( max( 0.0, 1.0 - cosi * cosi ) ) / ( Eta * Eta );
	cost       = sqrt( max( vec3( 0.0 ), 1.0 - sint2 ) );
	vec3 rs    = ( cosi - Eta * cost ) / ( cosi + Eta * cost );
	vec3 rp    = ( Eta * cosi - cost ) / ( Eta * cosi + cost );
	if( ( Fault & kFaultRpCosSwap ) != 0 )
		rp = ( Eta * cost - cosi ) / ( Eta * cost + cosi );
	if( ( Fault & kFaultSwapSP ) != 0 )
	{
		vec3 x = rs;
		rs     = rp;
		rp     = x;
	}
	vec3 Ts = 1.0 - rs * rs;
	vec3 Tp = 1.0 - rp * rp;
	R       = Coef( rs * rs, rp * rp, rs * rp, vec3( 0.0 ) );
	T       = Coef( Ts, Tp, sqrt( Ts * Tp ), vec3( 0.0 ) );
}

//One row of the coat's table at cos(i), interpolated between its two
//nearest entries with exact weights.
vec3 coatRow( int row, float cosi )
{
	float x = clamp( cosi, 0.0, 1.0 ) * float( kCoatSize - 1 );
	int i   = min( int( floor( x ) ), kCoatSize - 2 );
	float f = x - float( i );
	return mix( texelFetch( CoatTable, ivec2( i, row ), 0 ).rgb, texelFetch( CoatTable, ivec2( i + 1, row ), 0 ).rgb, f );
}

//The coated surface: what it reflects from the air (R) and from inside the
//sheet (Ri), which differ when the coat absorbs, and what it transmits.
void coated( float cosi, out Coef R, out Coef Ri, out Coef T )
{
	R  = Coef( coatRow( 0, cosi ), coatRow( 1, cosi ), coatRow( 6, cosi ), coatRow( 7, cosi ) );
	Ri = Coef( coatRow( 2, cosi ), coatRow( 3, cosi ), coatRow( 8, cosi ), coatRow( 9, cosi ) );
	T  = Coef( coatRow( 4, cosi ), coatRow( 5, cosi ), coatRow( 10, cosi ), coatRow( 11, cosi ) );
	if( ( Fault & kFaultCoatSymmetric ) != 0 )
		Ri = R;
}

//What the screen emits at P, before the source's gain; false off the screen.
//The screen shows the picture over black (rgb x alpha, as a host composites
//it), and emits that signal as light.
bool screenAt( vec3 P, out vec3 L )
{
	vec4 r = ScreenRect;
	L      = vec3( 0.0 );
	if( P.x < r.x || P.x > r.y || P.z < r.z || P.z > r.w )
		return false;
	vec2 tuv;
	bool lit = true;
	if( ScreenMap == 0 )
		tuv = vec2( ( P.x - r.x ) / ( r.y - r.x ), ( P.z - r.z ) / ( r.w - r.z ) );
	else if( ScreenMap == 1 )
		tuv = vec2( ( P.x - r.x ) / ( r.y - r.x ), ( r.w - P.z ) / ( r.w - r.z ) );
	else if( ScreenMap == 3 )
		tuv = vec2( ( r.y - P.x ) / ( r.y - r.x ), ( r.w - P.z ) / ( r.w - r.z ) );
	else
		lit = feedLookup( ScreenMap == 4 ? vec2( -P.x, -P.z ) : vec2( P.x, -P.z ), tuv );
	if( ( Fault & kFaultMirrored ) != 0 )
		tuv.x = 1.0 - tuv.x;
	vec3 c = vec3( 0.0 );
	if( lit )
	{
		vec4 t = textureLod( TextureSrc, clamp( tuv, HalfTexelSrc, 1.0 - HalfTexelSrc ) * MaxUVSrc, 0.0 );
		c      = toLight( t.rgb * t.a );
	}
	L = SourceBlack + ( 1.0 - SourceBlack ) * c;
	return true;
}

//How the eye sees a unit source down this ray: a polarised source's field,
//made transverse, through Q; anything else, Q's mean.
vec3 seen( Ray r )
{
	vec3 mean = 0.5 * ( r.qs + r.qp );
	if( dot( SourceE, SourceE ) == 0.0 || dot( r.e1, r.e1 ) == 0.0 )
		return mean;
	float a  = dot( SourceE, r.e1 );
	float b  = dot( SourceE, cross( r.d, r.e1 ) );
	float n2 = a * a + b * b;
	if( n2 < 1e-12 )
		return mean;
	return ( a * a * r.qs + b * b * r.qp + 2.0 * a * b * r.qr ) / n2;
}

//One camera ray, through every sheet it meets. Light from the screen and
//the spill accumulates in `light`; the weight with which the eye sees the
//stage (Dest, sampled once at the pixel) in `destW`; the weight that
//reached the screen at all, for the alpha-over negative control, in `screenW`.
void trace( vec3 eye, vec3 dir, inout vec3 light, inout vec3 destW, inout float screenW )
{
	sp            = 0;
	stack[ sp++ ] = Ray( eye, dir, vec3( 1.0 ), vec3( 1.0 ), vec3( 0.0 ), vec3( 0.0 ), vec3( 0.0 ), 0 );
	for( int guard = 0; guard < kMaxRays && sp > 0; ++guard )
	{
		Ray r = stack[ --sp ];
		float tf;
		vec2 local;
		int j    = nearestFace( r.o, r.d, tf, local );
		float ts = r.d.y < 0.0 ? -r.o.y / r.d.y : 1e30;

		if( j >= 0 && tf < ts )
		{
			vec3 P     = r.o + r.d * tf;
			vec3 n     = sheetNormal( j, local );
			float ci   = dot( -r.d, n );
			vec3 nf    = ci < 0.0 ? -n : n;
			float cosi = abs( ci );

			Coef Rb, Tb;
			vec3 cost;
			bare( cosi, Rb, Tb, cost );
			//The sheet's own colour over the path it actually crosses.
			vec3 tau  = pow( Tint, 1.0 / max( cost, vec3( 1e-4 ) ) );
			vec3 tau2 = tau * tau;

			//The surface the ray meets first (F) and the far one (B), F also
			//as seen from inside. Bare, all are Rb and Tb; a coat is on the
			//side the declared normal points to.
			Coef RF = Rb, RFi = Rb, TF = Tb, RBi = Rb, TB = Tb;
			if( Coated != 0 )
			{
				Coef Rc, Rci, Tc;
				coated( cosi, Rc, Rci, Tc );
				if( ci > 0.0 )
				{
					RF  = Rc;
					RFi = Rci;
					TF  = Tc;
				}
				else
				{
					RBi = Rci;
					TB  = Tc;
				}
			}
			//One round trip inside the sheet, and the way in and out again.
			Coef echo = cScale( cMul( RFi, RBi ), tau2 );
			Coef TF2  = cMul( TF, TF );

			vec3 sdir = cross( r.d, nf );
			float sl  = length( sdir );
			sdir      = sl > 1e-6 ? sdir / sl : FaceEdge[ j ];

			//Transmitted: through both surfaces and the bulk, with every
			//internal echo summed in place; displaced by the sheet's
			//thickness, not deviated.
			Coef Tt = cOver( cScale( cMul( TF, TB ), tau ), echo );
			if( ( Fault & kFaultOneSurface ) != 0 )
				Tt = cScale( TF, tau );
			vec3 to = P - nf * kEps;
			if( Thickness > 0.0 )
			{
				vec3 dt = refract( r.d, nf, 1.0 / EtaRay );
				to      = P + dt * ( Thickness / dot( dt, -nf ) ) - nf * kEps;
			}
			push( interact( r, to, r.d, sdir, Tt, r.refl ) );

			//Reflected. The direction is the mirror's; the --geometry negative
			//control mirrors in a 45-degree pane instead.
			vec3 nr = ( Fault & kFaultFixed45 ) != 0 ? Normal45 : nf;
			vec3 rd = reflect( r.d, nr );
			if( Thickness <= 0.0 )
			{
				//A film: every image of the two surfaces in one place, the
				//series R_F + T_F^2 R_B tau^2 / (1 - R_F' R_B tau^2) summed.
				push( interact( r, P + nf * kEps, rd, sdir, cAdd( RF, cOver( cScale( cMul( TF2, RBi ), tau2 ), echo ) ), 1 ) );
			}
			else
			{
				//The front surface's image, untinted ...
				push( interact( r, P + nf * kEps, rd, sdir, RF, 1 ) );
				//... and the back surface's, each traced through the sheet:
				//refract in, cross, reflect off the back, cross, and leave
				//parallel to the front image wherever that puts it. The third
				//image's weight carries the rest of the series, so the sum is
				//exact.
				vec3 dt     = refract( r.d, nf, 1.0 / EtaRay );
				vec3 dr     = reflect( dt, nf );
				float depth = ( Fault & kFaultOnePass ) != 0 ? 0.5 * Thickness : Thickness;
				vec3 Pk     = P;
				Coef W      = cScale( cMul( TF2, RBi ), ( Fault & kFaultTintOnce ) != 0 ? tau : tau2 );
				for( int k = 1; k <= 3; ++k )
				{
					vec3 Pb = Pk + dt * ( depth / dot( dt, -nf ) );
					Pk      = Pb + dr * ( depth / dot( dr, nf ) );
					push( interact( r, Pk + nf * kEps, rd, sdir, k < 3 ? W : cOver( W, echo ), 1 ) );
					W = cMul( W, echo );
				}
			}
			continue;
		}

		//Past every sheet: the screen, the stage, or the dark.
		vec3 mean = 0.5 * ( r.qs + r.qp );
		//A pane's screen is hidden from direct view: the stage front.
		if( r.refl == 0 && RigKind == 0 )
		{
			destW += mean;
			continue;
		}
		if( ts < 1e29 )
		{
			vec3 L;
			if( screenAt( r.o + r.d * ts, L ) )
			{
				//The screen's luminance toward the ray, off its normal (+y).
				float fall = 1.0;
				if( Falloff > 0.0 && ( Fault & kFaultFalloffOff ) == 0 )
					fall = pow( max( -r.d.y, 0.0 ), Falloff );
				light += seen( r ) * L * ( SourceGain * fall );
				screenW += maxOf( mean );
				continue;
			}
		}
		if( r.refl != 0 )
			light += mean * Spill;
		else
			destW += mean;
	}
}
)"
R"(
//A rotated grid of four: no two samples share a row or a column.
const vec2 kSub[ 4 ] = vec2[ 4 ]( vec2( -0.125, -0.375 ), vec2( 0.375, -0.125 ), vec2( 0.125, 0.375 ), vec2( -0.375, 0.125 ) );

void main()
{
	ivec2 px = ivec2( floor( uv * vec2( OutSize ) ) );

	//The stage is sampled once, at the pixel's own centre: a thin sheet
	//does not deviate what is seen through it.
	vec2 duv  = ( vec2( px ) + 0.5 ) / vec2( OutSize );
	vec4 dest = textureLod( TextureDest, clamp( duv, HalfTexelDest, 1.0 - HalfTexelDest ) * MaxUVDest, 0.0 );

	vec3 light    = vec3( 0.0 );
	vec3 destW    = vec3( 0.0 );
	float screenW = 0.0;
	for( int q = 0; q < 4; ++q )
	{
		vec2 ndc = ( vec2( px ) + 0.5 + kSub[ q ] ) / vec2( OutSize ) * 2.0 - 1.0;
		vec3 dir = normalize( Forward + CamRight * ( ndc.x * TanHalf.x ) + CamUp * ( ndc.y * TanHalf.y ) );
		trace( Eye, dir, light, destW, screenW );
	}
	light *= 0.25;
	destW *= 0.25;
	screenW *= 0.25;

	vec3 stage = toLight( dest.rgb * dest.a ) * StageGain;
	vec3 P     = destW * stage + light;
	if( ( Fault & kFaultAlphaOver ) != 0 )
		P = destW * stage * max( 0.0, 1.0 - 8.0 * screenW ) + light;
	if( ( Fault & kFaultCrossfade ) != 0 )
		P = mix( stage, P, Opacity );

	//Light clips at white, channel by channel, as a camera's would.
	vec3 shown = toSignal( min( P, vec3( 1.0 ) ) );
	//Opaque wherever the stage is; a ghost over a transparent layer carries
	//its own alpha, so that over black it is exactly this signal.
	float a   = clamp( max( dest.a, maxOf( shown ) ), 0.0, 1.0 );
	fragColor = a > 0.0 ? vec4( shown / a, a ) : vec4( 0.0 );
}
)";

std::string AssembleFragment( const char* body )
{
	return std::string( kVersion ) + kCommon + body;
}

std::string AssembleVertex()
{
	return std::string( kVersion ) + kVertex;
}

} // namespace pepper
