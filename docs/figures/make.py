"""make.py: writes every SVG in docs/figures from the CSVs in docs/data, in a light and a dark version,
and docs/social.png (see social_png for how).

Standard library only, and no clock or random state outside a fixed seed, so a second run writes the
same bytes.

One palette across the figures: blue for Metal (darker for the M3 Ultra, lighter for the M2), green for
CUDA on NVIDIA, grey for the real core 0x27 run and other references.
"""
import csv
import math
import random
from statistics import mean, median, stdev
from pathlib import Path

here = Path(__file__).resolve().parent
data = here.parent / "data"

THEMES = {
    "light": dict(fg="#1f2328", muted="#59636e", grid="#d1d9e0", paper="#ffffff", tile="#f6f8fa",
                  ultra="#0969da", m2="#54aeff", cuda="#1a7f37", ref="#818b98"),
    "dark": dict(fg="#e6edf3", muted="#9198a1", grid="#3d444d", paper="#0d1117", tile="#151b23",
                 ultra="#4493f8", m2="#a5d6ff", cuda="#3fb950", ref="#848d97"),
}
FONT = "-apple-system, BlinkMacSystemFont, 'Segoe UI', Helvetica, Arial, sans-serif"


def rows(name):
    with open(data / name, newline="") as f:
        return list(csv.DictReader(f))


def svg(width, height, body, label):
    return (f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" '
            f'viewBox="0 0 {width} {height}" role="img" aria-label="{label}" font-family="{FONT}">\n'
            + "".join(body) + "</svg>\n")


def text(x, y, s, fill, size=12, anchor="start", weight="normal", style=None):
    s = s.replace("&", "&amp;").replace("<", "&lt;")
    st = f' font-style="{style}"' if style else ""
    return (f'<text x="{x:.1f}" y="{y:.1f}" font-size="{size}" fill="{fill}" text-anchor="{anchor}" '
            f'font-weight="{weight}"{st}>{s}</text>\n')


def line(x1, y1, x2, y2, stroke, width=1.0, dash=None):
    d = f' stroke-dasharray="{dash}"' if dash else ""
    return (f'<line x1="{x1:.1f}" y1="{y1:.1f}" x2="{x2:.1f}" y2="{y2:.1f}" stroke="{stroke}" '
            f'stroke-width="{width}"{d}/>\n')


def rect(x, y, w, h, fill, rx=3, stroke=None):
    s = f' stroke="{stroke}" stroke-width="1.2"' if stroke else ""
    return f'<rect x="{x:.1f}" y="{y:.1f}" width="{w:.1f}" height="{h:.1f}" rx="{rx}" fill="{fill}"{s}/>\n'


def circle(x, y, r, fill, stroke=None):
    s = f' stroke="{stroke}" stroke-width="1.8"' if stroke else ""
    return f'<circle cx="{x:.1f}" cy="{y:.1f}" r="{r}" fill="{fill}"{s}/>\n'


def diamond(x, y, r, fill):
    return (f'<path d="M{x:.1f},{y - r:.1f} L{x + r:.1f},{y:.1f} L{x:.1f},{y + r:.1f} L{x - r:.1f},{y:.1f} Z" '
            f'fill="{fill}"/>\n')


def polyline(points, stroke, width=2.0, dash=None):
    d = f' stroke-dasharray="{dash}"' if dash else ""
    p = " ".join(f"{x:.1f},{y:.1f}" for x, y in points)
    return f'<polyline points="{p}" fill="none" stroke="{stroke}" stroke-width="{width}"{d} stroke-linejoin="round"/>\n'


def axes(body, t, left, right, bottom, xticks, yticks, sx, sy, xfmt, yfmt):
    for v in yticks:
        body.append(line(left, sy(v), right, sy(v), t["grid"]))
        body.append(text(left - 8, sy(v) + 4, yfmt(v), t["muted"], 11, "end"))
    for v in xticks:
        body.append(line(sx(v), bottom, sx(v), bottom + 4, t["muted"]))
        body.append(text(sx(v), bottom + 18, xfmt(v), t["muted"], 11, "middle"))
    body.append(line(left, bottom, right, bottom, t["muted"]))


