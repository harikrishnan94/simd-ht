"""Print Markdown tables from oa_bench JSON output.

    tools/summarize.py sweep.json [more.json ...] [--counters counters.json ...]

Several files may hold disjoint cases, for example when sizes ran on different machines.
The sweep table shows the median time per lookup over all repetitions of each case.
The ring table shows the AMAC ring sizes, when the files hold that sweep.
The counter table shows per-lookup hardware counts from OA_PMU=1 runs.
Kernels that a file does not hold are left out.
"""

import argparse
import json
import re
import statistics
from pathlib import Path

DESIGNS = (
    ("ScalarOA", "Linear probing"),
    ("ScalarCyclicGroupedOA", "Cyclic scalar"),
    ("SIMDGroupedOA", "SIMD"),
)
AMAC_RING = 32
KERNELS = DESIGNS + tuple((f"{name}_AMAC{AMAC_RING}", f"{label}, AMAC") for name, label in DESIGNS)
RING_SIZES = (8, 16, 32, 64)
RING_KEYS = (1 << 24, 1 << 28)
HIT_RATES = (0, 50, 100)
NAME = re.compile(r"(\w+)/keys:(\d+)/hit_percent:(\d+)/real_time")


def load(paths):
    rows = {}
    context = None
    for path in paths:
        data = json.loads(Path(path).read_text())
        context = data["context"]
        cases = {}
        for row in data["benchmarks"]:
            if row.get("error_occurred") or row.get("skipped"):
                raise ValueError(f"{row['name']}: {row.get('error_message', 'failed')}")
            if row.get("run_type") != "iteration":
                continue
            kernel, keys, hits = NAME.fullmatch(row["run_name"]).groups()
            cases.setdefault((kernel, int(keys), int(hits)), []).append(row)
        if overlap := rows.keys() & cases.keys():
            raise ValueError(f"{path} repeats cases from another file: {sorted(overlap)[:3]}")
        rows.update(cases)
    return context, rows


def table_size(keys):
    size = 32 * keys
    for unit, divisor in (("GiB", 1 << 30), ("MiB", 1 << 20), ("KiB", 1 << 10)):
        if size >= divisor:
            return f"{size // divisor} {unit}"
    return f"{size} B"


def present(rows):
    names = {kernel for kernel, _, _ in rows}
    return tuple((kernel, label) for kernel, label in KERNELS if kernel in names)


def sweep_table(rows):
    kernels = present(rows)
    sizes = sorted({keys for _, keys, _ in rows if (kernels[0][0], keys, 0) in rows})
    lines = [
        "| Keys | Table | Hits | " + " | ".join(label for _, label in kernels) + " |",
        "| --- | --- | ---: |" + " ---: |" * len(kernels),
    ]
    for keys in sizes:
        for hits in HIT_RATES:
            medians = {kernel: statistics.median(r["real_time"] for r in rows[(kernel, keys, hits)]) for kernel, _ in kernels}
            best = min(medians.values())
            cells = [f"2^{keys.bit_length() - 1}", table_size(keys), f"{hits}%"]
            for kernel, _ in kernels:
                text = f"{medians[kernel]:.2f}"
                cells.append(f"**{text}**" if medians[kernel] == best else text)
            lines.append("| " + " | ".join(cells) + " |")
    return lines


def ring_table(rows):
    columns = [("", "No ring")] + [(f"_AMAC{size}", f"Ring {size}") for size in RING_SIZES]
    lines = [
        "| Keys | Hits | Design | " + " | ".join(label for _, label in columns) + " |",
        "| --- | ---: | --- |" + " ---: |" * len(columns),
    ]
    for keys in RING_KEYS:
        for hits in HIT_RATES:
            for name, label in DESIGNS:
                cases = [(name + suffix, keys, hits) for suffix, _ in columns]
                if not all(case in rows for case in cases):
                    continue
                medians = [statistics.median(r["real_time"] for r in rows[case]) for case in cases]
                cells = [f"2^{keys.bit_length() - 1}", f"{hits}%", label]
                cells += [f"**{m:.2f}**" if m == min(medians) else f"{m:.2f}" for m in medians]
                lines.append("| " + " | ".join(cells) + " |")
    return lines if len(lines) > 2 else []


def spread_report(rows):
    def cv(times):
        return 100 * statistics.stdev(times) / statistics.mean(times) if len(times) > 1 else 0.0

    spreads = {key: cv([r["real_time"] for r in case]) for key, case in rows.items()}
    noisy = sorted((key for key, value in spreads.items() if value > 5), key=spreads.get, reverse=True)
    lines = [f"Maximum coefficient of variation: {max(spreads.values()):.2f}%."]
    for kernel, keys, hits in noisy:
        lines.append(f"- {kernel}, 2^{keys.bit_length() - 1} keys, {hits}% hits: {spreads[(kernel, keys, hits)]:.2f}%")
    return lines


def counter_table(rows):
    lines = [
        "| Keys | Hits | Kernel | Instructions | Cycles | Branch misses |",
        "| --- | ---: | --- | ---: | ---: | ---: |",
    ]
    for keys in sorted({keys for _, keys, _ in rows}):
        for hits in HIT_RATES:
            for kernel, label in present(rows):
                case = rows[(kernel, keys, hits)]
                value = lambda name: statistics.median(r[name] for r in case)
                lines.append(
                    f"| 2^{keys.bit_length() - 1} | {hits}% | {label} | {value('pmu_instructions'):.2f} "
                    f"| {value('pmu_cycles'):.1f} | {value('pmu_branch_misses'):.3f} |"
                )
    return lines


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("sweep", nargs="+")
    parser.add_argument("--counters", nargs="+")
    args = parser.parse_args()
    context, rows = load(args.sweep)
    repetitions = {len(case) for case in rows.values()}
    print(f"{len(rows)} cases, repetitions per case: {sorted(repetitions)}. Compiler: {context['compiler']}.")
    print(f"SIMD target: {context['simd_target']}. Median ns per lookup; bold is the lowest in its row.\n")
    print("\n".join(sweep_table(rows)))
    if ring := ring_table(rows):
        print("\nAMAC ring size, median ns per lookup.\n")
        print("\n".join(ring))
    print()
    print("\n".join(spread_report(rows)))
    if args.counters:
        _, counter_rows = load(args.counters)
        print()
        print("\n".join(counter_table(counter_rows)))


if __name__ == "__main__":
    main()
