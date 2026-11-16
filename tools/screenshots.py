#!/usr/bin/env python3
"""Render every built-in screen with the production renderer and publish PNGs.

    python3 tools/screenshots.py            # regenerate docs/screenshots/
    python3 tools/screenshots.py --check    # CI: fail if the UI changed unexpectedly

Requires a host build (cmake -S . -B build && cmake --build build) and Pillow.
The check tolerates tiny anti-aliasing differences between compilers/libm
versions but fails on any visible change: regenerate and review the PNGs when
a UI change is intended, and commit them with the change.
"""
import argparse
import subprocess
import sys
import tempfile
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "docs" / "screenshots"
SIM = ROOT / "build" / "stella_sim"

# A pixel "differs" if any channel moves by more than this; the image differs
# if more than this fraction of pixels do.
CHANNEL_TOLERANCE = 12
PIXEL_FRACTION = 0.002


def render(target: Path):
    if not SIM.exists():
        sys.exit(f"{SIM.relative_to(ROOT)} not found: run cmake -S . -B build && cmake --build build")
    subprocess.run([str(SIM), str(target)], check=True, stdout=subprocess.DEVNULL)
    return sorted(target.glob("*/*.ppm"))


def overview(images, path: Path):
    """Contact sheet of the compact (CYD) screens."""
    compact = [p for p in images if p.parent.name == "320x240"]
    cols, pad, label = 4, 10, 16
    w, h = 320, 240
    rows = (len(compact) + cols - 1) // cols
    sheet = Image.new("RGB", (cols * (w + pad) + pad, rows * (h + pad + label) + pad), (24, 20, 34))
    draw = ImageDraw.Draw(sheet)
    for i, p in enumerate(compact):
        x, y = pad + (i % cols) * (w + pad), pad + (i // cols) * (h + pad + label)
        sheet.paste(Image.open(p).convert("RGB"), (x, y + label))
        draw.text((x, y), p.stem, fill=(220, 210, 235))
    sheet.save(path, optimize=True)


def differs(a: Path, b: Path) -> str:
    if not b.exists():
        return "missing from docs/screenshots"
    ia, ib = Image.open(a).convert("RGB"), Image.open(b).convert("RGB")
    if ia.size != ib.size:
        return f"size {ia.size} != {ib.size}"
    diff = ImageChops.difference(ia, ib).point(lambda v: 255 if v > CHANNEL_TOLERANCE else 0).convert("L")
    count = ia.size[0] * ia.size[1] - diff.histogram()[0]  # pixels above tolerance
    if count > PIXEL_FRACTION * ia.size[0] * ia.size[1]:
        return f"{count} pixels differ"
    return ""


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true", help="compare with committed screenshots")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory() as tmp:
        ppms = render(Path(tmp))
        if args.check:
            problems = []
            expected = set()
            for p in ppms:
                rel = Path(p.parent.name) / (p.stem + ".png")
                expected.add(rel)
                reason = differs(p, OUT / rel)
                if reason:
                    problems.append(f"{rel}: {reason}")
            stale = {p.relative_to(OUT) for p in OUT.glob("*/*.png")} - expected
            problems += [f"{p}: no longer produced" for p in sorted(stale)]
            if problems:
                print("Screenshots differ from docs/screenshots (run tools/screenshots.py and review):")
                print("\n".join("  " + p for p in problems))
                sys.exit(1)
            print(f"{len(ppms)} screenshots match docs/screenshots")
            return
        for old in OUT.glob("*/*.png"):
            old.unlink()
        for p in ppms:
            dest = OUT / p.parent.name / (p.stem + ".png")
            dest.parent.mkdir(parents=True, exist_ok=True)
            Image.open(p).save(dest, optimize=True)
        overview(ppms, OUT / "overview.png")
        print(f"wrote {len(ppms)} screenshots and overview.png to {OUT.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