def legend(body, t, x, y, items):
    # items: (label, color, dash); one row each.
    for i, (label, color, dash) in enumerate(items):
        yy = y + i * 18
        body.append(line(x, yy, x + 24, yy, color, 2.5, dash))
        body.append(text(x + 32, yy + 4, label, t["fg"], 12))


def rough_rect(rng, x, y, w, h, stroke, fill):
    # Two loose passes over each edge, the look of a box drawn by hand.
    out = [f'<rect x="{x:.1f}" y="{y:.1f}" width="{w:.1f}" height="{h:.1f}" rx="6" fill="{fill}"/>\n']
    corners = [(x, y), (x + w, y), (x + w, y + h), (x, y + h), (x, y)]
    for _ in range(2):
        d = []
        for (x1, y1), (x2, y2) in zip(corners, corners[1:]):
            j = lambda: rng.uniform(-2.2, 2.2)
            mx, my = (x1 + x2) / 2 + j(), (y1 + y2) / 2 + j()
            d.append(f"M{x1 + j():.1f},{y1 + j():.1f} Q{mx:.1f},{my:.1f} {x2 + j():.1f},{y2 + j():.1f}")
        out.append(f'<path d="{" ".join(d)}" fill="none" stroke="{stroke}" stroke-width="1.6" '
                   f'stroke-linecap="round"/>\n')
    return "".join(out)


def rough_arrow(rng, x1, y, x2, stroke):
    j = lambda: rng.uniform(-1.5, 1.5)
    return (f'<path d="M{x1:.1f},{y + j():.1f} Q{(x1 + x2) / 2:.1f},{y + j() * 2:.1f} {x2:.1f},{y:.1f} '
            f'M{x2 - 9:.1f},{y - 6 + j():.1f} L{x2:.1f},{y:.1f} L{x2 - 9:.1f},{y + 6 + j():.1f}" '
            f'fill="none" stroke="{stroke}" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round"/>\n')


def hero(t):
    rng = random.Random(30)
    boxes = [("FAH client", "fah-client 8.5"), ("minicore", "this repo"), ("OpenMM Metal", "df64 mixed precision"),
             ("Apple GPU", "Apple Silicon")]
    colors = [t["ref"], t["ultra"], t["ultra"], t["m2"]]
    w, h, gap, top = 170, 70, 62, 40
    body = []
    for i, ((name, sub), c) in enumerate(zip(boxes, colors)):
        x = 20 + i * (w + gap)
        body.append(rough_rect(rng, x, top, w, h, c, t["paper"]))
        body.append(text(x + w / 2, top + 31, name, t["fg"], 17, "middle", "600"))
        body.append(text(x + w / 2, top + 52, sub, t["muted"], 12, "middle"))
    labels = ["wudata_01.dat", "System, State", "kernels"]
    for i, lab in enumerate(labels):
        x1 = 20 + i * (w + gap) + w + 6
        body.append(rough_arrow(rng, x1, top + h / 2, x1 + gap - 12, t["fg"]))
        body.append(text(x1 + (gap - 12) / 2, top - 8, lab, t["muted"], 11, "middle"))
    # The result goes back to the client, which uploads it; the core never talks to a server.
    y = top + h + 34
    x0, x1 = 20 + w / 2, 20 + w + gap + w / 2
    body.append(f'<path d="M{x1:.1f},{top + h + 6:.1f} Q{x1 + 3:.1f},{y:.1f} {(x0 + x1) / 2:.1f},{y:.1f} '
                f'Q{x0 - 3:.1f},{y:.1f} {x0:.1f},{top + h + 8:.1f} M{x0 - 6:.1f},{top + h + 17:.1f} '
                f'L{x0:.1f},{top + h + 8:.1f} L{x0 + 6:.1f},{top + h + 17:.1f}" fill="none" stroke="{t["fg"]}" '
                f'stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round"/>\n')
    body.append(text((x0 + x1) / 2, y + 18, "wuresults_01.dat", t["muted"], 11, "middle"))
    return svg(20 * 2 + 4 * w + 3 * gap, 200, body, "FAH client to minicore to OpenMM Metal to Apple GPU")


