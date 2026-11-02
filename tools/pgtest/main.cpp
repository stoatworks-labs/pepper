/**
    pgtest -- render SW Pepper and SW Pepper Feed offline, and measure what
    the glass is doing.

    The mixer gets TWO inputs: `inputTextures[0]` is Dest, the layer below --
    the stage seen through the sheet (`--input-a`); `[1]` is Src, this layer
    -- the hidden screen (`--input-b`). They may be different sizes, with
    different hardware padding, rendered to a third.

        pgtest --out /tmp/f.png      a picture, through the real plugin (--plugin feed for the feed)
        pgtest --list                every parameter, for the sweep (--plugin feed for the feed's)
        pgtest --names               no name over 16 bytes or duplicated; index 0 is Linear Light
        pgtest --mixer               two inputs, two MaxUVs, and the guards
        pgtest --additive            the ghost only ever adds light, in linear light
        pgtest --energy              a clear film: R + T = 1, per pixel, every sheet
        pgtest --fresnel             Brewster's null and Abeles' identity, out of the picture
        pgtest --double              a thick pane's images: their weights and where they land
        pgtest --geometry            dots land where the mirror puts them; the keystone
        pgtest --ripple              the foil rings at its frame's mode frequencies, by FFT
        pgtest --opacity             Opacity is the source's light; at 0 the glass stays
        pgtest --pyramid             the near face shows the clip the right way round
        pgtest --feed                SW Pepper Feed's arms are reflections, exactly where stated
        pgtest --mutation            one character of the shipped GLSL fails --double
        pgtest --bench               ms/frame at 720p, 1080p and 4K, pane and pyramid
        pgtest --pipe                raw frames in, raw frames out (two inputs)

    PGTEST_RENDERER=software renders on Apple's software renderer instead of
    the GPU.

    Every check renders through the REAL plugin class in a headless CGL
    context and measures the property out of the picture against the
    harness's own model (Harness.h), at 640x360 and at 320x180, and carries
    a negative control that builds the wrong answer into the real plugin
    (`SetFaultForTest`) and shows the check rejects it. Every tolerance is
    derived from something stated -- float roundoff through a counted number
    of operations, the subsample grid, a texel -- and AGENTS.md lists them.
*/

#include "Harness.h"

