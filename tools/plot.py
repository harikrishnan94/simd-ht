"""Draw lookup time against table size as an SVG, from oa_bench JSON output.

    tools/plot.py docs/lookup-time-arm.svg [--platform arm|x86] sweep.json [more.json ...]

Uses only the standard library. Each point is the median over all repetitions of one case.
The platform sets the chart title and the cache-size markers. Without --platform, the SIMD target
recorded in the JSON picks it: NEON64 is arm, AVX2 is x86.
"""

import argparse
import json
import math
import re
import statistics
from pathlib import Path

KERNELS = (
    ("ScalarOA", "Linear probing", "#0072B2", "", 2.5),
    ("ScalarCyclicGroupedOA", "Cyclic scalar", "#009E73", "", 2.5),
    ("SIMDGroupedOA", "SIMD", "#D55E00", "", 3.5),
    ("ScalarOA_AMAC32", "Linear probing, AMAC ring of 32", "#0072B2", "7 4", 2.5),
    ("ScalarCyclicGroupedOA_AMAC32", "Cyclic scalar, AMAC ring of 32", "#009E73", "7 4", 2.5),
    ("SIMDGroupedOA_AMAC32", "SIMD, AMAC ring of 32", "#D55E00", "7 4", 3.5),
)
PANELS = ((0, "All misses"), (50, "Half hits"), (100, "All hits"))
# Per-core L1d and L2, and the shared L3, as table bytes.
PLATFORMS = {
    "arm": ("ARM", (("L1d 64 KiB", 64 << 10), ("L2 2 MiB", 2 << 20), ("L3 36 MiB", 36 << 20))),
    "x86": ("x86", (("L1d 48 KiB", 48 << 10), ("L2 2 MiB", 2 << 20), ("L3 105 MiB", 105 << 20))),
}
SIMD_TARGETS = {"NEON64": "arm", "AVX2": "x86"}
NAME = re.compile(r"(\w+)/keys:(\d+)/hit_percent:(\d+)/real_time")

WIDTH, HEIGHT = 1200, 530
LEFT, TOP, PANEL_W, PANEL_H, GAP = 70, 104, 340, 300, 40


def load(paths):
    samples = {}
    context = {}
    for path in paths:
        data = json.loads(Path(path).read_text())
        context = data["context"]
        for row in data["benchmarks"]:
            if row.get("run_type") == "iteration":
                kernel, keys, hits = NAME.fullmatch(row["run_name"]).groups()
                samples.setdefault((kernel, int(keys), int(hits)), []).append(row["real_time"])
    repetitions = min(len(times) for times in samples.values())
    return {key: statistics.median(times) for key, times in samples.items()}, repetitions, context