def speed(t):
    r = rows("speed.csv")
    color = {"RTX 5060 Ti": t["ref"], "M3 Ultra": t["ultra"]}
    left, right, top, bar, step = 210, 600, 12, 28, 46
    xmax = 6.0
    sx = lambda h: left + (right - left) * h / xmax
    body = []
    bottom = top + step * len(r) - 10
    for v in range(0, 7):
        body.append(line(sx(v), top - 4, sx(v), bottom, t["grid"]))
        body.append(text(sx(v), bottom + 16, f"{v} h", t["muted"], 11, "middle"))
    body.append(text((left + right) / 2, bottom + 34, "Hours per unit", t["muted"], 11, "middle"))
    for i, x in enumerate(r):
        y = top + i * step
        body.append(text(left - 10, y + 12, x["machine"], t["fg"], 13, "end", "600"))
        body.append(text(left - 10, y + 27, f'{x["core"].split(" (")[0]}, {x["api"]}', t["muted"], 11, "end"))
        body.append(rect(left, y, sx(float(x["unit_h"])) - left, bar, color[x["machine"]]))
        est = "" if x["unit_h_from"] == "full run" else " est."
        body.append(text(sx(float(x["unit_h"])) + 8, y + 18,
                         f'{float(x["unit_h"]):.2f} h{est}, {float(x["s_per_1pct"]):,.0f} s per 1%', t["fg"], 12))
    return svg(right + 150, bottom + 42, body, "Hours per P18264 unit, M3 Ultra against the real core")


def progress(t):
    r = rows("progress.csv")
    left, right, top, bottom = 58, 640, 14, 230
    lo, hi = 150, 210
    sx = lambda p: left + (right - left) * p / 100
    sy = lambda s: bottom - (bottom - top) * (s - lo) / (hi - lo)
    body = []
    axes(body, t, left, right, bottom, range(0, 101, 10), range(lo, hi + 1, 10), sx, sy,
         lambda p: f"{p}%", lambda s: f"{s} s")
    body.append(text((left + right) / 2, bottom + 36, "Progress through the unit (each point is one 1% interval, 12,500 steps)",
                     t["muted"], 11, "middle"))
    real = [(int(x["pct"]), float(x["real_s"])) for x in r]
    ultra = [(int(x["pct"]), float(x["ultra_s"])) for x in r if x["ultra_s"]]
    body.append(polyline([(sx(p), sy(s)) for p, s in real], t["ref"], 1.5))
    body.append(polyline([(sx(p), sy(s)) for p, s in ultra], t["ultra"], 2.0))
    for p, s in ultra:
        body.append(circle(sx(p), sy(s), 3, t["ultra"]))
    rm, um = median([s for _, s in real]), median([s for _, s in ultra])
    legend(body, t, left + 300, top + 16, [(f"real core 0x27, RTX 5060 Ti: median {rm:.0f} s", t["ref"], None),
                                          (f"minicore, M3 Ultra: median {um:.0f} s", t["ultra"], None)])
    return svg(right + 20, bottom + 46, body, "Seconds per 1% across the unit, real core against the M3 Ultra")


def distance(t):
    r = rows("distance.csv")
    left, right, top, bottom = 58, 640, 30, 266
    sx = lambda s: left + (right - left) * s / 60000
    sy = lambda d: bottom - (bottom - top) * d / 1.8
    body = []
    axes(body, t, left, right, bottom, range(0, 60001, 10000), [0, 0.3, 0.6, 0.9, 1.2, 1.5, 1.8], sx, sy,
         lambda s: f"{s // 1000}k" if s else "0", lambda d: f"{d:.1f}")
    body.append(text((left + right) / 2, bottom + 36, "MD step (4 fs each; 60,000 steps = 0.24 ns)", t["muted"], 11, "middle"))
    body.append(text(left - 8, top - 16, "nm", t["muted"], 11, "end"))
    series = [("cuda_seed1_vs_seed3_nm", "CUDA seed 1 vs CUDA seed 3", t["cuda"], "6 4"),
              ("cuda_seed1_vs_real_nm", "CUDA seed 1 vs real", t["cuda"], None),
              ("ultra_vs_real_nm", "M3 Ultra (Metal) vs real", t["ultra"], None),
              ("m2_vs_real_nm", "M2 (Metal) vs real", t["m2"], None)]
    for key, _, color, dash in series:
        body.append(polyline([(sx(0), sy(0))] + [(sx(int(x["step"])), sy(float(x[key]))) for x in r], color, 2.0, dash))
    legend(body, t, left + 330, bottom - 84, [(lab, c, d) for _, lab, c, d in series])
    return svg(right + 20, bottom + 46, body, "Distance from the real trajectory against the distance between two CUDA seeds")


