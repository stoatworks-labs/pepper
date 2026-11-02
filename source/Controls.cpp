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
double IndexOf( SheetKind kind )
{
	switch( kind )
	{
	case SheetKind::PetFoil: return 1.65;
	case SheetKind::FloatGlass: return 1.52;
	case SheetKind::Acrylic: return 1.49;
	case SheetKind::Polycarbonate: return 1.585;
	default: return 1.5;
	}
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