def size_label(keys):
    size = 32 * keys
    for unit, divisor in (("GiB", 1 << 30), ("MiB", 1 << 20), ("KiB", 1 << 10)):
        if size >= divisor:
            return f"{size // divisor} {unit}"
    return f"{size} B"


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("output")
    parser.add_argument("--platform", choices=sorted(PLATFORMS))
    parser.add_argument("sweep", nargs="+")
    args = parser.parse_args()
    medians, repetitions, context = load(args.sweep)
    platform = args.platform or SIMD_TARGETS.get(context.get("simd_target"))
    if platform is None:
        parser.error("the JSON does not name a known SIMD target; pass --platform arm or --platform x86")
    platform_name, caches = PLATFORMS[platform]
    kernels = [kernel for kernel in KERNELS if any(name == kernel[0] for name, _, _ in medians)]
    sizes = sorted({keys for name, keys, _ in medians if name == KERNELS[0][0]})
    x_min, x_max = math.log2(sizes[0]), math.log2(sizes[-1])
    slowest = max(medians[(kernel[0], keys, hits)] for kernel in kernels for keys in sizes for hits, _ in PANELS)
    y_step = 5 if slowest <= 50 else 10
    y_max = y_step * math.ceil(slowest / y_step)

    lines = "Solid lines: one lookup at a time. " + (
        "Dashed colored lines: AMAC, a ring of 32 lookups in flight. " if any(k[0].endswith("_AMAC32") for k in kernels) else ""
    )
    note = lines + "Grey verticals: table size equals a cache size."

    out = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{WIDTH}" height="{HEIGHT}" viewBox="0 0 {WIDTH} {HEIGHT}" '
        'font-family="Helvetica, Arial, sans-serif">',
        f'<rect width="{WIDTH}" height="{HEIGHT}" fill="white"/>',
        f'<text x="{LEFT}" y="30" font-size="20" font-weight="bold" fill="#222">'
        f"Hash table lookup time on {platform_name}, load factor 0.5</text>",
        f'<text x="{LEFT}" y="52" font-size="13" fill="#555">'
        "Nanoseconds per lookup, lower is better. Independent shuffled queries on one core; "
        f"each point is the median of {repetitions} runs.</text>",
        f'<text x="{LEFT}" y="70" font-size="13" fill="#555">{note}</text>',
    ]
    for index, (hits, title) in enumerate(PANELS):
        x0 = LEFT + index * (PANEL_W + GAP)

        def px(keys):
            return x0 + (math.log2(keys) - x_min) / (x_max - x_min) * PANEL_W

        def py(ns):
            return TOP + PANEL_H - ns / y_max * PANEL_H

        out.append(f'<text x="{x0 + PANEL_W / 2}" y="{TOP - 8}" font-size="15" font-weight="bold" '
                   f'text-anchor="middle" fill="#222">{title}</text>')
        for ns in range(0, y_max + 1, y_step):
            out.append(f'<line x1="{x0}" x2="{x0 + PANEL_W}" y1="{py(ns):.1f}" y2="{py(ns):.1f}" stroke="#e5e5e5"/>')
            if index == 0:
                out.append(f'<text x="{x0 - 8}" y="{py(ns) + 4:.1f}" font-size="12" text-anchor="end" fill="#555">{ns}</text>')
        for label, table_bytes in caches:
            x = px(table_bytes / 32)
            out.append(f'<line x1="{x:.1f}" x2="{x:.1f}" y1="{TOP}" y2="{TOP + PANEL_H}" stroke="#999" stroke-dasharray="3 3"/>')
            out.append(f'<text x="{x + 3:.1f}" y="{TOP + 12}" font-size="10" fill="#777">{label}</text>')
        for keys in sizes:
            if (keys.bit_length() - 1) % 4 == 0:
                x = px(keys)
                out.append(f'<text x="{x:.1f}" y="{TOP + PANEL_H + 17}" font-size="11" text-anchor="middle" '
                           f'fill="#555">{size_label(keys)}</text>')
        out.append(f'<rect x="{x0}" y="{TOP}" width="{PANEL_W}" height="{PANEL_H}" fill="none" stroke="#bbb"/>')
        for kernel, _, color, dash, width in kernels:
            points = " ".join(f"{px(keys):.1f},{py(medians[(kernel, keys, hits)]):.1f}" for keys in sizes)
            dash_attr = f' stroke-dasharray="{dash}"' if dash else ""
            out.append(f'<polyline points="{points}" fill="none" stroke="{color}" stroke-width="{width}"{dash_attr} '
                       'stroke-linejoin="round"/>')
    out.append(f'<text x="{LEFT + 1.5 * PANEL_W + GAP}" y="{TOP + PANEL_H + 38}" font-size="12" '
               'text-anchor="middle" fill="#555">Table size (32 bytes per key; 2^10 to 2^28 keys)</text>')
    for index, (_, label, color, dash, width) in enumerate(kernels):
        x = LEFT + (index % 3) * 330
        y = HEIGHT - 55 + (index // 3) * 22
        dash_attr = f' stroke-dasharray="{dash}"' if dash else ""
        out.append(f'<line x1="{x}" x2="{x + 36}" y1="{y}" y2="{y}" stroke="{color}" '
                   f'stroke-width="{width}"{dash_attr}/>')
        out.append(f'<text x="{x + 42}" y="{y + 4}" font-size="12" fill="#222">{label}</text>')
    out.append("</svg>")
    Path(args.output).parent.mkdir(parents=True, exist_ok=True)
    Path(args.output).write_text("\n".join(out) + "\n")


if __name__ == "__main__":
    main()
