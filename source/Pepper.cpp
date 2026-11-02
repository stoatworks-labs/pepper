#include "Pepper.h"

#include "Controls.h"
#include "Diag.h"
#include "Shaders.h"

#include <algorithm>
#include <cmath>
#include <string>

using namespace ffglex;

namespace pepper
{
namespace
{
std::string glStringOrUnknown( GLenum name )
{
	const GLubyte* value = glGetString( name );
	return value ? reinterpret_cast< const char* >( value ) : "unknown";
}

std::string sizeText( const FFGLTextureStruct& t )
{
	return std::to_string( t.Width ) + "x" + std::to_string( t.Height ) + " of " + std::to_string( t.HardwareWidth ) + "x"
	       + std::to_string( t.HardwareHeight );
}

/// How many Opacity changes the log records, per instance: enough to follow
/// a whole fade frame by frame, not a line a frame for a show.
constexpr int kOpacityLines = 400;

/// Instances are numbered in creation order, so a host session's log can tell
/// the layer's mixer from a transition's (Arena makes separate instances).
int gInstances = 0;

/// The smallest contribution a ray is followed for, in linear light: under
/// half an 8-bit code even at black, where sRGB is steepest (1/8192 x 12.92
/// x 255 = 0.40 codes).
constexpr double kPruneLight = 1.0 / 8192.0;

void setVec3( GLint location, const rig::Vec3& v )
{
	glUniform3f( location, static_cast< float >( v.x ), static_cast< float >( v.y ), static_cast< float >( v.z ) );
}
} // namespace

Pepper::Pepper()
{
	//A mixer takes exactly two. This is a separate declaration from the type
	//(MixerEntry.cpp), read by the host through different function codes.
	SetMinInputs( 2 );
	SetMaxInputs( 2 );
	SetTimeSupported( true );

	//---------------------------------------------------------------------
	// Defaults. SetParamInfof reads each one back out of GetFloatParameter,
	// so these assignments are what the host is told the defaults are.
	//---------------------------------------------------------------------
	params[ PT_LINEAR_LIGHT ] = 1.0f;
	params[ PT_OPACITY ]      = 1.0f;
	params[ PT_SOURCE_LEVEL ] = ParamForSourceStops( kSourceStopsDefault );
	params[ PT_SOURCE_BLACK ] = 0.0f;
	params[ PT_POLARISATION ] = 0.0f;
	params[ PT_STAGE_LEVEL ]  = ParamForStageStops( kStageStopsDefault );
	params[ PT_SPILL ]        = 0.0f;

	params[ PT_RIG ]         = 0.0f;
	params[ PT_SHEET_ANGLE ] = ParamForSheetAngle( kSheetAngleDefaultDeg * kPi / 180.0 );
	params[ PT_SIDES ]       = 0.0f;
	params[ PT_CENTRE ]      = ParamForCentre( kCentreDefault );
	params[ PT_SCREEN ]      = 0.0f;

	params[ PT_SHEET ]        = 0.0f;
	params[ PT_THICKNESS ]    = 0.0f;
	params[ PT_TINT_R ]       = 1.0f;
	params[ PT_TINT_G ]       = 1.0f;
	params[ PT_TINT_B ]       = 1.0f;
	params[ PT_RIPPLE ]       = ParamForRipple( kRippleDefaultDeg * kPi / 180.0 );
	params[ PT_RIPPLE_SPEED ] = ParamForRippleHz( kRippleHzDefault );

	params[ PT_DISTANCE ]  = ParamForDistance( kDistanceDefault );
	params[ PT_ELEVATION ] = ParamForElevation( 0.0 );
	params[ PT_AZIMUTH ]   = ParamForAzimuth( 0.0 );

	//---------------------------------------------------------------------
	// Declaration, in index order.
	//---------------------------------------------------------------------
	//FIRST, deliberately: Arena does not expose a mixer's parameter 0, so it
	//holds a control whose default is right if it can never be changed.
	SetParamInfo( PT_LINEAR_LIGHT, "Linear Light", FF_TYPE_BOOLEAN, true );
	//Named Opacity, and the name is load-bearing: Resolume binds a mixer
	//parameter called Opacity to the LAYER's opacity fader, and a layer
	//transition ramps it (measured on genlock, wipe, relay and lenticular).
	//Here it is the source's light: fade the layer up and the ghost appears,
	//as the Victorian lamp did; the glass stays when it is down.
	SetParamInfof( PT_OPACITY, "Opacity", FF_TYPE_STANDARD );
	SetParamInfof( PT_SOURCE_LEVEL, "Source Level", FF_TYPE_STANDARD );
	SetParamInfof( PT_SOURCE_BLACK, "Source Black", FF_TYPE_STANDARD );
	SetOptionParamInfo( PT_POLARISATION, "Polarisation", static_cast< unsigned >( Polarisation::Count ), 0.0f );
	SetParamElementInfo( PT_POLARISATION, 0, "None", 0.0f );
	SetParamElementInfo( PT_POLARISATION, 1, "Horizontal", 1.0f );
	SetParamElementInfo( PT_POLARISATION, 2, "Vertical", 2.0f );
	SetParamInfof( PT_STAGE_LEVEL, "Stage Level", FF_TYPE_STANDARD );
	SetParamInfof( PT_SPILL, "Spill", FF_TYPE_STANDARD );

	SetOptionParamInfo( PT_RIG, "Rig", static_cast< unsigned >( RigKind::Count ), 0.0f );
	SetParamElementInfo( PT_RIG, 0, "Pane", 0.0f );
	SetParamElementInfo( PT_RIG, 1, "Pyramid", 1.0f );
	SetParamInfof( PT_SHEET_ANGLE, "Sheet Angle", FF_TYPE_STANDARD );
	SetOptionParamInfo( PT_SIDES, "Sides", 2, 0.0f );
	SetParamElementInfo( PT_SIDES, 0, "Four", 0.0f );
	SetParamElementInfo( PT_SIDES, 1, "Three", 1.0f );
	SetParamInfof( PT_CENTRE, "Centre", FF_TYPE_STANDARD );
	SetOptionParamInfo( PT_SCREEN, "Screen", static_cast< unsigned >( ScreenKind::Count ), 0.0f );
	SetParamElementInfo( PT_SCREEN, 0, "Feed", 0.0f );
	SetParamElementInfo( PT_SCREEN, 1, "As Is", 1.0f );

	SetOptionParamInfo( PT_SHEET, "Sheet", static_cast< unsigned >( SheetKind::Count ), 0.0f );
	for( int k = 0; k < static_cast< int >( SheetKind::Count ); ++k )
		SetParamElementInfo( PT_SHEET, static_cast< unsigned >( k ), NameOf( static_cast< SheetKind >( k ) ), static_cast< float >( k ) );
	SetParamInfof( PT_THICKNESS, "Thickness", FF_TYPE_STANDARD );
	//Three typed channels in a row is how a host knows to show one colour
	//picker; burin's names.
	SetParamInfof( PT_TINT_R, "Tint", FF_TYPE_RED );
	SetParamInfof( PT_TINT_G, "Tint_Green", FF_TYPE_GREEN );
	SetParamInfof( PT_TINT_B, "Tint_Blue", FF_TYPE_BLUE );
	SetParamInfof( PT_RIPPLE, "Ripple", FF_TYPE_STANDARD );
	SetParamInfof( PT_RIPPLE_SPEED, "Ripple Speed", FF_TYPE_STANDARD );

	SetParamInfof( PT_DISTANCE, "Distance", FF_TYPE_STANDARD );
	SetParamInfof( PT_ELEVATION, "Elevation", FF_TYPE_STANDARD );
	SetParamInfof( PT_AZIMUTH, "Azimuth", FF_TYPE_STANDARD );

	// Groups, the way Resolume shows them: each group one contiguous run.
	for( FFUInt32 i = PT_LINEAR_LIGHT; i <= PT_SPILL; ++i )
		SetParamGroup( i, "Light" );
	for( FFUInt32 i = PT_RIG; i <= PT_SCREEN; ++i )
		SetParamGroup( i, "Rig" );
	for( FFUInt32 i = PT_SHEET; i <= PT_RIPPLE_SPEED; ++i )
		SetParamGroup( i, "Sheet" );
	for( FFUInt32 i = PT_DISTANCE; i <= PT_AZIMUTH; ++i )
		SetParamGroup( i, "View" );

	// The About block. Declared inline rather than through a helper, because
	// SetParamInfo is protected on CFFGLPlugin.
	SetParamInfo( PT_ABOUT_FIRST, "About", FF_TYPE_TEXT, stoatworks::about::defaultText() );
	{
		FFUInt32 aboutId = PT_ABOUT_FIRST + 1;
		for( const auto& b : stoatworks::about::buttons() )
			SetParamInfo( aboutId++, b.label, FF_TYPE_EVENT, false );
	}
	for( FFUInt32 i = PT_ABOUT_FIRST; i < PT_COUNT; ++i )
		SetParamGroup( i, "About" );

	FFGLLog::LogToHost( "Created Pepper mixer" );

	diag::init();
	instance = ++gInstances;
}

//---------------------------------------------------------------------------
FFResult Pepper::InitGL( const FFGLViewportStruct* vp )
{
	diag::info( std::string( "GL vendor=" ) + glStringOrUnknown( GL_VENDOR ) + " renderer=" + glStringOrUnknown( GL_RENDERER )
	            + " version=" + glStringOrUnknown( GL_VERSION ) );

	fragmentText = fragmentOverride ? std::string( fragmentOverride ) : AssembleFragment( kPepperFragment );
	if( !shader.Compile( AssembleVertex(), fragmentText ) )
	{
		//Returning FF_FAIL here is invisible to the operator: the mixer
		//simply does nothing in Resolume. These lines are the only record.
		diag::error( "the pepper shader failed to compile - the mixer will do nothing" );
		FFGLLog::LogToHost( "Pepper: shader failed to compile" );
		DeInitGL();
		return FF_FAIL;
	}

	if( !quad.Initialise() )
	{
		diag::error( "quad geometry failed to initialise" );
		DeInitGL();
		return FF_FAIL;
	}

	diag::info( "initialised, viewport " + std::to_string( vp ? vp->width : 0 ) + "x" + std::to_string( vp ? vp->height : 0 )
	            + " (instance #" + std::to_string( instance ) + ")" );

	//Use base-class init as the success result so it retains the viewport.
	return CFFGLPlugin::InitGL( vp );
}

void Pepper::LogGuard( unsigned bit, const char* what )
{
	if( guardsLogged & bit )
		return;
	guardsLogged |= bit;
	diag::warn( std::string( "guard: called with " ) + what + " -- returned FF_FAIL (logged once)" );
}

//---------------------------------------------------------------------------
FFResult Pepper::ProcessOpenGL( ProcessOpenGLStruct* pGL )
{
	//The SDK's Add example guards on the input count and on each pointer,
	//with a comment saying a host calls a mixer with one input while the
	//operator is still patching. Not observed in Arena (genlock, one
	//sequence) -- but a mixer that dereferenced a null would take Resolume
	//down with it.
	if( pGL == nullptr || pGL->inputTextures == nullptr )
	{
		LogGuard( 1u, "no input array" );
		return FF_FAIL;
	}
	if( pGL->numInputTextures < 2 )
	{
		LogGuard( 2u, ( std::to_string( pGL->numInputTextures ) + " input(s)" ).c_str() );
		return FF_FAIL;
	}
	if( pGL->inputTextures[ 0 ] == nullptr || pGL->inputTextures[ 1 ] == nullptr )
	{
		LogGuard( 4u, "a null input" );
		return FF_FAIL;
	}

	const FFGLTextureStruct& dest = *pGL->inputTextures[ 0 ];//the layer below: the stage, the room
	const FFGLTextureStruct& src  = *pGL->inputTextures[ 1 ];//this layer: the hidden screen
	//HardwareWidth and HardwareHeight are the DENOMINATORS in
	//GetMaxGLTexCoords, so a zero there is an infinite MaxUV.
	if( dest.Width == 0 || dest.Height == 0 || dest.HardwareWidth == 0 || dest.HardwareHeight == 0 || src.Width == 0 || src.Height == 0
	    || src.HardwareWidth == 0 || src.HardwareHeight == 0 )
	{
		LogGuard( 8u, "a zero-sized input" );
		return FF_FAIL;
	}

	const int outW = static_cast< int >( currentViewport.width );
	const int outH = static_cast< int >( currentViewport.height );
	if( outW <= 0 || outH <= 0 )
		return FF_FAIL;

	++frames;
	if( frames == 1 )
		diag::info( "first frame: Dest " + sizeText( dest ) + ", Src " + sizeText( src ) + ", out " + std::to_string( outW ) + "x"
		            + std::to_string( outH ) );
	if( params[ PT_OPACITY ] != loggedOpacity && opacityLines < kOpacityLines )
	{
		//The only evidence a host gives of what drives a mixer's Opacity:
		//the layer fader, a transition, or the mixer's own slider.
		diag::info( "Opacity " + std::to_string( params[ PT_OPACITY ] ) + " at frame " + std::to_string( frames ) + " (instance #"
		            + std::to_string( instance ) + ")" );
		loggedOpacity = params[ PT_OPACITY ];
		++opacityLines;
	}

	const double seconds = clock.Tick();

	//-----------------------------------------------------------------
	// The rig, as numbers. In double here; the shader gets floats.
	//-----------------------------------------------------------------
	rig::Inputs in;
	in.rig       = OptionIndex( params[ PT_RIG ], static_cast< int >( RigKind::Count ) );
	in.angle     = SheetAngleFromParam( params[ PT_SHEET_ANGLE ] );
	in.sides     = SidesFromParam( params[ PT_SIDES ] );
	in.centre    = CentreFromParam( params[ PT_CENTRE ] );
	in.screen    = OptionIndex( params[ PT_SCREEN ], static_cast< int >( ScreenKind::Count ) );
	in.sheet     = OptionIndex( params[ PT_SHEET ], static_cast< int >( SheetKind::Count ) );
	in.thickness = ThicknessFromParam( params[ PT_THICKNESS ] );
	in.tint[ 0 ] = params[ PT_TINT_R ];
	in.tint[ 1 ] = params[ PT_TINT_G ];
	in.tint[ 2 ] = params[ PT_TINT_B ];
	in.ripple    = RippleFromParam( params[ PT_RIPPLE ] );
	in.rippleHz  = RippleHzFromParam( params[ PT_RIPPLE_SPEED ] );
	in.distance  = DistanceFromParam( params[ PT_DISTANCE ] );
	in.elevation = ElevationFromParam( params[ PT_ELEVATION ] );
	in.azimuth   = AzimuthFromParam( params[ PT_AZIMUTH ] );
	const double opacity = std::clamp( static_cast< double >( params[ PT_OPACITY ] ), 0.0, 1.0 );
	in.sourceGain        = SourceGainFromParam( params[ PT_SOURCE_LEVEL ] ) * opacity;
	in.sourceBlack       = SourceBlackFromParam( params[ PT_SOURCE_BLACK ] );
	in.spill             = SpillFromParam( params[ PT_SPILL ] );
	in.stageGain         = StageGainFromParam( params[ PT_STAGE_LEVEL ] );
	in.polarisation      = OptionIndex( params[ PT_POLARISATION ], static_cast< int >( Polarisation::Count ) );
	in.linearLight       = params[ PT_LINEAR_LIGHT ] > 0.5f;
	in.srcAspect         = static_cast< double >( src.Width ) / src.Height;
	in.outAspect         = static_cast< double >( outW ) / outH;
	in.seconds           = seconds;
	in.modesLocked       = ( fault & kFaultModesLocked ) != 0;
	const rig::Setup s   = rig::Build( in );
	lastSetup            = s;

	//-----------------------------------------------------------------
	// Bind both inputs. The declaration ORDER matters: every
	// ffglex::Scoped* clears its binding on exit rather than restoring it,
	// so they must unwind as activate(1), bind(1) then activate(0), bind(0).
	//-----------------------------------------------------------------
	ScopedShaderBinding shaderBinding( shader.GetGLID() );
	ScopedSamplerActivation activateDest( 0 );
	Scoped2DTextureBinding bindDest( dest.Handle );
	ScopedSamplerActivation activateSrc( 1 );
	Scoped2DTextureBinding bindSrc( src.Handle );

	shader.Set( "TextureDest", 0 );
	shader.Set( "TextureSrc", 1 );

	//One MaxUV per input. They are different numbers whenever the two
	//layers are different sizes, which for a mixer is the normal case.
	const FFGLTexCoords maxDest = GetMaxGLTexCoords( dest );
	const FFGLTexCoords maxSrc  = GetMaxGLTexCoords( src );
	shader.Set( "MaxUVDest", maxDest.s, maxDest.t );
	shader.Set( "MaxUVSrc", maxSrc.s, maxSrc.t );
	shader.Set( "HalfTexelDest", 0.5f / static_cast< float >( dest.Width ), 0.5f / static_cast< float >( dest.Height ) );
	shader.Set( "HalfTexelSrc", 0.5f / static_cast< float >( src.Width ), 0.5f / static_cast< float >( src.Height ) );
	if( fault & kFaultFoldedMaxUV )
		shader.Set( "FoldMaxUV", maxDest.s, maxDest.t );
	else
		shader.Set( "FoldMaxUV", 1.0f, 1.0f );
	glUniform2i( shader.FindUniform( "OutSize" ), outW, outH );

	shader.Set( "RigKind", s.rig );
	setVec3( shader.FindUniform( "Eye" ), s.eye );
	setVec3( shader.FindUniform( "Forward" ), s.forward );
	setVec3( shader.FindUniform( "CamRight" ), s.right );
	setVec3( shader.FindUniform( "CamUp" ), s.up );
	shader.Set( "TanHalf", static_cast< float >( s.tanHalfY * s.aspect ), static_cast< float >( s.tanHalfY ) );

	float origin[ 12 ] = {}, edge[ 12 ] = {}, up[ 12 ] = {}, normal[ 12 ] = {}, bounds[ 16 ] = {};
	float heights[ rig::kMaxFaces * rig::kModeCount ] = {};
	bool rippling = false;
	for( int j = 0; j < s.faces; ++j )
	{
		const rig::Face& f = s.face[ j ];
		const rig::Vec3* v[ 4 ] = { &f.origin, &f.edge, &f.up, &f.normal };
		float* out[ 4 ]         = { origin, edge, up, normal };
		for( int a = 0; a < 4; ++a )
		{
			out[ a ][ 3 * j + 0 ] = static_cast< float >( v[ a ]->x );
			out[ a ][ 3 * j + 1 ] = static_cast< float >( v[ a ]->y );
			out[ a ][ 3 * j + 2 ] = static_cast< float >( v[ a ]->z );
		}
		bounds[ 4 * j + 0 ] = static_cast< float >( f.half );
		bounds[ 4 * j + 1 ] = static_cast< float >( f.spread );
		bounds[ 4 * j + 2 ] = static_cast< float >( f.length );
		bounds[ 4 * j + 3 ] = static_cast< float >( f.width );
		for( int k = 0; k < rig::kModeCount; ++k )
		{
			heights[ rig::kModeCount * j + k ] = static_cast< float >( f.height[ k ] );
			rippling                           = rippling || f.amplitude[ k ] != 0.0;
		}
	}
	shader.Set( "FaceCount", s.faces );
	glUniform3fv( shader.FindUniform( "FaceOrigin" ), rig::kMaxFaces, origin );
	glUniform3fv( shader.FindUniform( "FaceEdge" ), rig::kMaxFaces, edge );
	glUniform3fv( shader.FindUniform( "FaceUp" ), rig::kMaxFaces, up );
	glUniform3fv( shader.FindUniform( "FaceNormal" ), rig::kMaxFaces, normal );
	glUniform4fv( shader.FindUniform( "FaceBounds" ), rig::kMaxFaces, bounds );
	glUniform1fv( shader.FindUniform( "ModeHeight" ), rig::kMaxFaces * rig::kModeCount, heights );
	shader.Set( "Rippling", rippling ? 1 : 0 );

	shader.Set( "ScreenMap", static_cast< int >( s.map ) );
	shader.Set( "ScreenRect", static_cast< float >( s.xMin ), static_cast< float >( s.xMax ), static_cast< float >( s.zMin ),
	            static_cast< float >( s.zMax ) );
	setVec3( shader.FindUniform( "SourceE" ), s.polarisation );

	float axes[ 16 ] = {};
	for( int j = 0; j < s.sides; ++j )
	{
		double ax, ay, tx, ty;
		rig::ArmAxes( s.sides, j, ax, ay, tx, ty );
		axes[ 4 * j + 0 ] = static_cast< float >( ax );
		axes[ 4 * j + 1 ] = static_cast< float >( ay );
		axes[ 4 * j + 2 ] = static_cast< float >( tx );
		axes[ 4 * j + 3 ] = static_cast< float >( ty );
	}
	shader.Set( "FeedSides", s.sides );
	shader.Set( "FeedR0", static_cast< float >( 0.5 * s.centre ) );
	shader.Set( "FeedArm", static_cast< float >( s.armW ), static_cast< float >( s.armD ) );
	glUniform4fv( shader.FindUniform( "FeedAxis" ), 4, axes );

	shader.Set( "Eta", static_cast< float >( s.eta ) );
	shader.Set( "Thickness", static_cast< float >( s.thickness ) );
	shader.Set( "Tint", static_cast< float >( s.tint[ 0 ] ), static_cast< float >( s.tint[ 1 ] ), static_cast< float >( s.tint[ 2 ] ) );

	shader.Set( "SourceGain", static_cast< float >( s.sourceGain ) );
	shader.Set( "SourceBlack", static_cast< float >( s.sourceBlack ) );
	shader.Set( "Spill", static_cast< float >( s.spill ) );
	shader.Set( "StageGain", static_cast< float >( s.stageGain ) );
	shader.Set( "Opacity", static_cast< float >( opacity ) );
	shader.Set( "LinearLight", s.linearLight ? 1 : 0 );
	//From Source Level, NOT Source Level x Opacity: which rays are followed
	//must not depend on the fader, or Opacity 0 would not be the black-Src
	//picture bit for bit (pgtest --opacity).
	const double brightest = std::max( { SourceGainFromParam( params[ PT_SOURCE_LEVEL ] ), s.spill, s.stageGain, 1e-6 } );
	shader.Set( "Prune", static_cast< float >( kPruneLight / brightest ) );

	const double c45 = std::sqrt( 0.5 );
	shader.Set( "Normal45", 0.0f, static_cast< float >( -c45 ), static_cast< float >( c45 ) );
	shader.Set( "Fault", fault );

	quad.Draw();

	return FF_SUCCESS;
}

//---------------------------------------------------------------------------
FFResult Pepper::DeInitGL()
{
	if( frames > 0 )
		diag::info( "DeInitGL after " + std::to_string( frames ) + " frames, last Opacity " + std::to_string( params[ PT_OPACITY ] )
		            + " (instance #" + std::to_string( instance ) + ")" );
	shader.FreeGLResources();
	quad.Release();
	return FF_SUCCESS;
}

FFResult Pepper::SetTime( double time )
{
	clock.Observe( time );
	return FF_SUCCESS;
}

//---------------------------------------------------------------------------
FFResult Pepper::SetFloatParameter( unsigned int index, float value )
{
	if( index >= PT_COUNT )
		return FF_FAIL;

	// The About buttons open a browser and store nothing.
	if( index >= PT_ABOUT_FIRST )
		return stoatworks::about::handleParam( index - PT_ABOUT_FIRST, value ) ? FF_SUCCESS : FF_FAIL;

	params[ index ] = value;
	return FF_SUCCESS;
}

float Pepper::GetFloatParameter( unsigned int index )
{
	if( index >= PT_COUNT )
		return 0.0f;

	return params[ index ];
}

//---------------------------------------------------------------------------
char* Pepper::GetTextParameter( unsigned int index )
{
	if( index == PT_ABOUT_FIRST )
	{
		aboutText = stoatworks::about::textParam( 0 );
		return const_cast< char* >( aboutText.c_str() );
	}

	return CFFGLPlugin::GetTextParameter( index );
}

FFResult Pepper::SetTextParameter( unsigned int index, const char* value )
{
	// See the declaration: the base class fails, and a failed default deletes
	// the instance. The About line is display-only, so there is genuinely
	// nothing to store -- but it has to say so successfully.
	if( index == PT_ABOUT_FIRST )
		return FF_SUCCESS;

	return CFFGLPlugin::SetTextParameter( index, value );
}

//---------------------------------------------------------------------------
void Pepper::SetHostInfo( const char* hostname, const char* version )
{
	CFFGLPlugin::SetHostInfo( hostname, version );
	diag::info( std::string( "host=" ) + ( hostname ? hostname : "?" ) + " version=" + ( version ? version : "?" ) + " loaded from "
	            + diag::modulePath() );
}

} // namespace pepper
