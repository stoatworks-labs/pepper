#pragma once

#include "Controls.h"

#include <complex>
#include <vector>

/**
    The coated surface, as numbers: a thin layer between the air and the
    sheet, worked out on the CPU in double and handed to the shader as a
    table over the angle of incidence.

    The coat is thin next to the light's coherence length, so its two faces
    INTERFERE: one layer between two media, summed as Airy's series of
    amplitudes. The sheet itself is thick next to it (12 um and up), so the
    sheet's two surfaces add in intensity, as the shader already sums them.

    Conventions, the same as the shader's bare surfaces: Born & Wolf's
    Fresnel coefficients, with p = s x k on both sides of every interface
    (so at normal incidence r_p = -r_s); complex index N = n + i k with
    fields exp( i ( k z - w t ) ), so a wave decays where Im( N cos ) > 0.
    The transmission amplitudes are normalised so that |t|^2 is the
    transmittance; reciprocity then makes them the same in both directions.

    For the shader, per channel and at each incidence: what the coated
    surface reflects from the air side (R1) and from the sheet side (R1'),
    what it transmits (T1), and the s-p products conj( x_s ) x_p of each,
    which carry the relative phase a polarised source needs.
*/
namespace pepper::coat
{

using Complex = std::complex< double >;

/// One channel's coated surface at one incidence.
struct Surface
{
	double Rs = 0.0, Rp = 0.0;  ///< reflectance from the air side
	double Ris = 0.0, Rip = 0.0;///< reflectance from the sheet side
	double Ts = 0.0, Tp = 0.0;  ///< transmittance, either way
	Complex c;                  ///< conj( r_s ) r_p, from the air
	Complex ci;                 ///< conj( r'_s ) r'_p, from the sheet
	Complex g;                  ///< conj( t_s ) t_p, normalised
};

/// air | coat (N, thickness nm) | sheet (index n), at cos( incidence ) in
/// the air, for light of wavelength lambda nm. `incoherent` is the --coating
/// negative control: the coat's two reflections added in intensity.
Surface At( double cosi, Complex coatN, double nanometres, double sheetN, double lambdaNm, bool incoherent = false );

/// The table the shader interpolates, as RGBA32F texels: kRows rows of
/// kSize, row r holding quantity r for the three channels in RGB. Entry i
/// is at cos( incidence ) = i / ( kSize - 1 ).
inline constexpr int kSize = 1024;
inline constexpr int kRows = 12;
enum Row : int
{
	kRs,
	kRp,
	kRis,
	kRip,
	kTs,
	kTp,
	kCre,
	kCim,
	kCire,
	kCiim,
	kGre,
	kGim,
};

void BuildTable( CoatingKind kind, double nanometres, const double sheetN[ 3 ], bool incoherent, std::vector< float >& rgba );

} // namespace pepper::coat
