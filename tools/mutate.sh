#!/usr/bin/env bash
#
# Mutation testing: change ONE character of the shipped code and require that a
# check fails. A check that still passes against a mutant was not looking at
# the code it claims to cover -- and a harness that compiled its own copy of
# the shaders, or predicted from the plugin's own geometry, would pass every
# mutant here.
#
# The tree is copied once into a temporary directory (the FFGL SDK is
# symlinked, not copied) and built arm64-only. Each mutant is written into the
# copy, its object file thrown away so the build cannot keep the last one, and
# only the named check run; then the file is put back the same way. Every
# named check is run once against the unmutated copy first, so a check that
# fails anyway cannot pass for one that caught something.
#
# The rotation's own cross terms, 2 c sn qr in Q's new diagonal, have no
# mutant: qr is zero until a ray has turned between two planes of incidence,
# so they reach the picture only at a third face, and flipping one moves no
# measured pixel by 3e-7 (AGENTS.md, "The mutation").
#
# Controls.cpp has no mutant here, on purpose: the harness reads what it asked
# for back through Controls.h (asked()), so a wrong mapping moves the plugin
# and the prediction together. The mappings' ends are pinned by --names.
#
#     tools/mutate.sh
#
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WORK="$(mktemp -d)"
trap 'rm -rf "${WORK:?}"' EXIT

# Every check here writes the plugin's log; keep it out of the real one.
export PEPPER_LOG_DIR="$WORK/logs"

# file | the exact text | its one-character mutant | the check that must fail | what it is
MUTANTS=(
	"source/Shaders.cpp|vec3 rp    = ( Eta * cosi - cost )|vec3 rp    = ( Eta * cosi + cost )|--fresnel|GLSL: p reflection's numerator signed wrong, so no Brewster null, - -> +"
	"source/Shaders.cpp|b * b * r.qp + 2.0 * a * b * r.qr ) / n2;|b * b * r.qp - 2.0 * a * b * r.qr ) / n2;|--polarised|GLSL: a polarised source seen with the s-p cross term negated, which only a ray turned between two planes of incidence carries, + -> -"
	"source/Shaders.cpp|return Coef( a.s / ( 1.0 - b.s ),|return Coef( a.s / ( 1.0 + b.s ),|--energy|GLSL: a sheet's internal echoes summed as an alternating series, - -> +"
	"source/Shaders.cpp|vec3 spr = c * sn * ( qp - qs )|vec3 spr = c * sn * ( qp + qs )|--polarised|GLSL: Q turned into the next face's s and p with its new cross term signed wrong, - -> +"
	"source/Shaders.cpp|fall = pow( max( -r.d.y, 0.0 ), Falloff );|fall = pow( max( +r.d.y, 0.0 ), Falloff );|--falloff|GLSL: the screen's falloff measured off its back, - -> +"
	"source/Shaders.cpp|texelFetch( CoatTable, ivec2( i + 1, row ), 0 )|texelFetch( CoatTable, ivec2( i - 1, row ), 0 )|--coating|GLSL: the coat's table interpolated toward the wrong neighbour, + -> -"
	"source/Shaders.cpp|Pk      = Pb + dr * ( depth / dot( dr, nf ) );|Pk      = Pb - dr * ( depth / dot( dr, nf ) );|--double|GLSL: the back surface's image walked the wrong way along the pane, + -> -"
	"source/Shaders.cpp|( P.x - r.x ) / ( r.y - r.x ), ( P.z - r.z )|( P.x + r.x ) / ( r.y - r.x ), ( P.z - r.z )|--geometry|GLSL: the pane's screen picture shifted across by its own width, - -> +"
	"source/Shaders.cpp|cuv = vec2( e / FeedArm.x + 0.5, r / FeedArm.y );|cuv = vec2( e / FeedArm.x - 0.5, r / FeedArm.y );|--feed|GLSL: every feed arm sampled half a clip to one side, + -> -"
	"source/Shaders.cpp|pow( ( c + 0.055 ) / 1.055, vec3( 2.4 ) )|pow( ( c + 0.055 ) / 1.055, vec3( 2.5 ) )|--additive|GLSL: sRGB decoded with the wrong exponent, 4 -> 5"
	"source/Rig.cpp|s.pyramidH       = ( 0.5 - r0 ) * std::tan( in.angle );|s.pyramidH       = ( 0.5 + r0 ) * std::tan( in.angle );|--pyramid|C++: the camera aimed at the wrong height on the pyramid, - -> +"
	"source/Rig.cpp|in.rippleHz * slope[ k ] / slope[ 0 ];|in.rippleHz * slope[ k ] / slope[ 1 ];|--ripple|C++: every mode's frequency scaled by the wrong fundamental, 0 -> 1"
	"source/Rig.cpp|d               = w / aspect;|d               = w * aspect;|--feed|C++: a feed arm's depth stretched by the aspect, not squeezed, / -> *"
	"source/Rig.cpp|ay = -std::cos( theta );|ay = +std::cos( theta );|--feed|C++: arm 0 pointing up the picture, away from the viewer, - -> +"
	"source/Rig.cpp|s.eye.y     = 2.0 * s.target.y - s.eye.y;|s.eye.y     = 2.0 * s.target.y + s.eye.y;|--geometry|C++: the Above rig's camera not reflected about the rig's centre, - -> +"
	"source/Rig.cpp|ChannelIndex( static_cast< SheetKind >( in.sheet ), c );|ChannelIndex( static_cast< SheetKind >( in.sheet ), 1 );|--fresnel|C++: every channel given green's index, so no dispersion, c -> 1"
	"source/Rig.cpp|px * std::cos( chi ) + py * std::sin( chi );|px * std::cos( chi ) - py * std::sin( chi );|--fresnel|C++: the polarisation angle taken clockwise, + -> -"
	"source/Coating.cpp|const Complex ds = 1.0 + a.rs * b.rs * E|const Complex ds = 1.0 - a.rs * b.rs * E|--coating|C++: Airy's denominator for s with the echo signed wrong, + -> -"
	"source/Coating.cpp|const Complex ris = ( -b.rs - a.rs * E ) / ds|const Complex ris = ( +b.rs - a.rs * E ) / ds|--coating|C++: the coat seen from inside the sheet without r' = -r, - -> +"
	"source/Pepper.cpp|shader.Set( \"MaxUVSrc\", maxSrc.s, maxSrc.t );|shader.Set( \"MaxUVSrc\", maxSrc.s, maxSrc.s );|--mixer|C++: Src's MaxUV square, t -> s"
	"source/Pepper.cpp|glActiveTexture( GL_TEXTURE0 + 1 );|glActiveTexture( GL_TEXTURE0 + 2 );|--mixer|C++: unit 2 left active after the coat's table is unbound, so Src's binding is never released, 1 -> 2"
	"source/Pepper.cpp|SourceGainFromParam( params[ PT_SOURCE_LEVEL ] ) * opacity;|SourceGainFromParam( params[ PT_SOURCE_LEVEL ] ) / opacity;|--opacity|C++: Opacity divides the source's light, * -> /"
)