def volume(t):
    r = rows("volume.csv")
    left, right, top, bottom = 64, 640, 30, 246
    lo, hi = 9285, 9325
    sx = lambda s: left + (right - left) * s / 1250000
    sy = lambda v: bottom - (bottom - top) * (v - lo) / (hi - lo)
    body = []
    axes(body, t, left, right, bottom, range(0, 1250001, 250000), range(lo, hi + 1, 10), sx, sy,
         lambda s: f"{s / 250000:.0f} ns", lambda v: f"{v:,}")
    body.append(text(left - 8, top - 16, "nm³", t["muted"], 11, "end"))
    for key, color in (("real_nm3", t["ref"]), ("cuda_seed3_nm3", t["cuda"])):
        body.append(polyline([(sx(int(x["step"])), sy(float(x[key]))) for x in r], color, 1.0))
    def stats(key):
        v = [float(x[key]) for x in r]
        return mean(v), stdev(v)
    (rm, rs), (cm, cs) = stats("real_nm3"), stats("cuda_seed3_nm3")
    legend(body, t, left, bottom + 40, [(f"real core 0x27: mean {rm:,.1f}, sd {rs:.1f}", t["ref"], None),
                                         (f"CUDA replay, seed 3: mean {cm:,.1f}, sd {cs:.1f}", t["cuda"], None)])
    return svg(right + 20, bottom + 76, body, "Box volume over the unit, real run against the CUDA replay")


def accuracy(t):
    r = rows("accuracy.csv")
    units = list(dict.fromkeys(x["unit"] for x in r))
    arms = [("M3 Ultra", "M3 Ultra, Metal mixed", t["ultra"], -7), ("M2", "M2, Metal mixed", t["m2"], 0),
            ("RTX 5060 Ti", "RTX 5060 Ti, CUDA mixed", t["cuda"], 7)]
    label_w, pw, gap, top, rh = 84, 270, 40, 40, 34
    bottom = top + rh * len(units)
    body = []
    panels = [("worst_force_rmse", "force_limit", "Force RMSE (kJ/mol/nm)", -3, 1),
              ("worst_dpe", "energy_limit", "|ΔPE| (kJ/mol)", -2, 2)]
    for k, (key, lim_key, title, e0, e1) in enumerate(panels):
        left = label_w + k * (pw + gap)
        sx = lambda v, left=left, e0=e0, e1=e1: left + pw * (math.log10(v) - e0) / (e1 - e0)
        body.append(text(left, top - 12, title, t["fg"], 12, "start", "600"))
        for e in range(e0, e1 + 1):
            body.append(line(sx(10 ** e), top, sx(10 ** e), bottom, t["grid"]))
            body.append(text(sx(10 ** e), bottom + 16, f"{10.0 ** e:g}", t["muted"], 11, "middle"))
        lim = float(r[0][lim_key])
        body.append(line(sx(lim), top - 4, sx(lim), bottom, t["ref"], 1.5, "5 3"))
        body.append(text(sx(lim) - 4, bottom - 6, f"limit {lim:g}", t["muted"], 11, "end"))
        body.append(line(left, bottom, left + pw, bottom, t["muted"]))
        for i, u in enumerate(units):
            y = top + rh * i + rh / 2
            if k == 0:
                body.append(text(label_w - 12, y + 4, u, t["fg"], 12, "end"))
            for m, _, color, dy in arms:
                v = float(next(x[key] for x in r if x["unit"] == u and x["machine"] == m))
                body.append(diamond(sx(v), y + dy, 5, color) if m == "RTX 5060 Ti" else circle(sx(v), y + dy, 4, color))
    lx = label_w
    for m, lab, color, _ in arms:
        body.append(diamond(lx + 5, bottom + 40, 5, color) if m == "RTX 5060 Ti" else circle(lx + 5, bottom + 40, 4, color))
        body.append(text(lx + 15, bottom + 44, lab, t["fg"], 12))
        lx += 200
    return svg(label_w + 2 * pw + gap + 20, bottom + 56, body,
               "Worst state-test force and energy error per unit against the Reference platform, Metal and CUDA")


