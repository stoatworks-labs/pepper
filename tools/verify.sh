#!/usr/bin/env bash
#
# Everything, in the order that fails fastest.
#
# Each check answers a question none of the others can:
#
#   shaders       do the three shaders (the vertex shader, the mixer's trace,
#                 the feed) compile through a real GLSL compiler, before a
#                 host has to find out (tools/glslc.sh). A shader that will
#                 not compile presents to an operator as "the mixer does
#                 nothing", with the real message buried in the log.
#   build         a fresh universal Release build, which is what would ship
#   suites        the plugins' claims, measured at TWO rasters, each with its
#                 own negative control: two inputs at two sizes with two
#                 MaxUVs; the ghost only adds light; R + T = 1; Brewster's
#                 null and Abeles's R_p = R_s^2, each channel at its own
#                 index; a thick pane's images where Snell puts them, with
#                 the series' weights; a coat's interference against an
#                 independent thin-film model; the screen's falloff; a
#                 polarised source through every face of the pyramid; the
#                 ghost where the mirror puts it at every angle, screen
#                 below or above; the foil's spectrum at
#                 the membrane's modes; Opacity is the source's light; the
#                 pyramid's faces and the feed's arms; and one character of
#                 the shipped GLSL mutated
#   software      the same suites on Apple's software renderer, which is what
#                 a GPU-less runner gets: a check calibrated on this Mac's GPU
#                 fails here before it fails anywhere else
#   sweep         does every control of both plugins change the picture
#   pipe          the filming mode: N frames in, N out; a standard parameter
#                 ramps and a boolean steps; a closed stdout is exit 1
#   registration  does each bundle contain a plugin at all -- a file-scope
#                 CFFGLPluginInfo nothing names, which a linker may drop
#                 while still producing a bundle that loads and exports
#                 plugMain -- and only its own (the two-bundle trap)
#   lipo          is the build really universal, or did CMake latch the
#                 architecture list before -DCMAKE_OSX_ARCHITECTURES arrived
#                 and report success anyway
#   plist         does CFBundleExecutable name the binary that is actually
#                 on disk -- if it does not, codesign reports "code object is
#                 not signed at all" about a *nested* object and mentions
#                 neither the plist nor the cause
#   codesign      the ad-hoc signature a release would make, against a copy
#   oxbow         the name, id, TYPE and input count a host reads. The only
#                 thing that makes SW Pepper a mixer is the eighth argument of
#                 a macro; nothing in the build would notice if it were
#                 wrong, and a mixer that registered as an effect would be
#                 handed one input and return FF_FAIL for ever. Parameter 0,
#                 which Arena hides on a mixer, and the Opacity name are
#                 asserted too. oxbow is an effect host: it renders the Feed
#                 (selftest) and must be REFUSED by the mixer's input guard.
#   source        GLSL reserved words as identifiers, and the FFGL SDK still
#                 at the fleet's pin
#   bench         the render cost, for the record. Not pass/fail.
#
# oxbow is found at $OXBOW, else ~/Projects/resolume/oxbow/build/oxbow; when
# neither exists the step is skipped and says so.
#
set -uo pipefail

cd "$(dirname "$0")/.."

BUILD="${BUILD:-build-universal}"
failures=0
LOGS="$( mktemp -d )"

# Every harness run instantiates the real plugin, and the plugin writes a
# log. Keep those lines out of the log an Arena session would be read from.
export PEPPER_LOG_DIR="${PEPPER_LOG_DIR:-$LOGS/pepper-logs}"

step() { printf '\n\033[1m== %s\033[0m\n' "$1"; }
pass() { printf '   \033[32mok\033[0m   %s\n' "$1"; }
fail() { printf '   \033[31mFAIL\033[0m %s\n' "$1"; failures=$(( failures + 1 )); }

step "shaders"
if tools/glslc.sh; then
	pass "every shader compiles"
else
	fail "a shader does not compile"
fi

#---------------------------------------------------------------------------
# A fresh universal Release build -- the one that would ship. The dev build
# in build/ is arm64 only and is not what any of the binary checks below
# should be looking at.
#---------------------------------------------------------------------------
step "build"
if [ ! -d "$BUILD" ]; then
	cmake -B "$BUILD" -DCMAKE_BUILD_TYPE=Release >/dev/null 2>&1
fi
if cmake --build "$BUILD" --parallel >"$LOGS/build.txt" 2>&1; then
	pass "universal Release build"
else
	tail -20 "$LOGS/build.txt" | sed 's/^/   /'
	fail "build failed -- run: cmake --build $BUILD"
	exit 1
