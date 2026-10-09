"""Draw lookup time against table size as an SVG, from oa_bench JSON output.

    tools/plot.py docs/lookup-time.svg sweep.json [more.json ...]

Uses only the standard library. Each point is the median over all repetitions of one case.
"""

import argparse
import json
import math
import re
import statistics
from pathlib import Path

KERNELS = (
    ("ScalarOA", "Linear probing", "#0072B2", "", 2.5),
    ("ScalarCyclicGroupedOA", "Grouped, cyclic scalar", "#009E73", "", 2.5),
    ("SIMDGroupedOA", "Grouped, SIMD", "#D55E00", "", 3.5),
)
PANELS = ((0, "All misses"), (50, "Half hits"), (100, "All hits"))
# Neoverse V2 per-core L1D and L2, and the host's shared L3, as table bytes.
CACHES = (("L1 64 KiB", 64 << 10), ("L2 2 MiB", 2 << 20), ("L3 36 MiB", 36 << 20))
NAME = re.compile(r"(\w+)/keys:(\d+)/hit_percent:(\d+)/real_time")

WIDTH, HEIGHT = 1200, 490
LEFT, TOP, PANEL_W, PANEL_H, GAP = 70, 92, 340, 300, 40


def load(paths):
    samples = {}
    for path in paths:
        for row in json.loads(Path(path).read_text())["benchmarks"]:
            if row.get("run_type") == "iteration":
                kernel, keys, hits = NAME.fullmatch(row["run_name"]).groups()
                samples.setdefault((kernel, int(keys), int(hits)), []).append(row["real_time"])
    repetitions = min(len(times) for times in samples.values())
    return {key: statistics.median(times) for key, times in samples.items()}, repetitions


def size_label(keys):
    size = 32 * keys
    for unit, divisor in (("GiB", 1 << 30), ("MiB", 1 << 20), ("KiB", 1 << 10)):
        if size >= divisor:
            return f"{size // divisor} {unit}"
    return f"{size} B"


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("output")
    parser.add_argument("sweep", nargs="+")
    args = parser.parse_args()
    medians, repetitions = load(args.sweep)
    sizes = sorted({keys for _, keys, _ in medians})
    x_min, x_max = math.log2(sizes[0]), math.log2(sizes[-1])
    y_max = 5 * math.ceil(max(medians.values()) / 5)

    out = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{WIDTH}" height="{HEIGHT}" viewBox="0 0 {WIDTH} {HEIGHT}" '
        'font-family="Helvetica, Arial, sans-serif">',
        f'<rect width="{WIDTH}" height="{HEIGHT}" fill="white"/>',
        f'<text x="{LEFT}" y="30" font-size="20" font-weight="bold" fill="#222">'
        "Hash table lookup time on Neoverse V2, load factor 0.5</text>",
        f'<text x="{LEFT}" y="52" font-size="13" fill="#555">'
        "Nanoseconds per lookup, lower is better. Independent shuffled queries on one core; "
        f"each point is the median of {repetitions} runs.</text>",
    ]
    for index, (hits, title) in enumerate(PANELS):
        x0 = LEFT + index * (PANEL_W + GAP)

        def px(keys):
            return x0 + (math.log2(keys) - x_min) / (x_max - x_min) * PANEL_W

        def py(ns):
            return TOP + PANEL_H - ns / y_max * PANEL_H

        out.append(f'<text x="{x0 + PANEL_W / 2}" y="{TOP - 8}" font-size="15" font-weight="bold" '
                   f'text-anchor="middle" fill="#222">{title}</text>')
        for ns in range(0, int(y_max) + 1, 5):
            out.append(f'<line x1="{x0}" x2="{x0 + PANEL_W}" y1="{py(ns):.1f}" y2="{py(ns):.1f}" stroke="#e5e5e5"/>')
            if index == 0:
                out.append(f'<text x="{x0 - 8}" y="{py(ns) + 4:.1f}" font-size="12" text-anchor="end" fill="#555">{ns}</text>')
        for label, table_bytes in CACHES:
            x = px(table_bytes / 32)
            out.append(f'<line x1="{x:.1f}" x2="{x:.1f}" y1="{TOP}" y2="{TOP + PANEL_H}" stroke="#999" stroke-dasharray="3 3"/>')
            out.append(f'<text x="{x + 3:.1f}" y="{TOP + 12}" font-size="10" fill="#777">{label}</text>')
        for keys in sizes:
            if (keys.bit_length() - 1) % 4 == 0:
                x = px(keys)
                out.append(f'<text x="{x:.1f}" y="{TOP + PANEL_H + 17}" font-size="11" text-anchor="middle" '
                           f'fill="#555">{size_label(keys)}</text>')
        out.append(f'<rect x="{x0}" y="{TOP}" width="{PANEL_W}" height="{PANEL_H}" fill="none" stroke="#bbb"/>')
        for kernel, _, color, dash, width in KERNELS:
            points = " ".join(f"{px(keys):.1f},{py(medians[(kernel, keys, hits)]):.1f}" for keys in sizes)
            dash_attr = f' stroke-dasharray="{dash}"' if dash else ""
            out.append(f'<polyline points="{points}" fill="none" stroke="{color}" stroke-width="{width}"{dash_attr} '
                       'stroke-linejoin="round"/>')
    out.append(f'<text x="{LEFT + 1.5 * PANEL_W + GAP}" y="{TOP + PANEL_H + 38}" font-size="12" '
               'text-anchor="middle" fill="#555">Table size (32 bytes per key; 2^10 to 2^28 keys)</text>')
    legend_x, legend_y = LEFT, HEIGHT - 32
    for index, (_, label, color, dash, width) in enumerate(KERNELS):
        x = legend_x + index * 220
        dash_attr = f' stroke-dasharray="{dash}"' if dash else ""
        out.append(f'<line x1="{x}" x2="{x + 28}" y1="{legend_y}" y2="{legend_y}" stroke="{color}" '
                   f'stroke-width="{width}"{dash_attr}/>')
        out.append(f'<text x="{x + 34}" y="{legend_y + 4}" font-size="12" fill="#222">{label}</text>')
    out.append("</svg>")
    Path(args.output).parent.mkdir(parents=True, exist_ok=True)
    Path(args.output).write_text("\n".join(out) + "\n")


if __name__ == "__main__":
    main()