def drift(t):
    r = rows("drift.csv")
    label_w, left, right, top, rh = 250, 260, 600, 12, 32
    lo, hi = -0.007, 0.005
    sx = lambda v: left + (right - left) * (v - lo) / (hi - lo)
    bottom = top + rh * len(r)
    body = []
    for v in (-0.006, -0.004, -0.002, 0.0, 0.002, 0.004):
        body.append(line(sx(v), top, sx(v), bottom, t["muted"] if v == 0 else t["grid"], 1.0))
        body.append(text(sx(v), bottom + 16, f"{v:+.3f}" if v else "0", t["muted"], 11, "middle"))
    body.append(text((left + right) / 2, bottom + 34, "Energy drift, kJ/mol/ps (mean ± SE)", t["muted"], 11, "middle"))
    color = {"Metal mixed": t["ultra"], "CUDA mixed": t["cuda"], "CPU": t["ref"]}
    for i, x in enumerate(r):
        y = top + rh * i + rh / 2
        m, se = float(x["mean_kj_mol_ps"]), float(x["se_kj_mol_ps"])
        c = color[x["platform"]]
        body.append(text(label_w - 12, y, x["arm"], t["fg"], 12, "end", "600"))
        body.append(text(label_w - 12, y + 13, f'{x["openmm"]}, {x["seeds"]} runs', t["muted"], 11, "end"))
        body.append(line(sx(m - se), y + 3, sx(m + se), y + 3, c, 2.0))
        for e in (m - se, m + se):
            body.append(line(sx(e), y - 2, sx(e), y + 8, c, 2.0))
        hollow = x["arm"].endswith("#5492")
        body.append(circle(sx(m), y + 3, 5, t["paper"] if hollow else c, c if hollow else None))
        body.append(text(right + 10, y + 7, f"{m:+.4f} ± {se:.4f}", t["fg"], 12))
    return svg(right + 130, bottom + 42, body, "Energy drift of DHFR in NVE, Metal before and after #5492, CUDA mixed and CPU")


def parity(t):
    r = rows("parity.csv")
    # Label, fill, glyph, glyph colour and outline of each verdict. The legend counts the M3 Ultra column,
    # and "not-rerun" the M2 column: rows ceb9707 changed that the M2 never ran at ceb9707.
    mark = {"match": ("match", t["ultra"], "✓", t["paper"], None),
            "by-design": ("differs by FAH's design", t["tile"], "≠", t["ultra"], t["ultra"]),
            "fah-only": ("needs FAH's keys", t["ref"], "FAH", t["paper"], None),
            "not-rerun": ("M2 not rerun", t["paper"], "–", t["muted"], t["muted"])}
    item_w, cw, rh, top = 196, 76, 22, 52
    body = []
    x = 0
    for key, (label, fill, glyph, ink, edge) in mark.items():
        n = sum(1 for e in r if e["m2" if key == "not-rerun" else "ultra"] == key)
        if not n:
            continue
        body.append(rect(x, 6, 34, 18, fill, 3, edge))
        body.append(text(x + 17, 19, glyph, ink, 12, "middle", "600"))
        body.append(text(x + 42, 20, f"{label} ({n})", t["fg"], 12))
        x += 42 + round(6.6 * len(f"{label} ({n})")) + 22
    body.append(text(item_w + cw / 2, top - 8, "M3 Ultra", t["fg"], 12, "middle", "600"))
    body.append(text(item_w + cw * 1.5, top - 8, "M2", t["fg"], 12, "middle", "600"))
    for i, e in enumerate(r):
        y = top + i * rh
        body.append(text(item_w - 10, y + 15, e["item"], t["fg"], 12, "end"))
        for j, key in enumerate(("ultra", "m2")):
            label, fill, glyph, ink, edge = mark[e[key]]
            cx = item_w + j * cw
            body.append(rect(cx + 2, y + 2, cw - 4, rh - 4, fill, 3, edge))
            body.append(text(cx + cw / 2, y + 15, glyph, ink, 12, "middle", "600"))
        body.append(text(item_w + 2 * cw + 12, y + 15, e["detail"], t["muted"], 11.5))
    return svg(item_w + 2 * cw + 400, top + len(r) * rh + 4, body,
               "Parity with core 0x27 item by item on the M3 Ultra and the M2")


