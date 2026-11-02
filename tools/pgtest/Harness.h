#pragma once

/**
    pgtest's plumbing: assertions, a PNG writer, the test cards, a headless
    GL context, the two rigs (the mixer with two inputs, the feed with one)
    and the harness's OWN model of the optics, in double.

    The model is written from Rig.h's description of where everything is
    and from the textbook -- Fresnel, Snell, a mirror image -- and never
    calls rig::Build, so a prediction made with it does not inherit a fault
    in the code it is checking. It reads what the harness ASKED for (the
    parameters it set, through Controls.h), never SetupForTest().

    One translation unit: main.cpp includes this once.
*/

#include "Controls.h"
#include "Feed.h"
#include "Names.h"
#include "Pepper.h"
#include "Rig.h"
#include "Shaders.h"

#include <OpenGL/OpenGL.h>
#include <OpenGL/gl3.h>
#include <zlib.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <functional>
#include <map>
#include <sstream>
#include <string>
#include <unistd.h>
#include <vector>

using namespace pepper;

namespace
{
int failures = 0;
bool quiet   = false;

void Check( bool ok, const std::string& what )
{
	if( !quiet )
		std::printf( "  %s  %s\n", ok ? "ok  " : "FAIL", what.c_str() );
	if( !ok )
		++failures;
}

/// A negative control: something the check must be able to REJECT. Passes
/// when `rejected` is true.
void Negative( bool rejected, const std::string& what )
{
	if( !quiet )
		std::printf( "  %s  negative control: %s\n", rejected ? "ok  " : "FAIL", what.c_str() );
	if( !rejected )
		++failures;
}

/// EVERY conversion must be a floating-point one.
__attribute__( ( format( printf, 1, 0 ) ) ) std::string fmt( const char* format, double a, double b = 0.0, double c = 0.0, double d = 0.0 )
{
	char buffer[ 320 ];
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-nonliteral"
	std::snprintf( buffer, sizeof( buffer ), format, a, b, c, d );
#pragma clang diagnostic pop
	return buffer;
}

/// Run a check silently and return how many of its assertions failed,
/// leaving the global count as it was: how a negative control asks "did the
/// check reject this?".
template< typename F >
int failuresOf( F&& run )
{
	const int before = failures;
	quiet            = true;
	run();
	quiet            = false;
	const int caught = failures - before;
	failures         = before;
	return caught;
}

constexpr double kFloatUlp = 1.0 / 16777216.0;///< 2^-24, float32's unit roundoff at 1
constexpr double kDegree   = kPi / 180.0;

/// The light-level tolerance for a value the shader computes in float: a
/// bound of 128 operations at 3 ULP each (GLSL lets division and sqrt be
/// that loose), and the tolerance a little over three times it.
constexpr double kLightBound = 128.0 * 3.0 * kFloatUlp;
constexpr double kLightTol   = 1e-4;
static_assert( kLightTol >= 3.0 * kLightBound, "the light tolerance must be at least three times its bound" );

//---------------------------------------------------------------------------
// PNG writer. zlib ships with the OS. Rows BOTTOM-UP in, top-down out.
//---------------------------------------------------------------------------
using Image  = std::vector< unsigned char >;
using ImageF = std::vector< float >;

void putU32( std::vector< unsigned char >& out, uint32_t value )
{
	out.push_back( static_cast< unsigned char >( value >> 24 ) );
	out.push_back( static_cast< unsigned char >( value >> 16 ) );
	out.push_back( static_cast< unsigned char >( value >> 8 ) );
	out.push_back( static_cast< unsigned char >( value ) );
}

void putChunk( std::vector< unsigned char >& out, const char* type, const std::vector< unsigned char >& data )
{
	putU32( out, static_cast< uint32_t >( data.size() ) );
	const size_t start = out.size();
	out.insert( out.end(), type, type + 4 );
	out.insert( out.end(), data.begin(), data.end() );
	uLong crc = crc32( 0L, Z_NULL, 0 );
	crc       = crc32( crc, out.data() + start, static_cast< uInt >( 4 + data.size() ) );
	putU32( out, static_cast< uint32_t >( crc ) );
}

bool writePng( const std::string& path, int width, int height, const Image& bottomUp )
{
	std::vector< unsigned char > raw;
	raw.reserve( static_cast< size_t >( height ) * ( 1 + static_cast< size_t >( width ) * 4 ) );
	for( int y = height - 1; y >= 0; --y )
	{
		raw.push_back( 0 );
		const unsigned char* row = bottomUp.data() + static_cast< size_t >( y ) * width * 4;
		raw.insert( raw.end(), row, row + static_cast< size_t >( width ) * 4 );
	}
	uLongf compressedSize = compressBound( static_cast< uLong >( raw.size() ) );
	std::vector< unsigned char > compressed( compressedSize );
	if( compress2( compressed.data(), &compressedSize, raw.data(), static_cast< uLong >( raw.size() ), 6 ) != Z_OK )
		return false;
	compressed.resize( compressedSize );

	std::vector< unsigned char > png = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
	std::vector< unsigned char > ihdr;
	putU32( ihdr, static_cast< uint32_t >( width ) );
	putU32( ihdr, static_cast< uint32_t >( height ) );
	ihdr.push_back( 8 );
	ihdr.push_back( 6 );
	ihdr.push_back( 0 );
	ihdr.push_back( 0 );
	ihdr.push_back( 0 );
	putChunk( png, "IHDR", ihdr );
	putChunk( png, "IDAT", compressed );
	putChunk( png, "IEND", {} );

	FILE* file = fopen( path.c_str(), "wb" );
	if( file == nullptr )
		return false;
	const size_t written = fwrite( png.data(), 1, png.size(), file );
	fclose( file );
	return written == png.size();
}

//---------------------------------------------------------------------------
// Pictures. Bottom-up, GL's orientation: row 0 is the BOTTOM, so a card's
// v runs up the picture exactly as the plugin's does.
//---------------------------------------------------------------------------
struct Rgb
{
	unsigned char r, g, b;
};

unsigned char toByte( double v )
{
	return static_cast< unsigned char >( std::lround( std::clamp( v, 0.0, 1.0 ) * 255.0 ) );
}

constexpr Rgb kBlack = { 0, 0, 0 };
constexpr Rgb kWhite = { 255, 255, 255 };

void put( Image& image, int width, int x, int y, Rgb c, unsigned char a = 255 )
{
	const size_t at = ( static_cast< size_t >( y ) * width + x ) * 4;
	image[ at + 0 ] = c.r;
	image[ at + 1 ] = c.g;
	image[ at + 2 ] = c.b;
	image[ at + 3 ] = a;
}

Rgb pixelAt( const Image& image, int width, int x, int y )
{
	const size_t at = ( static_cast< size_t >( y ) * width + x ) * 4;
	return { image[ at + 0 ], image[ at + 1 ], image[ at + 2 ] };
}

int l1( Rgb a, Rgb b )
{
	return std::abs( a.r - b.r ) + std::abs( a.g - b.g ) + std::abs( a.b - b.b );
}

Image flatField( int width, int height, Rgb c, unsigned char alpha = 255 )
{
	Image image( static_cast< size_t >( width ) * height * 4 );
	for( int y = 0; y < height; ++y )
		for( int x = 0; x < width; ++x )
			put( image, width, x, y, c, alpha );
	return image;
}

void hsv( double h, double s, double v, double& r, double& g, double& b )
{
	const double c = v * s;
	const double x = c * ( 1.0 - std::fabs( std::fmod( h * 6.0, 2.0 ) - 1.0 ) );
	const double m = v - c;
	double rr = 0, gg = 0, bb = 0;
	switch( static_cast< int >( h * 6.0 ) % 6 )
	{
	case 0: rr = c; gg = x; break;
	case 1: rr = x; gg = c; break;
	case 2: gg = c; bb = x; break;
	case 3: gg = x; bb = c; break;
	case 4: rr = x; bb = c; break;
	default: rr = c; bb = x; break;
	}
	r = rr + m;
	g = gg + m;
	b = bb + m;
}

/// Bars across the top, a grey ramp, a hue field, and a translucent strip
/// down the right: Resolume's demo clips are DXV with alpha, and a plugin
/// that mishandles alpha looks fine on an opaque card.
Image videoCard( int width, int height )
{
	Image image( static_cast< size_t >( width ) * height * 4 );
	for( int y = 0; y < height; ++y )
		for( int x = 0; x < width; ++x )
		{
			const double u = ( x + 0.5 ) / width, v = ( y + 0.5 ) / height;
			double r = 0, g = 0, b = 0;
			if( v > 0.72 )
			{
				static const double bars[ 7 ][ 3 ] = {
					{ 0.75, 0.75, 0.75 }, { 0.75, 0.75, 0.0 }, { 0.0, 0.75, 0.75 }, { 0.0, 0.75, 0.0 },
					{ 0.75, 0.0, 0.75 }, { 0.75, 0.0, 0.0 }, { 0.0, 0.0, 0.75 },
				};
				const int which = std::min( 6, static_cast< int >( u * 7.0 ) );
				r = bars[ which ][ 0 ];
				g = bars[ which ][ 1 ];
				b = bars[ which ][ 2 ];
			}
			else if( v > 0.58 )
				r = g = b = u;
			else
				hsv( u, 0.8, 0.3 + 0.65 * ( v / 0.58 ), r, g, b );
			put( image, width, x, y, { toByte( r ), toByte( g ), toByte( b ) }, u > 0.9 ? 96 : 255 );
		}
	return image;
}

/// Warm stripes, a dark panel and a half-transparent disc.
Image graphicCard( int width, int height )
{
	Image image( static_cast< size_t >( width ) * height * 4 );
	const double cx = 0.62 * width, cy = 0.45 * height, rad = 0.22 * height;
	for( int y = 0; y < height; ++y )
		for( int x = 0; x < width; ++x )
		{
			const double u = ( x + 0.5 ) / width, v = ( y + 0.5 ) / height;
			unsigned char alpha = 255;
			const int stripe    = static_cast< int >( std::floor( ( u * 1.6 + v ) * 8.0 ) ) & 1;
			Rgb c               = stripe ? Rgb { 0xF2, 0x8C, 0x28 } : Rgb { 0xF7, 0xC5, 0x6B };
			if( u < 0.3 && v > 0.15 && v < 0.85 )
				c = { 0x22, 0x1E, 0x38 };
			const double dx = ( x + 0.5 ) - cx, dy = ( y + 0.5 ) - cy;
			if( dx * dx + dy * dy < rad * rad )
			{
				c     = { 0x2E, 0x8B, 0xC0 };
				alpha = 128;
			}
			put( image, width, x, y, c, alpha );
		}
	return image;
}

/// Twelve vertical bars of distinct red, for the pipe's cue check.
Image barsCard( int width, int height )
{
	Image image( static_cast< size_t >( width ) * height * 4 );
	for( int y = 0; y < height; ++y )
		for( int x = 0; x < width; ++x )
		{
			const int j = std::min( 11, ( x * 12 ) / width );
			put( image, width, x, y, { static_cast< unsigned char >( 16 + 20 * j ), 90, static_cast< unsigned char >( 200 - 12 * j ) } );
		}
	return image;
}

/// Four quadrants of flat colour and one marker square, for `--mixer`.
struct QuadCard
{
	Image image;
	Rgb quadrant[ 4 ];
	Rgb marker;
	double markerU = 0.0, markerV = 0.0;
	int x0 = 0, x1 = 0, y0 = 0, y1 = 0;
};

QuadCard quadCard( int width, int height, bool srcSide )
{
	QuadCard card;
	if( srcSide )
	{
		card.quadrant[ 0 ] = { 0x00, 0xB4, 0xB4 };
		card.quadrant[ 1 ] = { 0xB4, 0xB4, 0x00 };
		card.quadrant[ 2 ] = { 0x3C, 0x78, 0xFF };
		card.quadrant[ 3 ] = { 0x60, 0x60, 0x60 };
		card.marker        = { 0xA0, 0xFF, 0x00 };
	}
	else
	{
		card.quadrant[ 0 ] = { 0xC8, 0x00, 0x00 };
		card.quadrant[ 1 ] = { 0x00, 0xC8, 0x00 };
		card.quadrant[ 2 ] = { 0x00, 0x00, 0xC8 };
		card.quadrant[ 3 ] = { 0xF0, 0xE0, 0xC0 };
		card.marker        = { 0xFF, 0x80, 0x00 };
	}
	card.image = Image( static_cast< size_t >( width ) * height * 4 );
	for( int y = 0; y < height; ++y )
		for( int x = 0; x < width; ++x )
			put( card.image, width, x, y, card.quadrant[ ( y >= height / 2 ? 2 : 0 ) + ( x >= width / 2 ? 1 : 0 ) ] );
	card.x0 = static_cast< int >( std::lround( 0.1875 * width ) );
	card.x1 = static_cast< int >( std::lround( 0.3125 * width ) );
	card.y0 = static_cast< int >( std::lround( 0.1875 * height ) );
	card.y1 = static_cast< int >( std::lround( 0.3125 * height ) );
	for( int y = card.y0; y < card.y1; ++y )
		for( int x = card.x0; x < card.x1; ++x )
			put( card.image, width, x, y, card.marker );
	card.markerU = ( 0.5 * ( card.x0 + card.x1 ) ) / width;
	card.markerV = ( 0.5 * ( card.y0 + card.y1 ) ) / height;
	return card;
}

/// White squares on black at stated (u, v) centres, `side` texels across:
/// what every geometric check measures. The default set is asymmetric, so
/// a mirror image is not the picture.
struct Dot
{
	double u, v;
};

const std::vector< Dot > kDots = { { 0.18, 0.22 }, { 0.80, 0.20 }, { 0.20, 0.80 }, { 0.82, 0.78 }, { 0.55, 0.62 } };
const std::vector< Dot > kTriangle = { { 0.28, 0.30 }, { 0.72, 0.36 }, { 0.36, 0.74 } };

Image dotsCard( int width, int height, const std::vector< Dot >& dots, int side )
{
	Image image = flatField( width, height, kBlack );
	for( const Dot& d : dots )
	{
		const int cx = static_cast< int >( std::lround( d.u * width ) );
		const int cy = static_cast< int >( std::lround( d.v * height ) );
		for( int y = cy - side / 2; y < cy - side / 2 + side; ++y )
			for( int x = cx - side / 2; x < cx - side / 2 + side; ++x )
				if( x >= 0 && x < width && y >= 0 && y < height )
					put( image, width, x, y, kWhite );
	}
	return image;
}

/// Where dotsCard actually put a dot's centre, in uv: the integer rounding
/// of the card, so a prediction aims at the dot that was drawn.
Dot dotCentre( const Dot& d, int width, int height, int side )
{
	const int cx = static_cast< int >( std::lround( d.u * width ) );
	const int cy = static_cast< int >( std::lround( d.v * height ) );
	return { ( cx - side / 2 + 0.5 * side ) / width, ( cy - side / 2 + 0.5 * side ) / height };
}

/// White above v = edge, black below, cut on a texel boundary.
Image edgeCard( int width, int height, int edgeRow, bool whiteAbove )
{
	Image image( static_cast< size_t >( width ) * height * 4 );
	for( int y = 0; y < height; ++y )
		for( int x = 0; x < width; ++x )
			put( image, width, x, y, ( ( y >= edgeRow ) == whiteAbove ) ? kWhite : kBlack );
	return image;
}

//---------------------------------------------------------------------------
// The two pictures the figures are made of: a stage (the layer below) and a
// figure (the hidden screen). Procedural, so a render is reproducible from
// the command line alone.
//---------------------------------------------------------------------------
double smoothstep( double a, double b, double x )
{
	const double t = std::clamp( ( x - a ) / ( b - a ), 0.0, 1.0 );
	return t * t * ( 3.0 - 2.0 * t );
}

double capsule( double px, double py, double ax, double ay, double bx, double by, double r )
{
	const double vx = bx - ax, vy = by - ay;
	const double t  = std::clamp( ( ( px - ax ) * vx + ( py - ay ) * vy ) / ( vx * vx + vy * vy ), 0.0, 1.0 );
	const double dx = px - ax - t * vx, dy = py - ay - t * vy;
	return std::sqrt( dx * dx + dy * dy ) - r;
}

/// A dark stage: a back wall in deep blue with three warm uplights, a
/// floor, a lectern. Mostly dark, as a real one is behind a ghost.
Image stageCard( int width, int height )
{
	Image image( static_cast< size_t >( width ) * height * 4 );
	const double aspect = static_cast< double >( width ) / height;
	for( int y = 0; y < height; ++y )
		for( int x = 0; x < width; ++x )
		{
			const double u = ( x + 0.5 ) / width, v = ( y + 0.5 ) / height;
			const double px = ( u - 0.5 ) * aspect;
			double r = 0.02, g = 0.025, b = 0.06;
			if( v > 0.22 )
			{
				//The wall, lit from below by three fixtures.
				for( int k = 0; k < 3; ++k )
				{
					const double cx = ( k - 1 ) * 0.62 * aspect / 1.7;
					const double dx = px - cx, h = v - 0.22;
					const double beam = std::exp( -dx * dx / ( 0.004 + 0.06 * h * h ) ) * std::exp( -h * 1.8 );
					r += 0.55 * beam;
					g += 0.30 * beam;
					b += 0.12 * beam;
				}
				b += 0.06 * ( 1.0 - v );
			}
			else
			{
				//The floor: a dark gloss with the beams' pools.
				const double depth = 0.22 - v;
				r = 0.03 + 0.05 * std::exp( -depth * 10.0 );
				g = 0.03 + 0.035 * std::exp( -depth * 10.0 );
				b = 0.05 + 0.02 * std::exp( -depth * 10.0 );
			}
			//A lectern, stage left.
			if( std::fabs( px + 0.42 * aspect ) < 0.05 && v > 0.12 && v < 0.36 )
			{
				r = 0.10;
				g = 0.07;
				b = 0.05;
			}
			put( image, width, x, y, { toByte( r ), toByte( g ), toByte( b ) } );
		}
	return image;
}

/// A figure: head, shoulders, an arm raised, in pale light on black -- the
/// classic ghost. Soft-edged, as a lit performer is.
Image figureCard( int width, int height )
{
	Image image( static_cast< size_t >( width ) * height * 4 );
	const double aspect = static_cast< double >( width ) / height;
	for( int y = 0; y < height; ++y )
		for( int x = 0; x < width; ++x )
		{
			const double u = ( x + 0.5 ) / width, v = ( y + 0.5 ) / height;
			const double px = ( u - 0.5 ) * aspect, py = v;
			double d = 1e9;
			d        = std::min( d, std::hypot( px, py - 0.83 ) - 0.075 );         //head
			d        = std::min( d, capsule( px, py, 0.0, 0.70, 0.0, 0.40, 0.11 ) );//torso
			d        = std::min( d, capsule( px, py, -0.05, 0.42, -0.08, 0.04, 0.05 ) );
			d        = std::min( d, capsule( px, py, 0.05, 0.42, 0.09, 0.04, 0.05 ) );
			d        = std::min( d, capsule( px, py, -0.10, 0.66, -0.22, 0.48, 0.04 ) );//arm down
			d        = std::min( d, capsule( px, py, 0.10, 0.66, 0.22, 0.80, 0.04 ) );  //arm up
			d        = std::min( d, capsule( px, py, 0.22, 0.80, 0.27, 0.95, 0.035 ) );
			const double body = 1.0 - smoothstep( -0.006, 0.006, d );
			const double rim  = std::exp( -std::max( 0.0, -d ) * 22.0 ) * body;
			const double lit  = body * ( 0.55 + 0.35 * v ) + 0.25 * rim;
			const double r = lit * 0.78, g = lit * 0.90, b = lit * 1.0;
			put( image, width, x, y, { toByte( r ), toByte( g ), toByte( b ) } );
		}
	return image;
}

Image generate( const std::string& name, int width, int height )
{
	if( name == "video" )
		return videoCard( width, height );
	if( name == "graphic" )
		return graphicCard( width, height );
	if( name == "quads-a" )
		return quadCard( width, height, false ).image;
	if( name == "quads-b" )
		return quadCard( width, height, true ).image;
	if( name == "bars" )
		return barsCard( width, height );
	if( name == "dots" )
		return dotsCard( width, height, kDots, std::max( 4, height / 40 ) );
	if( name == "stage" )
		return stageCard( width, height );
	if( name == "figure" )
		return figureCard( width, height );
	if( name == "black" )
		return flatField( width, height, kBlack );
	if( name == "white" )
		return flatField( width, height, kWhite );
	return flatField( width, height, { 0x00, 0x99, 0x00 } );//"flat"
}

const char* const kCardNames = "video | graphic | quads-a | quads-b | bars | dots | stage | figure | black | white | flat";

bool isCard( const std::string& name )
{
	static const char* names[] = { "video", "graphic", "quads-a", "quads-b", "bars", "dots", "stage", "figure", "black", "white", "flat" };
	for( const char* n : names )
		if( name == n )
			return true;
	return false;
}

//---------------------------------------------------------------------------
// GL plumbing.
//---------------------------------------------------------------------------
bool softwareRenderer()
{
	const char* renderer = std::getenv( "PGTEST_RENDERER" );
	return renderer != nullptr && std::strcmp( renderer, "software" ) == 0;
}

CGLContextObj createContext()
{
	const CGLPixelFormatAttribute accelerated[] = {
		kCGLPFAOpenGLProfile, static_cast< CGLPixelFormatAttribute >( kCGLOGLPVersion_GL4_Core ),
		kCGLPFAAccelerated,
		kCGLPFAColorSize, static_cast< CGLPixelFormatAttribute >( 24 ),
		kCGLPFAAlphaSize, static_cast< CGLPixelFormatAttribute >( 8 ),
		static_cast< CGLPixelFormatAttribute >( 0 )
	};
	const CGLPixelFormatAttribute software[] = {
		kCGLPFAOpenGLProfile, static_cast< CGLPixelFormatAttribute >( kCGLOGLPVersion_GL4_Core ),
		kCGLPFAColorSize, static_cast< CGLPixelFormatAttribute >( 24 ),
		kCGLPFAAlphaSize, static_cast< CGLPixelFormatAttribute >( 8 ),
		static_cast< CGLPixelFormatAttribute >( 0 )
	};
	//PGTEST_RENDERER=software asks for Apple's software renderer by id, on a
	//Mac that has a GPU: what a GPU-less runner falls back to.
	const CGLPixelFormatAttribute generic[] = {
		kCGLPFAOpenGLProfile, static_cast< CGLPixelFormatAttribute >( kCGLOGLPVersion_GL4_Core ),
		kCGLPFARendererID, static_cast< CGLPixelFormatAttribute >( kCGLRendererGenericFloatID ),
		kCGLPFAColorSize, static_cast< CGLPixelFormatAttribute >( 24 ),
		kCGLPFAAlphaSize, static_cast< CGLPixelFormatAttribute >( 8 ),
		static_cast< CGLPixelFormatAttribute >( 0 )
	};
	CGLPixelFormatObj format = nullptr;
	GLint formatCount        = 0;
	if( softwareRenderer() )
	{
		if( CGLChoosePixelFormat( generic, &format, &formatCount ) != kCGLNoError || format == nullptr )
			return nullptr;
		std::fprintf( stderr, "pgtest: PGTEST_RENDERER=software, Apple's software renderer\n" );
	}
	else if( CGLChoosePixelFormat( accelerated, &format, &formatCount ) != kCGLNoError || format == nullptr )
		if( CGLChoosePixelFormat( software, &format, &formatCount ) != kCGLNoError || format == nullptr )
			return nullptr;
	CGLContextObj context = nullptr;
	const CGLError error  = CGLCreateContext( format, nullptr, &context );
	CGLDestroyPixelFormat( format );
	if( error != kCGLNoError )
		return nullptr;
	CGLSetCurrentContext( context );
	return context;
}

GLuint makeTexture( int width, int height, const unsigned char* pixels, bool floatFormat = false )
{
	GLuint texture = 0;
	glGenTextures( 1, &texture );
	glBindTexture( GL_TEXTURE_2D, texture );
	if( floatFormat )
		glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA32F, width, height, 0, GL_RGBA, GL_FLOAT, nullptr );
	else
		glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
	glBindTexture( GL_TEXTURE_2D, 0 );
	return texture;
}

