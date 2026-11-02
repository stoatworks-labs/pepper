#include "Feed.h"

#include "Controls.h"
#include "Diag.h"
#include "Rig.h"
#include "Shaders.h"

#include <algorithm>
#include <string>

using namespace ffglex;

namespace pepper
{

Feed::Feed()
{
	SetMinInputs( 1 );
	SetMaxInputs( 1 );

	params[ PT_SIDES ]  = 0.0f;
	params[ PT_CENTRE ] = ParamForCentre( kCentreDefault );
	params[ PT_MOUNT ]  = 0.0f;

	//The same two controls, the same names and the same conversions as SW
	//Pepper's Rig group, so equal slider positions are the same layout bit
	//for bit.
	SetOptionParamInfo( PT_SIDES, "Sides", 2, 0.0f );
	SetParamElementInfo( PT_SIDES, 0, "Four", 0.0f );
	SetParamElementInfo( PT_SIDES, 1, "Three", 1.0f );
	SetParamInfof( PT_CENTRE, "Centre", FF_TYPE_STANDARD );
	SetOptionParamInfo( PT_MOUNT, "Mount", static_cast< unsigned >( MountKind::Count ), 0.0f );
	SetParamElementInfo( PT_MOUNT, 0, "Screen Below", 0.0f );
	SetParamElementInfo( PT_MOUNT, 1, "Screen Above", 1.0f );
	SetParamGroup( PT_SIDES, "Feed" );
	SetParamGroup( PT_CENTRE, "Feed" );
	SetParamGroup( PT_MOUNT, "Feed" );

	SetParamInfo( PT_ABOUT_FIRST, "About", FF_TYPE_TEXT, stoatworks::about::defaultText() );
	{
		FFUInt32 aboutId = PT_ABOUT_FIRST + 1;
		for( const auto& b : stoatworks::about::buttons() )
			SetParamInfo( aboutId++, b.label, FF_TYPE_EVENT, false );
	}
	for( FFUInt32 i = PT_ABOUT_FIRST; i < PT_COUNT; ++i )
		SetParamGroup( i, "About" );

	FFGLLog::LogToHost( "Created Pepper Feed effect" );
	diag::init();
}

FFResult Feed::InitGL( const FFGLViewportStruct* vp )
{
	if( !shader.Compile( AssembleVertex(), AssembleFragment( kFeedFragment ) ) )
	{
		diag::error( "the feed shader failed to compile - the effect will do nothing" );
		FFGLLog::LogToHost( "Pepper Feed: shader failed to compile" );
		DeInitGL();
		return FF_FAIL;
	}
	if( !quad.Initialise() )
	{
		diag::error( "feed quad geometry failed to initialise" );
		DeInitGL();
		return FF_FAIL;
	}
	diag::info( "feed initialised, viewport " + std::to_string( vp ? vp->width : 0 ) + "x" + std::to_string( vp ? vp->height : 0 ) );
	return CFFGLPlugin::InitGL( vp );
}

void Feed::LogGuard( unsigned bit, const char* what )
{
	if( guardsLogged & bit )
		return;
	guardsLogged |= bit;
	diag::warn( std::string( "feed guard: called with " ) + what + " -- returned FF_FAIL (logged once)" );
}

FFResult Feed::ProcessOpenGL( ProcessOpenGLStruct* pGL )
{
	if( pGL == nullptr || pGL->inputTextures == nullptr || pGL->numInputTextures < 1 || pGL->inputTextures[ 0 ] == nullptr )
	{
		LogGuard( 1u, "no input" );
		return FF_FAIL;
	}
	const FFGLTextureStruct& in = *pGL->inputTextures[ 0 ];
	if( in.Width == 0 || in.Height == 0 || in.HardwareWidth == 0 || in.HardwareHeight == 0 )
	{
		LogGuard( 2u, "a zero-sized input" );
		return FF_FAIL;
	}
	const int outW = static_cast< int >( currentViewport.width );
	const int outH = static_cast< int >( currentViewport.height );
	if( outW <= 0 || outH <= 0 )
		return FF_FAIL;
	if( ++frames == 1 )
		diag::info( "feed first frame: input " + std::to_string( in.Width ) + "x" + std::to_string( in.Height ) + " of "
		            + std::to_string( in.HardwareWidth ) + "x" + std::to_string( in.HardwareHeight ) + ", out " + std::to_string( outW )
		            + "x" + std::to_string( outH ) );

	const int sides     = SidesFromParam( params[ PT_SIDES ] );
	const double centre = CentreFromParam( params[ PT_CENTRE ] );
	const double aspect = static_cast< double >( in.Width ) / in.Height;
	double w, d;
	rig::FeedArm( sides, centre, aspect, w, d );
	float axes[ 16 ] = {};
	for( int j = 0; j < sides; ++j )
	{
		double ax, ay, tx, ty;
		rig::ArmAxes( sides, j, ax, ay, tx, ty );
		axes[ 4 * j + 0 ] = static_cast< float >( ax );
		axes[ 4 * j + 1 ] = static_cast< float >( ay );
		axes[ 4 * j + 2 ] = static_cast< float >( tx );
		axes[ 4 * j + 3 ] = static_cast< float >( ty );
	}

	ScopedShaderBinding shaderBinding( shader.GetGLID() );
	ScopedSamplerActivation activate( 0 );
	Scoped2DTextureBinding bind( in.Handle );
	shader.Set( "InputTexture", 0 );

	const FFGLTexCoords maxUV = GetMaxGLTexCoords( in );
	shader.Set( "MaxUV", maxUV.s, maxUV.t );
	shader.Set( "HalfTexel", 0.5f / static_cast< float >( in.Width ), 0.5f / static_cast< float >( in.Height ) );
	shader.Set( "FoldMaxUV", 1.0f, 1.0f );
	glUniform2i( shader.FindUniform( "OutSize" ), outW, outH );
	shader.Set( "ShortSide", static_cast< float >( std::min( outW, outH ) ) );

	shader.Set( "FeedSides", sides );
	//An apex-up pyramid's feed: each arm turned 180 degrees in place. The
	//--feed negative control lays it out for apex down regardless.
	const bool above = OptionIndex( params[ PT_MOUNT ], static_cast< int >( MountKind::Count ) ) == static_cast< int >( MountKind::Above );
	shader.Set( "FeedFlip", above && ( fault & kFaultAboveAsBelow ) == 0 ? 1 : 0 );
	shader.Set( "FeedR0", static_cast< float >( 0.5 * centre ) );
	shader.Set( "FeedArm", static_cast< float >( w ), static_cast< float >( d ) );
	glUniform4fv( shader.FindUniform( "FeedAxis" ), 4, axes );
	shader.Set( "Fault", fault );

	quad.Draw();
	return FF_SUCCESS;
}

FFResult Feed::DeInitGL()
{
	shader.FreeGLResources();
	quad.Release();
	return FF_SUCCESS;
}

FFResult Feed::SetFloatParameter( unsigned int index, float value )
{
	if( index >= PT_COUNT )
		return FF_FAIL;
	if( index >= PT_ABOUT_FIRST )
		return stoatworks::about::handleParam( index - PT_ABOUT_FIRST, value ) ? FF_SUCCESS : FF_FAIL;
	params[ index ] = value;
	return FF_SUCCESS;
}

float Feed::GetFloatParameter( unsigned int index )
{
	return index < PT_COUNT ? params[ index ] : 0.0f;
}

char* Feed::GetTextParameter( unsigned int index )
{
	if( index == PT_ABOUT_FIRST )
	{
		aboutText = stoatworks::about::textParam( 0 );
		return const_cast< char* >( aboutText.c_str() );
	}
	return CFFGLPlugin::GetTextParameter( index );
}

FFResult Feed::SetTextParameter( unsigned int index, const char* value )
{
	if( index == PT_ABOUT_FIRST )
		return FF_SUCCESS;
	return CFFGLPlugin::SetTextParameter( index, value );
}

void Feed::SetHostInfo( const char* hostname, const char* version )
{
	CFFGLPlugin::SetHostInfo( hostname, version );
	diag::info( std::string( "feed host=" ) + ( hostname ? hostname : "?" ) + " version=" + ( version ? version : "?" ) + " loaded from "
	            + diag::modulePath() );
}

} // namespace pepper
