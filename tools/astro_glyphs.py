#!/usr/bin/env python3
"""Generate MeteoPlaneRadar/AstroGlyphs.h - the astrological symbols for the
planets screen.

The built-in GFX font has no planet or sign glyphs, so they are drawn here from
simple geometry (lines, circles, arcs), rasterised to one-bit bitmaps and
written out as ASCII art the firmware draws pixel by pixel. The header is
human-editable: a '#' is a lit pixel, anything else is not. Re-run this to
regenerate it after changing a shape; it needs only Pillow.

    python3 tools/astro_glyphs.py            # writes the header
    python3 tools/astro_glyphs.py sheet.png  # also writes a 8x preview sheet

Shapes are the classic forms a chart uses (and Astrolog draws): Sun as a
circled dot, Venus the mirror, Mars the shield and spear, Jupiter the
"2 with a cross", Saturn the sickle, Uranus the H with a circle, Neptune the
trident, Pluto the circle in a crescent over a cross, the node a horseshoe.
"""
import sys
from PIL import Image, ImageDraw

SS = 8           # supersampling factor
PLANET_PX = 24   # glyph box on the display, pixels
SIGN_PX = 22


class G:
    """A glyph drawn in a unit square (0..1, y down), rasterised on demand."""

    def __init__(self, name, px, stroke=0.10):
        self.name, self.px, self.stroke = name, px, stroke
        self.ops = []

    def line(self, a, b):           self.ops.append(("line", a, b)); return self
    def poly(self, *pts):
        for a, b in zip(pts, pts[1:]): self.line(a, b)
        return self
    def circle(self, c, r):        self.ops.append(("circle", c, r)); return self
    def disc(self, c, r):          self.ops.append(("disc", c, r)); return self
    def hole(self, c, r):          self.ops.append(("hole", c, r)); return self
    def arc(self, c, r, a0, a1):   self.ops.append(("arc", c, r, a0, a1)); return self   # PIL degrees, clockwise from 3 o'clock
    def dot(self, c, r):           return self.disc(c, r)

    def render(self):
        n = self.px * SS
        im = Image.new("L", (n, n), 0)
        d = ImageDraw.Draw(im)
        w = max(1, int(self.stroke * n))
        s = lambda p: (p[0] * n, p[1] * n)
        for op in self.ops:
            k = op[0]
            if k == "line":
                d.line([s(op[1]), s(op[2])], fill=255, width=w)
                for p in (op[1], op[2]):          # round the ends
                    x, y = s(p); d.ellipse([x - w / 2, y - w / 2, x + w / 2, y + w / 2], fill=255)
            elif k in ("circle", "arc"):
                (cx, cy), r = s(op[1]), op[2] * n
                box = [cx - r, cy - r, cx + r, cy + r]
                if k == "circle": d.ellipse(box, outline=255, width=w)
                else:             d.arc(box, op[3], op[4], fill=255, width=w)
            elif k == "disc":
                (cx, cy), r = s(op[1]), op[2] * n
                d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=255)
            elif k == "hole":
                (cx, cy), r = s(op[1]), op[2] * n
                d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=0)
        # Downsample by coverage: a display pixel is lit when at least 45 % of
        # its supersamples are, which keeps 2 px strokes solid without bloating.
        rows = []
        px = im.load()
        for y in range(self.px):
            row = ""
            for x in range(self.px):
                cov = sum(px[x * SS + i, y * SS + j] for i in range(SS) for j in range(SS)) / (255.0 * SS * SS)
                row += "#" if cov >= 0.45 else "."
            rows.append(row)
        return rows


