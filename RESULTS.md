# Results

These measurements were taken on 2026-10-09 on four AWS `m8g.4xlarge` instances (Graviton4, Arm
Neoverse V2) in `ap-south-2a`. The [README](README.md) describes the three lookups and explains the
differences. This page holds the complete numbers and the method.

## Setup

| | |
| --- | --- |
| Instances | 4 x `m8g.4xlarge`: 16 Neoverse V2 cores, 64 GiB, Ubuntu 26.04, Linux 7.0.0-1008-aws |
| Caches | 64 KiB L1 data and 2 MiB L2 per core, 36 MiB L3 shared by the whole processor |
| Compiler | Clang 22.1.8, `-O3`, `-fno-vectorize -fno-slp-vectorize`, NEON through xsimd 14.3.0 |
| Harness | Google Benchmark 1.9.4, one thread pinned to CPU 8 |
| Binary | One `oa_bench` build, copied unchanged to all four instances |

The cases were split across the instances by table row, so the three lookups in any row ran on the
same machine:

| Instance | Cases |
| --- | --- |
| 1 | 2^28 keys, 0% hits; counters at 2^24 keys |
| 2 | 2^28 keys, 50% hits; probe statistics |
| 3 | 2^28 keys, 100% hits; counters at 2^28 keys |
| 4 | All other table sizes |

An `m8g.4xlarge` uses part of a larger processor. Other tenants can share its L3 cache and its
memory bandwidth. The variation report below shows how much the five runs of each case differ.

## Workload

- Keys are the numbers 1 to N, passed through the splitmix64 finalizer. The finalizer is a
  bijection, so the keys are distinct and nonzero. Absent keys are N + 1 to 2N, passed through the
  same function.
- The capacity is `2 * bit_ceil(N)`, so the load factor is exactly 0.5. Each table holds `32 * N`
  bytes.
- The query trace holds N keys, shuffled with a fixed seed. Either 0%, 50%, or 100% of them are
  present.
- One timed pass looks up every query and sums the values found. The benchmark checks the hit count
  and the value checksum after timing. Building the table and shuffling the trace are not timed.
- Google Benchmark counts each lookup as one iteration. Each case runs for at least 0.1 s after a
  0.05 s warmup. It runs five times, and the order of all runs is randomly interleaved.

## Lookup time

Median nanoseconds per lookup over five runs. Bold marks the lowest median in each row.

| Keys | Table | Hits | Linear probing | Cyclic scalar | SIMD |
| --- | --- | ---: | ---: | ---: | ---: |
| 2^10 | 32 KiB | 0% | 1.91 | 2.38 | **1.61** |
| 2^10 | 32 KiB | 50% | **1.71** | 2.08 | 1.85 |
| 2^10 | 32 KiB | 100% | **1.58** | 1.80 | 1.99 |
| 2^12 | 128 KiB | 0% | 2.17 | 2.60 | **1.75** |
| 2^12 | 128 KiB | 50% | **1.89** | 2.23 | 1.91 |
| 2^12 | 128 KiB | 100% | **1.68** | 1.89 | 2.10 |
| 2^14 | 512 KiB | 0% | 11.17 | 9.68 | **2.25** |
| 2^14 | 512 KiB | 50% | 6.82 | 9.52 | **2.12** |
| 2^14 | 512 KiB | 100% | 2.48 | **2.41** | 2.45 |
| 2^16 | 2 MiB | 0% | 13.38 | 12.18 | **4.81** |
| 2^16 | 2 MiB | 50% | 13.40 | 12.16 | **9.71** |
| 2^16 | 2 MiB | 100% | 5.73 | 5.53 | **3.39** |
| 2^18 | 8 MiB | 0% | 16.68 | 14.39 | **6.11** |
| 2^18 | 8 MiB | 50% | 17.41 | 14.51 | **11.36** |
| 2^18 | 8 MiB | 100% | 7.58 | 7.32 | **4.78** |
| 2^20 | 32 MiB | 0% | 21.39 | 17.87 | **9.69** |
| 2^20 | 32 MiB | 50% | 22.84 | 17.60 | **13.65** |
| 2^20 | 32 MiB | 100% | 10.40 | 10.26 | **9.55** |
| 2^22 | 128 MiB | 0% | 28.59 | 23.51 | **13.30** |
| 2^22 | 128 MiB | 50% | 33.43 | 23.29 | **17.63** |
| 2^22 | 128 MiB | 100% | 14.42 | 14.68 | **14.00** |
| 2^24 | 512 MiB | 0% | 33.99 | 25.48 | **14.06** |
| 2^24 | 512 MiB | 50% | 38.26 | 24.89 | **18.56** |
| 2^24 | 512 MiB | 100% | 15.19 | 15.55 | **14.84** |
| 2^26 | 2 GiB | 0% | 35.76 | 26.15 | **14.21** |
| 2^26 | 2 GiB | 50% | 39.93 | 25.32 | **18.83** |
| 2^26 | 2 GiB | 100% | 15.26 | 15.77 | **14.99** |
| 2^28 | 8 GiB | 0% | 36.53 | 26.87 | **14.57** |
| 2^28 | 8 GiB | 50% | 40.82 | 26.39 | **19.36** |
| 2^28 | 8 GiB | 100% | 15.74 | 16.08 | **15.30** |

