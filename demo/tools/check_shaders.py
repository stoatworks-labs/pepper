"""The demo's shaders must be the plugin's shaders, character for character.

    python3 demo/tools/check_shaders.py           compare; exit 1 on any drift
    python3 demo/tools/check_shaders.py --write   regenerate demo/shaders.js

Called from `tools/verify.sh`. Exit code 1 means a copy has drifted.

------------------------------------------------------------------- why

`demo/shaders.js` holds the GLSL pieces of `source/Shaders.cpp`: kVersion,
kVertex, the kCommon library both plugins share, SW Pepper Feed's fragment
and SW Pepper's. That is two copies of the same text, and two copies drift --
quietly, because a ghost reflected by a *plausible* shader looks exactly like
one reflected by the right one. The whole claim of the page is that it runs
the plugin's own shaders, so the claim needs something enforcing it. `pgtest`
drives the real plugin classes and has never heard of this page, and
`tools/glslc.sh` compiles the C++ copies and never looks at the JS one.

------------------------------------------------------------------- what it does

Pulls each piece out of `source/Shaders.cpp` and the matching backtick literal
out of `demo/shaders.js`, and compares them exactly -- no whitespace
normalisation, no comment stripping: a comment updated on one side only is
exactly the drift worth catching. kVersion is a raw string here like the rest.

A piece may be several adjacent raw strings (MSVC refuses one literal over
about 16 KB, C2026); this joins them as the compiler does, with nothing
between. kPepperFragment is three.

It fails when Shaders.cpp grows a piece this script does not list (a stage
the page would silently lack), and it checks the JOIN as well as the pieces:
AssembleFragment() in Shaders.cpp is kVersion + kCommon + the body and
AssembleVertex() is kVersion + kVertex; Pepper.cpp's and Feed.cpp's InitGL
compile those. plugin.js must assemble them the same way. A change to either
side's assembly fails here and needs a reader.

It checks the page's one change to the text, too. WebGL refuses `?:` on a
struct, and kPepperFragment's trace() has one; plugin.js's forWebGL() respells
that expression as a call to a helper it adds before trace(). This fails if
the expression or trace()'s signature (where the helper goes) is not in the
assembled fragment exactly once, as plugin.js spells them: a change there is a
change to what the page runs, and needs a reader.

And it fails when a piece declares a uniform that plugin.js never names: a
uniform the page does not set sits at 0, which is a control that is quietly
dead on the page and live in the plugin. Every piece is checked, not only the
fragments: kVertex declares FoldMaxUV and kCommon the feed's uniforms.

The one transformation is a decode, not a normalisation: a backtick cannot
appear raw inside a template literal, so --write escapes one as \\`.
kPepperFragment has three, in comments. This undoes that escape and REJECTS
any other backslash or any `${` on the JS side; there are none in the C++
bodies, so either could only be somebody hiding a difference.

`--write` produces shaders.js from the C++ by exactly that escape, so the copy
is spliced by this script and never typed.

------------------------------------------------------------------- what it cannot

Nothing here checks the PORTED half: Controls.cpp (every conversion and
index), Rig.cpp (the rig, the camera, the ripple's modes), Coating.cpp (the
coat's table) and the CPU half of Pepper.cpp's and Feed.cpp's ProcessOpenGL
(every uniform) in demo/plugin.js are a hand translation. demo/tools/crosscheck/
renders pgtest and the page from the same frames and compares the pictures; it
needs Chrome and a GPU, so it is run by hand, not by verify.sh. When you change
one of those, change the port too.
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.dont_write_bytecode = True

# JS constant, C++ symbol (all in source/Shaders.cpp), in the order they are
# declared there.
SHADERS = [
    ("VERSION", "kVersion"),
    ("VERTEX", "kVertex"),
    ("COMMON", "kCommon"),
    ("FEED_FRAGMENT", "kFeedFragment"),
    ("PEPPER_FRAGMENT", "kPepperFragment"),
]

# The pieces whose uniforms plugin.js must set (by name, quoted).
UNIFORM_PIECES = ["kVertex", "kCommon", "kFeedFragment", "kPepperFragment"]

# How the pieces are joined, on each side. Each entry is (file, text that must
# appear in it verbatim). The C++ lines are the plugins' assembly; the JS lines
# are plugin.js's copy of it. A change to either is a change to what runs.
ASSEMBLY = [
    ("source/Shaders.cpp",
     "std::string AssembleFragment( const char* body )\n{\n"
     "\treturn std::string( kVersion ) + kCommon + body;\n}"),
    ("source/Shaders.cpp",
     "std::string AssembleVertex()\n{\n"
     "\treturn std::string( kVersion ) + kVertex;\n}"),
    ("source/Pepper.cpp",
     "fragmentText = fragmentOverride ? std::string( fragmentOverride ) : AssembleFragment( kPepperFragment );"),
    ("source/Pepper.cpp", "if( !shader.Compile( AssembleVertex(), fragmentText ) )"),
    ("source/Feed.cpp", "if( !shader.Compile( AssembleVertex(), AssembleFragment( kFeedFragment ) ) )"),
    ("demo/plugin.js", "const vertex = S.VERSION + S.VERTEX;"),
    ("demo/plugin.js", "const assemble = (body) => S.VERSION + S.COMMON + body;"),
    ("demo/plugin.js", "pepper: () => new Program(gl, vertex, forWebGL(assemble(S.PEPPER_FRAGMENT)), 'SW Pepper'),"),
    ("demo/plugin.js", "feed: () => new Program(gl, vertex, assemble(S.FEED_FRAGMENT), 'SW Pepper Feed'),"),
]

# plugin.js's respelling for WebGL: the constants whose text must each occur
# exactly once in kVersion + kCommon + kPepperFragment.
WEBGL_ANCHORS = ["WEBGL_STRUCT_TERNARY", "WEBGL_PICK_BEFORE"]

HEADER = """// GENERATED from source/Shaders.cpp by demo/tools/check_shaders.py --write.
// Do not edit: tools/verify.sh fails if a character of this differs from the
// plugin's. The one escape allowed is \\` for a backtick inside a comment.
"""


def read(*parts):
    with open(os.path.join(REPO, *parts)) as handle:
        return handle.read()


def from_cpp(source, symbol):
    """The piece's text: its raw strings, joined as the compiler joins them.

    Returns (text, count of raw strings), or (None, 0) if the symbol is not
    there or what follows its `=` is not a run of adjacent raw strings ending
    in `;`.
    """
    match = re.search(r'const char\* const ' + symbol + r' = ((?:R"\(.*?\)"\s*)+);', source, re.S)
    if match is None:
        return None, 0
    parts = re.findall(r'R"\((.*?)\)"', match.group(1), re.S)
    rebuilt = re.sub(r'R"\(.*?\)"', "", match.group(1), flags=re.S)
    if rebuilt.strip() != "":
        return None, 0
    return "".join(parts), len(parts)


def cpp_pieces(source):
    """Every raw-string piece Shaders.cpp defines, by symbol."""
    return re.findall(r'const char\* const (k\w+) = R"\(', source)


def from_js(source, name):
    match = re.search(r'^export const ' + name + r' = `(.*?)`;$', source, re.S | re.M)
    if match is None:
        return None, None
    body = match.group(1)
    stray = re.search(r"\\(?!`)", body)
    if stray is not None:
        line = body[: stray.start()].count("\n") + 1
        return None, f"backslash that is not an escaped backtick, at line {line}"
    if "${" in body:
        return None, "template substitution"
    return body.replace("\\`", "`"), None


def uniforms(glsl):
    """Every uniform name a piece declares, comments stripped first. An array
    (`uniform vec3 FaceOrigin[ 4 ];`) counts by its name."""
    code = re.sub(r"//[^\n]*", "", glsl)
    return re.findall(r"^\s*uniform\s+\w+\s+(\w+)\s*(?:\[\s*\d+\s*\])?\s*;", code, re.M)


def write(cpp):
    out = [HEADER]
    for name, symbol in SHADERS:
        body, count = from_cpp(cpp, symbol)
        if body is None:
            print(f"FAIL  {symbol} not found in source/Shaders.cpp (or not a run of adjacent raw strings)")
            return 1
        if "\\" in body or "${" in body:
            print(f"FAIL  {symbol} holds a backslash or ${{; the escape scheme cannot carry it")
            return 1
        joined = f", {count} adjacent raw strings joined" if count > 1 else ""
        out.append(f"\n// {symbol}, source/Shaders.cpp{joined}\nexport const {name} = `{body.replace('`', chr(92) + '`')}`;\n")
    with open(os.path.join(REPO, "demo", "shaders.js"), "w") as handle:
        handle.write("".join(out))
    print("wrote demo/shaders.js")
    return 0


def first_difference(a, b):
    left, right = a.splitlines(), b.splitlines()
    for i in range(max(len(left), len(right))):
        x = left[i] if i < len(left) else "<missing>"
        y = right[i] if i < len(right) else "<missing>"
        if x != y:
            return i + 1, x, y
    return None


def check(cpp, js):
    problems = 0
    listed = {symbol for _, symbol in SHADERS}
    for symbol in cpp_pieces(cpp):
        if symbol not in listed:
            print(f"FAIL  source/Shaders.cpp defines {symbol}, which this check does not list -- "
                  "add it to SHADERS (and to the page, or say on the page why not)")
            problems += 1

    for name, symbol in SHADERS:
        cpp_text, count = from_cpp(cpp, symbol)
        js_text, complaint = from_js(js, name)
        if cpp_text is None:
            print(f"FAIL  {symbol} not found in source/Shaders.cpp (or not a run of adjacent raw strings)")
            problems += 1
        elif complaint is not None:
            print(f"FAIL  {name} in demo/shaders.js has a {complaint}")
            problems += 1
        elif js_text is None:
            print(f"FAIL  {name} not found in demo/shaders.js")
            problems += 1
        elif cpp_text == js_text:
            joined = f", {count} raw strings joined" if count > 1 else ""
            print(f"ok    {name:<20} matches {symbol} ({len(cpp_text)} chars{joined})")
        else:
            problems += 1
            print(f"FAIL  {name} has drifted from {symbol}")
            where = first_difference(cpp_text, js_text)
            if where:
                print(f"        first difference at line {where[0]}")
                print(f"          C++: {where[1]}")
                print(f"          js : {where[2]}")

    for path, text in ASSEMBLY:
        try:
            body = read(*path.split("/"))
        except FileNotFoundError:
            print(f"FAIL  {path} is missing")
            problems += 1
            continue
        if text in body:
            print(f"ok    assembly: {path} still says {text.splitlines()[0][:60]}")
        else:
            print(f"FAIL  assembly: {path} no longer says: {text.splitlines()[0]}")
            problems += 1

    try:
        plugin = read("demo", "plugin.js")
    except FileNotFoundError:
        plugin = ""

    pieces = [from_cpp(cpp, symbol)[0] for symbol in ("kVersion", "kCommon", "kPepperFragment")]
    if None not in pieces:
        fragment = "".join(pieces)
        for constant in WEBGL_ANCHORS:
            match = re.search(r"^const " + constant + r" = '([^'\\]*)';$", plugin, re.M)
            if match is None:
                print(f"FAIL  webgl: demo/plugin.js has no plain-quoted {constant}")
                problems += 1
                continue
            count = fragment.count(match.group(1))
            if count == 1:
                print(f"ok    webgl: {constant} is in SW Pepper's fragment once: {match.group(1)[:60]}")
            else:
                print(f"FAIL  webgl: {constant} ({match.group(1)}) is in SW Pepper's fragment {count} times, not once "
                      "-- the page's respelling no longer fits the shader")
                problems += 1
    for symbol in UNIFORM_PIECES:
        text, _ = from_cpp(cpp, symbol)
        if text is None:
            continue
        names = uniforms(text)
        unset = [n for n in names if f"'{n}'" not in plugin]
        if unset:
            print(f"FAIL  {symbol} declares {', '.join(unset)}, which demo/plugin.js never names -- "
                  "the page would leave it at 0")
            problems += 1
        else:
            print(f"ok    uniforms: all {len(names)} of {symbol}'s are named in demo/plugin.js")
    return problems


def main(argv):
    cpp = read("source", "Shaders.cpp")
    if "--write" in argv:
        return write(cpp)
    try:
        js = read("demo", "shaders.js")
    except FileNotFoundError:
        print("FAIL  demo/shaders.js is missing -- run demo/tools/check_shaders.py --write")
        return 1
    problems = check(cpp, js)
    print()
    if problems:
        print(f"{problems} problem(s) -- rerun demo/tools/check_shaders.py --write for a drifted copy "
              "(never edit shaders.js by hand); an assembly, uniform or unlisted-piece failure needs a reader")
        return 1
    print(f"all {len(SHADERS)} shader pieces, their assembly and their uniforms are the plugin's, and the one WebGL respelling still fits")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