/// Padding is a sentinel colour that appears nowhere else.
constexpr Rgb kSentinel     = { 0xFF, 0x00, 0xFF };
constexpr int kSentinelNear = 135;

struct InputSpec
{
	int usedW = 0, usedH = 0;
	int hwW = 0, hwH = 0;
	static InputSpec Exact( int w, int h )
	{
		return { w, h, w, h };
	}
	static InputSpec Padded( int w, int h, int hw, int hh )
	{
		return { w, h, hw, hh };
	}
};

/// Set a parameter of either plugin by display name, in host units.
template< typename P >
bool setByName( P& plugin, unsigned count, const std::string& name, float value )
{
	for( unsigned int i = 0; i < count; ++i )
	{
		const char* declared = plugin.GetParamName( i );
		if( declared != nullptr && name == declared )
		{
			plugin.SetFloatParameter( i, value );
			return true;
		}
	}
	std::fprintf( stderr, "no parameter called '%s'\n", name.c_str() );
	return false;
}

/// The output framebuffer, its texture, and reading it back.
struct Target
{
	int width = 0, height = 0;
	GLuint texture = 0, fbo = 0;

	void Init( int w, int h, bool floatOutput )
	{
		width   = w;
		height  = h;
		texture = makeTexture( w, h, nullptr, floatOutput );
		glGenFramebuffers( 1, &fbo );
		glBindFramebuffer( GL_FRAMEBUFFER, fbo );
		glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0 );
	}

	void Begin()
	{
		glBindFramebuffer( GL_FRAMEBUFFER, fbo );
		glViewport( 0, 0, width, height );
		glClearColor( 0.0f, 0.0f, 0.0f, 0.0f );
		glClear( GL_COLOR_BUFFER_BIT );
	}

	Image Pixels()
	{
		Image pixels( static_cast< size_t >( width ) * height * 4 );
		glBindFramebuffer( GL_FRAMEBUFFER, fbo );
		glPixelStorei( GL_PACK_ALIGNMENT, 1 );
		glReadPixels( 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data() );
		return pixels;
	}

	ImageF PixelsF()
	{
		ImageF pixels( static_cast< size_t >( width ) * height * 4 );
		glBindFramebuffer( GL_FRAMEBUFFER, fbo );
		glPixelStorei( GL_PACK_ALIGNMENT, 1 );
		glReadPixels( 0, 0, width, height, GL_RGBA, GL_FLOAT, pixels.data() );
		return pixels;
	}

	void Release()
	{
		if( fbo )
			glDeleteFramebuffers( 1, &fbo );
		if( texture )
			glDeleteTextures( 1, &texture );
		fbo = texture = 0;
	}
};