fi
# Our own warnings only: the SDK's are not ours to fix.
if grep -E "warning:" "$LOGS/build.txt" | grep -v "external/ffgl" >"$LOGS/warnings.txt"; then
	sed 's/^/   /' "$LOGS/warnings.txt" | head -10
	fail "the build warns in this repo's own code"
else
	pass "no warnings in this repo's own code"
fi

PGTEST="$BUILD/pgtest"
SUITES="names mixer additive energy fresnel double coating falloff polarised geometry ripple opacity pyramid feed mutation"

step "suites"
for t in $SUITES; do
	if "$PGTEST" --$t >"$LOGS/$t.txt" 2>&1; then
		pass "pgtest --$t"
	else
		grep -E '^ *FAIL' "$LOGS/$t.txt" | head -3 | sed 's/^/        /'
		fail "pgtest --$t"
	fi
done

#---------------------------------------------------------------------------
# The same suites on Apple's SOFTWARE renderer. Its interpolated uv is good
# only to the GL spec's 1 part in 10^5 (the GPU's to a float ULP), and its
# transcendental functions are not the GPU's. Running here too means a check
# calibrated on this Mac's GPU fails on this Mac.
#---------------------------------------------------------------------------
step "software renderer"
if [ "$(uname)" = "Darwin" ]; then
	for t in $SUITES; do
		[ "$t" = names ] && continue
		if PGTEST_RENDERER=software "$PGTEST" --$t >"$LOGS/sw-$t.txt" 2>&1; then
			pass "pgtest --$t (software)"
		else
			grep -E '^ *FAIL' "$LOGS/sw-$t.txt" | head -3 | sed 's/^/        /'
			fail "pgtest --$t on the software renderer -- run: PGTEST_RENDERER=software $PGTEST --$t"
		fi
	done
fi

step "sweep"
if python3 tools/sweep.py --binary "$PGTEST" --size 480x270 >"$LOGS/sweep.txt" 2>&1; then
	pass "$( tail -1 "$LOGS/sweep.txt" )"
else
	echo "   *** dead controls, see $LOGS/sweep.txt"
	tail -4 "$LOGS/sweep.txt" | sed 's/^/   /'
	fail "tools/sweep.py reports a dead control"
fi

#---------------------------------------------------------------------------
# The pipe. Three things, each a way a filming run has gone wrong in the
# fleet: the frame count in equals the frame count out; a cue on a boolean
# STEPS on its frame and a standard parameter ramps; and a reader that
# closes stdout makes the harness exit 1 -- SIGPIPE is ignored, so it is not
# killed with 141 and does not report success either.
#---------------------------------------------------------------------------
step "pipe"
W=64; H=36; FRAME=$(( W * H * 4 ))
tmp=$( mktemp -d )
bytes=$( head -c $(( FRAME * 3 )) /dev/zero | "$PGTEST" --pipe --size ${W}x${H} 2>/dev/null | wc -c | tr -d ' ' )
if [ "$bytes" -eq $(( FRAME * 3 )) ]; then
	pass "3 frames in, 3 frames out ($bytes bytes)"
else
	fail "3 frames in gave $bytes bytes out, expected $(( FRAME * 3 ))"
fi
# Dest is a flat 0x80 on stdin, Src is black (no ghost) and the foil is
# still, so the first pixel is the stage through the glass: T x Stage Level
# x the grey, through Linear Light or not. Stage Level ramps 0 -> 2/3 over
# frames 0..2; Linear Light steps from on to off at frame 4.
#   frames 0, 1, 2   rising strictly         (a STEPPED Stage Level holds at 1)
#   frames 2, 3      equal                   (a RAMPED Linear Light is 0.5 at 2,
#                                             0.25 at 3: off a frame or two early)
#   frames 3, 4      differ; 4, 5 equal      (the step lands on frame 4)
printf '0 Stage Level 0\n2 Stage Level 0.6667\n0 Linear Light 1\n4 Linear Light 0\n' > "$tmp/cues.txt"
head -c $(( FRAME * 6 )) /dev/zero | LC_ALL=C tr '\000' '\200' | "$PGTEST" --pipe --size ${W}x${H} --input-b black \
	--set "Ripple=0" --script "$tmp/cues.txt" 2>/dev/null > "$tmp/out.rgba"
verdict=$( python3 - "$tmp/out.rgba" $FRAME <<'PY'
import sys
d = open( sys.argv[ 1 ], "rb" ).read(); n = int( sys.argv[ 2 ] )
v = [ d[ i * n ] for i in range( len( d ) // n ) ]
ok = len( v ) == 6 and v[ 0 ] < v[ 1 ] < v[ 2 ] and v[ 2 ] == v[ 3 ] and v[ 3 ] != v[ 4 ] and v[ 4 ] == v[ 5 ]
print( ( "ok " if ok else "bad " ) + " ".join( str( x ) for x in v ) )
PY
)
case "$verdict" in
	ok*) pass "cues: Stage Level ramps, Linear Light steps (${verdict#ok })" ;;
	*)   fail "cues gave '${verdict#bad }': want three rising, then equal, then a step at frame 4" ;;