def planets():
    P = lambda name: G(name, PLANET_PX)
    out = []
    out.append(P("SUN").circle((0.5, 0.5), 0.40).dot((0.5, 0.5), 0.09))
    out.append(P("MOON").disc((0.46, 0.5), 0.40).hole((0.66, 0.5), 0.36))
    out.append(P("MERCURY").circle((0.5, 0.46), 0.20).line((0.5, 0.66), (0.5, 0.96)).line((0.32, 0.83), (0.68, 0.83))
               .arc((0.5, 0.06), 0.22, 15, 165))
    out.append(P("VENUS").circle((0.5, 0.38), 0.26).line((0.5, 0.64), (0.5, 0.97)).line((0.3, 0.84), (0.7, 0.84)))
    out.append(P("MARS").circle((0.40, 0.60), 0.28).line((0.60, 0.40), (0.92, 0.08))
               .line((0.92, 0.08), (0.64, 0.08)).line((0.92, 0.08), (0.92, 0.36)))
    out.append(P("JUPITER").line((0.70, 0.06), (0.70, 0.96)).line((0.08, 0.70), (0.94, 0.70))
               .arc((0.36, 0.28), 0.20, 190, 370).line((0.55, 0.32), (0.10, 0.70)))
    out.append(P("SATURN").line((0.34, 0.05), (0.34, 0.66)).line((0.14, 0.22), (0.56, 0.22))
               .arc((0.58, 0.62), 0.24, 180, 450).line((0.58, 0.86), (0.46, 0.97)))
    out.append(P("URANUS").line((0.26, 0.05), (0.26, 0.60)).line((0.74, 0.05), (0.74, 0.60)).line((0.26, 0.32), (0.74, 0.32))
               .line((0.5, 0.32), (0.5, 0.72)).circle((0.5, 0.84), 0.13))
    out.append(P("NEPTUNE").line((0.5, 0.18), (0.5, 0.96)).line((0.30, 0.82), (0.70, 0.82))
               .arc((0.5, 0.30), 0.30, 0, 180).line((0.20, 0.05), (0.20, 0.30)).line((0.80, 0.05), (0.80, 0.30))
               .line((0.5, 0.04), (0.5, 0.18)))
    out.append(P("PLUTO").arc((0.5, 0.40), 0.30, 0, 180).circle((0.5, 0.36), 0.12)
               .line((0.5, 0.70), (0.5, 0.97)).line((0.3, 0.86), (0.7, 0.86)))
    out.append(P("NODE").arc((0.5, 0.42), 0.28, 140, 400).circle((0.26, 0.66), 0.11).circle((0.74, 0.66), 0.11))
    return out


def signs():
    S = lambda name: G(name, SIGN_PX)
    out = []
    out.append(S("ARIES").line((0.5, 0.30), (0.5, 0.96)).arc((0.30, 0.30), 0.20, 180, 360).line((0.10, 0.30), (0.10, 0.52))
               .arc((0.70, 0.30), 0.20, 180, 360).line((0.90, 0.30), (0.90, 0.52)))
    out.append(S("TAURUS").circle((0.5, 0.64), 0.27).arc((0.5, 0.08), 0.30, 0, 180))
    out.append(S("GEMINI").line((0.32, 0.16), (0.32, 0.84)).line((0.68, 0.16), (0.68, 0.84))
               .line((0.12, 0.10), (0.88, 0.10)).line((0.12, 0.90), (0.88, 0.90)))
    out.append(S("CANCER").circle((0.28, 0.40), 0.13).arc((0.50, 0.44), 0.33, 200, 345)
               .circle((0.72, 0.62), 0.13).arc((0.50, 0.58), 0.33, 20, 165))
    out.append(S("LEO").circle((0.26, 0.72), 0.14).arc((0.52, 0.40), 0.26, 180, 360).line((0.78, 0.40), (0.78, 0.74))
               .arc((0.66, 0.78), 0.12, 0, 160))
    out.append(S("VIRGO").line((0.08, 0.30), (0.08, 0.86)).arc((0.21, 0.42), 0.13, 180, 360).line((0.34, 0.42), (0.34, 0.86))
               .arc((0.47, 0.42), 0.13, 180, 360).line((0.60, 0.42), (0.60, 0.74))
               .arc((0.74, 0.74), 0.14, 180, 450).line((0.74, 0.88), (0.58, 0.98)))
    out.append(S("LIBRA").line((0.08, 0.62), (0.28, 0.62)).arc((0.5, 0.58), 0.22, 160, 380).line((0.72, 0.62), (0.92, 0.62))
               .line((0.08, 0.86), (0.92, 0.86)))
    out.append(S("SCORPIO").line((0.08, 0.30), (0.08, 0.86)).arc((0.21, 0.42), 0.13, 180, 360).line((0.34, 0.42), (0.34, 0.86))
               .arc((0.47, 0.42), 0.13, 180, 360).line((0.60, 0.42), (0.60, 0.80)).line((0.60, 0.80), (0.92, 0.80))
               .line((0.92, 0.80), (0.80, 0.68)).line((0.92, 0.80), (0.80, 0.92)))
    out.append(S("SAGITTARIUS").line((0.14, 0.86), (0.86, 0.14)).line((0.86, 0.14), (0.50, 0.14)).line((0.86, 0.14), (0.86, 0.50))
               .line((0.30, 0.44), (0.56, 0.70)))
    out.append(S("CAPRICORN").line((0.08, 0.22), (0.26, 0.66)).line((0.26, 0.66), (0.44, 0.22)).line((0.44, 0.22), (0.56, 0.66))
               .circle((0.70, 0.74), 0.16).line((0.56, 0.92), (0.72, 0.92)))
    out.append(S("AQUARIUS").poly((0.06, 0.40), (0.24, 0.26), (0.42, 0.40), (0.60, 0.26), (0.78, 0.40), (0.94, 0.28))
               .poly((0.06, 0.74), (0.24, 0.60), (0.42, 0.74), (0.60, 0.60), (0.78, 0.74), (0.94, 0.62)))
    out.append(S("PISCES").arc((0.08, 0.5), 0.32, 290, 430).arc((0.92, 0.5), 0.32, 110, 250).line((0.28, 0.5), (0.72, 0.5)))
    return out