namespace
{

//---------------------------------------------------------------------------
// --list, --names
//---------------------------------------------------------------------------
template< typename P >
int listOf( P& plugin, unsigned count, unsigned aboutFirst )
{
	std::printf( "%-4s %-22s %-9s %10s   %-16s %s\n", "id", "name", "kind", "value", "range", "elements" );
	for( unsigned int id = 0; id < count; ++id )
	{
		const char* name = plugin.GetParamName( id );
		if( id >= aboutFirst )
		{
			std::printf( "%-4u %-22s %-9s %10s   %-16s %s\n", id, name ? name : "", "about", "-", "-", "the Stoatworks About block; not swept" );
			continue;
		}
		const char* kind = "standard";
		switch( plugin.GetParamType( id ) )
		{
		case FF_TYPE_BOOLEAN: kind = "boolean"; break;
		case FF_TYPE_EVENT: kind = "event"; break;
		case FF_TYPE_INTEGER: kind = "integer"; break;
		case FF_TYPE_OPTION: kind = "option"; break;
		case FF_TYPE_RED: kind = "red"; break;
		case FF_TYPE_GREEN: kind = "green"; break;
		case FF_TYPE_BLUE: kind = "blue"; break;
		case FF_TYPE_TEXT: kind = "text"; break;
		default: break;
		}
		RangeStruct range = plugin.GetParamRange( id );
		std::string elements;
		//An option's range reads back 0..1 whatever its element count.
		if( plugin.GetParamType( id ) == FF_TYPE_OPTION )
		{
			const unsigned n = std::max( 1u, plugin.GetNumParamElements( id ) );
			range.min        = 0.0f;
			range.max        = static_cast< float >( n ) - 1.0f;
			for( unsigned e = 0; e < n; ++e )
				elements += std::string( e ? " | " : "" ) + plugin.GetParamElementName( id, e );
		}
		char rangeText[ 32 ] = {};
		std::snprintf( rangeText, sizeof( rangeText ), "[%g .. %g]", range.min, range.max );
		std::printf( "%-4u %-22s %-9s %10.4f   %-16s %s\n", id, name ? name : "", kind, plugin.GetFloatParameter( id ), rangeText, elements.c_str() );
	}
	return 0;
}

int runList( bool feed )
{
	if( feed )
	{
		Feed plugin;
		return listOf( plugin, Feed::PT_COUNT, Feed::PT_ABOUT_FIRST );
	}
	Pepper plugin;
	return listOf( plugin, Pepper::PT_COUNT, Pepper::PT_ABOUT_FIRST );
}

template< typename P >
int namesOf( P& plugin, unsigned count, const char* label )
{
	int over = 0;
	std::vector< std::string > seen;
	for( unsigned int id = 0; id < count; ++id )
	{
		const char* name = plugin.GetParamName( id );
		if( name == nullptr )
			continue;
		if( std::strlen( name ) > 16 )
		{
			std::printf( "  %s %-3u  %-28s %zu bytes\n", label, id, name, std::strlen( name ) );
			++over;
		}
		if( std::find( seen.begin(), seen.end(), std::string( name ) ) != seen.end() )
		{
			std::printf( "  %s %-3u  %-28s duplicate\n", label, id, name );
			++over;
		}
		seen.push_back( name );
	}
	return over;
}

int runNames()
{
	std::printf( "names: sixteen bytes at most, none twice, and the mixer's two load-bearing ones\n\n" );
	Pepper mixer;
	Feed feed;
	const int overMixer = namesOf( mixer, Pepper::PT_COUNT, "SW Pepper" );
	const int overFeed  = namesOf( feed, Feed::PT_COUNT, "SW Pepper Feed" );
	Check( overMixer == 0, fmt( "SW Pepper: %.0f parameter names over 16 bytes or duplicated, of %.0f", overMixer, Pepper::PT_COUNT ) );
	Check( overFeed == 0, fmt( "SW Pepper Feed: %.0f parameter names over 16 bytes or duplicated, of %.0f", overFeed, Feed::PT_COUNT ) );

	const std::string ids[ 2 ]   = { names::kMixerId, names::kFeedId };
	const std::string shown[ 2 ] = { names::kMixerName, names::kFeedName };
	Check( ids[ 0 ].size() == 4 && ids[ 1 ].size() == 4 && ids[ 0 ] != ids[ 1 ], "the two plugin IDs are four bytes and different (" + ids[ 0 ] + ", " + ids[ 1 ] + ")" );
	Check( shown[ 0 ].size() <= 16 && shown[ 1 ].size() <= 16,
	       fmt( "the display names fit Resolume's sixteen bytes (%.0f and %.0f)", shown[ 0 ].size(), shown[ 1 ].size() ) );

	//Index 0 is sacrificial: Arena does not expose a mixer's first parameter
	//(genlock, wipe, relay, lenticular), so its default must be right for ever.
	const char* first = mixer.GetParamName( 0 );
	Check( first != nullptr && std::strcmp( first, "Linear Light" ) == 0 && mixer.GetParamType( 0 ) == FF_TYPE_BOOLEAN && mixer.GetFloatParameter( 0 ) == 1.0f,
	       std::string( "parameter 0, which Arena hides, is " ) + ( first ? first : "(none)" ) + " (boolean, default on)" );
	const char* opacity = mixer.GetParamName( Pepper::PT_OPACITY );
	Check( opacity != nullptr && std::strcmp( opacity, "Opacity" ) == 0 && mixer.GetParamType( Pepper::PT_OPACITY ) == FF_TYPE_STANDARD
	           && mixer.GetFloatParameter( Pepper::PT_OPACITY ) == 1.0f,
	       "the source's light is a standard parameter named Opacity (Arena binds it to the layer fader), default 1" );

	//The feed and the simulator's pyramid read the same two controls the
	//same way: same names, same defaults, same elements.
	bool same = std::strcmp( mixer.GetParamName( Pepper::PT_SIDES ), feed.GetParamName( Feed::PT_SIDES ) ) == 0
	            && std::strcmp( mixer.GetParamName( Pepper::PT_CENTRE ), feed.GetParamName( Feed::PT_CENTRE ) ) == 0
	            && mixer.GetFloatParameter( Pepper::PT_SIDES ) == feed.GetFloatParameter( Feed::PT_SIDES )
	            && mixer.GetFloatParameter( Pepper::PT_CENTRE ) == feed.GetFloatParameter( Feed::PT_CENTRE )
	            && mixer.GetNumParamElements( Pepper::PT_SIDES ) == feed.GetNumParamElements( Feed::PT_SIDES );
	for( unsigned e = 0; same && e < mixer.GetNumParamElements( Pepper::PT_SIDES ); ++e )
		same = std::strcmp( mixer.GetParamElementName( Pepper::PT_SIDES, e ), feed.GetParamElementName( Feed::PT_SIDES, e ) ) == 0;
	Check( same, "Sides and Centre are the same controls, defaults and elements in both plugins" );
	//Both plugins convert through the same two functions in Controls.cpp;
	//--feed shows the two layouts agree in the picture.
	Check( SidesFromParam( 0.0f ) == 4 && SidesFromParam( 1.0f ) == 3 && std::fabs( CentreFromParam( ParamForCentre( kCentreDefault ) ) - kCentreDefault ) < 1e-6,
	       "Sides element 0 is four, 1 is three; Centre's mapping inverts to its default" );
	return failures == 0 ? 0 : 1;
}

//---------------------------------------------------------------------------
// --mixer: two inputs, two sizes, two MaxUVs; the stage through the glass
// and the screen in it each land where they should; nothing of the padding
// reaches the picture. The negative control folds Dest's MaxUV into the
// vertex shader: the genlock trap.
//---------------------------------------------------------------------------
void mixerCheck( int outW, int outH, int fault )
{
	const InputSpec dest = InputSpec::Padded( 200, 120, 256, 256 );
	const InputSpec src  = InputSpec::Padded( 96, 70, 128, 128 );
	const QuadCard destCard = quadCard( dest.usedW, dest.usedH, false );
	const QuadCard srcCard  = quadCard( src.usedW, src.usedH, true );

	MixRig rig;
	if( !rig.Init( outW, outH, dest, src, true, fault ) )
	{
		Check( false, "the rig initialises" );
		return;
	}
	physics( rig );
	const World w = worldFor( asked( rig.plugin ), outW, outH, static_cast< double >( src.usedW ) / src.usedH );
	if( !quiet )
		std::printf( "  out %dx%d; Dest %dx%d of %dx%d; Src %dx%d of %dx%d\n", outW, outH, dest.usedW, dest.usedH, dest.hwW, dest.hwH, src.usedW,
		             src.usedH, src.hwW, src.hwH );

	auto renderWith = [ & ]( const Image& d, const Image& s ) {
		rig.UploadDest( d );
		rig.UploadSrc( s );
		rig.Render();
		return rig.PixelsF();
	};
	const ImageF tMap   = renderWith( flatField( dest.usedW, dest.usedH, kWhite ), flatField( src.usedW, src.usedH, kBlack ) );
	const ImageF stage  = renderWith( destCard.image, flatField( src.usedW, src.usedH, kBlack ) );
	const ImageF rMap   = renderWith( flatField( dest.usedW, dest.usedH, kBlack ), flatField( src.usedW, src.usedH, kWhite ) );
	const ImageF ghost  = renderWith( flatField( dest.usedW, dest.usedH, kBlack ), srcCard.image );

	//Each picture divided by its own white: what the plugin SAMPLED, per
	//pixel, free of the glass's own Fresnel weights.
	auto ratio = [ & ]( const ImageF& img, const ImageF& white, int x, int y, double out[ 3 ] ) {
		const double m = channel( white, outW, x, y, 0 );
		for( int c = 0; c < 3; ++c )
			out[ c ] = m > 1e-3 ? channel( img, outW, x, y, c ) / channel( white, outW, x, y, c ) : -1.0;
		return m > 1e-3;
	};

	for( int side = 0; side < 2; ++side )
	{
		const bool isDest       = side == 0;
		const QuadCard& card    = isDest ? destCard : srcCard;
		const ImageF& img       = isDest ? stage : ghost;
		const ImageF& white     = isDest ? tMap : rMap;
		const char* label       = isDest ? "Dest, seen through the glass" : "Src, in the glass";
		int sentinel = 0, checked = 0;
		double worst = 0.0;
		double sx = 0, sy = 0, mass = 0;
		const int markerHalf = l1( card.marker, card.quadrant[ 0 ] ) / 2;
		for( int y = 0; y < outH; ++y )
			for( int x = 0; x < outW; ++x )
			{
				double r[ 3 ];
				if( !ratio( img, white, x, y, r ) )
					continue;
				const Rgb seen = { toByte( r[ 0 ] ), toByte( r[ 1 ] ), toByte( r[ 2 ] ) };
				if( l1( seen, kSentinel ) < kSentinelNear )
					++sentinel;
				//The marker's colour, nearer it than any quadrant's: Src's
				//marker is closer to its own quadrant 1 than to quadrant 0.
				int nearestQuadrant = 1 << 20;
				for( const Rgb& q : card.quadrant )
					nearestQuadrant = std::min( nearestQuadrant, l1( seen, q ) );
				if( l1( seen, card.marker ) < std::min( markerHalf, nearestQuadrant ) )
				{
					sx += x + 0.5;
					sy += y + 0.5;
					mass += 1.0;
				}
				//Which quadrant every subsample of this pixel sampled, if one.
				int quadrant = -1;
				bool clean   = true;
				for( const auto& q : kSub )
				{
					double u, v;
					if( isDest )
					{
						//Dest is sampled once, at the pixel's centre.
						u = ( x + 0.5 ) / outW;
						v = ( y + 0.5 ) / outH;
					}
					else
					{
						const PaneHit h = paneRay( w, rayAt( w, x + 0.5 + q[ 0 ], y + 0.5 + q[ 1 ] ) );
						if( !h.onScreen )
						{
							clean = false;
							break;
						}
						u = h.u;
						v = h.v;
					}
					const int usedW = isDest ? dest.usedW : src.usedW, usedH = isDest ? dest.usedH : src.usedH;
					const double ux = u * usedW, vy = v * usedH;//in texels
					//Two texels clear of every boundary: the quadrant edges and the marker.
					const bool nearEdge = std::fabs( ux - usedW / 2 ) < 2.0 || std::fabs( vy - usedH / 2 ) < 2.0 || ux < 2.0 || vy < 2.0
					                      || ux > usedW - 2.0 || vy > usedH - 2.0
					                      || ( ux > card.x0 - 2.0 && ux < card.x1 + 2.0 && vy > card.y0 - 2.0 && vy < card.y1 + 2.0 );
					const int qd = ( vy >= usedH / 2 ? 2 : 0 ) + ( ux >= usedW / 2 ? 1 : 0 );
					if( nearEdge || ( quadrant >= 0 && qd != quadrant ) )
					{
						clean = false;
						break;
					}
					quadrant = qd;
				}
				if( !clean || quadrant < 0 )
					continue;
				++checked;
				const Rgb want = card.quadrant[ quadrant ];
				worst          = std::max( { worst, std::fabs( r[ 0 ] - want.r / 255.0 ), std::fabs( r[ 1 ] - want.g / 255.0 ), std::fabs( r[ 2 ] - want.b / 255.0 ) } );
			}
		Check( sentinel == 0, std::string( "  " ) + label + fmt( ": no texture padding reached the picture (%.0f sentinel pixels)", sentinel ) );
		Check( checked > 200 && worst <= kLightTol,
		       std::string( "  " ) + label + fmt( ": each quadrant is its own flat colour (%.0f pixels, worst %.2g, tolerance %.0e)", checked, worst, kLightTol ) );

		//The marker: Dest's at its own place in the frame; Src's where the
		//pane's mirror puts the floor point.
		double wantX, wantY, tolX, tolY;
		if( isDest )
		{
			wantX = card.markerU * outW;
			wantY = card.markerV * outH;
			//One Dest texel of output, the sampling's own blur.
			tolX = static_cast< double >( outW ) / dest.usedW;
			tolY = static_cast< double >( outH ) / dest.usedH;
		}
		else
		{
			project( w, mirror( w.sheets[ 0 ], paneScreenPoint( w, card.markerU, card.markerV ) ), wantX, wantY );
			double ax, ay;
			project( w, mirror( w.sheets[ 0 ], paneScreenPoint( w, card.markerU + 1.0 / src.usedW, card.markerV + 1.0 / src.usedH ) ), ax, ay );
			tolX = std::fabs( ax - wantX ) + 0.375;
			tolY = std::fabs( ay - wantY ) + 0.375;
		}
		const double gotX = mass > 0 ? sx / mass : -1, gotY = mass > 0 ? sy / mass : -1;
		Check( mass > 0 && std::fabs( gotX - wantX ) <= tolX && std::fabs( gotY - wantY ) <= tolY,
		       std::string( "  " ) + label + fmt( ": the marker is at (%.2f, %.2f) px", gotX, gotY )
		           + fmt( ", predicted (%.2f, %.2f), tolerance (%.2f, %.2f)", wantX, wantY, tolX, tolY ) );
	}
}

int runMixer()
{
	std::printf( "a mixer takes two inputs, of two sizes, with two MaxUVs\n\n" );
	{
		MixRig rig;
		if( !rig.Init( 320, 200 ) )
			return 1;
		rig.UploadDest( videoCard( 320, 200 ) );
		rig.UploadSrc( figureCard( 320, 200 ) );
		Check( rig.RenderBroken( 2, -2 ) == FF_FAIL, "a null input ARRAY returns FF_FAIL" );
		Check( rig.RenderBroken( 0, -1 ) == FF_FAIL, "zero input textures returns FF_FAIL" );
		Check( rig.RenderBroken( 1, -1 ) == FF_FAIL, "one input texture returns FF_FAIL" );
		Check( rig.RenderBroken( 2, 0 ) == FF_FAIL, "a null Dest returns FF_FAIL" );
		Check( rig.RenderBroken( 2, 1 ) == FF_FAIL, "a null Src returns FF_FAIL" );
		Check( rig.RenderBroken( 2, -1, true ) == FF_FAIL, "a zero-sized Dest returns FF_FAIL" );
		Check( rig.Render(), "and two real inputs still render afterwards" );
	}
	{
		FeedRig rig;
		if( !rig.Init( 320, 180, InputSpec::Exact( 320, 180 ) ) )
			return 1;
		rig.process.numInputTextures = 0;
		Check( rig.plugin.ProcessOpenGL( &rig.process ) == FF_FAIL, "SW Pepper Feed: no input returns FF_FAIL" );
		rig.process.numInputTextures = 1;
		rig.inputs[ 0 ]              = nullptr;
		Check( rig.plugin.ProcessOpenGL( &rig.process ) == FF_FAIL, "SW Pepper Feed: a null input returns FF_FAIL" );
		rig.inputs[ 0 ] = &rig.inStruct;
		Check( rig.Render(), "SW Pepper Feed: and a real input still renders" );
	}
	const int outs[ 2 ][ 2 ] = { { 320, 200 }, { 320, 180 } };
	for( const auto& o : outs )
	{
		std::printf( "\n" );
		mixerCheck( o[ 0 ], o[ 1 ], kFaultNone );
		const int caught = failuresOf( [ & ] { mixerCheck( o[ 0 ], o[ 1 ], kFaultFoldedMaxUV ); } );
		Negative( caught > 0, fmt( "Dest's MaxUV folded into the vertex shader fails %.0f assertion(s)", caught ) );
	}
	return failures == 0 ? 0 : 1;
}

//---------------------------------------------------------------------------
// The pane's predicted transmittance of the stage at one pixel: the mean of
// its four subsamples, each the clear film's unpolarised T at its own
// incidence, or 1 where the subsample does not meet the pane.
//---------------------------------------------------------------------------
double predictedT( const World& w, double eta, int x, int y )
{
	double sum = 0.0;
	for( const auto& q : kSub )
	{
		const Vec3 d = rayAt( w, x + 0.5 + q[ 0 ], y + 0.5 + q[ 1 ] );
		const PaneHit h = paneRay( w, d );
		if( !h.sheet )
		{
			sum += 1.0;
			continue;
		}
		const Fresnel f = fresnelAt( h.cosi, eta );
		sum += 0.5 * ( filmT( f.Rs ) + filmT( f.Rp ) );
	}
	return 0.25 * sum;
}

//---------------------------------------------------------------------------
// --additive
//---------------------------------------------------------------------------
void additiveCheck( int W, int H, int fault, bool gammaNegative )
{
	//1. The ghost of black adds nothing, to the bit, in the shipped settings.
	if( !gammaNegative )
	{
		Image blackSrc, halfSrc;
		for( int pass = 0; pass < 2; ++pass )
		{
			MixRig rig;
			if( !rig.Init( W, H, false, fault ) )
				return;
			rig.Set( "Ripple", 0.0f );
			rig.UploadDest( videoCard( W, H ) );
			Image src = graphicCard( W, H );
			for( int y = 0; y < H; ++y )
				for( int x = 0; x < W; ++x )
					if( pass == 0 || x < W / 2 )
						put( src, W, x, y, kBlack );
			rig.UploadSrc( src );
			rig.Render();
			( pass ? halfSrc : blackSrc ) = rig.Pixels();
		}
		//The ghost of Src's column u = 1/2 stands at the frame's centre
		//column (the camera is square on), and the pane does not move x.
		int differ = 0, differRight = 0;
		for( int y = 0; y < H; ++y )
			for( int x = 0; x < W; ++x )
				for( int c = 0; c < 4; ++c )
				{
					const size_t at = ( static_cast< size_t >( y ) * W + x ) * 4 + c;
					if( blackSrc[ at ] != halfSrc[ at ] )
						( x < W / 2 - 1 ? differ : differRight )++;
				}
		Check( differ == 0, fmt( "  a black Src adds exactly nothing: the left half, over its black, is the black-Src picture to the bit (%.0f bytes differ)", differ ) );
		Check( differRight > 0, fmt( "  (and the right half, over the picture, is not: %.0f bytes differ)", differRight ) );
	}

	//2. Nothing is darker than the stage dimmed by the glass, for any Src;
	//with a black Src it IS that, to the float.
	{
		MixRig rig;
		if( !rig.Init( W, H, true, fault ) )
			return;
		physics( rig );
		rig.Set( "Source Level", ParamForSourceStops( kSourceStopsDefault ) );
		const Image stage = videoCard( W, H );
		rig.UploadDest( stage );
		const Asked a = asked( rig.plugin );
		const World w = worldFor( a, W, H, static_cast< double >( W ) / H );
		double worstBlack = 0.0, worstDarker = 0.0;
		for( int pass = 0; pass < 2; ++pass )
		{
			rig.UploadSrc( pass == 0 ? flatField( W, H, kBlack ) : graphicCard( W, H ) );
			rig.Render();
			const ImageF out = rig.PixelsF();
			for( int y = 0; y < H; ++y )
				for( int x = 0; x < W; ++x )
				{
					const double T     = predictedT( w, a.eta, x, y );
					const size_t at    = ( static_cast< size_t >( y ) * W + x ) * 4;
					const double alpha = stage[ at + 3 ] / 255.0;
					const double shownA = out[ at + 3 ];
					for( int c = 0; c < 3; ++c )
					{
						//Linear Light off: the stage's light is its signal, premultiplied.
						const double want = T * stage[ at + c ] / 255.0 * alpha;
						const double got  = out[ at + c ] * shownA;
						if( pass == 0 )
							worstBlack = std::max( worstBlack, std::fabs( got - want ) );
						else
							worstDarker = std::max( worstDarker, want - got );
					}
				}
		}
		Check( worstBlack <= kLightTol, fmt( "  under a black Src the stage is dimmed by exactly the film's T at each pixel's incidence (worst %.2g, tolerance %.0e)", worstBlack, kLightTol ) );
		Check( worstDarker <= kLightTol, fmt( "  under any Src no pixel is darker than that dimmed stage (worst %.2g below, tolerance %.0e)", worstDarker, kLightTol ) );
	}

	//3. Superposition, in linear light: the picture of stage and screen is
	//the picture of the stage plus the picture of the screen.
	{
		MixRig rig;
		if( !rig.Init( W, H, true, fault ) )
			return;
		rig.Set( "Ripple", 0.0f );
		rig.Set( "Linear Light", gammaNegative ? 0.0f : 1.0f );
		rig.Set( "Source Level", ParamForSourceStops( 0.0 ) );
		rig.Set( "Stage Level", ParamForStageStops( -1.0 ) );
		ImageF pics[ 3 ];
		const Image destOf[ 3 ] = { videoCard( W, H ), videoCard( W, H ), flatField( W, H, kBlack ) };
		const Image srcOf[ 3 ]  = { graphicCard( W, H ), flatField( W, H, kBlack ), graphicCard( W, H ) };
		for( int k = 0; k < 3; ++k )
		{
			rig.UploadDest( destOf[ k ] );
			rig.UploadSrc( srcOf[ k ] );
			rig.Render();
			pics[ k ] = rig.PixelsF();
		}
		double worst = 0.0;
		long n       = 0;
		for( size_t i = 0; i < pics[ 0 ].size(); i += 4 )
			for( int c = 0; c < 3; ++c )
			{
				double L[ 3 ];
				for( int k = 0; k < 3; ++k )
					L[ k ] = decodeSrgb( pics[ k ][ i + c ] * pics[ k ][ i + 3 ] );
				if( L[ 0 ] >= 0.999 )
					continue;
				worst = std::max( worst, std::fabs( L[ 0 ] - L[ 1 ] - L[ 2 ] ) );
				++n;
			}
		Check( n > 1000 && worst <= kLightTol,
		       fmt( "  L(stage, screen) = L(stage, black) + L(black, screen) in linear light, %.0f samples (worst %.2g, tolerance %.0e)", n, worst, kLightTol ) );

		//Superposition holds under ANY decode the encode inverts, so it
		//cannot see a wrong one. The stage alone pins it: its light is
		//T x Stage Level x the card's own light, by IEC 61966-2-1. (The
		//gamma-space negative control fails this too.)
		{
			const Asked a = asked( rig.plugin );
			const World w = worldFor( a, W, H, static_cast< double >( W ) / H );
			const Image& stage = destOf[ 1 ];
			double worstStage  = 0.0;
			for( int y = 0; y < H; ++y )
				for( int x = 0; x < W; ++x )
				{
					const double T  = predictedT( w, a.eta, x, y );
					const size_t at = ( static_cast< size_t >( y ) * W + x ) * 4;
					for( int c = 0; c < 3; ++c )
					{
						const double want = T * a.stageGain * decodeSrgb( stage[ at + c ] / 255.0 * stage[ at + 3 ] / 255.0 );
						const double got  = decodeSrgb( pics[ 1 ][ at + c ] * pics[ 1 ][ at + 3 ] );
						worstStage        = std::max( worstStage, std::fabs( got - want ) );
					}
				}
			Check( worstStage <= kLightTol, fmt( "  and L(stage, black) is T x Stage Level x the card's sRGB-decoded light (worst %.2g, tolerance %.0e)", worstStage, kLightTol ) );
		}
	}
}

int runAdditive()
{
	std::printf( "the ghost only ever adds light: black disappears, nothing is darker than the dimmed stage\n" );
	for( const auto& r : kRasters )
	{
		std::printf( "\n  %dx%d\n", r[ 0 ], r[ 1 ] );
		additiveCheck( r[ 0 ], r[ 1 ], kFaultNone, false );
		const int over  = failuresOf( [ & ] { additiveCheck( r[ 0 ], r[ 1 ], kFaultAlphaOver, false ); } );
		const int gamma = failuresOf( [ & ] { additiveCheck( r[ 0 ], r[ 1 ], kFaultNone, true ); } );
		Negative( over > 0, fmt( "the ghost laid OVER the stage (a dark source darkens it) fails %.0f assertion(s)", over ) );
		Negative( gamma > 0, fmt( "the add done in gamma space fails %.0f assertion(s)", gamma ) );
	}
	return failures == 0 ? 0 : 1;
}

//---------------------------------------------------------------------------
// --energy
//---------------------------------------------------------------------------
void energyCheck( int W, int H, int fault, bool tinted, int onlySheet = -1 )
{
	MixRig rig;
	if( !rig.Init( W, H, true, fault ) )
		return;
	physics( rig );
	const double tint[ 3 ] = { 0.9, 0.7, 0.5 };
	if( tinted )
	{
		rig.Set( "Tint", static_cast< float >( tint[ 0 ] ) );
		rig.Set( "Tint_Green", static_cast< float >( tint[ 1 ] ) );
		rig.Set( "Tint_Blue", static_cast< float >( tint[ 2 ] ) );
	}
	for( int k = 0; k < static_cast< int >( SheetKind::Count ); ++k )
	{
		if( onlySheet >= 0 && k != onlySheet )
			continue;
		if( tinted && k != static_cast< int >( SheetKind::FloatGlass ) )
			continue;
		rig.Set( "Sheet", static_cast< float >( k ) );
		const Asked a = asked( rig.plugin );
		const World w = worldFor( a, W, H, static_cast< double >( W ) / H );

		rig.UploadDest( flatField( W, H, kBlack ) );
		rig.UploadSrc( flatField( W, H, kWhite ) );
		rig.Render();
		const ImageF R = rig.PixelsF();
		rig.UploadDest( flatField( W, H, kWhite ) );
		rig.UploadSrc( flatField( W, H, kBlack ) );
		rig.Render();
		const ImageF T = rig.PixelsF();

		double worst = 0.0, leastLoss = 1.0;
		long n       = 0;
		for( int y = 0; y < H; ++y )
			for( int x = 0; x < W; ++x )
			{
				if( !pixelOnScreen( w, x, y, 0.002 ) )
					continue;
				++n;
				double want[ 3 ] = { 1.0, 1.0, 1.0 };
				if( tinted )
				{
					for( int c = 0; c < 3; ++c )
					{
						double sum = 0.0;
						for( const auto& q : kSub )
						{
							const PaneHit h = paneRay( w, rayAt( w, x + 0.5 + q[ 0 ], y + 0.5 + q[ 1 ] ) );
							const Fresnel f = fresnelAt( h.cosi, a.eta );
							const double tp = std::pow( a.tint[ c ], 1.0 / f.cosT );
							sum += 0.5 * ( filmR( f.Rs, tp ) + filmT( f.Rs, tp ) + filmR( f.Rp, tp ) + filmT( f.Rp, tp ) );
						}
						want[ c ] = 0.25 * sum;
					}
				}
				for( int c = 0; c < 3; ++c )
				{
					const double got = channel( R, W, x, y, c ) + channel( T, W, x, y, c );
					worst            = std::max( worst, std::fabs( got - want[ c ] ) );
					leastLoss        = std::min( leastLoss, 1.0 - got );
				}
			}
		const char* sheet = NameOf( static_cast< SheetKind >( k ) );
		if( tinted )
			Check( n > 1000 && worst <= kLightTol && leastLoss > 0.01,
			       std::string( "  " ) + sheet + fmt( ", tint (%.1f, %.1f, %.1f): R + T falls short of 1 by the absorption", tint[ 0 ], tint[ 1 ], tint[ 2 ] )
			           + fmt( " (%.0f pixels, worst %.2g off the prediction, least loss %.3f)", n, worst, leastLoss ) );
		else
			Check( n > 1000 && worst <= kLightTol,
			       std::string( "  " ) + sheet + fmt( " (n %.3f): R + T = 1 at every pixel (%.0f pixels, worst %.2g, tolerance %.0e)", a.eta, n, worst, kLightTol ) );
	}
}

int runEnergy()
{
	std::printf( "a clear film reflects what it does not transmit: R + T = 1, per pixel, per sheet\n" );
	for( const auto& r : kRasters )
	{
		std::printf( "\n  %dx%d\n", r[ 0 ], r[ 1 ] );
		energyCheck( r[ 0 ], r[ 1 ], kFaultNone, false );
		energyCheck( r[ 0 ], r[ 1 ], kFaultNone, true );
		const int caught = failuresOf( [ & ] { energyCheck( r[ 0 ], r[ 1 ], kFaultOneSurface, false, 0 ); } );
		Negative( caught > 0, fmt( "transmission through one surface only, 1 - R, fails %.0f assertion(s)", caught ) );
	}
	return failures == 0 ? 0 : 1;
}

//---------------------------------------------------------------------------
// The film's reflectance of a POLARISED source at one pixel, as the shader
// meets it: each subsample's s and p film weights, the source's field
// projected onto that path's s direction.
//---------------------------------------------------------------------------
double predictedPolarisedR( const World& w, double eta, Vec3 E, int x, int y, bool& allOnScreen )
{
	double sum  = 0.0;
	allOnScreen = true;
	for( const auto& q : kSub )
	{
		const Vec3 d    = rayAt( w, x + 0.5 + q[ 0 ], y + 0.5 + q[ 1 ] );
		const PaneHit h = paneRay( w, d );
		if( !h.onScreen )
		{
			allOnScreen = false;
			continue;
		}
		const Fresnel f = fresnelAt( h.cosi, eta );
		Vec3 n          = w.sheets[ 0 ].normal;
		if( rig::dot( d, n ) > 0.0 )
			n = n * -1.0;
		const Vec3 s    = rig::normalize( rig::cross( d, n ) );
		const Vec3 rd   = h.reflected;
		const Vec3 e    = E - rd * rig::dot( E, rd );
		const double fs = rig::dot( e, s ) * rig::dot( e, s ) / rig::dot( e, e );
		sum += fs * filmR( f.Rs ) + ( 1.0 - fs ) * filmR( f.Rp );
	}
	return 0.25 * sum;
}

//---------------------------------------------------------------------------
// --fresnel
//---------------------------------------------------------------------------
struct BrewsterResult
{
	double nullRow = 0.0, predictedRow = 0.0, ratio = 1.0, ratioBound = 0.0;
	bool found = false;
};

/// Renders the centre column with the source polarised p (Vertical) and s
/// (Horizontal) at the sheet angle that puts Brewster's angle on the level
/// ray, and finds the p ghost's null.
BrewsterResult brewster( int W, int H, int fault, int sheet )
{
	BrewsterResult r;
	MixRig rig;
	if( !rig.Init( W, H, true, fault ) )
		return r;
	physics( rig );
	rig.Set( "Sheet", static_cast< float >( sheet ) );
	const double n = IndexOf( static_cast< SheetKind >( sheet ) );
	rig.Set( "Sheet Angle", ParamForSheetAngle( 0.5 * kPi - std::atan( n ) ) );
	rig.UploadDest( flatField( W, H, kBlack ) );
	rig.UploadSrc( flatField( W, H, kWhite ) );
	const Asked a = asked( rig.plugin );
	const World w = worldFor( a, W, H, static_cast< double >( W ) / H );

	std::vector< double > p( H ), s( H );
	for( int pass = 0; pass < 2; ++pass )
	{
		rig.Set( "Polarisation", pass == 0 ? 2.0f : 1.0f );
		rig.Render();
		const ImageF img = rig.PixelsF();
		for( int y = 0; y < H; ++y )
			( pass == 0 ? p : s )[ y ] = 0.5 * ( channel( img, W, W / 2 - 1, y, 0 ) + channel( img, W, W / 2, y, 0 ) );
	}
	std::vector< bool > valid( H, false );
	for( int y = 0; y < H; ++y )
		valid[ y ] = pixelOnScreen( w, W / 2 - 1, y, 0.002 ) && pixelOnScreen( w, W / 2, y, 0.002 );
	int best = -1;
	for( int y = 1; y + 1 < H; ++y )
		if( valid[ y - 1 ] && valid[ y ] && valid[ y + 1 ] && ( best < 0 || p[ y ] < p[ best ] ) )
			best = y;
	if( best < 0 )
		return r;
	r.found          = true;
	const double den = p[ best - 1 ] - 2.0 * p[ best ] + p[ best + 1 ];
	r.nullRow        = best + 0.5 + ( den > 0.0 ? 0.5 * ( p[ best - 1 ] - p[ best + 1 ] ) / den : 0.0 );
	r.ratio          = p[ best ] / s[ best ];

	//Where the level camera ray meets the sheet at incidence 90 - phi, a
	//ray tilted up by alpha meets it at 90 - phi - alpha: the null is at
	//alpha = 90 - phi - atan(n).
	const double alpha = 0.5 * kPi - a.angle - std::atan( n );
	r.predictedRow     = ( std::tan( alpha ) / w.tanY + 1.0 ) * 0.5 * H;

	//The bound on the null's depth: the harness's own model of that pixel,
	//its subsamples up to 7/8 px off the null and its columns off centre.
	bool on = false;
	const double modelP = 0.5 * ( predictedPolarisedR( w, n, { 0, 0, 1 }, W / 2 - 1, best, on ) + predictedPolarisedR( w, n, { 0, 0, 1 }, W / 2, best, on ) );
	const double modelS = 0.5 * ( predictedPolarisedR( w, n, { 1, 0, 0 }, W / 2 - 1, best, on ) + predictedPolarisedR( w, n, { 1, 0, 0 }, W / 2, best, on ) );
	r.ratioBound        = modelP / modelS;
	return r;
}

struct AbelesResult
{
	double residual = 1.0, modelResidual = 0.0, floatBound = 0.0, Rs = 0.0, Rp = 0.0;
};

/// At incidence 45 degrees exactly, the reflectances of one surface obey
/// R_p = R_s^2 (Abeles, 1950) whatever the index. Read off the film: its
/// R_f = 2R/(1+R) inverts to R = R_f/(2 - R_f).
AbelesResult abeles( int W, int H, int fault, int sheet )
{
	AbelesResult r;
	MixRig rig;
	if( !rig.Init( W, H, true, fault ) )
		return r;
	physics( rig );
	rig.Set( "Sheet", static_cast< float >( sheet ) );
	const double n = IndexOf( static_cast< SheetKind >( sheet ) );
	rig.UploadDest( flatField( W, H, kBlack ) );
	rig.UploadSrc( flatField( W, H, kWhite ) );

	//The sheet angle that puts the centre ray of pixel (W/2, H/2) at 45
	//degrees' incidence, by bisection on the harness's own world.
	const int px = W / 2, py = H / 2;
	double lo = 40.0 * kDegree, hi = 50.0 * kDegree;
	for( int it = 0; it < 60; ++it )
	{
		const double mid = 0.5 * ( lo + hi );
		rig.Set( "Sheet Angle", ParamForSheetAngle( mid ) );
		Asked a = asked( rig.plugin );
		a.angle = mid;
		const World w   = worldFor( a, W, H, static_cast< double >( W ) / H );
		const PaneHit h = paneRay( w, rayAt( w, px + 0.5, py + 0.5 ) );
		//A steeper sheet meets the level ray more nearly face on.
		( std::acos( h.cosi ) > 45.0 * kDegree ? lo : hi ) = mid;
	}
	rig.Set( "Sheet Angle", ParamForSheetAngle( 0.5 * ( lo + hi ) ) );
	const World w = worldFor( asked( rig.plugin ), W, H, static_cast< double >( W ) / H );

	double film[ 2 ];
	for( int pass = 0; pass < 2; ++pass )
	{
		rig.Set( "Polarisation", pass == 0 ? 1.0f : 2.0f );
		rig.Render();
		film[ pass ] = channel( rig.PixelsF(), W, px, py, 0 );
	}
	r.Rs       = film[ 0 ] / ( 2.0 - film[ 0 ] );
	r.Rp       = film[ 1 ] / ( 2.0 - film[ 1 ] );
	r.residual = std::fabs( r.Rp - r.Rs * r.Rs );
	bool on    = false;
	const double ms = predictedPolarisedR( w, n, { 1, 0, 0 }, px, py, on );
	const double mp = predictedPolarisedR( w, n, { 0, 0, 1 }, px, py, on );
	const double rs = ms / ( 2.0 - ms ), rp = mp / ( 2.0 - mp );
	r.modelResidual = std::fabs( rp - rs * rs );
	//What float roundoff can do to the measured residual: each film value
	//good to kLightBound relative, through R = R_f / (2 - R_f) and R_s^2.
	const double errS = kLightBound * film[ 0 ] * 2.0 / ( ( 2.0 - film[ 0 ] ) * ( 2.0 - film[ 0 ] ) );
	const double errP = kLightBound * film[ 1 ] * 2.0 / ( ( 2.0 - film[ 1 ] ) * ( 2.0 - film[ 1 ] ) );
	r.floatBound      = errP + 2.0 * r.Rs * errS;
	return r;
}

void fresnelCheck( int W, int H, int fault, bool brewsterOnly )
{
	constexpr double kRowTol   = 0.1;
	constexpr double kRatioTol = 1e-4;
	constexpr double kAbelesTol = 3e-5;
	for( int k = 0; k < static_cast< int >( SheetKind::Count ); ++k )
	{
		const BrewsterResult b = brewster( W, H, fault, k );
		const char* sheet      = NameOf( static_cast< SheetKind >( k ) );
		Check( b.found && std::fabs( b.nullRow - b.predictedRow ) <= kRowTol,
		       std::string( "  " ) + sheet + fmt( ": the p ghost's null is at row %.3f, Brewster's angle puts it at %.3f (tolerance %.1f px)", b.nullRow, b.predictedRow, kRowTol ) );
		Check( b.found && b.ratio <= kRatioTol && 3.0 * b.ratioBound <= kRatioTol,
		       std::string( "  " ) + sheet + fmt( ": there p/s is %.2g -- p vanishes, s does not (tolerance %.0e, the subsample spread bounds it at %.1g)", b.ratio, kRatioTol, b.ratioBound ) );
		if( brewsterOnly )
			continue;
		const AbelesResult a = abeles( W, H, fault, k );
		const double tol     = kAbelesTol;
		Check( a.residual <= tol && 3.0 * ( a.modelResidual + a.floatBound ) <= tol,
		       std::string( "  " ) + sheet + fmt( ": at 45 degrees R_p = R_s^2 (R_s %.5f, R_p %.6f, residual %.1g, tolerance %.0e)", a.Rs, a.Rp, a.residual, tol ) );
	}
	if( brewsterOnly )
		return;

	//s rises with incidence: down the centre column of a 45-degree pane.
	MixRig rig;
	if( !rig.Init( W, H, true, fault ) )
		return;
	physics( rig );
	rig.Set( "Polarisation", 1.0f );
	rig.UploadDest( flatField( W, H, kBlack ) );
	rig.UploadSrc( flatField( W, H, kWhite ) );
	rig.Render();
	const ImageF img = rig.PixelsF();
	const World w    = worldFor( asked( rig.plugin ), W, H, static_cast< double >( W ) / H );
	int rows = 0, wrong = 0;
	for( int y = 0; y + 1 < H; ++y )
	{
		if( !pixelOnScreen( w, W / 2, y, 0.002 ) || !pixelOnScreen( w, W / 2, y + 1, 0.002 ) )
			continue;
		++rows;
		//Up the picture the camera ray tilts up and meets the sheet more
		//nearly face on.
		if( !( channel( img, W, W / 2, y + 1, 0 ) < channel( img, W, W / 2, y, 0 ) ) )
			++wrong;
	}
	Check( rows > H / 4 && wrong == 0, fmt( "  s rises monotonically with incidence down the centre column (%.0f rows, %.0f out of order)", rows, wrong ) );
}

int runFresnel()
{
	std::printf( "Fresnel, out of the picture: Brewster's null for p, and Abeles' R_p = R_s^2 at 45 degrees\n" );
	for( const auto& r : kRasters )
	{
		std::printf( "\n  %dx%d\n", r[ 0 ], r[ 1 ] );
		fresnelCheck( r[ 0 ], r[ 1 ], kFaultNone, false );
		const int cosSwap = failuresOf( [ & ] { fresnelCheck( r[ 0 ], r[ 1 ], kFaultRpCosSwap, true ); } );
		const int swapSP  = failuresOf( [ & ] { fresnelCheck( r[ 0 ], r[ 1 ], kFaultSwapSP, true ); } );
		Negative( cosSwap > 0, fmt( "r_p with cos(i) and cos(t) swapped (no null) fails %.0f assertion(s)", cosSwap ) );
		Negative( swapSP > 0, fmt( "s and p swapped fails %.0f assertion(s)", swapSP ) );
	}
	return failures == 0 ? 0 : 1;
}

//---------------------------------------------------------------------------
// --double
//---------------------------------------------------------------------------
Vec3 refractInto( Vec3 d, Vec3 n, double eta )
{
	//GLSL's refract( I, N, eta ): N faces the incoming side.
	const double c = rig::dot( n, d );
	const double k = 1.0 - eta * eta * ( 1.0 - c * c );
	return d * eta - n * ( eta * c + std::sqrt( std::max( 0.0, k ) ) );
}

Vec3 reflectOff( Vec3 d, Vec3 n )
{
	return d - n * ( 2.0 * rig::dot( d, n ) );
}

/// Where image k of a thick pane's reflection lands on the floor screen,
/// for one camera ray: k = 0 the front surface, k >= 1 the back surface's
/// k-th, each a round trip of 2 t tan(theta_t) further along the pane.
bool imageLanding( const World& w, Vec3 d, double eta, double thickness, int k, double onePass, double& v )
{
	const PaneHit h = paneRay( w, d );
	if( !h.sheet )
		return false;
	Vec3 nf = w.sheets[ 0 ].normal;
	if( rig::dot( d, nf ) > 0.0 )
		nf = nf * -1.0;
	Vec3 origin = h.P;
	if( k > 0 )
	{
		const Vec3 dt = refractInto( d, nf, 1.0 / eta );
		const Vec3 dr = reflectOff( dt, nf );
		const double depth = onePass * thickness;
		for( int m = 0; m < k; ++m )
		{
			const Vec3 back = origin + dt * ( depth / rig::dot( dt, nf * -1.0 ) );
			origin          = back + dr * ( depth / rig::dot( dr, nf ) );
		}
	}
	const Vec3 rd = reflectOff( d, nf );
	if( rd.y >= 0.0 )
		return false;
	const Vec3 L = origin + rd * ( -origin.y / rd.y );
	v            = ( L.z - w.zMin ) / ( w.zMax - w.zMin );
	return true;
}

void doubleCheck( int W, int H, int fault, const char* fragment = nullptr )
{
	constexpr int kSrcW = 640, kSrcH = 360;
	MixRig rig;
	if( !rig.Init( W, H, InputSpec::Exact( W, H ), InputSpec::Exact( kSrcW, kSrcH ), true, fault, fragment ) )
		return;
	physics( rig );
	rig.Set( "Sheet", static_cast< float >( SheetKind::FloatGlass ) );
	rig.Set( "Thickness", 1.0f );
	const double tint[ 3 ] = { 0.9, 0.75, 0.6 };
	rig.Set( "Tint", static_cast< float >( tint[ 0 ] ) );
	rig.Set( "Tint_Green", static_cast< float >( tint[ 1 ] ) );
	rig.Set( "Tint_Blue", static_cast< float >( tint[ 2 ] ) );
	rig.Set( "Polarisation", 1.0f );//s, at the centre column
	rig.UploadDest( flatField( W, H, kBlack ) );
	const Asked a  = asked( rig.plugin );
	const World w  = worldFor( a, W, H, static_cast< double >( kSrcW ) / kSrcH );
	const double t = a.thickness * 1.0;//a pane's rig size is the world unit

	//1. The weights, at one pixel: Src cut between image k and image k+1, so
	//exactly images 0..k land on white.
	const int px = W / 2, py = H / 2 + H / 12;
	double lo[ 4 ], hi[ 4 ];
	for( int k = 0; k < 4; ++k )
	{
		lo[ k ] = 1e9;
		hi[ k ] = -1e9;
		for( const auto& q : kSub )
		{
			double v = 0.0;
			imageLanding( w, rayAt( w, px + 0.5 + q[ 0 ], py + 0.5 + q[ 1 ] ), a.eta, t, k, 1.0, v );
			lo[ k ] = std::min( lo[ k ], v );
			hi[ k ] = std::max( hi[ k ], v );
		}
	}
	const bool rising = lo[ 1 ] > hi[ 0 ];
	double P[ 4 ][ 3 ];
	bool resolvable = true;
	for( int k = 0; k < 4; ++k )
	{
		Image card;
		if( k == 3 )
			card = flatField( kSrcW, kSrcH, kWhite );
		else
		{
			const double gapLo = rising ? hi[ k ] : hi[ k + 1 ];
			const double gapHi = rising ? lo[ k + 1 ] : lo[ k ];
			const int row      = static_cast< int >( std::lround( 0.5 * ( gapLo + gapHi ) * kSrcH ) );
			resolvable         = resolvable && row - gapLo * kSrcH >= 2.0 && gapHi * kSrcH - row >= 2.0;
			card               = edgeCard( kSrcW, kSrcH, row, !rising );
		}
		rig.UploadSrc( card );
		rig.Render();
		const ImageF img = rig.PixelsF();
		for( int c = 0; c < 3; ++c )
			P[ k ][ c ] = channel( img, W, px, py, c );
	}
	Check( resolvable, "  the images are two Src texels or more apart at the measured pixel, so each can be cut out alone" );

	const PaneHit h  = paneRay( w, rayAt( w, px + 0.5, py + 0.5 ) );
	const Fresnel fz = fresnelAt( h.cosi, a.eta );
	double worst1 = 0.0, worst2 = 0.0, worstSum = 0.0;
	for( int c = 0; c < 3; ++c )
	{
		const double tp = std::pow( a.tint[ c ], 1.0 / fz.cosT );
		const double W0 = P[ 0 ][ c ], W1 = P[ 1 ][ c ] - P[ 0 ][ c ], W2 = P[ 2 ][ c ] - P[ 1 ][ c ];
		const double w1 = ( 1.0 - W0 ) * ( 1.0 - W0 ) * W0 * tp * tp;
		const double w2 = ( 1.0 - W0 ) * ( 1.0 - W0 ) * W0 * W0 * W0 * tp * tp * tp * tp;
		worst1          = std::max( worst1, std::fabs( W1 / w1 - 1.0 ) );
		worst2          = std::max( worst2, std::fabs( W2 / w2 - 1.0 ) );
		worstSum        = std::max( worstSum, std::fabs( P[ 3 ][ c ] - filmR( W0, tp ) ) );
	}
	//Relative: the subsample spread is second order (<1e-4) and W2, the
	//smallest, is a difference of two floats near 0.1 known to 1e-7.
	constexpr double kRelTol = 1e-3;
	Check( worst1 <= kRelTol, fmt( "  W1 = (1 - W0)^2 W0 tau^2 from the measured W0, tau over the refracted path (worst %.2g relative, tolerance %.0e)", worst1, kRelTol ) );
	Check( worst2 <= kRelTol, fmt( "  W2 = (1 - W0)^2 W0^3 tau^4 (worst %.2g relative, tolerance %.0e)", worst2, kRelTol ) );
	Check( worstSum <= kLightTol, fmt( "  all of them sum to the series' total (worst %.2g, tolerance %.0e)", worstSum, kLightTol ) );

	//2. Where the images land: Src cut at v = 1/2, the steps down the
	//centre column against Snell's 2 t tan(theta_t) along the pane.
	rig.UploadSrc( edgeCard( kSrcW, kSrcH, kSrcH / 2, true ) );
	rig.Render();
	const ImageF img = rig.PixelsF();
	std::vector< double > prof( H );
	for( int y = 0; y < H; ++y )
		prof[ y ] = channel( img, W, px, y, 0 );
	for( int k = 0; k < 2; ++k )
	{
		//The row whose centre ray puts image k on the edge: bracketed between
		//two pixel centres, then bisected.
		auto landingAt = [ & ]( double y, double& v ) { return imageLanding( w, rayAt( w, px + 0.5, y ), a.eta, t, k, 1.0, v ); };
		double y0 = -1.0, y1 = -1.0;
		for( int j = 0; j + 1 < H && y0 < 0.0; ++j )
		{
			double va, vb;
			if( landingAt( j + 0.5, va ) && landingAt( j + 1.5, vb ) && ( va - 0.5 ) * ( vb - 0.5 ) <= 0.0 )
			{
				y0 = j + 0.5;
				y1 = j + 1.5;
			}
		}
		if( y0 < 0.0 )
		{
			Check( false, fmt( "  image %.0f's edge is in the frame", k ) );
			continue;
		}
		double v0 = 0.0;
		landingAt( y0, v0 );
		const bool below = v0 < 0.5;
		for( int it = 0; it < 60; ++it )
		{
			const double ym = 0.5 * ( y0 + y1 );
			double vm       = 0.0;
			landingAt( ym, vm );
			( ( vm < 0.5 ) == below ? y0 : y1 ) = ym;
		}
		const double want = 0.5 * ( y0 + y1 );
		//The half-level crossing between the levels two pixels either side.
		const int jb = static_cast< int >( std::floor( want - 2.0 ) ), ja = static_cast< int >( std::floor( want + 2.0 ) );
		double got   = -1.0;
		if( jb >= 0 && ja < H )
		{
			const double mid = 0.5 * ( prof[ jb ] + prof[ ja ] );
			for( int j = jb; j < ja; ++j )
				if( ( prof[ j ] - mid ) * ( prof[ j + 1 ] - mid ) <= 0.0 && prof[ j ] != prof[ j + 1 ] )
				{
					got = j + 0.5 + ( mid - prof[ j ] ) / ( prof[ j + 1 ] - prof[ j ] );
					break;
				}
		}
		constexpr double kStepTol = 0.5;
		Check( got >= 0.0 && std::fabs( got - want ) <= kStepTol,
		       fmt( "  image %.0f's edge is at row %.2f, predicted %.2f (tolerance %.1f px)", k, got, want, kStepTol ) );
	}
}

int runDouble()
{
	std::printf( "a thick pane: the front image, then the back's, each tinted twice more and 2 t tan(theta_t) along\n" );
	for( const auto& r : kRasters )
	{
		std::printf( "\n  %dx%d\n", r[ 0 ], r[ 1 ] );
		doubleCheck( r[ 0 ], r[ 1 ], kFaultNone );
		const int onePass  = failuresOf( [ & ] { doubleCheck( r[ 0 ], r[ 1 ], kFaultOnePass ); } );
		const int tintOnce = failuresOf( [ & ] { doubleCheck( r[ 0 ], r[ 1 ], kFaultTintOnce ); } );
		Negative( onePass > 0, fmt( "the back image displaced by one pass, t tan(theta_t), fails %.0f assertion(s)", onePass ) );
		Negative( tintOnce > 0, fmt( "the back image tinted once, not twice, fails %.0f assertion(s)", tintOnce ) );
	}
	return failures == 0 ? 0 : 1;
}

//---------------------------------------------------------------------------
// --geometry
//---------------------------------------------------------------------------
struct View
{
	double angle, elevation, azimuth;
};

const std::vector< Dot > kCorners = { { 0.2, 0.2 }, { 0.8, 0.2 }, { 0.2, 0.8 }, { 0.8, 0.8 }, { 0.62, 0.55 } };

/// Every dot of kCorners measured and predicted for one view. Indices
/// follow kCorners: 0 BL, 1 BR, 2 TL, 3 TR, 4 the orientation dot.
struct DotFit
{
	bool visible[ 5 ] = {};///< predicted in view
	bool found[ 5 ]   = {};///< and measured there
	double gx[ 5 ] = {}, gy[ 5 ] = {}, px[ 5 ] = {}, py[ 5 ] = {}, tol[ 5 ] = {};
};

DotFit fitDots( int W, int H, int fault, const View& v )
{
	DotFit fit;
	constexpr int kSrcW = 640, kSrcH = 360, kSide = 10;
	MixRig rig;
	if( !rig.Init( W, H, InputSpec::Exact( W, H ), InputSpec::Exact( kSrcW, kSrcH ), true, fault ) )
		return fit;
	physics( rig );
	rig.Set( "Sheet Angle", ParamForSheetAngle( v.angle * kDegree ) );
	rig.Set( "Elevation", ParamForElevation( v.elevation * kDegree ) );
	rig.Set( "Azimuth", ParamForAzimuth( v.azimuth * kDegree ) );
	rig.UploadDest( flatField( W, H, kBlack ) );
	rig.UploadSrc( dotsCard( kSrcW, kSrcH, kCorners, kSide ) );
	rig.Render();
	const ImageF img = rig.PixelsF();
	const World w    = worldFor( asked( rig.plugin ), W, H, static_cast< double >( kSrcW ) / kSrcH );
	for( size_t i = 0; i < kCorners.size(); ++i )
	{
		const Dot c    = dotCentre( kCorners[ i ], kSrcW, kSrcH, kSide );
		const Vec3 S   = paneScreenPoint( w, c.u, c.v );
		const Vec3 img3 = mirror( w.sheets[ 0 ], S );
		double x, y;
		if( !project( w, img3, x, y ) )
			continue;
		//Visible only if the ray to the mirror image meets the pane and comes
		//back down onto this very point of the screen.
		const PaneHit h = paneRay( w, rig::normalize( img3 - w.eye ) );
		if( !h.onScreen || std::fabs( h.u - c.u ) > 1e-6 || std::fabs( h.v - c.v ) > 1e-6 )
			continue;
		//The dot's corners, projected: its size, and how far the projective
		//map moves the centroid off the projected centre.
		double cx = 0, cy = 0, extent = 0;
		const double hu = 0.5 * kSide / kSrcW, hv = 0.5 * kSide / kSrcH;
		const double du[ 4 ] = { -hu, hu, -hu, hu }, dv[ 4 ] = { -hv, -hv, hv, hv };
		for( int k = 0; k < 4; ++k )
		{
			double ex, ey;
			project( w, mirror( w.sheets[ 0 ], paneScreenPoint( w, c.u + du[ k ], c.v + dv[ k ] ) ), ex, ey );
			cx += 0.25 * ex;
			cy += 0.25 * ey;
			extent = std::max( extent, std::hypot( ex - x, ey - y ) );
		}
		if( x - extent < 2 || y - extent < 2 || x + extent > W - 2 || y + extent > H - 2 )
			continue;
		double mass      = 0;
		fit.visible[ i ] = true;
		fit.found[ i ]   = centroid( img, W, H, x, y, extent + 3.0, 0.0, fit.gx[ i ], fit.gy[ i ], mass );
		fit.px[ i ]      = x;
		fit.py[ i ]      = y;
		//Euler-Maclaurin bounds a sampled trapezoid's centroid within 1/8 px
		//here; three times that, plus twice the projective shift.
		fit.tol[ i ] = 0.375 + 2.0 * std::hypot( cx - x, cy - y );
	}
	return fit;
}

void geometryCheck( int W, int H, int fault, const std::vector< View >& views )
{
	for( const View& v : views )
	{
		const DotFit f = fitDots( W, H, fault, v );
		int seen = 0, wrong = 0;
		double worst = 0.0;
		for( int i = 0; i < 5; ++i )
		{
			if( !f.visible[ i ] )
				continue;
			++seen;
			//A dot the mirror puts in view and the picture does not show is
			//as wrong as one in the wrong place.
			const double e = f.found[ i ] ? std::hypot( f.gx[ i ] - f.px[ i ], f.gy[ i ] - f.py[ i ] ) : 1e9;
			if( f.found[ i ] )
				worst = std::max( worst, e / f.tol[ i ] );
			if( e > f.tol[ i ] )
				++wrong;
		}
		Check( seen >= 4 && wrong == 0,
		       fmt( "  sheet %.0f, elevation %.0f, azimuth %.0f: ", v.angle, v.elevation, v.azimuth )
		           + fmt( "%.0f of the %.0f dots in view where the mirror puts them (worst found %.2f of its tolerance)", seen - wrong, seen, worst ) );
		if( f.found[ 0 ] && f.found[ 1 ] && f.found[ 2 ] )
			Check( signedArea( f.gx[ 0 ], f.gy[ 0 ], f.gx[ 1 ], f.gy[ 1 ], f.gx[ 2 ], f.gy[ 2 ] ) > 0.0, "    and the ghost reads the right way round, not mirrored" );
		if( v.elevation == 0.0 && v.azimuth == 0.0 && f.found[ 0 ] && f.found[ 1 ] && f.found[ 2 ] && f.found[ 3 ] )
		{
			const double top = f.gx[ 3 ] - f.gx[ 2 ], bottom = f.gx[ 1 ] - f.gx[ 0 ];
			const double ptop = f.px[ 3 ] - f.px[ 2 ], pbottom = f.px[ 1 ] - f.px[ 0 ];
			const double ratio = top / bottom, want = ptop / pbottom;
			const double tol   = ( f.tol[ 2 ] + f.tol[ 3 ] ) / std::fabs( bottom ) + ( f.tol[ 0 ] + f.tol[ 1 ] ) * std::fabs( top ) / ( bottom * bottom );
			if( v.angle == 45.0 )
				Check( std::fabs( ratio - 1.0 ) <= tol && std::fabs( f.gx[ 0 ] - f.gx[ 2 ] ) <= f.tol[ 0 ] + f.tol[ 2 ]
				           && std::fabs( f.gy[ 2 ] - f.gy[ 3 ] ) <= f.tol[ 2 ] + f.tol[ 3 ],
				       fmt( "    at 45 degrees from a level seat the corners make a rectangle (top/bottom %.4f, tolerance %.4f)", ratio, tol ) );
			else
				Check( std::fabs( ratio - want ) <= tol && std::fabs( want - 1.0 ) > 3.0 * tol,
				       fmt( "    the keystone: top/bottom %.4f, predicted %.4f (tolerance %.4f)", ratio, want, tol ) );
		}
	}
}

int runGeometry()
{
	std::printf( "a dot on the hidden screen appears where the pane's mirror image of it is, projected\n" );
	const std::vector< View > views = { { 45, 0, 0 }, { 40, 0, 0 }, { 50, 0, 0 }, { 45, 15, 0 }, { 45, 0, 25 }, { 55, 10, -20 } };
	for( const auto& r : kRasters )
	{
		std::printf( "\n  %dx%d\n", r[ 0 ], r[ 1 ] );
		geometryCheck( r[ 0 ], r[ 1 ], kFaultNone, views );
		const int fixed    = failuresOf( [ & ] { geometryCheck( r[ 0 ], r[ 1 ], kFaultFixed45, { { 40, 0, 0 }, { 50, 0, 0 } } ); } );
		const int mirrored = failuresOf( [ & ] { geometryCheck( r[ 0 ], r[ 1 ], kFaultMirrored, { { 45, 0, 0 } } ); } );
		Negative( fixed > 0, fmt( "the reflection about a 45-degree pane whatever Sheet Angle says fails %.0f assertion(s)", fixed ) );
		Negative( mirrored > 0, fmt( "the screen's picture mirrored left to right fails %.0f assertion(s)", mirrored ) );
	}
	return failures == 0 ? 0 : 1;
}

//---------------------------------------------------------------------------
// --ripple
//---------------------------------------------------------------------------
/// The pane frame's membrane mode frequencies, from Rig.h's statement of
/// its size and the textbook f_mn ~ sqrt( (m/W)^2 + (n/L)^2 ).
std::vector< double > modeFrequencies( double f0, double A, double angle )
{
	const double Wf = A + 2.0, Lf = 1.5 / std::cos( angle );
	auto g          = [ & ]( int m, int n ) { return std::sqrt( ( m / Wf ) * ( m / Wf ) + ( n / Lf ) * ( n / Lf ) ); };
	std::vector< double > f;
	for( int k = 0; k < rig::kModeCount; ++k )
		f.push_back( f0 * g( rig::kModeM[ k ], rig::kModeN[ k ] ) / g( 1, 1 ) );
	return f;
}

void rippleCheck( int W, int H, int fault )
{
	constexpr int kFrames = 256;
	constexpr double kRate = 8.0;//frames a second
	constexpr int kSrcW = 640, kSrcH = 360, kSide = 12;
	MixRig rig;
	if( !rig.Init( W, H, InputSpec::Exact( W, H ), InputSpec::Exact( kSrcW, kSrcH ), true, fault ) )
		return;
	physics( rig );
	rig.Set( "Ripple", 1.0f );
	rig.Set( "Ripple Speed", ParamForRippleHz( 1.0 ) );
	const std::vector< Dot > srcDot = { { 0.37, 0.41 } };
	const int destSide              = std::max( 6, H / 30 );
	const Asked a                   = asked( rig.plugin );
	const World w                   = worldFor( a, W, H, static_cast< double >( kSrcW ) / kSrcH );
	const Dot c                     = dotCentre( srcDot[ 0 ], kSrcW, kSrcH, kSide );
	double gx0, gy0;
	project( w, mirror( w.sheets[ 0 ], paneScreenPoint( w, c.u, c.v ) ), gx0, gy0 );
	//The Dest marker just right of the ghost's window, so one small scissor
	//box holds both windows (below).
	const double ghostR   = 0.08 * H;
	const double markerR  = destSide + 3.0;
	const double markerX  = gx0 + ghostR + markerR + 4.0;
	const std::vector< Dot > destDot = { { markerX / W, gy0 / H } };
	rig.UploadSrc( dotsCard( kSrcW, kSrcH, srcDot, kSide ) );
	rig.UploadDest( dotsCard( W, H, destDot, destSide ) );
	const Dot dc = dotCentre( destDot[ 0 ], W, H, destSide );

	//Only the two windows are measured, so only they are shaded: the
	//scissor test leaves every fragment inside them exactly as it was and
	//saves the software renderer the other 97 % of 256 frames. The plugin
	//does not touch the scissor state; a host would not set it.
	const int x0 = std::max( 0, static_cast< int >( std::floor( gx0 - ghostR ) ) - 2 );
	const int x1 = std::min( W, static_cast< int >( std::ceil( dc.u * W + markerR ) ) + 2 );
	const int y0 = std::max( 0, static_cast< int >( std::floor( gy0 - std::max( ghostR, markerR ) ) ) - 2 );
	const int y1 = std::min( H, static_cast< int >( std::ceil( gy0 + std::max( ghostR, markerR ) ) ) + 2 );
	Check( dc.u * W + markerR + 2.0 < W, "  the marker and the ghost's window both fit the picture" );
	glScissor( x0, y0, x1 - x0, y1 - y0 );
	glEnable( GL_SCISSOR_TEST );

	//One frame every 1/8 s for 32 s: the ghost dot's track, and the Dest
	//marker's. The marker's centroid can move only as T varies across it,
	//which is smooth and a fraction of a percent: far under 0.01 px.
	std::vector< double > xs( kFrames ), ys( kFrames );
	double minX = 1e9, maxX = -1e9, minY = 1e9, maxY = -1e9, destMove = 0.0, dx0 = 0.0, dy0 = 0.0;
	for( int f = 0; f < kFrames; ++f )
	{
		rig.seconds = f / kRate;
		rig.Render();
		const ImageF img = rig.PixelsF();
		double gx, gy, mass, dx, dy, dmass;
		centroid( img, W, H, gx0, gy0, ghostR, 0.0, gx, gy, mass );
		centroid( img, W, H, dc.u * W, dc.v * H, markerR, 0.0, dx, dy, dmass );
		if( f == 0 )
		{
			dx0 = dx;
			dy0 = dy;
		}
		destMove = std::max( destMove, std::hypot( dx - dx0, dy - dy0 ) );
		xs[ f ]  = gx;
		ys[ f ]  = gy;
		minX     = std::min( minX, gx );
		maxX     = std::max( maxX, gx );
		minY     = std::min( minY, gy );
		maxY     = std::max( maxY, gy );
	}
	glDisable( GL_SCISSOR_TEST );
	const double ghostRange = std::max( maxX - minX, maxY - minY );
	Check( destMove <= 0.01, fmt( "  a Dest marker seen through the rippling film does not move (%.4f px, tolerance 0.01)", destMove ) );
	Check( ghostRange >= 0.5, fmt( "  while the ghost of a dot does (%.2f px)", ghostRange ) );

	//The spectrum of the dot's track, Hann-windowed.
	double mx = 0, my = 0;
	for( int f = 0; f < kFrames; ++f )
	{
		mx += xs[ f ] / kFrames;
		my += ys[ f ] / kFrames;
	}
	const int bins = kFrames / 2;
	std::vector< double > power( bins + 1, 0.0 );
	for( int b = 0; b <= bins; ++b )
	{
		double xr = 0, xi = 0, yr = 0, yi = 0;
		for( int f = 0; f < kFrames; ++f )
		{
			const double hann = 0.5 - 0.5 * std::cos( 2.0 * kPi * f / kFrames );
			const double ph   = -2.0 * kPi * b * f / kFrames;
			xr += hann * ( xs[ f ] - mx ) * std::cos( ph );
			xi += hann * ( xs[ f ] - mx ) * std::sin( ph );
			yr += hann * ( ys[ f ] - my ) * std::cos( ph );
			yi += hann * ( ys[ f ] - my ) * std::sin( ph );
		}
		power[ b ] = xr * xr + xi * xi + yr * yr + yi * yi;
	}
	const double binHz            = kRate / kFrames;
	const std::vector< double > fk = modeFrequencies( a.rippleHz, w.A, a.angle );
	double top                    = 0.0;
	int topBin                    = 0;
	for( int b = 1; b <= bins; ++b )
		if( power[ b ] > top )
		{
			top    = power[ b ];
			topBin = b;
		}
	auto nearMode = [ & ]( double b ) {
		for( double f : fk )
			if( std::fabs( b - f / binHz ) <= 1.5 )
				return true;
		return false;
	};
	int found = 0;
	std::string list;
	for( double f : fk )
	{
		const int centre = static_cast< int >( std::lround( f / binHz ) );
		bool peak        = false;
		for( int b = std::max( 1, centre - 1 ); b <= std::min( bins - 1, centre + 1 ); ++b )
			if( power[ b ] >= power[ b - 1 ] && power[ b ] >= power[ b + 1 ] && power[ b ] >= 0.005 * top )
				peak = true;
		found += peak ? 1 : 0;
		list += fmt( peak ? " %.3f" : " (%.3f)", f );
	}
	int stray = 0;
	for( int b = 2; b < bins; ++b )
		if( power[ b ] >= power[ b - 1 ] && power[ b ] >= power[ b + 1 ] && power[ b ] >= 0.05 * top && !nearMode( b ) )
			++stray;
	Check( found >= 4 && nearMode( topBin ),
	       fmt( "  the track's spectrum peaks at %.0f of the frame's six mode frequencies, Hz:", found ) + list
	           + fmt( " (strongest at %.3f Hz)", topBin * binHz ) );
	Check( stray == 0, fmt( "  and nowhere else above 5%% of the strongest (%.0f stray peaks)", stray ) );
}

int runRipple()
{
	std::printf( "the foil ripples as a membrane in its frame: the ghost wobbles at the modes' frequencies, the stage does not\n" );
	for( const auto& r : kRasters )
	{
		std::printf( "\n  %dx%d\n", r[ 0 ], r[ 1 ] );
		rippleCheck( r[ 0 ], r[ 1 ], kFaultNone );
		const int caught = failuresOf( [ & ] { rippleCheck( r[ 0 ], r[ 1 ], kFaultModesLocked ); } );
		Negative( caught > 0, fmt( "every mode at the fundamental's frequency fails %.0f assertion(s)", caught ) );
	}
	return failures == 0 ? 0 : 1;
}

//---------------------------------------------------------------------------
// --opacity
//---------------------------------------------------------------------------
void opacityCheck( int W, int H, int fault )
{
	//Opacity 0 is the black-Src picture, to the bit: the glass stays.
	Image atZero, blackSrc;
	ImageF atZeroF, blackSrcF;
	for( int pass = 0; pass < 2; ++pass )
	{
		for( int fl = 0; fl < 2; ++fl )
		{
			MixRig rig;
			if( !rig.Init( W, H, fl == 1, fault ) )
				return;
			rig.UploadDest( videoCard( W, H ) );
			rig.UploadSrc( pass == 0 ? figureCard( W, H ) : flatField( W, H, kBlack ) );
			rig.Set( "Opacity", pass == 0 ? 0.0f : 1.0f );
			rig.seconds = 1.25;//the shipped ripple, mid-swing
			rig.Render();
			if( fl == 0 )
				( pass == 0 ? atZero : blackSrc ) = rig.Pixels();
			else
				( pass == 0 ? atZeroF : blackSrcF ) = rig.PixelsF();
		}
	}
	int floatDiffer = 0;
	for( size_t i = 0; i < atZeroF.size(); ++i )
		if( std::memcmp( &atZeroF[ i ], &blackSrcF[ i ], 4 ) != 0 )
			++floatDiffer;
	Check( differingBytes( atZero, blackSrc ) == 0 && floatDiffer == 0,
	       fmt( "  Opacity 0 is the black-Src picture, to the bit, 8-bit and float (%.0f bytes, %.0f floats differ): the glass stays", differingBytes( atZero, blackSrc ), floatDiffer ) );

	//The ghost's light is linear in Opacity.
	MixRig rig;
	if( !rig.Init( W, H, true, fault ) )
		return;
	rig.Set( "Ripple", 0.0f );
	rig.UploadDest( flatField( W, H, kBlack ) );
	rig.UploadSrc( figureCard( W, H ) );
	rig.Set( "Opacity", 1.0f );
	rig.Render();
	const ImageF full = rig.PixelsF();
	double worst      = 0.0;
	long n            = 0;
	for( float o : { 0.25f, 0.5f, 0.75f } )
	{
		rig.Set( "Opacity", o );
		rig.Render();
		const ImageF part = rig.PixelsF();
		for( size_t i = 0; i < part.size(); i += 4 )
			for( int c = 0; c < 3; ++c )
			{
				const double L1 = decodeSrgb( full[ i + c ] * full[ i + 3 ] );
				if( L1 >= 0.999 )
					continue;
				worst = std::max( worst, std::fabs( decodeSrgb( part[ i + c ] * part[ i + 3 ] ) - o * L1 ) );
				++n;
			}
	}
	Check( worst <= kLightTol, fmt( "  the ghost's light is Opacity times its light at 1, in linear light (%.0f samples, worst %.2g, tolerance %.0e)", n, worst, kLightTol ) );
}

int runOpacity()
{
	std::printf( "Opacity is the source's light: the ghost scales with it, and at 0 the glass is still there\n" );
	for( const auto& r : kRasters )
	{
		std::printf( "\n  %dx%d\n", r[ 0 ], r[ 1 ] );
		opacityCheck( r[ 0 ], r[ 1 ], kFaultNone );
		const int caught = failuresOf( [ & ] { opacityCheck( r[ 0 ], r[ 1 ], kFaultCrossfade ); } );
		Negative( caught > 0, fmt( "Opacity as a crossfade to the bare stage fails %.0f assertion(s)", caught ) );
	}
	return failures == 0 ? 0 : 1;
}

//---------------------------------------------------------------------------
// --pyramid
//---------------------------------------------------------------------------
/// The first sheet a ray meets, or -1.
int firstSheet( const World& w, Vec3 o, Vec3 d, double& t )
{
	int best = -1;
	t        = 1e30;
	for( size_t j = 0; j < w.sheets.size(); ++j )
	{
		double tj;
		if( hitSheet( w.sheets[ j ], o, d, tj ) && tj < t )
		{
			t    = tj;
			best = static_cast< int >( j );
		}
	}
	return best;
}

/// The clip's upper part. From a level seat with four sides the FAR arm of
/// the feed is seen directly through the pyramid, squashed, across the
/// lower part of the near face's ghost (AGENTS.md) -- real, and in the way
/// of a measurement, so the dots stay above it.
const std::vector< Dot > kHighTriangle = { { 0.28, 0.62 }, { 0.72, 0.66 }, { 0.40, 0.90 } };

struct PyramidDots
{
	int seen = 0, wrong = 0;
	double worst = 0.0;
	bool orientationOk = true;
	int orientations   = 0;
};

/// The dots of kTriangle in the feed, measured from one view: through face
/// `face`'s reflection (direct = false) or straight down onto the screen.
/// `fromFeed`, if given, replaces the formula's picture points with ones
/// SW Pepper Feed was MEASURED to draw (key 3 j + i, in units of the feed's
/// short side, `feedPixel` of them to a feed pixel): the two plugins'
/// layouts compared in the picture.
using FeedPoints = std::map< int, std::pair< double, double > >;

PyramidDots pyramidDots( int W, int H, int fault, int sides, double azimuthDeg, double elevationDeg, bool direct,
                         const FeedPoints* fromFeed = nullptr, double feedPixel = 0.0, const Image* clip = nullptr, int clipW = 640, int clipH = 360 )
{
	PyramidDots r;
	constexpr int kSide = 32;
	MixRig rig;
	if( !rig.Init( W, H, InputSpec::Exact( W, H ), InputSpec::Exact( clipW, clipH ), true, fault ) )
		return r;
	physics( rig );
	rig.Set( "Rig", 1.0f );
	rig.Set( "Sides", sides == 4 ? 0.0f : 1.0f );
	rig.Set( "Screen", 0.0f );
	rig.Set( "Sheet", static_cast< float >( SheetKind::Acrylic ) );
	rig.Set( "Elevation", ParamForElevation( elevationDeg * kDegree ) );
	rig.Set( "Azimuth", ParamForAzimuth( azimuthDeg * kDegree ) );
	rig.UploadDest( flatField( W, H, kBlack ) );
	const int side = clip ? std::max( 4, clipW / 24 ) : kSide;
	rig.UploadSrc( clip ? *clip : dotsCard( clipW, clipH, kHighTriangle, side ) );
	rig.Render();
	const ImageF img = rig.PixelsF();
	const World w    = worldFor( asked( rig.plugin ), W, H, static_cast< double >( clipW ) / clipH );
	const int face   = static_cast< int >( std::lround( azimuthDeg / ( 360.0 / sides ) ) + sides ) % sides;
	for( int j = 0; j < sides; ++j )
	{
		if( !direct && j != face )
			continue;
		double gx[ 3 ], gy[ 3 ];
		bool all = true;
		for( int i = 0; i < 3; ++i )
		{
			const Dot c = dotCentre( kHighTriangle[ i ], clipW, clipH, side );
			double fx, fy;
			feedPoint( sides, w.r0, w.armW, w.armD, j, c.u, c.v, fx, fy );
			if( fromFeed )
			{
				const auto found = fromFeed->find( 3 * j + i );
				if( found == fromFeed->end() )
				{
					all = false;
					continue;
				}
				fx = found->second.first;
				fy = found->second.second;
			}
			const Vec3 S = { fx, 0.0, -fy };
			const Vec3 X = direct ? S : mirror( w.sheets[ j ], S );
			double x, y;
			if( !project( w, X, x, y ) )
			{
				all = false;
				continue;
			}
			if( !direct )
			{
				//Seen only if the ray to the image meets this face first.
				double t;
				const Vec3 d = rig::normalize( X - w.eye );
				if( firstSheet( w, w.eye, d, t ) != j || t > std::sqrt( rig::dot( X - w.eye, X - w.eye ) ) )
				{
					all = false;
					continue;
				}
			}
			//The dot's extent and its projective shift, as in --geometry.
			const double hu = 0.5 * side / clipW, hv = 0.5 * side / clipH;
			double cx = 0, cy = 0, extent = 0;
			const double du[ 4 ] = { -hu, hu, -hu, hu }, dv[ 4 ] = { -hv, -hv, hv, hv };
			for( int k = 0; k < 4; ++k )
			{
				double ex, ey, qx, qy;
				feedPoint( sides, w.r0, w.armW, w.armD, j, c.u + du[ k ], c.v + dv[ k ], qx, qy );
				const Vec3 Q = { qx, 0.0, -qy };
				project( w, direct ? Q : mirror( w.sheets[ j ], Q ), ex, ey );
				cx += 0.25 * ex;
				cy += 0.25 * ey;
				extent = std::max( extent, std::hypot( ex - x, ey - y ) );
			}
			if( x - extent < 2 || y - extent < 2 || x + extent > W - 2 || y + extent > H - 2 )
			{
				all = false;
				continue;
			}
			double mass;
			//Straight down, the screen is bright and the faces' own
			//reflections of it faint: count only what is over a third.
			if( !centroid( img, W, H, x, y, extent + 2.0, direct ? 0.3 : 0.0, gx[ i ], gy[ i ], mass ) )
			{
				all = false;
				continue;
			}
			double tol = 0.375 + 2.0 * std::hypot( cx - x, cy - y );
			if( fromFeed )
			{
				//The feed's own measurement error, 0.375 of its pixels, seen here.
				double ex, ey;
				project( w, Vec3 { fx + feedPixel, 0.0, -fy }, ex, ey );
				tol += 0.375 * std::hypot( ex - x, ey - y );
			}
			const double e = std::hypot( gx[ i ] - x, gy[ i ] - y );
			++r.seen;
			r.worst = std::max( r.worst, e / tol );
			if( e > tol )
				++r.wrong;
		}
		if( all )
		{
			++r.orientations;
			//Through a face, the right way round (as the clip); straight
			//down, the arm itself, which is the clip mirrored.
			const double area = signedArea( gx[ 0 ], gy[ 0 ], gx[ 1 ], gy[ 1 ], gx[ 2 ], gy[ 2 ] );
			if( direct ? area >= 0.0 : area <= 0.0 )
				r.orientationOk = false;
		}
	}
	return r;
}

void pyramidCheck( int W, int H, int fault )
{
	const struct
	{
		int sides;
		std::vector< double > azimuths;
	} cases[] = { { 4, { 0, 90, 180, -90 } }, { 3, { 0, 120, -120 } } };
	for( const auto& c : cases )
		for( double az : c.azimuths )
		{
			const PyramidDots d = pyramidDots( W, H, fault, c.sides, az, 0.0, false );
			Check( d.seen == 3 && d.wrong == 0 && d.orientations == 1 && d.orientationOk,
			       fmt( "  %.0f sides, from azimuth %.0f: the near face shows the clip the right way round, ", c.sides, az )
			           + fmt( "%.0f of 3 dots where its mirror puts them (worst found %.2f of tolerance)", d.seen - d.wrong, d.worst ) );
		}
	const PyramidDots top = pyramidDots( W, H, fault, 4, 0.0, 60.0, true );
	Check( top.seen >= 8 && top.wrong == 0 && top.orientations >= 2 && top.orientationOk,
	       fmt( "  from 60 degrees up the screen is seen directly: %.0f dots of %.0f arms where they are, each arm mirrored (worst found %.2f of tolerance)",
	            top.seen - top.wrong, top.orientations, top.worst ) );
}

int runPyramid()
{
	std::printf( "the pyramid: each face reflects its arm of the feed, which reads the right way round\n" );
	for( const auto& r : kRasters )
	{
		std::printf( "\n  %dx%d\n", r[ 0 ], r[ 1 ] );
		pyramidCheck( r[ 0 ], r[ 1 ], kFaultNone );
		const int caught = failuresOf( [ & ] { pyramidCheck( r[ 0 ], r[ 1 ], kFaultRotatedArms ); } );
		Negative( caught > 0, fmt( "the arms as rotated copies, not reflections, fails %.0f assertion(s)", caught ) );
	}
	return failures == 0 ? 0 : 1;
}

//---------------------------------------------------------------------------
// --feed
//---------------------------------------------------------------------------
void feedCheck( int W, int H, int fault )
{
	//The clip at about the arm's own scale, so its minification is mild.
	const int inW = W / 4, inH = H / 4;
	const int side = std::max( 4, inW / 24 );
	const Image clip = dotsCard( inW, inH, kTriangle, side );
	for( int sides : { 4, 3 } )
	{
		FeedRig rig;
		if( !rig.Init( W, H, InputSpec::Exact( inW, inH ), true, fault ) )
			return;
		rig.Set( "Sides", sides == 4 ? 0.0f : 1.0f );
		rig.Upload( clip );
		rig.Render();
		const ImageF img = rig.out.PixelsF();
		rig.Render();
		const Image bytes = rig.out.Pixels();
		const double S    = std::min( W, H );
		const double A    = static_cast< double >( inW ) / inH;
		const double c    = CentreFromParam( rig.plugin.GetFloatParameter( Feed::PT_CENTRE ) );
		const double r0   = 0.5 * c;
		const double armW = std::min( c * std::tan( kPi / sides ), A * ( 0.5 - r0 ) ), armD = armW / A;

		int seen = 0, wrong = 0, mirrored = 0;
		double worst = 0.0;
		FeedPoints measured;
		for( int j = 0; j < sides; ++j )
		{
			double gx[ 3 ] = {}, gy[ 3 ] = {};
			int found      = 0;
			for( int i = 0; i < 3; ++i )
			{
				const Dot d = dotCentre( kTriangle[ i ], inW, inH, side );
				double fx, fy;
				feedPoint( sides, r0, armW, armD, j, d.u, d.v, fx, fy );
				const double x = fx * S + 0.5 * W, y = fy * S + 0.5 * H;
				double mass;
				//Half the dot, in the arm's pixels, and some.
				const double extent = 0.75 * side * armW * S / inW + 2.0;
				if( !centroid( img, W, H, x, y, extent, 0.0, gx[ i ], gy[ i ], mass ) )
					continue;
				measured[ 3 * j + i ] = { ( gx[ i ] - 0.5 * W ) / S, ( gy[ i ] - 0.5 * H ) / S };
				++seen;
				++found;
				const double e = std::hypot( gx[ i ] - x, gy[ i ] - y );
				worst          = std::max( worst, e );
				if( e > 0.375 )
					++wrong;
			}
			if( found == 3 && signedArea( gx[ 0 ], gy[ 0 ], gx[ 1 ], gy[ 1 ], gx[ 2 ], gy[ 2 ] ) < 0.0 )
				++mirrored;
		}
		Check( seen == 3 * sides && wrong == 0,
		       fmt( "  %.0f sides: %.0f of %.0f dots where its arm puts them (worst found %.3f px, tolerance 0.375)", sides, seen - wrong, 3.0 * sides, worst ) );
		Check( mirrored == sides, fmt( "  every arm holds the clip MIRRORED, determinant -1 (%.0f of %.0f)", mirrored, sides ) );

		//Outside the arms: exactly black. Everywhere: opaque.
		int lit = 0, translucent = 0;
		for( int y = 0; y < H; ++y )
			for( int x = 0; x < W; ++x )
			{
				const size_t at = ( static_cast< size_t >( y ) * W + x ) * 4;
				if( bytes[ at + 3 ] != 255 )
					++translucent;
				const double px = ( x + 0.5 - 0.5 * W ) / S, py = ( y + 0.5 - 0.5 * H ) / S;
				bool inside     = false;
				for( int j = 0; j < sides && !inside; ++j )
				{
					const double theta = 2.0 * kPi * j / sides;
					const double ax = std::sin( theta ), ay = -std::cos( theta );
					const double r = px * ax + py * ay - r0, e = px * -ay + py * ax;
					inside         = r >= -1e-4 && r <= armD + 1e-4 && std::fabs( e ) <= 0.5 * armW + 1e-4;
				}
				if( !inside && ( bytes[ at ] || bytes[ at + 1 ] || bytes[ at + 2 ] ) )
					++lit;
			}
		Check( lit == 0 && translucent == 0, fmt( "  outside the arms exactly black (%.0f lit), alpha 255 everywhere (%.0f not)", lit, translucent ) );

		//The simulator looks into the same layout: from 60 degrees up, SW
		//Pepper's pyramid screen shows each dot where the FEED was measured
		//to put it -- the two plugins compared, not each against a formula.
		if( sides == 4 )
		{
			const PyramidDots top = pyramidDots( W, H, fault, 4, 0.0, 60.0, true, &measured, 1.0 / S, &clip, inW, inH );
			Check( top.seen >= 8 && top.wrong == 0,
			       fmt( "  SW Pepper's pyramid screen shows the feed's own dots where the feed drew them (%.0f seen from 60 degrees up, worst %.2f of tolerance)", top.seen, top.worst ) );
		}
	}
}

int runFeed()
{
	std::printf( "SW Pepper Feed: the clip in each arm, top outward, as a mirror image, over black\n" );
	for( const auto& r : kRasters )
	{
		std::printf( "\n  %dx%d\n", r[ 0 ], r[ 1 ] );
		feedCheck( r[ 0 ], r[ 1 ], kFaultNone );
		const int caught = failuresOf( [ & ] { feedCheck( r[ 0 ], r[ 1 ], kFaultRotatedArms ); } );
		Negative( caught > 0, fmt( "rotated arms, the common layout, fails %.0f assertion(s)", caught ) );
	}
	return failures == 0 ? 0 : 1;
}

//---------------------------------------------------------------------------
// --mutation
//---------------------------------------------------------------------------
int runMutation()
{
	std::printf( "one character of the shipped fragment shader fails a check\n\n" );
	const std::string shipped = AssembleFragment( kPepperFragment );
	const std::string from    = "vec3 tau  = pow( Tint, vec3( 1.0 / max( cost, 1e-4 ) ) );";
	const std::string to      = "vec3 tau  = pow( Tint, vec3( 1.0 / max( cosi, 1e-4 ) ) );";
	const size_t at           = shipped.find( from );
	Check( at != std::string::npos && shipped.find( from, at + 1 ) == std::string::npos, "the line to mutate occurs exactly once in the shipped shader" );
	if( at == std::string::npos )
		return 1;
	std::string mutated = shipped;
	mutated.replace( at, from.size(), to );
	int differ = 0;
	for( size_t i = 0; i < shipped.size(); ++i )
		if( shipped[ i ] != mutated[ i ] )
			++differ;
	Check( differ == 1, fmt( "the mutation is one character (%.0f differ): the tint's path from the angle of incidence, not of refraction", differ ) );

	Image defaultBytes, hookBytes;
	for( int pass = 0; pass < 2; ++pass )
	{
		MixRig rig;
		if( !rig.Init( 320, 180, false, kFaultNone, pass ? shipped.c_str() : nullptr ) )
			return 1;
		rig.UploadDest( stageCard( 320, 180 ) );
		rig.UploadSrc( figureCard( 320, 180 ) );
		rig.Set( "Thickness", 0.5f );
		rig.Set( "Tint_Blue", 0.7f );
		rig.seconds = 0.7;
		rig.Render();
		( pass ? hookBytes : defaultBytes ) = rig.Pixels();
	}
	Check( differingBytes( defaultBytes, hookBytes ) == 0, "the shipped text through the hook renders the default path's bytes" );

	const int shippedFails = failuresOf( [ & ] { doubleCheck( 320, 180, kFaultNone, shipped.c_str() ); } );
	Check( shippedFails == 0, "--double passes with the shipped text through the hook" );
	const int mutatedFails = failuresOf( [ & ] { doubleCheck( 320, 180, kFaultNone, mutated.c_str() ); } );
	Negative( mutatedFails > 0, fmt( "--double fails the mutated shader (%.0f assertion(s))", mutatedFails ) );
	return failures == 0 ? 0 : 1;
}

//---------------------------------------------------------------------------
// --bench
//---------------------------------------------------------------------------
double benchAt( int width, int height, int frames, bool pyramid )
{
	MixRig rig;
	if( !rig.Init( width, height ) )
		return -1.0;
	rig.UploadDest( stageCard( width, height ) );
	rig.UploadSrc( figureCard( width, height ) );
	if( pyramid )
		rig.Set( "Rig", 1.0f );
	const int warmup = 5;
	for( int frame = 0; frame < warmup; ++frame )
	{
		rig.seconds = frame / 60.0;
		rig.Render();
	}
	glFinish();
	const auto start = std::chrono::steady_clock::now();
	for( int frame = 0; frame < frames; ++frame )
	{
		rig.seconds = ( warmup + frame ) / 60.0;
		rig.Render();
	}
	glFinish();
	const auto end = std::chrono::steady_clock::now();
	return std::chrono::duration< double >( end - start ).count() * 1000.0 / frames;
}

int runBench( int frames )
{
	struct Size
	{
		const char* name;
		int width, height;
	};
	const Size sizes[] = { { "1280x720 ", 1280, 720 }, { "1920x1080", 1920, 1080 }, { "3840x2160", 3840, 2160 } };
	std::printf( "%d frames each after a 5-frame warm-up, glFinish both sides; shipped defaults (a PET foil pane, rippling), then Rig = Pyramid.\n\n", frames );
	std::printf( "resolution     pane ms/frame   %% of 60fps   | pyramid ms/frame   %% of 60fps\n" );
	for( const Size& size : sizes )
	{
		const double pane = benchAt( size.width, size.height, frames, false );
		const double pyr  = benchAt( size.width, size.height, frames, true );
		if( pane < 0.0 || pyr < 0.0 )
			return 1;
		std::printf( "%s      %7.3f       %5.1f%%     |     %7.3f        %5.1f%%\n", size.name, pane, pane / 16.667 * 100.0, pyr, pyr / 16.667 * 100.0 );
	}
	return 0;
}

//---------------------------------------------------------------------------
// --set, strictly: a value is a number, all of it, or an option's element
// name. Anything else is refused, never read as 0.
//---------------------------------------------------------------------------
template< typename P >
bool applyOne( P& plugin, unsigned count, unsigned aboutFirst, const std::string& setting )
{
	const size_t equals = setting.find( '=' );
	if( equals == std::string::npos )
	{
		std::fprintf( stderr, "--set %s: expected Name=Value\n", setting.c_str() );
		return false;
	}
	const std::string name = setting.substr( 0, equals ), text = setting.substr( equals + 1 );
	for( unsigned id = 0; id < aboutFirst && id < count; ++id )
	{
		const char* declared = plugin.GetParamName( id );
		if( declared == nullptr || name != declared )
			continue;
		char* end         = nullptr;
		const float value = std::strtof( text.c_str(), &end );
		if( !text.empty() && end != nullptr && *end == '\0' && std::isfinite( value ) )
		{
			plugin.SetFloatParameter( id, value );
			return true;
		}
		if( plugin.GetParamType( id ) == FF_TYPE_OPTION )
			for( unsigned e = 0; e < plugin.GetNumParamElements( id ); ++e )
				if( text == plugin.GetParamElementName( id, e ) )
				{
					plugin.SetFloatParameter( id, static_cast< float >( e ) );
					return true;
				}
		std::fprintf( stderr, "--set %s: '%s' is not a number%s\n", setting.c_str(), text.c_str(),
		              plugin.GetParamType( id ) == FF_TYPE_OPTION ? " or one of the option's names (try --list)" : "" );
		return false;
	}
	std::fprintf( stderr, "--set %s: no parameter called '%s' (try --list)\n", setting.c_str(), name.c_str() );
	return false;
}

template< typename P >
bool applySettings( P& plugin, unsigned count, unsigned aboutFirst, const std::vector< std::string >& settings )
{
	for( const std::string& s : settings )
		if( !applyOne( plugin, count, aboutFirst, s ) )
			return false;
	return true;
}

//---------------------------------------------------------------------------
// --pipe: the fleet's frame format, with a second input, in relay's and
// lenticular's shape.
//
//   * stdin is DEST, inputTextures[0], the layer below. Output-sized.
//   * `--pipe-src PATH` is SRC, inputTextures[1], this layer. Raw RGBA,
//     top row first, at `--src-size` (default: the output size). A FIFO
//     will do. Without it the `--input-b` card is held.
//
// Frame k is shown at k / --fps seconds (default 30), the clock the foil's
// ripple runs on. `--script` is the cue sheet: `frame Name value`, value in
// host units, '#' a comment; a STANDARD parameter ramps between keys, an
// option, boolean or event steps. Unknown names are refused before a frame
// is read. SIGPIPE is ignored: a closed stdout is exit 1.
//---------------------------------------------------------------------------
using Track = std::vector< std::pair< int, float > >;

std::map< std::string, Track > loadScript( const std::string& path, std::string& error )
{
	std::map< std::string, Track > tracks;
	std::ifstream file( path );
	if( !file )
	{
		error = "cannot open " + path;
		return tracks;
	}
	std::string line;
	int lineNumber = 0;
	while( std::getline( file, line ) )
	{
		++lineNumber;
		const size_t hash = line.find( '#' );
		if( hash != std::string::npos )
			line.erase( hash );
		std::istringstream in( line );
		int frame = 0;
		if( !( in >> frame ) )
			continue;
		std::vector< std::string > words;
		std::string word;
		while( in >> word )
			words.push_back( word );
		if( words.size() < 2 )
		{
			error = path + ":" + std::to_string( lineNumber ) + ": expected `frame Parameter Name value`";
			return {};
		}
		char* end         = nullptr;
		const float value = std::strtof( words.back().c_str(), &end );
		if( end == nullptr || *end != '\0' )
		{
			error = path + ":" + std::to_string( lineNumber ) + ": '" + words.back() + "' is not a number";
			return {};
		}
		words.pop_back();
		std::string name = words.front();
		for( size_t i = 1; i < words.size(); ++i )
			name += " " + words[ i ];
		tracks[ name ].emplace_back( frame, value );
	}
	for( auto& entry : tracks )
		std::sort( entry.second.begin(), entry.second.end() );
	return tracks;
}

float valueAt( const Track& track, int frame, bool ramps )
{
	if( track.empty() )
		return 0.0f;
	if( frame <= track.front().first )
		return track.front().second;
	if( frame >= track.back().first )
		return track.back().second;
	for( size_t i = 1; i < track.size(); ++i )
	{
		if( frame < track[ i ].first )
		{
			const auto& a = track[ i - 1 ];
			const auto& b = track[ i ];
			if( !ramps )
				return a.second;
			const float span = static_cast< float >( b.first - a.first );
			const float t    = span > 0.0f ? ( static_cast< float >( frame - a.first ) / span ) : 1.0f;
			return a.second + ( b.second - a.second ) * t;
		}
		if( frame == track[ i ].first )
			return track[ i ].second;
	}
	return track.back().second;
}

bool readFrame( int fd, Image& frame )
{
	size_t filled = 0;
	while( filled < frame.size() )
	{
		const ssize_t got = read( fd, frame.data() + filled, frame.size() - filled );
		if( got <= 0 )
			return false;
		filled += static_cast< size_t >( got );
	}
	return true;
}

bool writeAll( int fd, const Image& frame )
{
	size_t written = 0;
	while( written < frame.size() )
	{
		const ssize_t n = write( fd, frame.data() + written, frame.size() - written );
		if( n <= 0 )
			return false;
		written += static_cast< size_t >( n );
	}
	return true;
}

void flipRows( const Image& in, Image& out, int width, int height )
{
	const size_t row = static_cast< size_t >( width ) * 4;
	for( int y = 0; y < height; ++y )
		std::memcpy( out.data() + static_cast< size_t >( height - 1 - y ) * row, in.data() + static_cast< size_t >( y ) * row, row );
}

int runPipe( int width, int height, int srcWidth, int srcHeight, double fps, const std::string& scriptPath, const std::string& srcPath,
             const std::string& inputB, const std::vector< std::string >& settings )
{
	signal( SIGPIPE, SIG_IGN );
	MixRig rig;
	if( !rig.Init( width, height, InputSpec::Exact( width, height ), InputSpec::Exact( srcWidth, srcHeight ) ) )
		return 1;
	if( !applySettings( rig.plugin, Pepper::PT_COUNT, Pepper::PT_ABOUT_FIRST, settings ) )
		return 2;

	struct Automation
	{
		unsigned int id;
		bool ramps;
		Track track;
	};
	std::vector< Automation > automation;
	if( !scriptPath.empty() )
	{
		std::string error;
		const std::map< std::string, Track > tracks = loadScript( scriptPath, error );
		if( !error.empty() )
		{
			std::fprintf( stderr, "%s\n", error.c_str() );
			return 2;
		}
		for( const auto& entry : tracks )
		{
			bool found = false;
			for( unsigned int id = 0; id < Pepper::PT_ABOUT_FIRST; ++id )
			{
				const char* name = rig.plugin.GetParamName( id );
				if( name != nullptr && entry.first == name )
				{
					const unsigned int type = rig.plugin.GetParamType( id );
					const bool ramps        = type == FF_TYPE_STANDARD || type == FF_TYPE_RED || type == FF_TYPE_GREEN || type == FF_TYPE_BLUE;
					automation.push_back( { id, ramps, entry.second } );
					found = true;
					break;
				}
			}
			if( !found )
			{
				std::fprintf( stderr, "script names '%s', which is not a parameter (try --list)\n", entry.first.c_str() );
				return 2;
			}
		}
	}

	int srcFd = -1;
	if( !srcPath.empty() )
	{
		srcFd = open( srcPath.c_str(), O_RDONLY );
		if( srcFd < 0 )
		{
			std::fprintf( stderr, "cannot open --pipe-src %s\n", srcPath.c_str() );
			return 2;
		}
	}
	else
		rig.UploadSrc( generate( inputB, srcWidth, srcHeight ) );

	Image destIn( static_cast< size_t >( width ) * height * 4 ), destUp( destIn.size() );
	Image srcIn( static_cast< size_t >( srcWidth ) * srcHeight * 4 ), srcUp( srcIn.size() );
	Image out( destIn.size() );
	int index  = 0;
	int result = 0;
	for( ;; ++index )
	{
		if( !readFrame( STDIN_FILENO, destIn ) )
			break;
		if( srcFd >= 0 )
		{
			if( !readFrame( srcFd, srcIn ) )
				break;
			flipRows( srcIn, srcUp, srcWidth, srcHeight );
			rig.UploadSrc( srcUp );
		}
		flipRows( destIn, destUp, width, height );
		rig.UploadDest( destUp );
		for( const Automation& track : automation )
			rig.plugin.SetFloatParameter( track.id, valueAt( track.track, index, track.ramps ) );
		rig.seconds = index / fps;
		if( !rig.Render() )
		{
			std::fprintf( stderr, "ProcessOpenGL failed at frame %d\n", index );
			result = 1;
			break;
		}
		flipRows( rig.Pixels(), out, width, height );
		if( !writeAll( STDOUT_FILENO, out ) )
		{
			std::fprintf( stderr, "pgtest --pipe: stdout closed at frame %d\n", index );
			result = 1;
			break;
		}
	}
	if( srcFd >= 0 )
		close( srcFd );
	std::fprintf( stderr, "pgtest --pipe: %d frames\n", index );
	return result;
}

//---------------------------------------------------------------------------
void usage()
{
	std::printf(
		"pgtest -- render and measure SW Pepper (FFGL mixer) and SW Pepper Feed (effect)\n"
		"\n"
		"  --out PATH        render through the plugin (default pepper.png)\n"
		"  --plugin NAME     pepper (default) or feed, for --out, --list and --set\n"
		"  --input-a NAME    the mixer's DEST card, the stage behind the glass (default stage)\n"
		"  --input-b NAME    the mixer's SRC card, the hidden screen; the feed's input (default figure)\n"
		"                    %s\n"
		"  --src-size WxH    Src's size (default: the output size)\n"
		"  --size WxH        picture size (default 1280x720)\n"
		"  --time SECONDS    the host clock for --out (the foil's ripple), default 0\n"
		"  --set \"Name=V\"    a parameter by display name, in host units, or an option by its\n"
		"                    element name (\"Rig=Pyramid\"). Repeatable. Unparsable values are refused.\n"
		"  --list            every parameter, its kind, default and range\n"
		"  --names --mixer --additive --energy --fresnel --double --geometry --ripple\n"
		"  --opacity --pyramid --feed --mutation\n"
		"                    the checks (see the top of tools/pgtest/main.cpp)\n"
		"  --bench           ms/frame at 720p, 1080p and 4K (--frames N, default 30)\n"
		"  --pipe            raw RGBA Dest frames on stdin, raw RGBA frames on stdout\n"
		"  --pipe-src PATH   raw RGBA Src frames for --pipe (a file or FIFO); default: --input-b, held\n"
		"  --fps N           --pipe's frame rate, for the clock (default 30)\n"
		"  --script PATH     cues for --pipe: 'frame Name value'; standard parameters ramp, the rest step\n"
		"  --help\n",
		kCardNames );
}

bool parseSize( const std::string& text, int& w, int& h )
{
	const size_t x = text.find( 'x' );
	if( x == std::string::npos )
		return false;
	char* end = nullptr;
	w         = static_cast< int >( std::strtol( text.substr( 0, x ).c_str(), &end, 10 ) );
	if( end == nullptr || *end != '\0' )
		return false;
	h = static_cast< int >( std::strtol( text.substr( x + 1 ).c_str(), &end, 10 ) );
	return end != nullptr && *end == '\0' && w > 0 && h > 0;
}
} // namespace