void upload( GLuint texture, int w, int h, const Image& bottomUp )
{
	glBindTexture( GL_TEXTURE_2D, texture );
	glTexSubImage2D( GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, bottomUp.data() );
	glBindTexture( GL_TEXTURE_2D, 0 );
}

//---------------------------------------------------------------------------
// The mixer's rig: SW Pepper, TWO input textures and an output, each at its
// own size. inputTextures[0] is Dest (the stage, `--input-a`), [1] is Src
// (the hidden screen, `--input-b`).
//---------------------------------------------------------------------------
struct MixRig
{
	Pepper plugin;
	Target out;
	InputSpec destSpec, srcSpec;
	GLuint destTexture = 0, srcTexture = 0;
	FFGLTextureStruct destStruct = {}, srcStruct = {};
	FFGLTextureStruct* inputs[ 2 ] = { nullptr, nullptr };
	ProcessOpenGLStruct process    = {};
	bool ready                     = false;
	bool primed                    = false;
	double seconds                 = 0.0;

	bool Init( int outW, int outH, InputSpec dest, InputSpec src, bool floatOutput = false, int fault = 0, const char* fragment = nullptr )
	{
		destSpec = dest;
		srcSpec  = src;
		plugin.SetFaultForTest( fault );
		plugin.SetFragmentShaderForTest( fragment );
		plugin.SetClockScaleForTest( 1.0 );

		FFGLViewportStruct viewport = {};
		viewport.width              = static_cast< FFUInt32 >( outW );
		viewport.height             = static_cast< FFUInt32 >( outH );
		if( plugin.InitGL( &viewport ) != FF_SUCCESS )
		{
			std::fprintf( stderr, "InitGL failed -- see the diagnostics log for the shader\n" );
			return false;
		}

		destTexture = makeTexture( dest.hwW, dest.hwH, flatField( dest.hwW, dest.hwH, kSentinel ).data() );
		srcTexture  = makeTexture( src.hwW, src.hwH, flatField( src.hwW, src.hwH, kSentinel ).data() );
		out.Init( outW, outH, floatOutput );

		auto fill = []( FFGLTextureStruct& t, const InputSpec& s, GLuint handle ) {
			t.Width          = static_cast< FFUInt32 >( s.usedW );
			t.Height         = static_cast< FFUInt32 >( s.usedH );
			t.HardwareWidth  = static_cast< FFUInt32 >( s.hwW );
			t.HardwareHeight = static_cast< FFUInt32 >( s.hwH );
			t.Handle         = handle;
		};
		fill( destStruct, dest, destTexture );
		fill( srcStruct, src, srcTexture );
		inputs[ 0 ]              = &destStruct;
		inputs[ 1 ]              = &srcStruct;
		process.numInputTextures = 2;
		process.inputTextures    = inputs;
		process.HostFBO          = out.fbo;
		ready                    = true;
		return true;
	}