Run-to-run variation is the coefficient of variation over the five runs of a case. Its maximum is
6.83%. Four cases exceed 5%, all at 512 KiB or 32 MiB:

| Lookup | Table | Hits | Variation |
| --- | --- | ---: | ---: |
| SIMD | 512 KiB | 0% | 6.83% |
| Linear probing | 512 KiB | 100% | 6.65% |
| SIMD | 32 MiB | 100% | 6.45% |
| SIMD | 32 MiB | 0% | 5.46% |

From 2 MiB up, the fastest lookup's range of runs overlaps another range in one row only. That row
is all hits at 32 MiB, where the three ranges overlap. The three ranges also overlap at 512 KiB with
all hits.

## Small tables and the branch predictor

At 2^10 and 2^12 keys, the query trace has only 1,024 or 4,096 entries, and it repeats on every
pass. The branch predictor learns that sequence:

| Linear-probing misses | 32 KiB | 128 KiB | 512 KiB | 2 MiB |
| --- | ---: | ---: | ---: | ---: |
| Branch misses per lookup | 0.001 | 0.003 | 1.00 | 1.18 |

These counts come from one `OA_PMU=1` run on an `m8g.24xlarge` development machine. They depend on
the code and the query order, not on other load. Both tables fit in the 2 MiB L2, yet
linear-probing misses go from 2.17 ns at 128 KiB to 11.17 ns at 512 KiB. These counts explain the
jump. The two smallest sizes are a best case for every lookup whose branches depend on the data.

## Hardware counters

These are user-space counts per lookup from a separate `OA_PMU=1` run, with one run per case.
Branch misses are Linux's generic `branch-misses` event. A counter run reads the counters once per
pass, so its times are not used.

| Keys | Hits | Lookup | Instructions | Cycles | Branch misses |
| --- | ---: | --- | ---: | ---: | ---: |
| 2^24 | 0% | Linear probing | 28.50 | 92.7 | 1.409 |
| 2^24 | 0% | Cyclic scalar | 34.15 | 70.2 | 1.311 |
| 2^24 | 0% | SIMD | 28.97 | 38.8 | 0.287 |
| 2^24 | 50% | Linear probing | 26.62 | 105.9 | 1.500 |
| 2^24 | 50% | Cyclic scalar | 30.74 | 68.3 | 1.413 |
| 2^24 | 50% | SIMD | 29.33 | 51.3 | 0.879 |
| 2^24 | 100% | Linear probing | 24.75 | 41.9 | 0.584 |
| 2^24 | 100% | Cyclic scalar | 27.34 | 42.9 | 0.544 |
| 2^24 | 100% | SIMD | 29.69 | 41.0 | 0.054 |
| 2^28 | 0% | Linear probing | 28.50 | 100.6 | 1.419 |
| 2^28 | 0% | Cyclic scalar | 34.15 | 75.0 | 1.301 |
| 2^28 | 0% | SIMD | 28.97 | 40.5 | 0.286 |
| 2^28 | 50% | Linear probing | 26.63 | 111.5 | 1.498 |
| 2^28 | 50% | Cyclic scalar | 30.75 | 72.2 | 1.396 |
| 2^28 | 50% | SIMD | 29.33 | 53.2 | 0.865 |
| 2^28 | 100% | Linear probing | 24.75 | 43.7 | 0.569 |
| 2^28 | 100% | Cyclic scalar | 27.34 | 45.0 | 0.534 |
| 2^28 | 100% | SIMD | 29.69 | 43.1 | 0.055 |