esac
head -c $(( FRAME * 3 )) /dev/zero | "$PGTEST" --pipe --size ${W}x${H} 2>/dev/null | head -c 1 >/dev/null
status=${PIPESTATUS[1]}
if [ "$status" -eq 1 ]; then
	pass "a closed stdout is exit 1 (SIGPIPE ignored)"
else
	fail "a closed stdout gave exit $status, expected 1"
fi
rm -rf "$tmp"

#---------------------------------------------------------------------------
# The two bundles, as a host and a release would see them.
#---------------------------------------------------------------------------
OXBOW="${OXBOW:-$HOME/Projects/resolume/oxbow/build/oxbow}"

# bundle-name binary identifier id display-name type inputs
check_bundle() {
	local name="$1" ident="$2" id="$3" display="$4" kind="$5" inputs="$6"
	local bundle="$BUILD/$name.bundle"
	local bin="$bundle/Contents/MacOS/$name"

	step "$name.bundle"
	if [ ! -d "$bundle" ]; then
		fail "no $bundle"
		return
	fi

	# `nm ... | grep -q X` FAILS when grep FINDS its match under
	# `set -o pipefail`: grep exits at once, nm takes SIGPIPE, and the
	# pipeline reports that failure. Capture and match instead of piping.
	local syms
	syms=$( nm -gU "$bin" 2>/dev/null )
	case "$syms" in
		*_plugMain*) pass "exports plugMain" ;;
		*) fail "no plugMain -- the bundle contains no plugin" ;;
	esac

	local archs
	archs=$( lipo -archs "$bin" 2>/dev/null )
	case "$archs" in *arm64*) pass "arm64 present" ;; *) fail "no arm64 (got: $archs)" ;; esac
	case "$archs" in *x86_64*) pass "x86_64 present" ;; *) fail "no x86_64 (got: $archs) -- a universal build was asked for" ;; esac

	local exe
	exe=$( /usr/libexec/PlistBuddy -c "Print :CFBundleExecutable" "$bundle/Contents/Info.plist" 2>/dev/null )
	if [ -n "$exe" ] && [ -f "$bundle/Contents/MacOS/$exe" ]; then
		pass "CFBundleExecutable ($exe) is on disk"
	else
		fail "CFBundleExecutable is '$exe' but no such binary exists -- codesign would fail"
	fi
	local got
	got=$( /usr/libexec/PlistBuddy -c "Print :CFBundleIdentifier" "$bundle/Contents/Info.plist" 2>/dev/null )
	if [ "$got" = "$ident" ]; then
		pass "CFBundleIdentifier is $got"
	else
		fail "CFBundleIdentifier is '$got', expected $ident"
	fi

	local tmp
	tmp=$( mktemp -d )
	cp -R "$bundle" "$tmp/"
	if codesign --force --sign - --timestamp=none "$tmp/$name.bundle" >/dev/null 2>&1 \
		&& codesign --verify --deep --strict "$tmp/$name.bundle" >/dev/null 2>&1; then
		pass "ad-hoc signs and verifies (on a copy)"
	else
		fail "ad-hoc signing failed"
	fi
	rm -rf "$tmp"

	if [ -x "$OXBOW" ]; then
		local probe
		probe=$( "$OXBOW" probe "$bundle" 2>&1 )
		case "$probe" in *"name:        $display"$'\n'*) pass "a host reads the name $display" ;; *) fail "wrong or missing name -- see: $OXBOW probe $bundle" ;; esac
		case "$probe" in *"id:          $id"*) pass "a host reads the id $id" ;; *) fail "wrong or missing id (want $id)" ;; esac
		case "$probe" in *"type:        $kind"*) pass "a host reads the type $kind" ;; *) fail "wrong plugin type (want $kind) -- the 8th argument of CFFGLPluginInfo" ;; esac
		case "$probe" in *"inputs:      $inputs"*) pass "a host is told $inputs input(s)" ;; *) fail "wrong input count (want $inputs) -- SetMinInputs/SetMaxInputs are separate from the type" ;; esac
		# The other plugin's registration must not be in this bundle: the
		# two-bundle trap, where both entry files land in the shared core.
		case "$probe" in *"PG01"*"PG02"*|*"PG02"*"PG01"*) fail "both plugins registered in one bundle" ;; esac
		printf '%s\n' "$probe" > "$LOGS/probe-$name.txt"
	else
		printf '   skipped oxbow: none at %s (set OXBOW=...)\n' "$OXBOW"
	fi
}