	bool Init( int outW, int outH, bool floatOutput = false, int fault = 0, const char* fragment = nullptr )
	{
		return Init( outW, outH, InputSpec::Exact( outW, outH ), InputSpec::Exact( outW, outH ), floatOutput, fault, fragment );
	}

	void UploadDest( const Image& bottomUp )
	{
		upload( destTexture, destSpec.usedW, destSpec.usedH, bottomUp );
	}

	void UploadSrc( const Image& bottomUp )
	{
		upload( srcTexture, srcSpec.usedW, srcSpec.usedH, bottomUp );
	}

	bool Set( const std::string& name, float value )
	{
		return setByName( plugin, Pepper::PT_COUNT, name, value );
	}

	/// The host's SetTime, in seconds (the harness sets the clock's unit).
	/// The plugin's clock counts from the first time it is told, so the
	/// first render is primed with a frame at 0: `seconds` is then absolute.
	bool Render()
	{
		if( !primed && seconds != 0.0 )
		{
			plugin.SetTime( 0.0 );
			out.Begin();
			plugin.ProcessOpenGL( &process );
		}
		primed = true;
		plugin.SetTime( seconds );
		out.Begin();
		return plugin.ProcessOpenGL( &process ) == FF_SUCCESS;
	}

	FFResult RenderBroken( int numInputs, int nullIndex, bool zeroSize = false )
	{
		FFGLTextureStruct* saved[ 2 ] = { inputs[ 0 ], inputs[ 1 ] };
		const FFGLTextureStruct savedDest = destStruct;
		if( nullIndex >= 0 && nullIndex < 2 )
			inputs[ nullIndex ] = nullptr;
		if( nullIndex == -2 )
			process.inputTextures = nullptr;
		if( zeroSize )
			destStruct.HardwareWidth = 0;
		process.numInputTextures = static_cast< FFUInt32 >( numInputs );
		out.Begin();
		const FFResult result    = plugin.ProcessOpenGL( &process );
		inputs[ 0 ]              = saved[ 0 ];
		inputs[ 1 ]              = saved[ 1 ];
		destStruct               = savedDest;
		process.inputTextures    = inputs;
		process.numInputTextures = 2;
		return result;
	}

