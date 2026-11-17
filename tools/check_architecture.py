#!/usr/bin/env python3
"""Architecture guard: enforce Stella's layering and portability rules.

    python3 tools/check_architecture.py

Rules (see docs/ARCHITECTURE.md):
  1. A module under src/stella/<layer>/ may only include headers from the
     layers listed in ALLOWED[<layer>] (its own layer is always allowed).
  2. Only port/, boards/ and osal/osal_freertos.cpp may include platform
     headers (Arduino, ESP-IDF, FreeRTOS, TFT_eSPI, ...).
  3. The portable code never allocates on the heap at runtime: no
     malloc/new/std containers outside host/ and tests/ (placement new in the
     OSAL is the one exception).
Exit status 1 on any violation.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src" / "stella"

ALLOWED = {
    "core": set(),
    "osal": {"core"},
    "hal": {"core"},
    "gfx": {"core", "hal"},
    "fonts": {"gfx"},
    "input": {"core", "hal"},
    "storage": {"core", "hal"},
    "service": {"core", "osal"},
    "net": {"core", "osal", "hal", "storage", "service"},
    "ui": {"core", "gfx", "fonts", "input"},
    "app": {"core", "hal", "gfx", "fonts", "input", "ui", "storage", "service", "net"},
    "runtime": {"core", "osal", "hal", "gfx", "fonts", "input", "ui", "storage", "service", "net", "app"},
    "apps": {"core", "gfx", "fonts", "input", "ui", "app", "service", "net"},
    "port": {"core", "osal", "hal", "storage", "service", "net", "app", "runtime", "input"},
    "boards": {"core", "hal", "input", "port", "gfx"},
}
PLATFORM_ALLOWED = {"port", "boards"}
PLATFORM_HEADERS = re.compile(
    r'#include\s*[<"](Arduino\.h|esp_|freertos/|driver/|nvs|WiFi|Preferences\.h|TFT_eSPI|XPT2046|SPI\.h|soc/|rom/)')
HEAP = re.compile(r'\b(malloc|calloc|realloc|free)\s*\(|\bnew\s+(?!\()[A-Za-z_]|std::(vector|string|map|list|deque|'
                  r'unordered_map|function|shared_ptr|unique_ptr)\b')
INCLUDE = re.compile(r'#include\s*"([^"]+)"')


def layer_of(path: Path) -> str:
    return path.relative_to(SRC).parts[0]


def resolve(path: Path, include: str):
    target = (path.parent / include).resolve()
    try:
        return target.relative_to(SRC.resolve()).parts[0]
    except ValueError:
        return None  # outside src/stella (e.g. generated board headers)


def strip_comments(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


def main():
    problems = []
    if not SRC.is_dir():
        sys.exit("src/stella not found")
    for path in sorted(SRC.rglob("*")):
        if path.suffix not in (".h", ".cpp"):
            continue
        layer = layer_of(path)
        rel = path.relative_to(ROOT)
        if layer not in ALLOWED:
            problems.append(f"{rel}: unknown layer '{layer}' (add it to tools/check_architecture.py and the docs)")
            continue
        raw = path.read_text(encoding="utf-8")
        code = strip_comments(raw)
        for include in INCLUDE.findall(code):
            target = resolve(path, include)
            if target and target != layer and target not in ALLOWED[layer]:
                problems.append(f"{rel}: layer '{layer}' must not include '{include}' (layer '{target}')")
        platform_ok = layer in PLATFORM_ALLOWED or path.name == "osal_freertos.cpp"
        if not platform_ok and PLATFORM_HEADERS.search(code):
            problems.append(f"{rel}: platform header in portable code")
        if layer not in PLATFORM_ALLOWED:
            for m in HEAP.finditer(code):
                problems.append(f"{rel}: heap use '{m.group(0).strip()}' in portable code")
    if problems:
        print("Architecture violations:")
        print("\n".join("  " + p for p in problems))
        sys.exit(1)
    print("architecture rules hold for", sum(1 for _ in SRC.rglob("*.[ch]*")), "files")


if __name__ == "__main__":
    main()