def emit(path, groups):
    lines = []
    lines.append("// =============================================================================")
    lines.append("//  MeteoPlaneRadar")
    lines.append("//  AstroGlyphs.h - the planet and sign symbols of the planets screen.")
    lines.append("//")
    lines.append("//  GENERATED by tools/astro_glyphs.py from simple geometry; edit the shapes")
    lines.append("//  there, or touch up a pixel here by hand - '#' is lit, '.' is not. The")
    lines.append("//  firmware draws them pixel by pixel in the body's colour (see")
    lines.append("//  drawGlyph() in ScreenPlanets.cpp), so there is no font involved.")
    lines.append("//")
    lines.append("//  Project: MeteoPlaneRadar - live aircraft radar on a round touchscreen")
    lines.append("//  Author:  Petr / chiptron.cz   (vyvoj / development: chiptron.cz)")
    lines.append("// =============================================================================")
    lines.append("#pragma once")
    lines.append("#include <Arduino.h>")
    lines.append("")
    lines.append("struct AstroGlyph { uint8_t w, h; const char* const* rows; };")
    lines.append("")
    for gname, glyphs, order in groups:
        for g in glyphs:
            rows = g.render()
            lines.append(f"static const char* const GLYPH_{g.name}_ROWS[{g.px}] = {{")
            for r in rows:
                lines.append(f'  "{r}",')
            lines.append("};")
        lines.append("")
        lines.append(f"// Indexed by {order}.")
        lines.append(f"static const AstroGlyph {gname}[{len(glyphs)}] = {{")
        for g in glyphs:
            lines.append(f"  {{ {g.px}, {g.px}, GLYPH_{g.name}_ROWS }},")
        lines.append("};")
        lines.append("")
    with open(path, "w") as f:
        f.write("\n".join(lines))


def sheet(path, groups):
    scale = 8
    allg = [g for _, gl, _ in groups for g in gl]
    cols = 12
    cell = (PLANET_PX + 2) * scale
    rows_n = (len(allg) + cols - 1) // cols
    im = Image.new("RGB", (cols * cell, rows_n * cell), (0, 0, 0))
    d = ImageDraw.Draw(im)
    for i, g in enumerate(allg):
        ox, oy = (i % cols) * cell, (i // cols) * cell
        for y, r in enumerate(g.render()):
            for x, ch in enumerate(r):
                if ch == "#":
                    d.rectangle([ox + x * scale, oy + y * scale, ox + (x + 1) * scale - 1, oy + (y + 1) * scale - 1], fill=(255, 210, 80))
        d.text((ox + 2, oy + cell - 14), g.name, fill=(120, 120, 120))
    im.save(path)


if __name__ == "__main__":
    import os
    here = os.path.dirname(os.path.abspath(__file__))
    groups = [("PLANET_GLYPHS", planets(), "AstroBody (Astro.h)"),
              ("SIGN_GLYPHS", signs(), "sign number, 0 = Aries")]
    emit(os.path.join(here, "..", "MeteoPlaneRadar", "AstroGlyphs.h"), groups)
    if len(sys.argv) > 1:
        sheet(sys.argv[1], groups)
