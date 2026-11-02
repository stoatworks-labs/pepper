"""Every parameter of both plugins must actually change the picture.

A uniform name that does not match between the C++ and the GLSL is silently
ignored: glGetUniformLocation returns -1, glUniform on -1 is a documented no-op,
and nothing in the build says a word. A control can therefore be completely dead
while everything compiles, links, loads and renders. Nothing else in this repo
catches that.

So: render each parameter at two positions with both input cards present, and
report any that made no difference at all. Both plugins are swept: SW Pepper
(the mixer) and SW Pepper Feed (`--plugin feed`).

    python3 tools/sweep.py [--size WxH] [--jobs N] [--binary PATH]

Exit code 1 means something is dead.

------------------------------------------------------------------ the traps

**The mixer needs two inputs.** The harness feeds them itself -- `--input-a`
is Dest, the stage behind the glass, and `--input-b` is Src, the hidden
screen -- and the defaults are the stage card and the figure card.

**Sides, Centre and Screen belong to the pyramid.** The default rig is the
pane, which has no sides, no centre and a screen it shows as it is; they are
swept with Rig = Pyramid.

**Ripple Speed needs time to pass.** At t = 0 every mode is at its phase
offset whatever its speed, so it is swept at 1.25 s. (The harness primes the
clock at 0 and then renders at --time; see MixRig::Render.)

**Azimuth's two ends are one seat.** -180 and +180 degrees put the camera in
the same place, so the ends render the same picture; it is swept from 0 to
+90 degrees instead (0.5 to 0.75).

**Every name must be unique.** `--set` finds a parameter by name and takes the
first match. `pgtest --names` asserts it.

**Never sweep the About block.** Those are buttons that open a web browser.
"""
import argparse
import concurrent.futures
import os
import pathlib
import re
import subprocess
import sys
import tempfile
import zlib

ROOT = pathlib.Path(__file__).resolve().parent.parent
BIN = str(ROOT / "build" / "pgtest")
SCRATCH = tempfile.mkdtemp(prefix="pgsweep")

WIDTH, HEIGHT = 480, 270

# Parameters that cannot or must not be swept, with the reason.
SKIP = {}

PYRAMID = {"Rig": "Pyramid"}

# (plugin, name) -> the other settings it is swept under.
CONTEXT = {
    ("pepper", "Sides"): PYRAMID,
    ("pepper", "Centre"): PYRAMID,
    ("pepper", "Screen"): PYRAMID,
}

# (plugin, name) -> the two positions swept, where the range's ends will not do.
POSITIONS = {
    ("pepper", "Azimuth"): (0.5, 0.75),
}

# (plugin, name) -> extra harness arguments.
EXTRA = {
    ("pepper", "Ripple Speed"): ["--time", "1.25"],
}


def parameters(plugin):
    """id, name, kind, low, high from the harness's own declaration."""
    out = subprocess.run([BIN, "--list", "--plugin", plugin], capture_output=True, text=True)
    if out.returncode != 0:
        print("could not list parameters:", out.stdout, out.stderr)
        sys.exit(1)

    found = []
    for line in out.stdout.splitlines():
        m = re.match(
            r"\s*(\d+)\s+(.+?)\s{2,}(\S+)\s+([\d.eE+-]+)\s+\[\s*([\d.eE+-]+)\s*\.\.\s*([\d.eE+-]+)\s*\]",
            line,
        )
        if m:
            found.append(
                (int(m.group(1)), m.group(2).strip(), m.group(3),
                 float(m.group(5)), float(m.group(6)))
            )
        else:
            m = re.match(r"\s*(\d+)\s+(.+?)\s{2,}(about|text|buffer)\s", line)
            if m:
                found.append((int(m.group(1)), m.group(2).strip(), m.group(3), 0.0, 0.0))
    return found


def render(path, plugin, overrides, extra):
    args = [BIN, "--out", path, "--plugin", plugin, "--size", f"{WIDTH}x{HEIGHT}"] + extra
    for name, value in overrides.items():
        args += ["--set", f"{name}={value}"]
    r = subprocess.run(args, capture_output=True, text=True)
    if r.returncode != 0:
        print("render failed:", " ".join(args), r.stdout, r.stderr)
        sys.exit(1)
    return pathlib.Path(path).read_bytes()


