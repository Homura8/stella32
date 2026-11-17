#!/usr/bin/env python3
"""Rasterise the Celestia Glow fonts into Stella's 4-bit anti-aliased format.

Usage:  python3 tools/fontgen.py            (writes src/stella/fonts/*.cpp)

Fonts (SIL OFL 1.1, see tools/fonts/*-OFL.txt) come from the Celestia Glow site:
Quicksand for text, Baloo 2 for headings and the clock. The generated tables are
committed because Arduino builds cannot run Python. Edit ROLES below to change
sizes, then rerun; output is deterministic for a given Pillow/FreeType version.
"""
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
FONTS = ROOT / "tools" / "fonts"
OUT = ROOT / "src" / "stella" / "fonts"

ASCII = "".join(chr(c) for c in range(32, 127))
EXTRAS = "\u00b0\u00b7\u2013\u2022\u2026"  # degree, middle dot, en dash, bullet, ellipsis
CLOCK = " 0123456789:-APM\u00b0"  # clock digits and big temperatures

# role -> (font file, pixel size per density class, character set)
ROLES = {
    "caption":  ("quicksand-700.woff", {"compact": 11, "regular": 13}, ASCII + EXTRAS),
    "body":     ("quicksand-600.woff", {"compact": 14, "regular": 17}, ASCII + EXTRAS),
    "title":    ("baloo-2-700.woff",   {"compact": 19, "regular": 24}, ASCII + EXTRAS),
    "headline": ("baloo-2-700.woff",   {"compact": 26, "regular": 34}, ASCII + EXTRAS),
    "display":  ("baloo-2-700.woff",   {"compact": 64, "regular": 88}, CLOCK),
    # The bedside clock's numerals: as large as "88:88" plus AM/PM still fits
    # the display width (see ClockApp::BedsideScreen).
    "bedside":  ("baloo-2-700.woff",   {"compact": 96, "regular": 144}, " 0123456789:"),
    # The time in Home's header: clearly bigger than a headline, far smaller than
    # the display numerals. Digits, colon, AM/PM and the "--:--" of an unknown time.
    "hometime": ("baloo-2-700.woff",   {"compact": 44, "regular": 60}, CLOCK),
}
DENSITIES = ("compact", "regular")


def glyph_signature(font, ch):
    """Bounding box plus pixels: identical to .notdef means the glyph is missing."""
    image = Image.new("L", (256, 256), 0)
    ImageDraw.Draw(image).text((64, 160), ch, font=font, fill=255, anchor="ls")
    return font.getbbox(ch, anchor="ls"), image.tobytes()


def has_glyph(font, ch, notdef):
    return ch == " " or glyph_signature(font, ch) != notdef


def rasterise(font, ch):
    left, top, right, bottom = font.getbbox(ch, anchor="ls")
    width, height = max(0, right - left), max(0, bottom - top)
    advance = int(round(font.getlength(ch)))
    if width == 0 or height == 0:
        return dict(w=0, h=0, left=0, top=0, adv=advance, px=[])
    image = Image.new("L", (width, height), 0)
    ImageDraw.Draw(image).text((-left, -top), ch, font=font, fill=255, anchor="ls")
    px = [(v * 15 + 127) // 255 for v in image.tobytes()]
    return dict(w=width, h=height, left=left, top=-top, adv=advance, px=px)


def build(role, density):
    file, sizes, chars = ROLES[role]
    font = ImageFont.truetype(str(FONTS / file), sizes[density])
    notdef = glyph_signature(font, "\uffff")
    chars = sorted({c for c in chars if has_glyph(font, c, notdef)} | {"?"}, key=ord)
    glyphs, bitmap = [], bytearray()
    for ch in chars:
        g = rasterise(font, ch)
        g["offset"] = len(bitmap)
        px = g["px"] + ([0] if len(g["px"]) % 2 else [])
        bitmap.extend((px[i] << 4) | px[i + 1] for i in range(0, len(px), 2))
        g["cp"] = ord(ch)
        glyphs.append(g)
    letters = [g for g in glyphs if chr(g["cp"]).isalnum() or g["cp"] < 128]
    ascent = max(g["top"] for g in letters)
    descent = max(g["h"] - g["top"] for g in letters)
    line = int(round((ascent + descent) * 1.25))
    name = f"k{role.capitalize()}{density.capitalize()}"
    fallback = next(i for i, g in enumerate(glyphs) if g["cp"] == ord("?"))
    out = [f"// {role} / {density}: {font.getname()[0]} {sizes[density]}px, {len(glyphs)} glyphs, "
           f"{len(bitmap)} bitmap bytes"]
    out.append(f"static const uint8_t {name}Bits[] = {{")
    for i in range(0, len(bitmap), 24):
        out.append("  " + ",".join(f"0x{b:02x}" for b in bitmap[i:i + 24]) + ",")
    out.append("};")
    out.append(f"static const Glyph {name}Glyphs[] = {{")
    for g in glyphs:
        out.append(f"  {{{g['offset']}, {g['w']}, {g['h']}, {g['left']}, {g['top']}, {g['adv']}}},")
    out.append("};")
    out.append(f"static const uint16_t {name}Codepoints[] = {{"
               + ",".join(str(g["cp"]) for g in glyphs) + "};")
    out.append(f"const Font {name} = {{\"{role}-{density}\", {name}Bits, {name}Glyphs, {name}Codepoints, "
               f"{len(glyphs)}, {fallback}, {ascent}, {descent}, {line}}};")
    return name, "\n".join(out), len(bitmap) + 8 * len(glyphs) + 2 * len(glyphs)


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    total = 0
    for density in DENSITIES:
        parts, names = [], []
        for role in ROLES:
            name, code, size = build(role, density)
            parts.append(code)
            names.append((role, name))
            total += size
        body = "\n\n".join(parts)
        roles = ", ".join(f"&{n}" for _, n in names)
        OUT.joinpath(f"fonts_{density}.cpp").write_text(
            "// GENERATED by tools/fontgen.py from the Celestia Glow fonts (SIL OFL 1.1).\n"
            "// Do not edit by hand; change tools/fontgen.py and regenerate.\n"
            '#include "fonts.h"\n\nnamespace stella {\nnamespace fonts {\n\n'
            f"{body}\n\nconst FontSet k{density.capitalize()} = {{{roles}}};\n\n"
            "}  // namespace fonts\n}  // namespace stella\n")
    print(f"wrote {OUT.relative_to(ROOT)}/fonts_*.cpp ({total} bytes of font data)")


if __name__ == "__main__":
    main()