tree="$WORK/tree"
mkdir -p "$tree/external"
cp -R "$REPO/source" "$REPO/tools" "$REPO/cmake" "$REPO/CMakeLists.txt" "$tree/"
ln -s "$REPO/external/ffgl" "$tree/external/ffgl"
generator=()
command -v ninja >/dev/null 2>&1 && generator=( -G Ninja )
cmake -S "$tree" -B "$tree/build" "${generator[@]}" -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64 >/dev/null
build() { cmake --build "$tree/build" --target pgtest -j"$(sysctl -n hw.ncpu)" >"$WORK/build.log" 2>&1; }
# Throws away the object file built from $1, so the next build compiles it
# whatever the clock says.
forget() { find "$tree/build" -name "$( basename "$1" ).o" -delete; }
build || { tail -20 "$WORK/build.log"; echo "the unmutated copy does not build"; exit 1; }

checks=$( for entry in "${MUTANTS[@]}"; do IFS='|' read -r _ _ _ check _ <<<"$entry"; echo "$check"; done | sort -u )
for check in $checks; do
	"$tree/build/pgtest" "$check" >"$WORK/log" 2>&1 || { grep FAIL "$WORK/log" | head -3; echo "pgtest $check fails without a mutant"; exit 1; }
done
printf 'baseline: %s pass unmutated\n' "$( echo $checks )"

caught=0
for entry in "${MUTANTS[@]}"; do
	IFS='|' read -r file original mutant check what <<<"$entry"
	cp "$REPO/$file" "$tree/$file"

	python3 - "$tree/$file" "$original" "$mutant" <<'PY'
import sys, pathlib
path, original, mutant = pathlib.Path(sys.argv[1]), sys.argv[2], sys.argv[3]
text = path.read_text()
if text.count(original) != 1:
    sys.exit(f"mutation target found {text.count(original)} times in {path}: '{original}'")
if sum(a != b for a, b in zip(original, mutant)) != 1 or len(original) != len(mutant):
    sys.exit("a mutant must differ by exactly one character")
path.write_text(text.replace(original, mutant))
PY

	printf '\n== mutant: %s\n' "$what"
	forget "$file"
	if ! build; then
		printf '   FAIL  the mutant does not build -- pick one that compiles\n'
		tail -5 "$WORK/build.log" | sed 's/^/        /'
	elif "$tree/build/pgtest" "$check" >"$WORK/log" 2>&1; then
		printf '   FAIL  %s still PASSES -- the check does not cover this code\n' "$check"
	else
		printf '   ok    %s fails against the mutant:\n' "$check"
		{ grep -E '^ *FAIL' "$WORK/log" || true; } | head -2 | cut -c1-160 | sed 's/^/        /'
		caught=$(( caught + 1 ))
	fi
	cp "$REPO/$file" "$tree/$file"
	forget "$file"
done

printf '\nmutants: %d, caught: %d\n' "${#MUTANTS[@]}" "$caught"
[[ "$caught" -eq "${#MUTANTS[@]}" ]]
