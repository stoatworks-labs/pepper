"""The browser demo against the plugin: the same frames, two renderers, one picture.

    python3 demo/tools/crosscheck/crosscheck.py [--pgtest build-universal/pgtest] [--size 640x360] [--keep DIR]

Each case below is rendered twice:

  - by the PLUGIN: SW Pepper through `pgtest --pipe` (the real Pepper class in
    a GL 4.1 context, Dest on stdin, Src from a file), SW Pepper Feed through
    `pgtest --plugin feed --out` (the real Feed class, on one of the harness's
    own cards). Build pgtest first; tools/verify.sh leaves one in
    build-universal/.
  - by the PAGE: demo/ served from here and loaded in headless Chrome, through
    window.pepperDemo.renderCase, which makes a fresh instance and renders the
    same frames with the same settings.

The mixer's frames are written here: a stage (Dest) with a grid, gradients
and flat patches, and a dark screen (Src) with bright soft lights, a
checkerboard and saturated bars, the light a ghost is made of. One case's
screen is partly transparent, two are at another shape than the output. The
feed's input is the harness's quads-b or bars card, rebuilt here bit for bit
from Harness.h. A case with a time is the second frame of a two-frame pipe at
1 / time fps, so pgtest's clock (frame / fps) reaches it; the page is handed
1 / fps, the same division.

The GPU half is the plugin's GLSL (check_shaders.py), with one expression
respelled for WebGL (plugin.js forWebGL), so what this mostly tests is the
PORT: every conversion in Controls.cpp, the rig and the camera from Rig.cpp,
the foil's ripple, the coat's table from Coating.cpp and every uniform
Pepper.cpp and Feed.cpp set -- and the respelling, in the thick-sheet cases.
A difference is a port bug, or a difference between GL 4.1 on this machine
and WebGL2 through ANGLE on the same GPU. Before any picture, the page's
tables of controls (names, kinds, defaults, elements) are compared with
`pgtest --list` and `pgtest --list --plugin feed`.

A case passes when no pixel is more than --max levels of 255 apart and
--exact % are identical. Negative controls -- the page with one control one
slider step (0.001) off, or its clock a frame late -- are compared with the
plugin's picture as it should be, and each must fail that test, so the
comparison is shown to see a slip of that size.

Measured on an M4 Max (GL 4.1 against Chrome 156's ANGLE on Metal),
2 November 2026, at 640x360 and 1280x720: all 26 + 3 controls agree; every
case within one level everywhere and 99.997% to 100% identical. Each negative
control is caught: Sheet Angle one step off is 93.5% identical and 13 levels
out somewhere, Distance 78%, Source Level and Coat Thickness 77% and 86%,
Thickness one step off 3 levels out, the clock a frame late 71%, the feed's
Centre one step off 116 levels out at an arm's edge.

Not run by tools/verify.sh: it needs a GPU and Chrome. Exit 1 if a table
differs, a uniform is missing or unset, a case fails or a negative control
passes.
"""
import argparse
import functools
import http.server
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import threading
import zlib

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
DEMO = os.path.abspath(os.path.join(HERE, "..", ".."))
REPO = os.path.abspath(os.path.join(DEMO, ".."))
sys.dont_write_bytecode = True