check_bundle Pepper "com.stoatworks.ffgl.pepper" PG01 "SW Pepper" mixer "2..2"
if [ -x "$OXBOW" ] && [ -f "$LOGS/probe-Pepper.txt" ]; then
	probe=$( cat "$LOGS/probe-Pepper.txt" )
	# Arena does not expose a mixer's parameter 0 (genlock, wipe, relay), so
	# index 0 is a control whose default is right for ever: Linear Light, on.
	case "$probe" in
		*"[ 0] Linear Light     type=0   default=1 "*) pass "parameter 0, which Arena hides, is Linear Light, a boolean, on" ;;
		*) fail "parameter 0 is not Linear Light (boolean, on) -- Arena hides a mixer's first parameter" ;;
	esac
	# Arena binds a mixer parameter named Opacity to the layer's fader.
	case "$probe" in
		*"] Opacity          type=10"*) pass "the source's light is a standard parameter named Opacity" ;;
		*) fail "no standard parameter named Opacity -- Arena binds the layer fader by that name" ;;
	esac
	# oxbow hosts effects: it hands a plugin one input. A mixer must refuse
	# that (its guard, FF_FAIL), not draw from an unbound texture unit.
	selftest=$( "$OXBOW" selftest "$BUILD/Pepper.bundle" 2>&1 )
	case "$selftest" in
		*"FF_PROCESS_OPENGL failed on frame 0"*) pass "one input, as an effect host gives, is refused (FF_FAIL)" ;;
		*) fail "the mixer did not refuse a single input" ;;
	esac
fi
check_bundle PepperFeed "com.stoatworks.ffgl.pepperfeed" PG02 "SW Pepper Feed" effect "1..1"
if [ -x "$OXBOW" ]; then
	selftest=$( "$OXBOW" selftest "$BUILD/PepperFeed.bundle" 2>&1 )
	case "$selftest" in
		*"selftest:    PASS"*) pass "oxbow selftest renders 120 frames through it" ;;
		*) printf '%s\n' "$selftest" | tail -6 | sed 's/^/   /'; fail "oxbow selftest failed on PepperFeed.bundle" ;;
	esac
fi

#---------------------------------------------------------------------------
# The source.
#---------------------------------------------------------------------------
step "source"
# GLSL 4.10's reserved words, declared as identifiers: a declaration of one
# compiles on some drivers and not on others (noise1-4 are built-ins on
# some). Only declarations -- a type, then the word -- since calling a
# built-in such as round() is what it is for.
if python3 - <<'PY'
import re, sys, pathlib
words = ( "patch sample input output filter common active half layout flat packed smooth round "
          "noise1 noise2 noise3 noise4 superp partition resource namespace using cast external "
          "interface long short fixed unsigned inline goto asm class union enum typedef template "
          "this volatile public static extern sizeof near far" ).split()
types = r"(?:float|int|uint|bool|[iuhdf]?vec[234]|mat[234]|sampler2D|Ray|Coef|void|const)"
pattern = re.compile( r"\b" + types + r"\s+(" + "|".join( words ) + r")\b" )
text = pathlib.Path( "source/Shaders.cpp" ).read_text()
glsl = "".join( re.findall( r'R"\((.*?)\)"', text, re.S ) )
glsl = re.sub( r"//[^\n]*", "", glsl )# comments are free to use any word
hits = sorted( set( m.group( 1 ) for m in pattern.finditer( glsl ) ) )
if hits:
	print( "   declared in GLSL: " + ", ".join( hits ) )
	sys.exit( 1 )
# The check must be able to see one.
if not pattern.search( "float sample = 1.0;" ):
	sys.exit( "   the reserved-word pattern matches nothing" )
PY
then
	pass "no GLSL reserved word is declared as an identifier"
else
	fail "a GLSL reserved word is declared as an identifier"
fi
pin=$( git -C external/ffgl rev-parse --short=7 HEAD 2>/dev/null )
if [ "$pin" = "b1afaf9" ]; then
	pass "external/ffgl at the fleet's pin, b1afaf9"
else
	fail "external/ffgl is at '$pin', not the fleet's b1afaf9"
fi

step "bench"
"$PGTEST" --bench --frames 30 2>&1 | sed -n '2,12p' | sed 's/^/   /'

printf '\n'
if [ "$failures" -eq 0 ]; then
	printf '\033[32mall checks passed\033[0m\n'
else
	printf '\033[31m%d check(s) failed\033[0m (logs in %s)\n' "$failures" "$LOGS"
fi
exit $(( failures > 0 ? 1 : 0 ))