	Image Pixels()
	{
		return out.Pixels();
	}

	ImageF PixelsF()
	{
		return out.PixelsF();
	}

	~MixRig()
	{
		if( !ready )
			return;
		plugin.DeInitGL();
		out.Release();
		glDeleteTextures( 1, &srcTexture );
		glDeleteTextures( 1, &destTexture );
	}
};

//---------------------------------------------------------------------------
// The feed's rig: SW Pepper Feed, one input.
//---------------------------------------------------------------------------
struct FeedRig
{
	Feed plugin;
	Target out;
	InputSpec spec;
	GLuint texture = 0;
	FFGLTextureStruct inStruct = {};
	FFGLTextureStruct* inputs[ 1 ] = { nullptr };
	ProcessOpenGLStruct process    = {};
	bool ready                     = false;

	bool Init( int outW, int outH, InputSpec in, bool floatOutput = false, int fault = 0 )
	{
		spec = in;
		plugin.SetFaultForTest( fault );
		FFGLViewportStruct viewport = {};
		viewport.width              = static_cast< FFUInt32 >( outW );
		viewport.height             = static_cast< FFUInt32 >( outH );
		if( plugin.InitGL( &viewport ) != FF_SUCCESS )
		{
			std::fprintf( stderr, "Feed InitGL failed -- see the diagnostics log for the shader\n" );
			return false;
		}
		texture = makeTexture( in.hwW, in.hwH, flatField( in.hwW, in.hwH, kSentinel ).data() );
		out.Init( outW, outH, floatOutput );
		inStruct.Width           = static_cast< FFUInt32 >( in.usedW );
		inStruct.Height          = static_cast< FFUInt32 >( in.usedH );
		inStruct.HardwareWidth   = static_cast< FFUInt32 >( in.hwW );
		inStruct.HardwareHeight  = static_cast< FFUInt32 >( in.hwH );
		inStruct.Handle          = texture;
		inputs[ 0 ]              = &inStruct;
		process.numInputTextures = 1;
		process.inputTextures    = inputs;
		process.HostFBO          = out.fbo;
		ready                    = true;
		return true;
	}