def pixels(png):
    """Raw RGBA out of the harness's own PNG (filter 0 rows), so nothing else
    is a dependency."""
    i = 8
    idat = b""
    width = height = 0
    while i < len(png):
        length = int.from_bytes(png[i:i + 4], "big")
        kind = png[i + 4:i + 8]
        data = png[i + 8:i + 8 + length]
        if kind == b"IHDR":
            width = int.from_bytes(data[0:4], "big")
            height = int.from_bytes(data[4:8], "big")
        elif kind == b"IDAT":
            idat += data
        i += 12 + length
    raw = zlib.decompress(idat)
    stride = width * 4
    out = bytearray()
    for row in range(height):
        out += raw[row * (stride + 1) + 1:(row + 1) * (stride + 1)]
    return out


def difference(a, b):
    pa, pb = pixels(a), pixels(b)
    if len(pa) != len(pb):
        return 1.0, len(pa)
    changed = sum(1 for x, y in zip(pa, pb) if x != y)
    return changed / max(len(pa), 1), changed


def sweep_one(job):
    plugin, pid, name, low, high, context, extra = job

    lo = dict(context)
    hi = dict(context)
    lo[name] = low
    hi[name] = high

    a = render(f"{SCRATCH}/{plugin}_{pid}_lo.png", plugin, lo, extra)
    b = render(f"{SCRATCH}/{plugin}_{pid}_hi.png", plugin, hi, extra)
    fraction, count = difference(a, b)
    # Progress as it happens, on stderr, so a run that is cut off still says
    # how far it got.
    print(f"  swept {plugin} {pid:3d} {name}", file=sys.stderr, flush=True)
    return plugin, pid, name, fraction, count


def main():
    global WIDTH, HEIGHT, BIN

    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--size", default="%dx%d" % (WIDTH, HEIGHT))
    ap.add_argument("--jobs", type=int, default=0)
    ap.add_argument("--binary", default=BIN)
    args = ap.parse_args()
    BIN = args.binary
    if "x" in args.size:
        WIDTH, HEIGHT = (int(v) for v in args.size.split("x", 1))
    jobs = args.jobs or min(8, os.cpu_count() or 1)

    if not pathlib.Path(BIN).exists():
        print(f"{BIN} is not built")
        return 1

    # The harness writes the plugin's log on every render; keep it out of
    # the log an Arena session would be read from.
    os.environ.setdefault("PEPPER_LOG_DIR", os.path.join(SCRATCH, "logs"))

    skipped = []
    work = []
    for plugin in ("pepper", "feed"):
        for pid, name, kind, low, high in parameters(plugin):
            if kind == "about":
                skipped.append((f"{plugin} {name}", "a button that opens a web browser"))
                continue
            if kind in ("text", "buffer") or name in SKIP:
                skipped.append((f"{plugin} {name}", SKIP.get(name, kind)))
                continue
            context = CONTEXT.get((plugin, name), {})
            extra = EXTRA.get((plugin, name), [])
            low, high = POSITIONS.get((plugin, name), (low, high))
            work.append((plugin, pid, name, low, high, context, extra))

    results = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as pool:
        for r in pool.map(sweep_one, work):
            results.append(r)

    dead = []
    for plugin, pid, name, fraction, count in sorted(results):
        if count == 0:
            dead.append(f"{plugin} {name}")
            print(f"DEAD  {plugin:6s} {pid:4d}  {name}")
        else:
            print(f"ok    {plugin:6s} {pid:4d}  {name}  ({count} subpixels, {fraction * 100:.2f}%)")

    print()
    for name, why in skipped:
        print(f"skip  {name}: {why}")

    print(f"\n{len(results)} swept, {len(dead)} dead, {len(skipped)} skipped, {jobs} at a time")
    if dead:
        print("\nDEAD CONTROLS: " + ", ".join(dead))
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
