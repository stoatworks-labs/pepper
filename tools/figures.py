"""The README's figures, rendered through the real plugins by pgtest.

    python3 tools/figures.py [--binary PATH] [--out docs]

Each figure is one or more panels side by side (or in a grid), each panel one
`pgtest --out` render with the settings listed below, so every picture in the
README can be remade and every setting it claims can be read here. Nothing is
retouched: the panels are composited over black and pasted into a grid with a
dark gutter, by hand-rolled PNG code (zlib only), and that is all.
"""
import argparse
import os
import pathlib
import subprocess
import sys
import tempfile
import zlib

ROOT = pathlib.Path(__file__).resolve().parent.parent
GUTTER = 8
GUTTER_RGB = (16, 16, 18)

# Host units (0..1), as pgtest --set takes them; Controls.h has the mappings:
#   Sheet Angle 25..65 deg linear  (0.5 = 45, 0.2 = 33, 0.325 = 38, 0.75 = 55)
#   Elevation  -20..60 deg linear  (0.25 = 0, 0.375 = 10, 0.5625 = 25, 1 = 60)
#   Azimuth   -180..180 deg linear (0.5 = 0, 0.625 = 45)
#   Source Level -3..+6 stops      (0.3333 = 0 stops)
#   Distance 1..12 rig sizes, geometric
FIGURES = {
    # The pane, as shipped: a PET foil at 45 degrees, the figure card on the
    # floor screen, the stage card behind.
    "hero": (1, [
        ("pepper", {}, 1280, 720),
    ]),
    # A thin foil against a 4 % thick tinted float-glass pane: the back
    # surface's image, offset along the pane and tinted twice.
    "glass": (2, [
        ("pepper", {"Ripple": "0"}, 640, 360),
        ("pepper", {"Ripple": "0", "Sheet": "Float Glass", "Thickness": "1",
                    "Tint": "0.8", "Tint_Green": "0.95", "Tint_Blue": "0.85"}, 640, 360),
    ]),
    # The sheet at 38, 45 and 55 degrees from a level seat, 4.7 rig sizes
    # back: a keystone one way (wider at the top), a rectangle, a keystone
    # the other way. --geometry measures it: 1.0442 and 0.9605 top/bottom at
    # 40 and 50 degrees.
    "angles": (3, [
        ("pepper", {"Ripple": "0", "Sheet Angle": "0.325", "Distance": "0.62", "--input-b": "graphic"}, 480, 270),
        ("pepper", {"Ripple": "0", "Sheet Angle": "0.5", "Distance": "0.62", "--input-b": "graphic"}, 480, 270),
        ("pepper", {"Ripple": "0", "Sheet Angle": "0.75", "Distance": "0.62", "--input-b": "graphic"}, 480, 270),
    ]),
    # A white screen at 0 stops in a sheet at 33 degrees, so the level ray
    # meets it at 57, near Brewster's 58.8 for PET: unpolarised, polarised
    # along the picture's vertical (p for this rig: it vanishes), and along
    # its horizontal (s: it does not).
    "brewster": (3, [
        ("pepper", {"Ripple": "0", "Sheet Angle": "0.2", "Source Level": "0.3333", "--input-b": "white"}, 480, 270),
        ("pepper", {"Ripple": "0", "Sheet Angle": "0.2", "Source Level": "0.3333", "Polarisation": "Vertical", "--input-b": "white"}, 480, 270),
        ("pepper", {"Ripple": "0", "Sheet Angle": "0.2", "Source Level": "0.3333", "Polarisation": "Horizontal", "--input-b": "white"}, 480, 270),
    ]),
    # The pyramid on a phone, four acrylic faces at 45 degrees: from a seat 10 degrees up, square on to a face
    # and on a corner; three sides; and from 60 degrees up, where the screen's
    # own feed is seen directly through the faces.
    "pyramid": (2, [
        ("pepper", {"Rig": "Pyramid", "Sheet": "Acrylic", "Elevation": "0.375"}, 640, 360),
        ("pepper", {"Rig": "Pyramid", "Sheet": "Acrylic", "Elevation": "0.375", "Azimuth": "0.625"}, 640, 360),
        ("pepper", {"Rig": "Pyramid", "Sheet": "Acrylic", "Elevation": "0.375", "Sides": "Three"}, 640, 360),
        ("pepper", {"Rig": "Pyramid", "Sheet": "Acrylic", "Elevation": "1"}, 640, 360),
    ]),
    # SW Pepper Feed: the figure card laid out for a real four- and
    # three-sided pyramid.
    "feed": (2, [
        ("feed", {}, 640, 360),
        ("feed", {"Sides": "Three"}, 640, 360),
    ]),
}