# name, settings as `pgtest --set` takes them (host values; options by
# element name), and for the mixer: seconds on the clock, and the screen's
# frame ("screen", "screen-alpha", "screen-4x3", "screen-portrait").
MIXER_CASES = [
    ("defaults", {}, 0.0, "screen"),
    ("defaults-t1.25", {}, 1.25, "screen"),
    ("pane-above", {"Mount": "Screen Above"}, 0.0, "screen"),
    ("pane-steep-near", {"Sheet Angle": "0.3", "Distance": "0.15", "Ripple": "0"}, 0.0, "screen"),
    ("float-glass-thick", {"Sheet": "Float Glass", "Thickness": "1", "Ripple": "0"}, 0.0, "screen"),
    ("float-glass-thick-angle", {"Sheet": "Float Glass", "Thickness": "0.6", "Sheet Angle": "0.62", "Azimuth": "0.35", "Ripple": "0"}, 0.0, "screen"),
    ("coat-aluminium", {"Coating": "Aluminium", "Coat Thickness": "0.5"}, 0.0, "screen"),
    ("coat-titania-thick", {"Coating": "Titanium Dioxide", "Coat Thickness": "0.8", "Sheet": "Acrylic", "Thickness": "0.4", "Ripple": "0"}, 0.0, "screen"),
    ("polar-vertical", {"Polarisation": "Vertical", "Sheet Angle": "0.36", "Viewing Angle": "0.5"}, 0.0, "screen"),
    ("polar-45-acrylic", {"Polarisation": "45 Degrees", "Sheet": "Acrylic", "Sheet Angle": "0.42"}, 0.0, "screen"),
    ("pyramid-four", {"Rig": "Pyramid", "Sheet": "Acrylic", "Ripple": "0", "Elevation": "0.4"}, 0.0, "screen"),
    ("pyramid-three-as-is", {"Rig": "Pyramid", "Sides": "Three", "Screen": "As Is", "Centre": "0.6", "Ripple": "0"}, 0.0, "screen"),
    ("pyramid-above-135", {"Rig": "Pyramid", "Mount": "Screen Above", "Polarisation": "135 Degrees", "Elevation": "0.35", "Azimuth": "0.62"}, 0.0, "screen"),
    ("view-moved", {"Distance": "0.6", "Elevation": "0.6", "Azimuth": "0.8"}, 0.0, "screen"),
    ("gamma-gains", {"Linear Light": "0", "Source Level": "0.8", "Stage Level": "0.3", "Source Black": "0.5", "Spill": "0.4"}, 0.0, "screen"),
    ("tint-polycarbonate", {"Tint": "0.8", "Tint_Green": "0.5", "Tint_Blue": "0.9", "Sheet": "Polycarbonate", "Thickness": "0.5", "Ripple": "0"}, 0.0, "screen"),
    ("ripple-strong-t0.7", {"Ripple": "0.8", "Ripple Speed": "0.9"}, 0.7, "screen"),
    ("opacity-half", {"Opacity": "0.5"}, 0.0, "screen"),
    ("screen-alpha", {}, 0.0, "screen-alpha"),
    ("screen-4x3", {"Sheet Angle": "0.45"}, 0.0, "screen-4x3"),
    ("screen-portrait-pyramid", {"Rig": "Pyramid", "Ripple": "0"}, 0.0, "screen-portrait"),
]
# name, settings, the harness's card (quads-b or bars), its size as a share
# of the output's (1 = the output's raster).
FEED_CASES = [
    ("feed-defaults", {}, "quads-b", (1, 1)),
    ("feed-three", {"Sides": "Three"}, "quads-b", (1, 1)),
    ("feed-above", {"Mount": "Screen Above"}, "quads-b", (1, 1)),
    ("feed-centre-0", {"Centre": "0"}, "quads-b", (1, 1)),
    ("feed-centre-1-three", {"Centre": "1", "Sides": "Three"}, "quads-b", (1, 1)),
    ("feed-bars-4x3", {"Centre": "0.25"}, "bars", (0.75, 1)),
]
# The negative controls: a case rendered by the page with one setting a little
# off (or its clock a frame late), compared with the plugin's render of the
# case as it should be. Each is the size of a plausible port slip.
NEGATIVES = [
    ("defaults", "sheet-angle-1-step", {"Sheet Angle": "0.501"}, None, "Sheet Angle one slider step (0.001) off"),
    ("defaults", "distance-1-step", {"Distance": "0.3697"}, None, "Distance one step off"),
    ("defaults", "source-level-1-step", {"Source Level": "0.6121"}, None, "Source Level one step off"),
    ("float-glass-thick", "thickness-1-step", {"Thickness": "0.999"}, None, "Thickness one step off"),
    ("coat-aluminium", "coat-1-step", {"Coat Thickness": "0.501"}, None, "Coat Thickness one step off"),
    ("ripple-strong-t0.7", "clock-1-frame", {}, 0.7 + 1 / 60, "the clock one 60 Hz frame late"),
    ("feed-defaults", "feed-centre-1-step", {"Centre": "0.401"}, None, "the feed's Centre one step off"),
]


def parse_size(text):
    w, h = text.lower().split("x")
    return int(w), int(h)


def run_pgtest(pgtest, args, stdin=None, env=None):
    done = subprocess.run([pgtest, *args], input=stdin, capture_output=True, check=False, env=env)
    if done.returncode != 0:
        sys.exit(f"pgtest {' '.join(args)} failed: {done.stderr.decode(errors='replace').strip()}")
    return done.stdout