	void Upload( const Image& bottomUp )
	{
		upload( texture, spec.usedW, spec.usedH, bottomUp );
	}

	bool Set( const std::string& name, float value )
	{
		return setByName( plugin, Feed::PT_COUNT, name, value );
	}

	bool Render()
	{
		out.Begin();
		return plugin.ProcessOpenGL( &process ) == FF_SUCCESS;
	}

	~FeedRig()
	{
		if( !ready )
			return;
		plugin.DeInitGL();
		out.Release();
		glDeleteTextures( 1, &texture );
	}
};

//---------------------------------------------------------------------------
// The harness's own optics, in double.
//---------------------------------------------------------------------------
using rig::Vec3;

/// Fresnel at a dielectric surface, from air, at cos(i).
struct Fresnel
{
	double Rs = 0.0, Rp = 0.0, cosT = 1.0;
};

Fresnel fresnelAt( double cosi, double n )
{
	Fresnel f;
	const double sint2 = ( 1.0 - cosi * cosi ) / ( n * n );
	f.cosT             = std::sqrt( std::max( 0.0, 1.0 - sint2 ) );
	const double rs    = ( cosi - n * f.cosT ) / ( cosi + n * f.cosT );
	const double rp    = ( n * cosi - f.cosT ) / ( n * cosi + f.cosT );
	f.Rs               = rs * rs;
	f.Rp               = rp * rp;
	return f;
}

/// A clear film's reflectance and transmittance from one surface's R, with
/// the path's own transmittance tp (1 for a clear sheet): every internal
/// echo summed.
double filmR( double R, double tp = 1.0 )
{
	return R + ( 1.0 - R ) * ( 1.0 - R ) * R * tp * tp / ( 1.0 - R * R * tp * tp );
}

double filmT( double R, double tp = 1.0 )
{
	return ( 1.0 - R ) * ( 1.0 - R ) * tp / ( 1.0 - R * R * tp * tp );
}

double decodeSrgb( double c )
{
	c = std::clamp( c, 0.0, 1.0 );
	return c <= 0.04045 ? c / 12.92 : std::pow( ( c + 0.055 ) / 1.055, 2.4 );
}

/// What the harness asked for, in the plugin's units, through Controls.h.
struct Asked
{
	int rig = 0, sides = 4, screen = 0, sheet = 0, polarisation = 0;
	double angle = 0.0, centre = 0.3, thickness = 0.0, ripple = 0.0, rippleHz = 0.5;
	double distance = 2.5, elevation = 0.0, azimuth = 0.0;
	double eta = 1.5;
	double tint[ 3 ] = { 1.0, 1.0, 1.0 };
	double sourceGain = 1.0, stageGain = 1.0, sourceBlack = 0.0, spill = 0.0;
	bool linearLight = true;
};

Asked asked( Pepper& p )
{
	auto v = [ & ]( unsigned id ) { return p.GetFloatParameter( id ); };
	Asked a;
	a.rig          = OptionIndex( v( Pepper::PT_RIG ), static_cast< int >( RigKind::Count ) );
	a.sides        = SidesFromParam( v( Pepper::PT_SIDES ) );
	a.screen       = OptionIndex( v( Pepper::PT_SCREEN ), static_cast< int >( ScreenKind::Count ) );
	a.sheet        = OptionIndex( v( Pepper::PT_SHEET ), static_cast< int >( SheetKind::Count ) );
	a.polarisation = OptionIndex( v( Pepper::PT_POLARISATION ), static_cast< int >( Polarisation::Count ) );
	a.angle        = SheetAngleFromParam( v( Pepper::PT_SHEET_ANGLE ) );
	a.centre       = CentreFromParam( v( Pepper::PT_CENTRE ) );
	a.thickness    = ThicknessFromParam( v( Pepper::PT_THICKNESS ) );
	a.ripple       = RippleFromParam( v( Pepper::PT_RIPPLE ) );
	a.rippleHz     = RippleHzFromParam( v( Pepper::PT_RIPPLE_SPEED ) );
	a.distance     = DistanceFromParam( v( Pepper::PT_DISTANCE ) );
	a.elevation    = ElevationFromParam( v( Pepper::PT_ELEVATION ) );
	a.azimuth      = AzimuthFromParam( v( Pepper::PT_AZIMUTH ) );
	a.eta          = IndexOf( static_cast< SheetKind >( a.sheet ) );
	a.tint[ 0 ]    = v( Pepper::PT_TINT_R );
	a.tint[ 1 ]    = v( Pepper::PT_TINT_G );
	a.tint[ 2 ]    = v( Pepper::PT_TINT_B );
	a.sourceGain   = SourceGainFromParam( v( Pepper::PT_SOURCE_LEVEL ) ) * std::clamp( static_cast< double >( v( Pepper::PT_OPACITY ) ), 0.0, 1.0 );
	a.stageGain    = StageGainFromParam( v( Pepper::PT_STAGE_LEVEL ) );
	a.sourceBlack  = SourceBlackFromParam( v( Pepper::PT_SOURCE_BLACK ) );
	a.spill        = SpillFromParam( v( Pepper::PT_SPILL ) );
	a.linearLight  = v( Pepper::PT_LINEAR_LIGHT ) > 0.5f;
	return a;
}

