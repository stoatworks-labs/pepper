#include "Coating.h"

#include <algorithm>
#include <cmath>

namespace pepper::coat
{
namespace
{
/// N cos( theta ) in a medium of index N, for Snell's invariant S = sin of
/// the angle in the air: sqrt( N^2 - S^2 ), the principal root, which for
/// n, k >= 0 has Im >= 0 -- the wave decays into an absorbing medium.
Complex normalPart( Complex N, double S )
{
	return std::sqrt( N * N - S * S );
}

/// Born & Wolf's interface coefficients from medium i into j, in terms of
/// xi = N cos( theta ).
struct Interface
{
	Complex rs, rp, ts, tp;
};

Interface across( Complex Ni, Complex xi, Complex Nj, Complex xj )
{
	Interface f;
	f.rs = ( xi - xj ) / ( xi + xj );
	f.ts = 2.0 * xi / ( xi + xj );
	f.rp = ( Nj * Nj * xi - Ni * Ni * xj ) / ( Nj * Nj * xi + Ni * Ni * xj );
	f.tp = 2.0 * Ni * Nj * xi / ( Nj * Nj * xi + Ni * Ni * xj );
	return f;
}
} // namespace

Surface At( double cosi, Complex coatN, double nanometres, double sheetN, double lambdaNm, bool incoherent )
{
	cosi           = std::clamp( cosi, 0.0, 1.0 );
	const double S = std::sqrt( std::max( 0.0, 1.0 - cosi * cosi ) );
	const Complex N0( 1.0, 0.0 ), Ns( sheetN, 0.0 );
	const Complex x0 = cosi, x1 = normalPart( coatN, S ), xs = normalPart( Ns, S );

	const Interface a = across( N0, x0, coatN, x1 );//air into the coat
	const Interface b = across( coatN, x1, Ns, xs );//the coat into the sheet

	//The layer's round trip, exp( 2 i beta ), beta = 2 pi d N cos / lambda.
	const Complex beta = 2.0 * 3.14159265358979323846 * nanometres / lambdaNm * x1;
	const Complex one  = std::exp( Complex( 0.0, 1.0 ) * beta );
	const Complex E    = one * one;

	//Airy: every echo inside the coat, summed in amplitude. From the sheet
	//side the interfaces are met in the other order, with r_ji = -r_ij.
	const Complex ds = 1.0 + a.rs * b.rs * E, dp = 1.0 + a.rp * b.rp * E;
	const Complex rs = ( a.rs + b.rs * E ) / ds, rp = ( a.rp + b.rp * E ) / dp;
	const Complex ris = ( -b.rs - a.rs * E ) / ds, rip = ( -b.rp - a.rp * E ) / dp;
	//The transmitted amplitude, normalised by the power each side carries:
	//|t|^2 ( n_s cos_s ) / ( n_0 cos_0 ) is the transmittance, for s and p.
	const double norm = cosi > 0.0 ? std::sqrt( xs.real() / x0.real() ) : 0.0;
	const Complex ts  = a.ts * b.ts * one / ds * norm, tp = a.tp * b.tp * one / dp * norm;

	Surface s;
	s.Rs  = std::norm( rs );
	s.Rp  = std::norm( rp );
	s.Ris = std::norm( ris );
	s.Rip = std::norm( rip );
	s.Ts  = std::norm( ts );
	s.Tp  = std::norm( tp );
	s.c   = std::conj( rs ) * rp;
	s.ci  = std::conj( ris ) * rip;
	s.g   = std::conj( ts ) * tp;

	if( incoherent )
	{
		//The negative control: the coat's two faces added in intensity, as
		//if it were thick. Only meaningful for a clear coat.
		auto add = []( double R1, double R2 ) { return R1 + ( 1.0 - R1 ) * ( 1.0 - R1 ) * R2 / ( 1.0 - R1 * R2 ); };
		s.Rs = s.Ris = add( std::norm( a.rs ), std::norm( b.rs ) );
		s.Rp = s.Rip = add( std::norm( a.rp ), std::norm( b.rp ) );
		s.Ts         = 1.0 - s.Rs;
		s.Tp         = 1.0 - s.Rp;
	}
	return s;
}

void BuildTable( CoatingKind kind, double nanometres, const double sheetN[ 3 ], bool incoherent, std::vector< float >& rgba )
{
	rgba.assign( static_cast< size_t >( kSize ) * kRows * 4, 0.0f );
	for( int c = 0; c < 3; ++c )
	{
		double n, k;
		CoatIndex( kind, c, n, k );
		for( int i = 0; i < kSize; ++i )
		{
			const Surface s = At( static_cast< double >( i ) / ( kSize - 1 ), Complex( n, k ), nanometres, sheetN[ c ], kChannelNm[ c ], incoherent );
			const double v[ kRows ] = { s.Rs, s.Rp, s.Ris, s.Rip, s.Ts, s.Tp, s.c.real(), s.c.imag(), s.ci.real(), s.ci.imag(), s.g.real(), s.g.imag() };
			for( int r = 0; r < kRows; ++r )
				rgba[ ( static_cast< size_t >( r ) * kSize + i ) * 4 + c ] = static_cast< float >( v[ r ] );
		}
	}
}

} // namespace pepper::coat
