"""make.py: writes every SVG in docs/figures from the CSVs in docs/data, in a light and a dark version.

Standard library only, and no clock or random state outside a fixed seed, so a second run writes the
same bytes.
"""
import csv
import random
from pathlib import Path

here = Path(__file__).resolve().parent
data = here.parent / "data"

THEMES = {
    "light": dict(fg="#1f2328", muted="#59636e", grid="#d1d9e0", paper="#ffffff", ultra="#0969da",
                  m2="#8250df", cuda="#1a7f37", pair="#9a6700", match="#1a7f37", design="#9a6700",
                  fah="#59636e"),
    "dark": dict(fg="#e6edf3", muted="#9198a1", grid="#3d444d", paper="#0d1117", ultra="#4493f8",
                 m2="#ab7df8", cuda="#3fb950", pair="#d29922", match="#3fb950", design="#d29922",
                 fah="#9198a1"),
}
FONT = "-apple-system, BlinkMacSystemFont, 'Segoe UI', Helvetica, Arial, sans-serif"


def rows(name):
    with open(data / name, newline="") as f:
        return list(csv.DictReader(f))


def svg(width, height, body, label):
    return (f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" '
            f'viewBox="0 0 {width} {height}" role="img" aria-label="{label}" font-family="{FONT}">\n'
            + "".join(body) + "</svg>\n")


def text(x, y, s, fill, size=13, anchor="start", weight="normal"):
    s = s.replace("&", "&amp;").replace("<", "&lt;")
    return (f'<text x="{x:.1f}" y="{y:.1f}" font-size="{size}" fill="{fill}" text-anchor="{anchor}" '
            f'font-weight="{weight}">{s}</text>\n')


def line(x1, y1, x2, y2, stroke, width=1.0, dash=None):
    d = f' stroke-dasharray="{dash}"' if dash else ""
    return (f'<line x1="{x1:.1f}" y1="{y1:.1f}" x2="{x2:.1f}" y2="{y2:.1f}" stroke="{stroke}" '
            f'stroke-width="{width}"{d}/>\n')


def polyline(points, stroke, width=2.0, dash=None):
    d = f' stroke-dasharray="{dash}"' if dash else ""
    p = " ".join(f"{x:.1f},{y:.1f}" for x, y in points)
    return f'<polyline points="{p}" fill="none" stroke="{stroke}" stroke-width="{width}"{d} stroke-linejoin="round"/>\n'


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
             ("Apple GPU", "M2 to M3 Ultra")]
    colors = [t["muted"], t["ultra"], t["m2"], t["cuda"]]
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
    color = {"RTX 5060 Ti": t["cuda"], "M3 Ultra": t["ultra"], "M2": t["m2"]}
    left, right, top, bar, step = 210, 620, 34, 26, 48
    xmax = 50.0
    sx = lambda h: left + (right - left) * h / xmax
    body = [text(left, 18, "Hours per unit, P18264 (919,221 atoms, 5 ns)", t["fg"], 13, "start", "600")]
    for v in range(0, 51, 10):
        body.append(line(sx(v), top - 4, sx(v), top + step * len(r) - 10, t["grid"]))
        body.append(text(sx(v), top + step * len(r) + 6, f"{v} h", t["muted"], 11, "middle"))
    body.append(line(sx(48), top - 4, sx(48), top + step * len(r) - 10, t["fg"], 1.2, "4 3"))
    body.append(text(sx(48) - 4, top + 8, "48 h timeout", t["fg"], 11, "end"))
    for i, x in enumerate(r):
        y = top + i * step
        body.append(text(left - 10, y + 12, x["machine"], t["fg"], 13, "end", "600"))
        body.append(text(left - 10, y + 27, f'{x["core"].split(" (")[0]}, {x["os"]}', t["muted"], 11, "end"))
        body.append(f'<rect x="{left}" y="{y}" width="{sx(float(x["unit_h"])) - left:.1f}" height="{bar}" '
                    f'rx="3" fill="{color[x["machine"]]}"/>\n')
        body.append(text(sx(float(x["unit_h"])) + 8, y + 18,
                         f'{float(x["unit_h"]):.1f} h ({float(x["s_per_1pct"]):,.0f} s per 1%)', t["fg"], 12))
    return svg(right + 30, top + step * len(r) + 16, body, "Hours per P18264 unit by machine")


def axes(body, t, left, right, top, bottom, xticks, yticks, sx, sy, xfmt, yfmt):
    for v in yticks:
        body.append(line(left, sy(v), right, sy(v), t["grid"]))
        body.append(text(left - 8, sy(v) + 4, yfmt(v), t["muted"], 11, "end"))
    for v in xticks:
        body.append(line(sx(v), bottom, sx(v), bottom + 4, t["muted"]))
        body.append(text(sx(v), bottom + 18, xfmt(v), t["muted"], 11, "middle"))
    body.append(line(left, bottom, right, bottom, t["muted"]))


def legend(body, t, x, y, items):
    for i, (label, color, dash) in enumerate(items):
        yy = y + i * 18
        body.append(line(x, yy, x + 24, yy, color, 2.5, dash))
        body.append(text(x + 32, yy + 4, label, t["fg"], 12))