/// One sheet, as Rig.h describes it: a plane through `origin` with unit
/// `edge` along its bottom and `up` up its slope, bounded |e| <= half +
/// spread s, 0 <= s <= length.
struct Sheet
{
	Vec3 origin, edge, up, normal;
	double half = 0.0, spread = 0.0, length = 0.0;
};

/// The world as the harness understands it from Rig.h: the camera, the
/// sheets and the screen.
struct World
{
	int W = 0, H = 0;
	Vec3 eye, forward, right, up;
	double tanX = 0.0, tanY = 0.0;
	std::vector< Sheet > sheets;
	bool pane = true;
	double A = 16.0 / 9.0;///< Src's aspect
	double xMin = 0, xMax = 0, zMin = 0, zMax = 0;
	//The pyramid's feed.
	int sides = 4;
	double r0 = 0.15, armW = 0.0, armD = 0.0;
};

World worldFor( const Asked& a, int W, int H, double srcAspect )
{
	World w;
	w.W    = W;
	w.H    = H;
	w.A    = srcAspect;
	w.pane = a.rig == static_cast< int >( RigKind::Pane );
	const double cosA = std::cos( a.angle ), sinA = std::sin( a.angle );
	Vec3 target;
	double rigSize = 1.0;
	if( w.pane )
	{
		//The floor screen, x in [-A/2, A/2], z in [0, 1]; the pane through
		//the line y = 0, z = 0, leaning toward the audience at the angle.
		Sheet s;
		s.origin = { 0, 0, 0 };
		s.edge   = { 1, 0, 0 };
		s.up     = { 0, sinA, cosA };
		s.normal = { 0, -cosA, sinA };
		s.half   = 0.5 * srcAspect + 1.0;
		s.length = 1.5 / cosA;
		w.sheets.push_back( s );
		w.xMin = -0.5 * srcAspect;
		w.xMax = 0.5 * srcAspect;
		w.zMin = 0.0;
		w.zMax = 1.0;
		target = { 0, 0.5, 0 };
	}
	else
	{
		//The pyramid, from the feed's own geometry: face j stands on the
		//line at apothem c/2 along arm j's direction and reaches apothem 1/2.
		const int n       = a.sides;
		const double tn   = std::tan( kPi / n );
		const double h    = ( 0.5 - 0.5 * a.centre ) * std::tan( a.angle );
		w.sides           = n;
		w.r0              = 0.5 * a.centre;
		rigSize           = 0.6 * 2.0 * 0.5 * tn;
		target            = { 0, 0.5 * h, 0 };
		for( int j = 0; j < n; ++j )
		{
			const double theta = 2.0 * kPi * j / n;
			//Arm j points along rot(theta)(0, -1) in the picture; picture
			//(x, y) is world (x, -z).
			const Vec3 out     = { std::sin( theta ), 0, std::cos( theta ) };
			const Vec3 lateral = { std::cos( theta ), 0, -std::sin( theta ) };
			Sheet s;
			s.origin            = out * w.r0;
			s.edge              = lateral;
			const Vec3 topPoint = out * 0.5 + Vec3 { 0, h, 0 };
			s.up                = rig::normalize( topPoint - s.origin );
			s.normal            = rig::normalize( rig::cross( s.edge, s.up ) );
			s.half              = w.r0 * tn;
			s.length            = std::sqrt( rig::dot( topPoint - s.origin, topPoint - s.origin ) );
			s.spread            = ( 0.5 * tn - s.half ) / s.length;
			w.sheets.push_back( s );
		}
		const double halfX = srcAspect >= 1.0 ? 0.5 * srcAspect : 0.5;
		const double halfZ = srcAspect >= 1.0 ? 0.5 : 0.5 / srcAspect;
		w.xMin             = -halfX;
		w.xMax             = halfX;
		w.zMin             = -halfZ;
		w.zMax             = halfZ;
		w.armW             = std::min( a.centre * tn, srcAspect * ( 0.5 - w.r0 ) );
		w.armD             = w.armW / srcAspect;
	}
	const double D  = a.distance * rigSize;
	const double ce = std::cos( a.elevation );
	w.eye           = target + Vec3 { D * ce * std::sin( a.azimuth ), D * std::sin( a.elevation ), D * ce * std::cos( a.azimuth ) };
	w.forward       = rig::normalize( target - w.eye );
	w.right         = rig::normalize( rig::cross( w.forward, Vec3 { 0, 1, 0 } ) );
	w.up            = rig::cross( w.right, w.forward );
	w.tanY          = std::tan( 15.0 * kDegree );
	w.tanX          = w.tanY * W / H;
	return w;
}

/// The shader's four subsamples, a rotated grid.
const double kSub[ 4 ][ 2 ] = { { -0.125, -0.375 }, { 0.375, -0.125 }, { 0.125, 0.375 }, { -0.375, 0.125 } };

/// The camera ray through a continuous pixel position (pixel i spans
/// [i, i+1), rows from the bottom).
Vec3 rayAt( const World& w, double px, double py )
{
	const double nx = px / w.W * 2.0 - 1.0, ny = py / w.H * 2.0 - 1.0;
	return rig::normalize( w.forward + w.right * ( nx * w.tanX ) + w.up * ( ny * w.tanY ) );
}

/// Where a world point appears, in continuous pixel coordinates; false
/// behind the camera.
bool project( const World& w, Vec3 X, double& px, double& py )
{
	const Vec3 v    = X - w.eye;
	const double zc = rig::dot( v, w.forward );
	if( zc <= 1e-9 )
		return false;
	px = ( rig::dot( v, w.right ) / ( zc * w.tanX ) + 1.0 ) * 0.5 * w.W;
	py = ( rig::dot( v, w.up ) / ( zc * w.tanY ) + 1.0 ) * 0.5 * w.H;
	return true;
}

/// A ray's meeting with sheet j: the parameter, the local (e, s), or false.
bool hitSheet( const Sheet& s, Vec3 o, Vec3 d, double& t )
{
	const double dn = rig::dot( d, s.normal );
	if( std::fabs( dn ) < 1e-12 )
		return false;
	t = rig::dot( s.origin - o, s.normal ) / dn;
	if( t <= 1e-9 )
		return false;
	const Vec3 q    = o + d * t - s.origin;
	const double e  = rig::dot( q, s.edge );
	const double sl = rig::dot( q, s.up );
	return sl >= 0.0 && sl <= s.length && std::fabs( e ) <= s.half + s.spread * sl;
}