## Probe statistics

`build/oa_probe_stats` replays insertion and lookup on plain key arrays and counts what each lookup
reads. A line is one 64-byte cache line. The lane 0 row describes the table that `SIMDGroupedOA`
searches. Its key compares are those of a scalar scan from lane 0, which is one of the earlier
designs below.

| Probe order | Hit: key compares | Hit: lines | Hit: 2+ lines | Hit: first compare matches | Miss: key compares | Miss: lines | Miss: 2+ lines |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Linear (ScalarOA) | 1.5000 | 1.1250 | 11.08% | 75.00% | 2.4999 | 1.3750 | 28.43% |
| Grouped, lane 0 first (layout of SIMDGroupedOA) | 2.0994 | 1.0497 | 4.08% | 42.98% | 4.9134 | 1.2284 | 16.33% |
| Grouped, lane hash & 3 first (ScalarCyclicGroupedOA) | 1.5177 | 1.0497 | 4.08% | 75.00% | 2.5586 | 1.2284 | 16.33% |

Full groups: 16.33% of all groups.

Share of lookups by key compares before the loop exits:

| Lookups | 1 | 2 | 3 | 4 | 5+ |
| --- | ---: | ---: | ---: | ---: | ---: |
| Linear hits | 75.00% | 14.35% | 5.15% | 2.36% | 3.14% |
| Linear misses | 50.00% | 19.67% | 10.48% | 6.29% | 13.56% |
| Cyclic hits | 75.00% | 14.25% | 4.85% | 1.82% | 4.08% |
| Cyclic misses | 50.01% | 19.42% | 9.62% | 4.62% | 16.33% |

Before counting, `--verify` built the real tables at 2^20 keys and checked that the replay put every
key in the same slot.

## Instructions on the hot path

These counts come from the disassembly, and they include the hash and the benchmark loop:

| Path | `SIMDGroupedOA` | Earlier SIMD lookup with per-lane masks |
| --- | ---: | ---: |
| Hit in the home group | 29 | 34 |
| Miss that ends in the home group | 26 | 39 |
| Each further full group | 13 | 27 |

`SIMDGroupedOA` narrows both compare results with `uzp1` and `xtn`. It moves them to a general
register once and branches once. A hit then needs `clz`, `sub`, and `ldr`, plus one `add` that
recomputes the group address. A miss tests `keys[3]` with one scalar load.

The earlier lookup moved four 64-bit lanes to general registers and rebuilt a 4-bit mask with
`extr`, `and`, and `bfi`. That took eight instructions before the branch. It then did the same with
two more compares to look for an empty lane.

## Earlier designs

### Scalar scan from lane 0

This scalar lookup searched the table of `SIMDGroupedOA`. It compared lanes 0 to 3 in order and
then tested for an empty lane. Its first compare found 43% of hits, and a hit compared 2.10 keys.
In the 8 GiB table it ran 34.68 instructions and 98.4 cycles per miss, with 1.36 branch misses. Per
hit it ran 27.21 instructions and 57.1 cycles, with 1.05 branch misses.

### SIMD with per-lane masks

