#include "Controls.h"

#include <algorithm>
#include <cmath>

namespace pepper
{
namespace
{
constexpr double kDegree = kPi / 180.0;

double clamp01( double value )
{
	return std::min( 1.0, std::max( 0.0, value ) );
}

double linear( double t, double lo, double hi )
{
	return lo + ( hi - lo ) * clamp01( t );
}

float linearParam( double value, double lo, double hi )
{
	return static_cast< float >( clamp01( ( value - lo ) / ( hi - lo ) ) );
}

/// lo * ( hi / lo )^t: geometric between two ends.
double geometric( double t, double lo, double hi )
{
	return lo * std::pow( hi / lo, clamp01( t ) );
}

float geometricParam( double value, double lo, double hi )
{
	if( !( value > 0.0 ) )
		return 0.0f;
	return static_cast< float >( clamp01( std::log( value / lo ) / std::log( hi / lo ) ) );
}
} // namespace

int OptionIndex( float value, int count )
{
	return std::clamp( static_cast< int >( std::lround( value ) ), 0, count - 1 );
}

//---------------------------------------------------------------------------
double SourceGainFromParam( float value )
{
	return std::exp2( linear( value, kSourceStopsMin, kSourceStopsMax ) );
}

double StageGainFromParam( float value )
{
	return std::exp2( linear( value, kStageStopsMin, kStageStopsMax ) );
}

double SourceBlackFromParam( float value )
{
	return clamp01( value ) * kSourceBlackMax;
}

double SpillFromParam( float value )
{
	return clamp01( value ) * kSpillMax;
}

float ParamForSourceStops( double stops )
{
	return linearParam( stops, kSourceStopsMin, kSourceStopsMax );
}

float ParamForStageStops( double stops )
{
	return linearParam( stops, kStageStopsMin, kStageStopsMax );
}

float ParamForSourceBlack( double fraction )
{
	return linearParam( fraction, 0.0, kSourceBlackMax );
}

float ParamForSpill( double fraction )
{
	return linearParam( fraction, 0.0, kSpillMax );
}

double PolarisationAngle( Polarisation kind )
{
	switch( kind )
	{
	case Polarisation::Horizontal: return 0.0;
	case Polarisation::Vertical: return 90.0 * kDegree;
	case Polarisation::Deg45: return 45.0 * kDegree;
	case Polarisation::Deg135: return 135.0 * kDegree;
	default: return 0.0;
	}
}

double ViewAngleFromParam( float value )
{
	return linear( value, kViewAngleMinDeg, kViewAngleMaxDeg ) * kDegree;
}

float ParamForViewAngle( double radians )
{
	return linearParam( radians / kDegree, kViewAngleMinDeg, kViewAngleMaxDeg );
}

double FalloffPower( double fullAngle )
{
	//cos^p( half ) = 1/2. At 180 degrees the half angle is 90 and cos is 0:
	//no finite p, and the emitter is Lambertian, p = 0.
	const double c = std::cos( 0.5 * fullAngle );
	if( !( c > 1e-9 ) )
		return 0.0;
	return std::log( 0.5 ) / std::log( c );
}

//---------------------------------------------------------------------------
int SidesFromParam( float value )
{
	return OptionIndex( value, 2 ) == 0 ? 4 : 3;
}

double SheetAngleFromParam( float value )
{
	return linear( value, kSheetAngleMinDeg, kSheetAngleMaxDeg ) * kDegree;
}

double CentreFromParam( float value )
{
	return linear( value, kCentreMin, kCentreMax );
}

float ParamForSheetAngle( double radians )
{
	return linearParam( radians / kDegree, kSheetAngleMinDeg, kSheetAngleMaxDeg );
}

float ParamForCentre( double fraction )
{
	return linearParam( fraction, kCentreMin, kCentreMax );
}

//---------------------------------------------------------------------------
double IndexAt( SheetKind kind, double nanometres )
{
	const double um  = nanometres * 1e-3;
	const double um2 = um * um;
	switch( kind )
	{
	case SheetKind::PetFoil:
		//Drawn polyester film's in-plane index, 1.65 at the d line (its
		//birefringence is not modelled), dispersing as cast PET does: the
		//slope of a Cauchy fit, A + B / lambda^2, to Zhang et al., Appl.
		//Opt. 59, 2337 (2020), 0.42 to 0.70 um, B = 0.0112 um^2.
		return 1.65 + 0.0112 * ( 1.0 / um2 - 1.0 / ( 0.5876 * 0.5876 ) );
	case SheetKind::FloatGlass:
		//Clear soda-lime window glass: Rubin, Sol. Energy Mater. 12, 275
		//(1985), n = 1.5130 - 0.003169 lambda^2 + 0.003962 / lambda^2.
		return 1.5130 - 0.003169 * um2 + 0.003962 / um2;
	case SheetKind::Acrylic:
		//PMMA: Sultanova et al., Acta Phys. Pol. A 116, 585 (2009), one-term
		//Sellmeier n^2 - 1 = 1.1819 lambda^2 / ( lambda^2 - 0.011313 ).
		return std::sqrt( 1.0 + 1.1819 * um2 / ( um2 - 0.011313 ) );
	case SheetKind::Polycarbonate:
		//Sultanova et al. (2009): n^2 - 1 = 1.4182 lambda^2 / ( lambda^2 - 0.021304 ).
		return std::sqrt( 1.0 + 1.4182 * um2 / ( um2 - 0.021304 ) );
	default: return 1.5;
	}
}

double IndexOf( SheetKind kind )
{
	return IndexAt( kind, 587.6 );
}

double ChannelIndex( SheetKind kind, int channel )
{
	return IndexAt( kind, kChannelNm[ std::clamp( channel, 0, 2 ) ] );
}

const char* NameOf( SheetKind kind )
{
	switch( kind )
	{
	case SheetKind::PetFoil: return "PET Foil";
	case SheetKind::FloatGlass: return "Float Glass";
	case SheetKind::Acrylic: return "Acrylic";
	case SheetKind::Polycarbonate: return "Polycarbonate";
	default: return "?";
	}
}

const char* NameOf( CoatingKind kind )
{
	switch( kind )
	{
	case CoatingKind::None: return "None";
	case CoatingKind::Aluminium: return "Aluminium";
	case CoatingKind::TitaniumDioxide: return "Titanium Dioxide";
	default: return "?";
	}
}

double CoatThicknessNm( CoatingKind kind, float value )
{
	if( kind == CoatingKind::Aluminium )
		return clamp01( value ) * kAluminiumMaxNm;
	if( kind == CoatingKind::TitaniumDioxide )
	{
		double n, k;
		CoatIndex( kind, kRayChannel, n, k );
		return clamp01( value ) * kTitaniaMaxQuarters * kChannelNm[ kRayChannel ] / ( 4.0 * n );
	}
	return 0.0;
}

float ParamForCoatThickness( CoatingKind kind, double nanometres )
{
	const double full = CoatThicknessNm( kind, 1.0f );
	return full > 0.0 ? static_cast< float >( clamp01( nanometres / full ) ) : 0.0f;
}

void CoatIndex( CoatingKind kind, int channel, double& n, double& k )
{
	//At 611.3, 549.1 and 464.3 nm, interpolated linearly in the tabulated
	//data refractiveindex.info publishes.
	static const double kAluminium[ 3 ][ 2 ] = {
		//Rakic et al., Appl. Opt. 37, 5271 (1998), Lorentz-Drude fit: bulk
		//aluminium. A film a few nm thick, with its oxide skin, differs.
		{ 1.2408, 7.0886 },
		{ 0.9618, 6.4484 },
		{ 0.6350, 5.4814 },
	};
	static const double kTitania[ 3 ][ 2 ] = {
		//Siefke et al., Adv. Opt. Mater. 4, 1780 (2016): an ALD film.
		{ 2.3992, 4.43e-9 },
		{ 2.4365, 5.61e-8 },
		{ 2.5273, 5.42e-6 },
	};
	const int c = std::clamp( channel, 0, 2 );
	n = 1.0;
	k = 0.0;
	if( kind == CoatingKind::Aluminium )
	{
		n = kAluminium[ c ][ 0 ];
		k = kAluminium[ c ][ 1 ];
	}
	else if( kind == CoatingKind::TitaniumDioxide )
	{
		n = kTitania[ c ][ 0 ];
		k = kTitania[ c ][ 1 ];
	}
}

double ThicknessFromParam( float value )
{
	return clamp01( value ) * kThicknessMax;
}

double RippleFromParam( float value )
{
	return clamp01( value ) * kRippleMaxDeg * kDegree;
}

double RippleHzFromParam( float value )
{
	return geometric( value, kRippleHzMin, kRippleHzMax );
}

float ParamForThickness( double rigSizes )
{
	return linearParam( rigSizes, 0.0, kThicknessMax );
}

float ParamForRipple( double radians )
{
	return linearParam( radians / kDegree, 0.0, kRippleMaxDeg );
}

float ParamForRippleHz( double hertz )
{
	return geometricParam( hertz, kRippleHzMin, kRippleHzMax );
}

//---------------------------------------------------------------------------
double DistanceFromParam( float value )
{
	return geometric( value, kDistanceMin, kDistanceMax );
}

double ElevationFromParam( float value )
{
	return linear( value, kElevationMinDeg, kElevationMaxDeg ) * kDegree;
}

double AzimuthFromParam( float value )
{
	return linear( value, -kAzimuthMaxDeg, kAzimuthMaxDeg ) * kDegree;
}

float ParamForDistance( double rigSizes )
{
	return geometricParam( rigSizes, kDistanceMin, kDistanceMax );
}

float ParamForElevation( double radians )
{
	return linearParam( radians / kDegree, kElevationMinDeg, kElevationMaxDeg );
}

float ParamForAzimuth( double radians )
{
	return linearParam( radians / kDegree, -kAzimuthMaxDeg, kAzimuthMaxDeg );
}

} // namespace pepper