/// The mirror image of X in sheet j's plane.
Vec3 mirror( const Sheet& s, Vec3 X )
{
	return X - s.normal * ( 2.0 * rig::dot( X - s.origin, s.normal ) );
}

/// The pane's camera ray: the first surface it meets. For a pane the stage
/// front hides the floor, so a ray that reaches the floor first sees the
/// stage. Fills the incidence cosine and the reflected ray's landing.
struct PaneHit
{
	bool sheet    = false;///< the ray meets the pane
	bool onScreen = false;///< and its reflection lands on the screen
	double cosi   = 0.0;
	Vec3 P, reflected, landing;
	double u = 0.0, v = 0.0;///< the landing in the screen's picture
};

PaneHit paneRay( const World& w, Vec3 d )
{
	PaneHit h;
	double t = 0.0;
	if( !hitSheet( w.sheets[ 0 ], w.eye, d, t ) )
		return h;
	if( d.y < 0.0 && -w.eye.y / d.y < t )
		return h;
	h.sheet          = true;
	const Vec3 n     = w.sheets[ 0 ].normal;
	h.P              = w.eye + d * t;
	h.cosi           = std::fabs( rig::dot( d, n ) );
	h.reflected      = d - n * ( 2.0 * rig::dot( d, n ) );
	if( h.reflected.y >= 0.0 )
		return h;
	h.landing  = h.P + h.reflected * ( -h.P.y / h.reflected.y );
	h.u        = ( h.landing.x - w.xMin ) / ( w.xMax - w.xMin );
	h.v        = ( h.landing.z - w.zMin ) / ( w.zMax - w.zMin );
	h.onScreen = h.u > 0.0 && h.u < 1.0 && h.v > 0.0 && h.v < 1.0;
	return h;
}

/// Is every subsample of pixel (x, y) a pane ray that lands on the screen
/// at least `margin` (in uv) inside its edges?
bool pixelOnScreen( const World& w, int x, int y, double margin )
{
	for( const auto& q : kSub )
	{
		const PaneHit h = paneRay( w, rayAt( w, x + 0.5 + q[ 0 ], y + 0.5 + q[ 1 ] ) );
		if( !h.onScreen || h.u < margin || h.u > 1.0 - margin || h.v < margin || h.v > 1.0 - margin )
			return false;
	}
	return true;
}

/// A floor-screen picture point in the world, for a pane.
Vec3 paneScreenPoint( const World& w, double u, double v )
{
	return { w.xMin + u * ( w.xMax - w.xMin ), 0.0, w.zMin + v * ( w.zMax - w.zMin ) };
}

/// A clip point (u, v) in arm j of the feed, in the picture's own
/// coordinates (x right, y up, units of the short side).
void feedPoint( int sides, double r0, double armW, double armD, int j, double u, double v, double& px, double& py )
{
	const double theta = 2.0 * kPi * j / sides;
	const double ax = std::sin( theta ), ay = -std::cos( theta );
	const double tx = -ay, ty = ax;
	const double r = r0 + v * armD, e = ( u - 0.5 ) * armW;
	px = ax * r + tx * e;
	py = ay * r + ty * e;
}

//---------------------------------------------------------------------------
// Measurement.
//---------------------------------------------------------------------------
int differingBytes( const Image& a, const Image& b )
{
	int n = 0;
	for( size_t i = 0; i < a.size() && i < b.size(); ++i )
		if( a[ i ] != b[ i ] )
			++n;
	return n;
}

double channel( const ImageF& img, int W, int x, int y, int c )
{
	return img[ ( static_cast< size_t >( y ) * W + x ) * 4 + c ];
}

/// The light-weighted centroid of a float picture in a window, using the
/// mean of the three channels above `floor`. False if nothing is there.
bool centroid( const ImageF& img, int W, int H, double cx, double cy, double radius, double floorLevel, double& gx, double& gy, double& mass )
{
	const int x0 = std::max( 0, static_cast< int >( std::floor( cx - radius ) ) );
	const int x1 = std::min( W - 1, static_cast< int >( std::ceil( cx + radius ) ) );
	const int y0 = std::max( 0, static_cast< int >( std::floor( cy - radius ) ) );
	const int y1 = std::min( H - 1, static_cast< int >( std::ceil( cy + radius ) ) );
	double sx = 0, sy = 0;
	mass      = 0;
	for( int y = y0; y <= y1; ++y )
		for( int x = x0; x <= x1; ++x )
		{
			const double m = ( channel( img, W, x, y, 0 ) + channel( img, W, x, y, 1 ) + channel( img, W, x, y, 2 ) ) / 3.0 - floorLevel;
			if( m <= 0.0 )
				continue;
			sx += ( x + 0.5 ) * m;
			sy += ( y + 0.5 ) * m;
			mass += m;
		}
	if( mass <= 0.0 )
		return false;
	gx = sx / mass;
	gy = sy / mass;
	return true;
}

/// The signed area of a triangle: its orientation.
double signedArea( double ax, double ay, double bx, double by, double cx, double cy )
{
	return 0.5 * ( ( bx - ax ) * ( cy - ay ) - ( cx - ax ) * ( by - ay ) );
}

const int kRasters[ 2 ][ 2 ] = { { 640, 360 }, { 320, 180 } };

/// The settings every physics check starts from: a clear film, still, the
/// signal read as light (Linear Light off: out = P, exactly, below the clip),
/// unit gains, no spill, no black floor.
void physics( MixRig& rig )
{
	rig.Set( "Linear Light", 0.0f );
	rig.Set( "Opacity", 1.0f );
	rig.Set( "Source Level", ParamForSourceStops( 0.0 ) );
	rig.Set( "Stage Level", ParamForStageStops( 0.0 ) );
	rig.Set( "Source Black", 0.0f );
	rig.Set( "Spill", 0.0f );
	rig.Set( "Polarisation", 0.0f );
	rig.Set( "Thickness", 0.0f );
	rig.Set( "Ripple", 0.0f );
	rig.Set( "Tint", 1.0f );
	rig.Set( "Tint_Green", 1.0f );
	rig.Set( "Tint_Blue", 1.0f );
	rig.Set( "Sheet Angle", ParamForSheetAngle( 45.0 * kDegree ) );
	rig.Set( "Distance", ParamForDistance( kDistanceDefault ) );
	rig.Set( "Elevation", ParamForElevation( 0.0 ) );
	rig.Set( "Azimuth", ParamForAzimuth( 0.0 ) );
}

} // namespace