This SIMD lookup used the code described under the hot path above. In the 8 GiB table it ran 45.17
instructions and 79.5 cycles per miss, with 0.25 branch misses. Per hit it ran 35.34 instructions
and 63.6 cycles, with 0.05 branch misses.

Both designs were measured with the same setup as the tables above. Median nanoseconds per lookup:

| Keys | Table | Lane 0 scan: misses | Half hits | Hits | Per-lane masks: misses | Half hits | Hits |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 2^10 | 32 KiB | 2.51 | 2.07 | 1.65 | 4.59 | 3.89 | 3.25 |
| 2^12 | 128 KiB | 2.83 | 2.26 | 1.74 | 4.99 | 4.16 | 3.43 |
| 2^14 | 512 KiB | 6.16 | 8.62 | 7.91 | 5.26 | 4.56 | 3.79 |
| 2^16 | 2 MiB | 12.99 | 13.67 | 10.50 | 7.78 | 12.15 | 4.88 |
| 2^18 | 8 MiB | 15.95 | 15.80 | 11.96 | 10.85 | 14.63 | 6.90 |
| 2^20 | 32 MiB | 20.38 | 19.48 | 14.49 | 18.95 | 20.88 | 13.90 |
| 2^22 | 128 MiB | 30.29 | 26.51 | 18.41 | 26.33 | 27.49 | 20.22 |
| 2^24 | 512 MiB | 33.21 | 28.67 | 19.80 | 27.38 | 29.29 | 21.75 |
| 2^26 | 2 GiB | 34.45 | 29.31 | 20.10 | 27.79 | 29.97 | 22.13 |
| 2^28 | 8 GiB | 35.31 | 30.55 | 20.51 | 28.46 | 31.10 | 22.69 |

### Tuning experiments

These experiments ran on a shared `m8g.24xlarge` development machine, so their times cannot be
compared with the tables above. The full log is in the git history of this file.

- Cyclic start lane: starting the scalar scan at `hash & 3` raised the share of hits found by the
  first compare from 43% to 75%. It cut compares per hit from 2.10 to 1.52.
- One loop for the cyclic scan: the lookup returns from a single loop on the first match or the first
  empty lane. This replaced a version with a separate test for the first lane. At 2^24 keys, hit time
  fell from 18.8 ns to 16.3 ns.
- Cheaper group address: keeping the probe position as a cell index lets Clang address a group with
  one `add ..., lsl #4`, instead of `lsr` plus `add ..., lsl #6`. In a paired test at 2^28 keys this
  saved about 1 ns per lookup. In the same test, one extra independent `and` on the hit path of
  linear probing cost 0.42 ns, and three cost 2.03 ns.
- Two SIMD variants that were slower: indexing the values with `3 - countl_zero / 16` cost 33
  instructions per hit, because Clang added a sign extension and kept a null check. Computing the
  value offset with xsimd `select` and `reduce_add` needed two 128-bit constants. AArch64 calls do not preserve
  128-bit vector registers, so the constants were reloaded from the stack on every probe.

## Reproduce

Build as described in the README. Then run the full matrix on one machine:

```sh
taskset -c 8 build/oa_bench \
    --benchmark_min_time=0.1s --benchmark_min_warmup_time=0.05 \
    --benchmark_repetitions=5 --benchmark_enable_random_interleaving=true \
    --benchmark_display_aggregates_only=true \
    --benchmark_out=sweep.json --benchmark_out_format=json
sudo env OA_PMU=1 taskset -c 8 build/oa_bench --benchmark_filter='/keys:(16777216|268435456)/' \
    --benchmark_min_time=0.1s --benchmark_out=counters.json --benchmark_out_format=json
build/oa_probe_stats --verify 20
build/oa_probe_stats 28
python3 tools/summarize.py sweep.json --counters counters.json
python3 tools/plot.py docs/lookup-time.svg sweep.json
```

To split the work across machines, give each machine a `--benchmark_filter` that selects whole
rows, such as `'/keys:268435456/hit_percent:0/'`. Then pass all the JSON files to both scripts.