def distance(t):
    r = rows("distance.csv")
    left, right, top, bottom = 58, 640, 34, 270
    sx = lambda s: left + (right - left) * s / 60000
    sy = lambda d: bottom - (bottom - top) * d / 1.8
    body = [text(left, 18, "Solute distance between trajectories (nm), first 60,000 steps", t["fg"], 13, "start", "600")]
    axes(body, t, left, right, top, bottom, range(0, 60001, 10000), [0, 0.3, 0.6, 0.9, 1.2, 1.5, 1.8], sx, sy,
         lambda s: f"{s // 1000}k" if s else "0", lambda d: f"{d:.1f}")
    body.append(text((left + right) / 2, bottom + 36, "MD step (4 fs each; 60,000 steps = 0.24 ns)", t["muted"], 11, "middle"))
    series = [("cuda_seed1_vs_seed3_nm", "CUDA seed 1 vs CUDA seed 3", t["pair"], "6 4"),
              ("cuda_seed1_vs_real_nm", "CUDA seed 1 vs real", t["cuda"], None),
              ("ultra_vs_real_nm", "M3 Ultra (Metal) vs real", t["ultra"], None),
              ("m2_vs_real_nm", "M2 (Metal) vs real", t["m2"], None)]
    for key, _, color, dash in series:
        body.append(polyline([(sx(0), sy(0))] + [(sx(int(x["step"])), sy(float(x[key]))) for x in r], color, 2.0, dash))
    legend(body, t, left + 330, bottom - 84, [(lab, c, d) for _, lab, c, d in series])
    return svg(right + 20, bottom + 46, body, "Distance from the real trajectory against the distance between two CUDA seeds")


def volume(t):
    r = rows("volume.csv")
    left, right, top, bottom = 64, 640, 34, 250
    lo, hi = 9285, 9325
    sx = lambda s: left + (right - left) * s / 1250000
    sy = lambda v: bottom - (bottom - top) * (v - lo) / (hi - lo)
    body = [text(left, 18, "Box volume over the unit (nm³), the 250 frames both runs wrote", t["fg"], 13, "start", "600")]
    axes(body, t, left, right, top, bottom, range(0, 1250001, 250000), range(lo, hi + 1, 10), sx, sy,
         lambda s: f"{s / 250000:.0f} ns", lambda v: f"{v:,}")
    for key, color, dash in (("real_nm3", t["cuda"], None), ("cuda_seed3_nm3", t["pair"], None)):
        body.append(polyline([(sx(int(x["step"])), sy(float(x[key]))) for x in r], color, 1.0, dash))
    def stats(key):
        v = [float(x[key]) for x in r]
        m = sum(v) / len(v)
        return m, (sum((a - m) ** 2 for a in v) / (len(v) - 1)) ** 0.5
    (rm, rs), (cm, cs) = stats("real_nm3"), stats("cuda_seed3_nm3")
    legend(body, t, left, bottom + 40, [(f"real core 0x27: mean {rm:,.1f}, sd {rs:.1f}", t["cuda"], None),
                                         (f"CUDA replay, seed 3: mean {cm:,.1f}, sd {cs:.1f}", t["pair"], None)])
    return svg(right + 20, bottom + 76, body, "Box volume over the unit, real run against the CUDA replay")


def parity(t):
    r = rows("parity.csv")
    mark = {"match": ("match", t["match"]), "by-design": ("differs by design", t["design"]),
            "fah-only": ("FAH only", t["fah"])}
    cols, cw, ch, left, top = 3, 228, 30, 10, 54
    body = []
    x = left
    for key in ("match", "by-design", "fah-only"):
        n = sum(1 for e in r if e["verdict"] == key)
        label, color = mark[key]
        body.append(f'<rect x="{x}" y="10" width="14" height="14" rx="3" fill="{color}"/>\n')
        body.append(text(x + 20, 22, f"{label} ({n})", t["fg"], 12))
        x += 170
    order = sorted(range(len(r)), key=lambda i: (["match", "by-design", "fah-only"].index(r[i]["verdict"]), i))
    for k, i in enumerate(order):
        e = r[i]
        cx, cy = left + (k % cols) * cw, top + (k // cols) * ch
        color = mark[e["verdict"]][1]
        body.append(f'<rect x="{cx}" y="{cy}" width="{cw - 8}" height="{ch - 6}" rx="4" fill="none" '
                    f'stroke="{color}" stroke-width="1.2"/>\n')
        body.append(f'<rect x="{cx}" y="{cy}" width="6" height="{ch - 6}" rx="2" fill="{color}"/>\n')
        body.append(text(cx + 14, cy + 16, e["item"], t["fg"], 12))
    rows_n = (len(r) + cols - 1) // cols
    return svg(left * 2 + cols * cw - 8, top + rows_n * ch, body, "Parity with core 0x27, item by item")


for theme, t in THEMES.items():
    for name, make in (("hero", hero), ("speed", speed), ("distance", distance), ("volume", volume),
                       ("parity", parity)):
        (here / f"{name}-{theme}.svg").write_text(make(t))