# ---------------------------------------------------------------- the frames
def rgba(r, g, b, a=None):
    a = np.full_like(r, 1.0) if a is None else a
    return np.round(np.clip(np.stack([r, g, b, a], axis=2), 0, 1) * 255).astype(np.uint8)


def stage_frame(w, h):
    """Dest, top row first: gradients, a grid, flat patches. Opaque."""
    y, x = np.mgrid[0:h, 0:w].astype(np.float64)
    u, v = (x + 0.5) / w, (y + 0.5) / h
    r = 0.15 + 0.5 * u
    g = 0.2 + 0.35 * (1 - v)
    b = 0.3 + 0.2 * np.cos(2 * np.pi * u * 1.5)
    grid = ((x.astype(int) % max(8, w // 16)) < 2) | ((y.astype(int) % max(8, h // 9)) < 2)
    r, g, b = [np.where(grid, 0.85, c) for c in (r, g, b)]
    patch = (np.abs(u - 0.7) < 0.08) & (np.abs(v - 0.65) < 0.12)
    r, g, b = np.where(patch, 0.9, r), np.where(patch, 0.15, g), np.where(patch, 0.1, b)
    patch = (np.abs(u - 0.22) < 0.06) & (np.abs(v - 0.3) < 0.1)
    r, g, b = np.where(patch, 0.1, r), np.where(patch, 0.7, g), np.where(patch, 0.95, b)
    return rgba(r, g, b)


def screen_frame(w, h, alpha=False):
    """Src, top row first: bright soft lights, a checkerboard and bars on dark."""
    y, x = np.mgrid[0:h, 0:w].astype(np.float64)
    u, v = (x + 0.5) / w, (y + 0.5) / h
    r = np.full_like(u, 0.03)
    g = np.full_like(u, 0.03)
    b = np.full_like(u, 0.05)
    for cu, cv, rad, col in ((0.3, 0.35, 0.12, (1.0, 0.85, 0.6)), (0.68, 0.55, 0.15, (0.4, 0.7, 1.0)),
                             (0.5, 0.8, 0.08, (1.0, 0.3, 0.5))):
        k = np.exp(-((u - cu) ** 2 + ((v - cv) * h / w) ** 2) / (2 * rad * rad))
        r, g, b = r + col[0] * k, g + col[1] * k, b + col[2] * k
    checker = (((x // max(4, w // 32)) + (y // max(4, h // 18))) % 2 == 0) & (u > 0.06) & (u < 0.26) & (v > 0.62) & (v < 0.92)
    r, g, b = [np.where(checker, 0.95, c) for c in (r, g, b)]
    bars = (v > 0.08) & (v < 0.2) & (u > 0.55) & (u < 0.95)
    j = np.floor((u - 0.55) / 0.4 * 6)
    r = np.where(bars, (j % 2 == 0) * 1.0, r)
    g = np.where(bars, (j % 3 == 0) * 1.0, g)
    b = np.where(bars, (j >= 3) * 1.0, b)
    if not alpha:
        return rgba(r, g, b)
    # Partly transparent, premultiplied as the kit's clips are: colour <= alpha.
    a = np.clip(0.25 + 0.75 * u, 0, 1)
    return rgba(np.minimum(r, 1) * a, np.minimum(g, 1) * a, np.minimum(b, 1) * a, a)


def card(name, w, h):
    """The harness's quads-b or bars card (Harness.h), top row first."""
    out = np.zeros((h, w, 4), np.uint8)
    out[..., 3] = 255
    yb, x = np.mgrid[0:h, 0:w]  # yb: the harness's bottom-up row
    if name == "bars":
        j = np.minimum(11, (x * 12) // w)
        out[..., 0], out[..., 1], out[..., 2] = 16 + 20 * j, 90, 200 - 12 * j
    else:
        quads = np.array([(0x00, 0xB4, 0xB4), (0xB4, 0xB4, 0x00), (0x3C, 0x78, 0xFF), (0x60, 0x60, 0x60)], np.uint8)
        q = np.where(yb >= h // 2, 2, 0) + np.where(x >= w // 2, 1, 0)
        out[..., :3] = quads[q]
        lround = lambda t: int(np.floor(t + 0.5))  # std::lround, for t > 0
        x0, x1, y0, y1 = lround(0.1875 * w), lround(0.3125 * w), lround(0.1875 * h), lround(0.3125 * h)
        inside = (x >= x0) & (x < x1) & (yb >= y0) & (yb < y1)
        out[inside, :3] = (0xA0, 0xFF, 0x00)
    return out[::-1].copy()  # bottom-up -> top row first


def read_png(path):
    """pgtest's own PNGs: 8-bit RGBA, filter 0 on every row."""
    with open(path, "rb") as handle:
        data = handle.read()
    pos, idat, width, height = 8, b"", 0, 0
    while pos < len(data):
        length = int.from_bytes(data[pos:pos + 4], "big")
        kind = data[pos + 4:pos + 8]
        body = data[pos + 8:pos + 8 + length]
        if kind == b"IHDR":
            width, height = int.from_bytes(body[0:4], "big"), int.from_bytes(body[4:8], "big")
            if body[8:10] != b"\x08\x06":
                sys.exit(f"{path}: not 8-bit RGBA")
        elif kind == b"IDAT":
            idat += body
        pos += 12 + length
    raw = np.frombuffer(zlib.decompress(idat), np.uint8).reshape(height, 1 + width * 4)
    if raw[:, 0].any():
        sys.exit(f"{path}: a row with a PNG filter; pgtest writes filter 0")
    return raw[:, 1:].tobytes(), width, height


# ---------------------------------------------------------------- the server
class Handler(http.server.SimpleHTTPRequestHandler):
    """demo/ as it is, plus /xc/ for the job, the frames and the page's answers."""

    def __init__(self, *args, state, **kwargs):
        self.state = state
        super().__init__(*args, directory=DEMO, **kwargs)

    def log_message(self, *args):
        pass

    def do_GET(self):
        if self.path == "/xc/job.json":
            return self.reply(json.dumps(self.state["job"]).encode(), "application/json")
        if self.path.startswith("/xc/frame/"):
            return self.reply(self.state["frames"][self.path[len("/xc/frame/"):]], "application/octet-stream")
        return super().do_GET()

    def do_POST(self):
        body = self.rfile.read(int(self.headers.get("Content-Length", 0)))
        if self.path.startswith("/xc/out/"):
            self.state["page"][self.path[len("/xc/out/"):]] = body
        elif self.path == "/xc/table":
            self.state["table"] = json.loads(body)
        elif self.path == "/xc/missing":
            self.state["missing"] = json.loads(body)
        elif self.path == "/xc/error":
            self.state["error"] = body.decode(errors="replace")
            self.state["finished"].set()
        elif self.path == "/xc/done":
            self.state["finished"].set()
        self.reply(b"ok", "text/plain")

    def reply(self, body, kind):
        self.send_response(200)
        self.send_header("Content-Type", kind)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)


# ---------------------------------------------------------------- the judging
def compare(a, b, width, height):
    x = np.frombuffer(a, np.uint8).reshape(height, width, 4).astype(np.int16)
    y = np.frombuffer(b, np.uint8).reshape(height, width, 4).astype(np.int16)
    worst = np.abs(x - y).max(axis=2)
    return {
        "exact": float((worst == 0).mean() * 100.0),
        "within1": float((worst <= 1).mean() * 100.0),
        "within4": float((worst <= 4).mean() * 100.0),
        "max": int(worst.max()),
        "mean": float(np.abs(x - y)[..., :3].mean()),
    }


def passes(s, args):
    return s["max"] <= args.max and s["exact"] >= args.exact


LIST_ROW = re.compile(r"^(\d+)\s+(.+?)\s{2,}(boolean|standard|option|red|green|blue|about)\s+(\S+)\s+(\[[^\]]*\]|-)\s*(.*)$")


def compare_table(pgtest, page_table, plugin, env):
    """`pgtest --list` against the page's table: name, kind, default (4 dp), elements, in order."""
    args = ["--list"] + (["--plugin", "feed"] if plugin == "feed" else [])
    rows = []
    for line in run_pgtest(pgtest, args, env=env).decode().splitlines()[1:]:
        m = LIST_ROW.match(line.rstrip())
        if m is None:
            continue
        index, name, kind, value, _, elements = m.groups()
        if kind == "about":
            continue
        rows.append((int(index), name, kind, float(value), [e.strip() for e in elements.split("|")] if elements else []))
    problems = []
    if len(rows) != len(page_table):
        problems.append(f"pgtest lists {len(rows)} controls, the page {len(page_table)}")
    for (index, name, kind, value, elements), mine in zip(rows, page_table):
        if (name, kind) != (mine["name"], mine["kind"]) or abs(value - mine["value"]) > 0.00005 or elements != mine["elements"]:
            problems.append(f"#{index}: pgtest says {name} {kind} {value:.4f} {elements}, the page "
                            f"{mine['name']} {mine['kind']} {mine['value']:.4f} {mine['elements']}")
    return len(rows), problems


def main(argv):
    parser = argparse.ArgumentParser()
    parser.add_argument("--pgtest", default=os.path.join(REPO, "build-universal", "pgtest"))
    parser.add_argument("--size", default="640x360")
    parser.add_argument("--keep", help="write each case's two pictures here as PNGs")
    parser.add_argument("--max", type=int, default=1,
                        help="the largest difference, in levels of 255, allowed anywhere (default 1)")
    parser.add_argument("--exact", type=float, default=99.0,
                        help="share of pixels, %%, that must be identical (default 99)")
    parser.add_argument("--chrome", default=os.environ.get(
        "CHROME", "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"))
    parser.add_argument("--port", type=int, default=8794)
    args = parser.parse_args(argv)
    width, height = parse_size(args.size)
    if not os.access(args.pgtest, os.X_OK):
        sys.exit(f"no pgtest at {args.pgtest}: build it (cmake -B build-universal && cmake --build build-universal --target pgtest)")

    work = tempfile.mkdtemp(prefix="pepper-xc-")
    # The harness writes the plugin's log; keep it out of ~/Library/Logs.
    env = {**os.environ, "PEPPER_LOG_DIR": os.path.join(work, "log")}
    size = f"{width}x{height}"
    frame_bytes = width * height * 4
    shapes = {
        "screen": (width, height),
        "screen-alpha": (width, height),
        "screen-4x3": (height * 4 // 3, height),
        "screen-portrait": (height * 3 // 4, height * 4 // 3),
    }
    frames = {"stage": stage_frame(width, height).tobytes()}
    for key, (sw, sh) in shapes.items():
        frames[key] = screen_frame(sw, sh, alpha=key == "screen-alpha").tobytes()

    def plugin_mixer(settings, seconds, screen):
        sw, sh = shapes[screen]
        src_path = os.path.join(work, f"{screen}.rgba")
        count = 2 if seconds else 1
        with open(src_path, "wb") as handle:
            handle.write(frames[screen] * count)
        sets = [x for k, v in settings.items() for x in ("--set", f"{k}={v}")]
        fps = repr(1.0 / seconds) if seconds else "30"
        out = run_pgtest(args.pgtest, ["--pipe", "--size", size, "--pipe-src", src_path, "--src-size", f"{sw}x{sh}",
                                       "--fps", fps, *sets], stdin=frames["stage"] * count, env=env)
        if len(out) != frame_bytes * count:
            sys.exit(f"pgtest --pipe gave {len(out)} bytes, not {count} frame(s)")
        return out[-frame_bytes:], (fps if seconds else None)

    plugin, cases = {}, []
    for name, settings, seconds, screen in MIXER_CASES:
        plugin[name], fps = plugin_mixer(settings, seconds, screen)
        sw, sh = shapes[screen]
        cases.append({"name": name, "plugin": "pepper", "settings": settings, "width": width, "height": height,
                      "dest": "stage", "src": screen, "srcWidth": sw, "srcHeight": sh, "fps": fps})
    for name, settings, which, (fw, fh) in FEED_CASES:
        sw, sh = round(width * fw), round(height * fh)
        key = f"{which}-{sw}x{sh}"
        frames.setdefault(key, card(which, sw, sh).tobytes())
        png = os.path.join(work, f"{name}.png")
        sets = [x for k, v in settings.items() for x in ("--set", f"{k}={v}")]
        run_pgtest(args.pgtest, ["--plugin", "feed", "--out", png, "--input-b", which, "--size", size,
                                 "--src-size", f"{sw}x{sh}", *sets], env=env)
        pixels, pw, ph = read_png(png)
        if (pw, ph) != (width, height):
            sys.exit(f"pgtest --out for {name} wrote {pw}x{ph}")
        plugin[name] = pixels
        cases.append({"name": name, "plugin": "feed", "settings": settings, "width": width, "height": height,
                      "dest": None, "src": key, "srcWidth": sw, "srcHeight": sh, "fps": None})

    by_name = {c["name"]: c for c in cases}
    for base, name, change, seconds, _ in NEGATIVES:
        late = {**by_name[base], "name": name, "settings": {**by_name[base]["settings"], **change}}
        if seconds is not None:
            late["fps"] = repr(1.0 / seconds)
        cases.append(late)

    state = {"job": {"cases": cases}, "frames": frames, "page": {}, "table": None, "missing": None,
             "error": None, "finished": threading.Event()}
    server = http.server.ThreadingHTTPServer(("127.0.0.1", args.port), functools.partial(Handler, state=state))
    threading.Thread(target=server.serve_forever, daemon=True).start()
    profile = tempfile.mkdtemp(prefix="pepper-chrome-")
    chrome = subprocess.Popen([
        args.chrome, "--headless=new", "--no-first-run", "--no-default-browser-check",
        f"--user-data-dir={profile}", "--use-angle=metal", "--enable-gpu", "--ignore-gpu-blocklist",
        "--window-size=1600,1200", f"http://127.0.0.1:{args.port}/tools/crosscheck/",
    ], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        if not state["finished"].wait(600):
            sys.exit("the page never finished (600 s)")
    finally:
        chrome.terminate()
        try:
            chrome.wait(10)
        except subprocess.TimeoutExpired:
            chrome.kill()
        server.shutdown()
        shutil.rmtree(profile, ignore_errors=True)
    if state["error"]:
        shutil.rmtree(work, ignore_errors=True)
        sys.exit(f"the page failed:\n{state['error']}")

    failures = 0
    for which in ("pepper", "feed"):
        count, table_problems = compare_table(args.pgtest, (state["table"] or {}).get(which, []), which, env)
        if table_problems:
            failures += len(table_problems)
            print(f"the page's {which} controls differ from pgtest --list:")
            for line in table_problems:
                print(f"  {line}")
        else:
            print(f"controls, {which}: all {count} of pgtest --list's, same names, kinds, defaults and elements, in order")
    gaps = state["missing"] or {}
    for label, key in (("uniforms the page set that a program lacks", "missing"),
                       ("uniforms a program has that the page never set", "unset"), ("GL errors", "errors")):
        if gaps.get(key):
            failures += 1
            print(f"{label}: {', '.join(sorted(set(gaps[key])))}")
    print(f"every uniform set, none missing, no GL error" if not any(gaps.get(k) for k in ("missing", "unset", "errors")) else "")

    if args.keep:
        from PIL import Image
        os.makedirs(args.keep, exist_ok=True)

    print(f"\n{size}, one frame each\n")
    print(f"{'case':<26} {'==':>9} {'<=1':>9} {'<=4':>9} {'max':>4} {'mean':>7}")
    for name in [c[0] for c in MIXER_CASES] + [c[0] for c in FEED_CASES]:
        got = state["page"].get(name)
        if got is None or len(got) != frame_bytes:
            print(f"{name:<26} no picture from the page")
            failures += 1
            continue
        s = compare(plugin[name], got, width, height)
        ok = passes(s, args)
        failures += 0 if ok else 1
        print(f"{name:<26} {s['exact']:8.3f}% {s['within1']:8.3f}% {s['within4']:8.3f}% {s['max']:>4} {s['mean']:7.4f}  {'ok' if ok else 'FAIL'}")
        if args.keep:
            for label, data in (("plugin", plugin[name]), ("page", got)):
                Image.frombytes("RGBA", (width, height), data).save(os.path.join(args.keep, f"{name}-{label}.png"))

    print("\nnegative controls: the page a little off, against the plugin as it should be")
    for base, name, _, _, label in NEGATIVES:
        late = state["page"].get(name)
        s = compare(plugin[base], late, width, height) if late else None
        caught = s is not None and not passes(s, args)
        failures += 0 if caught else 1
        print(f"  {label:<40} " + (f"{s['exact']:8.3f}% {s['within1']:8.3f}% {s['max']:>4} {s['mean']:7.4f}  "
                                     f"{'caught' if caught else 'NOT CAUGHT'}" if s else "no picture"))
    print(f"\n{'every case passes, every negative control is caught' if not failures else f'{failures} problem(s)'}"
          f" (pass: no pixel more than {args.max} level(s) apart and {args.exact}% identical)")
    shutil.rmtree(work, ignore_errors=True)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
