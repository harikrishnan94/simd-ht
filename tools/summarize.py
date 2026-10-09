"""Print Markdown tables from oa_bench JSON output.

    tools/summarize.py sweep.json [more.json ...] [--counters counters.json ...]

Several files may hold disjoint cases, for example when sizes ran on different machines.
The sweep table shows the median time per lookup over all repetitions of each case.
The counter table shows per-lookup hardware counts from OA_PMU=1 runs.
"""

import argparse
import json
import re
import statistics
from pathlib import Path

KERNELS = (
    ("ScalarOA", "Linear probing"),
    ("ScalarCyclicGroupedOA", "Grouped, cyclic scalar"),
    ("SIMDGroupedOA", "Grouped, SIMD"),
)
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


def sweep_table(rows):
    sizes = sorted({keys for _, keys, _ in rows})
    lines = [
        "| Keys | Table | Hits | " + " | ".join(label for _, label in KERNELS) + " |",
        "| --- | --- | ---: |" + " ---: |" * len(KERNELS),
    ]
    for keys in sizes:
        for hits in HIT_RATES:
            times = {kernel: [r["real_time"] for r in rows[(kernel, keys, hits)]] for kernel, _ in KERNELS}
            medians = {kernel: statistics.median(t) for kernel, t in times.items()}
            best = min(medians.values())
            cells = [f"2^{keys.bit_length() - 1}", table_size(keys), f"{hits}%"]
            for kernel, _ in KERNELS:
                text = f"{medians[kernel]:.2f}"
                cells.append(f"**{text}**" if medians[kernel] == best else text)
            lines.append("| " + " | ".join(cells) + " |")
    return lines


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
            for kernel, label in KERNELS:
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
    print()
    print("\n".join(spread_report(rows)))
    if args.counters:
        _, counter_rows = load(args.counters)
        print()
        print("\n".join(counter_table(counter_rows)))


if __name__ == "__main__":
    main()