int main( int argc, char** argv )
{
	std::string outPath = "pepper.png";
	std::string inputA = "stage", inputB = "figure", plugin = "pepper";
	int width = 1280, height = 720, srcWidth = 0, srcHeight = 0, frames = 0;
	double seconds = 0.0, fps = 30.0;
	bool wantList = false, wantBench = false, wantPipe = false;
	std::string scriptPath, srcPath, check;
	std::vector< std::string > settings;
	static const char* checks[] = { "--names", "--mixer", "--additive", "--energy", "--fresnel", "--double", "--geometry",
		                            "--ripple", "--opacity", "--pyramid", "--feed", "--mutation" };

	for( int i = 1; i < argc; ++i )
	{
		const std::string argument = argv[ i ];
		const bool hasNext         = i + 1 < argc;
		bool isCheck               = false;
		for( const char* c : checks )
			isCheck = isCheck || argument == c;
		if( argument == "--help" )
		{
			usage();
			return 0;
		}
		else if( isCheck )
			check = argument;
		else if( argument == "--out" && hasNext )
			outPath = argv[ ++i ];
		else if( argument == "--plugin" && hasNext )
		{
			plugin = argv[ ++i ];
			if( plugin != "pepper" && plugin != "feed" )
			{
				std::fprintf( stderr, "--plugin is pepper or feed\n" );
				return 2;
			}
		}
		else if( ( argument == "--input-a" || argument == "--input-b" ) && hasNext )
		{
			const std::string name = argv[ ++i ];
			if( !isCard( name ) )
			{
				std::fprintf( stderr, "%s: no card called '%s' (%s)\n", argument.c_str(), name.c_str(), kCardNames );
				return 2;
			}
			( argument == "--input-a" ? inputA : inputB ) = name;
		}
		else if( ( argument == "--size" || argument == "--src-size" ) && hasNext )
		{
			if( !parseSize( argv[ ++i ], argument == "--size" ? width : srcWidth, argument == "--size" ? height : srcHeight ) )
			{
				std::fprintf( stderr, "%s wants a positive WxH\n", argument.c_str() );
				return 2;
			}
		}
		else if( ( argument == "--time" || argument == "--fps" || argument == "--frames" ) && hasNext )
		{
			char* end          = nullptr;
			const double value = std::strtod( argv[ ++i ], &end );
			if( end == nullptr || *end != '\0' || !std::isfinite( value ) || ( argument != "--time" && value <= 0.0 ) )
			{
				std::fprintf( stderr, "%s wants a number\n", argument.c_str() );
				return 2;
			}
			if( argument == "--time" )
				seconds = value;
			else if( argument == "--fps" )
				fps = value;
			else
				frames = static_cast< int >( value );
		}
		else if( argument == "--set" && hasNext )
			settings.push_back( argv[ ++i ] );
		else if( argument == "--list" )
			wantList = true;
		else if( argument == "--bench" )
			wantBench = true;
		else if( argument == "--pipe" )
			wantPipe = true;
		else if( argument == "--pipe-src" && hasNext )
			srcPath = argv[ ++i ];
		else if( argument == "--script" && hasNext )
			scriptPath = argv[ ++i ];
		else
		{
			std::fprintf( stderr, "unknown argument: %s\n", argument.c_str() );
			usage();
			return 2;
		}
	}
	if( srcWidth == 0 )
	{
		srcWidth  = width;
		srcHeight = height;
	}

	if( wantList )
		return runList( plugin == "feed" );
	if( check == "--names" )
		return runNames();

	CGLContextObj context = createContext();
	if( context == nullptr )
	{
		std::fprintf( stderr, "could not create an OpenGL context\n" );
		return 1;
	}

	int result = 0;
	if( check == "--mixer" )
		result = runMixer();
	else if( check == "--additive" )
		result = runAdditive();
	else if( check == "--energy" )
		result = runEnergy();
	else if( check == "--fresnel" )
		result = runFresnel();
	else if( check == "--double" )
		result = runDouble();
	else if( check == "--geometry" )
		result = runGeometry();
	else if( check == "--ripple" )
		result = runRipple();
	else if( check == "--opacity" )
		result = runOpacity();
	else if( check == "--pyramid" )
		result = runPyramid();
	else if( check == "--feed" )
		result = runFeed();
	else if( check == "--mutation" )
		result = runMutation();
	else if( wantBench )
		result = runBench( frames > 0 ? frames : 30 );
	else if( wantPipe )
		result = runPipe( width, height, srcWidth, srcHeight, fps, scriptPath, srcPath, inputB, settings );
	else if( plugin == "feed" )
	{
		FeedRig rig;
		if( !rig.Init( width, height, InputSpec::Exact( srcWidth, srcHeight ) ) )
			return 1;
		rig.Upload( generate( inputB, srcWidth, srcHeight ) );
		if( !applySettings( rig.plugin, Feed::PT_COUNT, Feed::PT_ABOUT_FIRST, settings ) )
			return 2;
		if( !rig.Render() || !writePng( outPath, width, height, rig.out.Pixels() ) )
		{
			std::fprintf( stderr, "could not render or write %s\n", outPath.c_str() );
			return 1;
		}
		std::printf( "wrote %s\n", outPath.c_str() );
	}
	else
	{
		MixRig rig;
		if( !rig.Init( width, height, InputSpec::Exact( width, height ), InputSpec::Exact( srcWidth, srcHeight ) ) )
			return 1;
		rig.UploadDest( generate( inputA, width, height ) );
		rig.UploadSrc( generate( inputB, srcWidth, srcHeight ) );
		if( !applySettings( rig.plugin, Pepper::PT_COUNT, Pepper::PT_ABOUT_FIRST, settings ) )
			return 2;
		rig.seconds = seconds;
		if( !rig.Render() || !writePng( outPath, width, height, rig.Pixels() ) )
		{
			std::fprintf( stderr, "could not render or write %s\n", outPath.c_str() );
			return 1;
		}
		std::printf( "wrote %s\n", outPath.c_str() );
	}

	CGLSetCurrentContext( nullptr );
	CGLDestroyContext( context );
	if( result != 0 )
		std::printf( "\n%d check(s) FAILED\n", failures > 0 ? failures : result );
	return result;
}