# docs/social.png, the 1280x640 card for link previews. make.py writes it as figures/social.svg, set in
# Inter 4.1 (figures/fonts, SIL Open Font License), and renders that SVG to PNG with headless Google
# Chrome, since Pillow is not in the toolchain. The PNG is byte-identical across runs with the same
# Chrome; make.py records the version it used in figures/social-renderer.txt and leaves the PNG alone
# when it finds no Chrome.
CHROME = ["/Applications/Google Chrome.app/Contents/MacOS/Google Chrome", "google-chrome", "chromium"]


def social_svg():
    t = THEMES["dark"]
    W, H = 1280, 640
    face = "".join(f"@font-face {{ font-family: InterCard; font-weight: {w}; src: url('fonts/Inter-{n}.woff2'); }} "
                   for w, n in ((400, "Regular"), (600, "SemiBold")))
    body = [f'<style>{face}text {{ font-family: InterCard; }}</style>\n', rect(0, 0, W, H, t["paper"], 0),
            rect(0, 0, 16, H, t["ultra"], 0),
            text(80, 170, "fah-core-metal", t["fg"], 96, "start", "600"),
            text(80, 240, "Folding@home on Apple Silicon GPUs", t["muted"], 40),
            text(80, 290, "through OpenMM Metal", t["muted"], 40)]
    # Hours per P18264 unit, the same numbers as Figure 2.
    for k, x in enumerate(rows("speed.csv")):
        y = 360 + k * 90
        c = t["ultra"] if x["machine"] == "M3 Ultra" else t["ref"]
        w = 110 * float(x["unit_h"])
        body.append(text(80, y + 28, x["machine"], t["fg"], 30, "start", "600"))
        body.append(text(80, y + 54, f'{x["core"].split(" (")[0]}, {x["api"]}', t["muted"], 18))
        body.append(rect(420, y, w, 50, c, 4))
        est = "" if x["unit_h_from"] == "full run" else " est."
        body.append(text(440 + w, y + 36, f'{float(x["unit_h"]):.2f} h{est}', t["fg"], 30, "start", "600"))
    body.append(text(80, 590, "P18264, 919,221 atoms, hours per unit; the M3 Ultra's from its first 5%", t["muted"], 22))
    return svg(W, H, body, "fah-core-metal: Folding@home on Apple Silicon GPUs through OpenMM Metal")


def social_png(svg_path, png_path):
    import shutil
    import subprocess
    exe = next((c for c in CHROME if Path(c).exists() or shutil.which(c)), None)
    if exe is None:
        print("make.py: no Chrome found, docs/social.png left as it is")
        return
    version = subprocess.run([exe, "--version"], capture_output=True, text=True).stdout.strip()
    out = png_path.with_suffix(".tmp.png")
    subprocess.run([exe, "--headless=new", "--disable-gpu", "--hide-scrollbars", "--force-device-scale-factor=1",
                    "--allow-file-access-from-files", "--window-size=1280,640", f"--screenshot={out}",
                    svg_path.as_uri()], check=True, capture_output=True)
    out.replace(png_path)
    (here / "social-renderer.txt").write_text(f"{version}\n")


for theme, t in THEMES.items():
    for name, make in (("hero", hero), ("speed", speed), ("progress", progress),
                       ("distance", distance), ("volume", volume), ("accuracy", accuracy), ("drift", drift),
                       ("parity", parity)):
        (here / f"{name}-{theme}.svg").write_text(make(t))
(here / "social.svg").write_text(social_svg())
social_png(here / "social.svg", here.parent / "social.png")