def read_png(path):
    """RGBA rows, top first, out of pgtest's own PNG (8-bit RGBA, filter 0)."""
    data = pathlib.Path(path).read_bytes()
    i, idat, width, height = 8, b"", 0, 0
    while i < len(data):
        length = int.from_bytes(data[i:i + 4], "big")
        kind = data[i + 4:i + 8]
        chunk = data[i + 8:i + 8 + length]
        if kind == b"IHDR":
            width = int.from_bytes(chunk[0:4], "big")
            height = int.from_bytes(chunk[4:8], "big")
            if chunk[8] != 8 or chunk[9] != 6:
                sys.exit(f"{path}: not 8-bit RGBA")
        elif kind == b"IDAT":
            idat += chunk
        i += 12 + length
    raw = zlib.decompress(idat)
    stride = width * 4
    rows = []
    for y in range(height):
        start = y * (stride + 1)
        if raw[start] != 0:
            sys.exit(f"{path}: row filter {raw[start]}, expected 0")
        rows.append(raw[start + 1:start + 1 + stride])
    return width, height, rows


def write_png(path, width, height, rows):
    """Opaque RGB."""
    def chunk(kind, payload):
        body = kind + payload
        return len(payload).to_bytes(4, "big") + body + zlib.crc32(body).to_bytes(4, "big")
    raw = b"".join(b"\x00" + bytes(r) for r in rows)
    ihdr = width.to_bytes(4, "big") + height.to_bytes(4, "big") + bytes([8, 2, 0, 0, 0])
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")
    pathlib.Path(path).write_bytes(png)


def render(binary, scratch, index, plugin, settings, width, height):
    out = os.path.join(scratch, f"panel{index}.png")
    args = [binary, "--out", out, "--plugin", plugin, "--size", f"{width}x{height}"]
    for name, value in settings.items():
        if name.startswith("--"):
            args += [name, value]
        else:
            args += ["--set", f"{name}={value}"]
    r = subprocess.run(args, capture_output=True, text=True)
    if r.returncode != 0:
        sys.exit("render failed: " + " ".join(args) + "\n" + r.stdout + r.stderr)
    return read_png(out)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--binary", default=str(ROOT / "build" / "pgtest"))
    ap.add_argument("--out", default=str(ROOT / "docs"))
    args = ap.parse_args()

    os.environ.setdefault("PEPPER_LOG_DIR", tempfile.mkdtemp(prefix="pepper-figure-logs"))
    pathlib.Path(args.out).mkdir(parents=True, exist_ok=True)
    scratch = tempfile.mkdtemp(prefix="pepper-figures")
    index = 0
    for name, (columns, panels) in FIGURES.items():
        images = []
        for plugin, settings, w, h in panels:
            images.append(render(args.binary, scratch, index, plugin, settings, w, h))
            index += 1
        pw, ph = images[0][0], images[0][1]
        rows_of_panels = (len(images) + columns - 1) // columns
        width = columns * pw + (columns - 1) * GUTTER
        height = rows_of_panels * ph + (rows_of_panels - 1) * GUTTER
        canvas = [bytearray(bytes(GUTTER_RGB) * width) for _ in range(height)]
        for k, (w, h, rows) in enumerate(images):
            ox = (k % columns) * (pw + GUTTER)
            oy = (k // columns) * (ph + GUTTER)
            for y in range(h):
                src = rows[y]
                dst = canvas[oy + y]
                for x in range(w):
                    r, g, b, a = src[4 * x:4 * x + 4]
                    # Over black, as a host composites the top of its stack.
                    dst[3 * (ox + x):3 * (ox + x) + 3] = bytes((c * a + 127) // 255 for c in (r, g, b))
        path = os.path.join(args.out, f"{name}.png")
        write_png(path, width, height, canvas)
        print(f"wrote {path} ({width}x{height}, {len(images)} panel(s))")
    return 0


if __name__ == "__main__":
    sys.exit(main())
