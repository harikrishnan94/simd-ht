# Results

Measured on 2026-10-09 on four AWS `m8g.4xlarge` instances (Graviton4, Arm Neoverse V2) in
`ap-south-2a`. The [README](README.md) explains the kernels and why they differ. This page holds the
complete numbers and the method.

## Setup

| | |
| --- | --- |
| Instances | 4 x `m8g.4xlarge`: 16 Neoverse V2 cores, 64 GiB, Ubuntu 26.04, Linux 7.0.0-1008-aws |
| Caches | 64 KiB L1 data and 2 MiB L2 per core; 36 MiB L3 shared by the whole processor |
| Compiler | Clang 22.1.8, `-O3`, `-fno-vectorize -fno-slp-vectorize`, NEON through xsimd 14.3.0 |
| Harness | Google Benchmark 1.9.4, one thread pinned to CPU 8 |
| Binary | One `oa_bench` build, copied unchanged to all four instances |

Each instance ran a disjoint share of the cases, so all five kernels in one row of a table always
ran on the same machine:

| Instance | Cases |
| --- | --- |
| 1 | 2^28 keys, 0% hits; counters at 2^24 keys |
| 2 | 2^28 keys, 50% hits; probe statistics |
| 3 | 2^28 keys, 100% hits; counters at 2^28 keys |
| 4 | All other table sizes |

An `m8g.4xlarge` is a slice of a larger processor, so other tenants can share its L3 cache and
memory bandwidth. Five interleaved repetitions per case and the variation report below show how
much that mattered.

## Workload

- Keys are distinct 64-bit values from a bijective mix of `1, 2, ..., N`. Absent keys come from a
  disjoint range of the same mix.
- Capacity is `2 * bit_ceil(N)`, so the load factor is exactly 0.5. Each table holds `32 * N` bytes.
- The query trace holds `N` keys, shuffled with a fixed seed. 0%, 50%, and 100% of them are present.
- One timed pass looks up every query and sums the values found. Hit count and value checksum are
  checked after timing. Table construction and shuffling are not timed.
- Google Benchmark counts every lookup as one iteration. Each case runs at least 0.1 s after a 0.05 s
  warmup, five times, with the order of all repetitions randomly interleaved.

## Lookup time

Median nanoseconds per lookup over five repetitions. Bold is the lowest median in its row.

| Keys | Table | Hits | Linear probing | Grouped, scalar | Grouped, cyclic scalar | Grouped, SIMD per-lane masks | Grouped, SIMD combined mask |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 2^10 | 32 KiB | 0% | 1.91 | 2.51 | 2.38 | 4.59 | **1.61** |
| 2^10 | 32 KiB | 50% | **1.71** | 2.07 | 2.08 | 3.89 | 1.85 |
| 2^10 | 32 KiB | 100% | **1.58** | 1.65 | 1.80 | 3.25 | 1.99 |
| 2^12 | 128 KiB | 0% | 2.17 | 2.83 | 2.60 | 4.99 | **1.75** |
| 2^12 | 128 KiB | 50% | **1.89** | 2.26 | 2.23 | 4.16 | 1.91 |
| 2^12 | 128 KiB | 100% | **1.68** | 1.74 | 1.89 | 3.43 | 2.10 |
| 2^14 | 512 KiB | 0% | 11.17 | 6.16 | 9.68 | 5.26 | **2.25** |
| 2^14 | 512 KiB | 50% | 6.82 | 8.62 | 9.52 | 4.56 | **2.12** |
| 2^14 | 512 KiB | 100% | 2.48 | 7.91 | **2.41** | 3.79 | 2.45 |
| 2^16 | 2 MiB | 0% | 13.38 | 12.99 | 12.18 | 7.78 | **4.81** |
| 2^16 | 2 MiB | 50% | 13.40 | 13.67 | 12.16 | 12.15 | **9.71** |
| 2^16 | 2 MiB | 100% | 5.73 | 10.50 | 5.53 | 4.88 | **3.39** |
| 2^18 | 8 MiB | 0% | 16.68 | 15.95 | 14.39 | 10.85 | **6.11** |
| 2^18 | 8 MiB | 50% | 17.41 | 15.80 | 14.51 | 14.63 | **11.36** |
| 2^18 | 8 MiB | 100% | 7.58 | 11.96 | 7.32 | 6.90 | **4.78** |
| 2^20 | 32 MiB | 0% | 21.39 | 20.38 | 17.87 | 18.95 | **9.69** |
| 2^20 | 32 MiB | 50% | 22.84 | 19.48 | 17.60 | 20.88 | **13.65** |
| 2^20 | 32 MiB | 100% | 10.40 | 14.49 | 10.26 | 13.90 | **9.55** |
| 2^22 | 128 MiB | 0% | 28.59 | 30.29 | 23.51 | 26.33 | **13.30** |
| 2^22 | 128 MiB | 50% | 33.43 | 26.51 | 23.29 | 27.49 | **17.63** |
| 2^22 | 128 MiB | 100% | 14.42 | 18.41 | 14.68 | 20.22 | **14.00** |
| 2^24 | 512 MiB | 0% | 33.99 | 33.21 | 25.48 | 27.38 | **14.06** |
| 2^24 | 512 MiB | 50% | 38.26 | 28.67 | 24.89 | 29.29 | **18.56** |
| 2^24 | 512 MiB | 100% | 15.19 | 19.80 | 15.55 | 21.75 | **14.84** |
| 2^26 | 2 GiB | 0% | 35.76 | 34.45 | 26.15 | 27.79 | **14.21** |
| 2^26 | 2 GiB | 50% | 39.93 | 29.31 | 25.32 | 29.97 | **18.83** |
| 2^26 | 2 GiB | 100% | 15.26 | 20.10 | 15.77 | 22.13 | **14.99** |
| 2^28 | 8 GiB | 0% | 36.53 | 35.31 | 26.87 | 28.46 | **14.57** |
| 2^28 | 8 GiB | 50% | 40.82 | 30.55 | 26.39 | 31.10 | **19.36** |
| 2^28 | 8 GiB | 100% | 15.74 | 20.51 | 16.08 | 22.69 | **15.30** |

Run-to-run variation is the coefficient of variation over the five runs of a case. It is at most
11.40%. Six cases exceed 5%, all at 512 KiB or 32 MiB:

| Kernel | Table | Hits | Variation |
| --- | --- | ---: | ---: |
| Grouped, SIMD per-lane masks | 32 MiB | 100% | 11.40% |
| Grouped, SIMD per-lane masks | 32 MiB | 50% | 10.36% |
| Grouped, SIMD combined mask | 512 KiB | 0% | 6.83% |
| Linear probing | 512 KiB | 100% | 6.65% |
| Grouped, SIMD combined mask | 32 MiB | 100% | 6.45% |
| Grouped, SIMD combined mask | 32 MiB | 0% | 5.46% |

From 2 MiB up, the fastest kernel's range of runs overlaps another kernel's range in one row only: all
hits at 32 MiB, where linear probing, cyclic scalar, and combined-mask lookups overlap. At 512 KiB with
all hits, the same three overlap.

## Small tables and the branch predictor

At 2^10 and 2^12 keys the query trace has only 1,024 or 4,096 entries, and it repeats on every pass.
The branch predictor learns that sequence:

| Linear-probing misses | 32 KiB | 128 KiB | 512 KiB | 2 MiB |
| --- | ---: | ---: | ---: | ---: |
| Branch misses per lookup | 0.001 | 0.003 | 1.00 | 1.18 |

These counts come from one `OA_PMU=1` run of the same binary on the development machine listed under
earlier experiments. They depend on the code and the query order, not on other load. They explain
why linear-probing misses jump from 2.17 ns to 11.17 ns between 128 KiB and 512 KiB, although both
tables fit in the 2 MiB L2. The two smallest sizes are a best case for every kernel whose branches
depend on the data.

## Hardware counters

Per-lookup user-space counts from a separate `OA_PMU=1` run, one repetition per case. Branch misses
are Linux's generic `branch-misses` event. Counter runs read the counters once per pass, so their
times are not used.

| Keys | Hits | Kernel | Instructions | Cycles | Branch misses |
| --- | ---: | --- | ---: | ---: | ---: |
| 2^24 | 0% | Linear probing | 28.50 | 92.7 | 1.409 |
| 2^24 | 0% | Grouped, scalar | 34.68 | 91.4 | 1.362 |
| 2^24 | 0% | Grouped, cyclic scalar | 34.15 | 70.2 | 1.311 |
| 2^24 | 0% | Grouped, SIMD per-lane masks | 45.17 | 75.3 | 0.249 |
| 2^24 | 0% | Grouped, SIMD combined mask | 28.97 | 38.8 | 0.287 |
| 2^24 | 50% | Linear probing | 26.62 | 105.9 | 1.500 |
| 2^24 | 50% | Grouped, scalar | 30.94 | 78.6 | 1.410 |
| 2^24 | 50% | Grouped, cyclic scalar | 30.74 | 68.3 | 1.413 |
| 2^24 | 50% | Grouped, SIMD per-lane masks | 40.25 | 80.4 | 0.728 |
| 2^24 | 50% | Grouped, SIMD combined mask | 29.33 | 51.3 | 0.879 |
| 2^24 | 100% | Linear probing | 24.75 | 41.9 | 0.584 |
| 2^24 | 100% | Grouped, scalar | 27.21 | 54.6 | 1.057 |
| 2^24 | 100% | Grouped, cyclic scalar | 27.34 | 42.9 | 0.544 |
| 2^24 | 100% | Grouped, SIMD per-lane masks | 35.34 | 59.6 | 0.052 |
| 2^24 | 100% | Grouped, SIMD combined mask | 29.69 | 41.0 | 0.054 |
| 2^28 | 0% | Linear probing | 28.50 | 100.6 | 1.419 |
| 2^28 | 0% | Grouped, scalar | 34.68 | 98.4 | 1.356 |
| 2^28 | 0% | Grouped, cyclic scalar | 34.15 | 75.0 | 1.301 |
| 2^28 | 0% | Grouped, SIMD per-lane masks | 45.17 | 79.5 | 0.249 |
| 2^28 | 0% | Grouped, SIMD combined mask | 28.97 | 40.5 | 0.286 |
| 2^28 | 50% | Linear probing | 26.63 | 111.5 | 1.498 |
| 2^28 | 50% | Grouped, scalar | 30.95 | 83.5 | 1.398 |
| 2^28 | 50% | Grouped, cyclic scalar | 30.75 | 72.2 | 1.396 |
| 2^28 | 50% | Grouped, SIMD per-lane masks | 40.25 | 85.5 | 0.728 |
| 2^28 | 50% | Grouped, SIMD combined mask | 29.33 | 53.2 | 0.865 |
| 2^28 | 100% | Linear probing | 24.75 | 43.7 | 0.569 |
| 2^28 | 100% | Grouped, scalar | 27.21 | 57.1 | 1.050 |
| 2^28 | 100% | Grouped, cyclic scalar | 27.34 | 45.0 | 0.534 |
| 2^28 | 100% | Grouped, SIMD per-lane masks | 35.34 | 63.6 | 0.052 |
| 2^28 | 100% | Grouped, SIMD combined mask | 29.69 | 43.1 | 0.055 |

## Probe statistics

`build/oa_probe_stats` replays insertion and lookup on bare key arrays and counts what each lookup
reads. A line is one 64-byte cache line. The SIMD kernels read whole groups in the lane-zero layout,
so their line counts equal `ScalarGroupedOA`'s.

| Probe order | Hit: key compares | Hit: lines | Hit: 2+ lines | Hit: first compare matches | Miss: key compares | Miss: lines | Miss: 2+ lines |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Linear (ScalarOA) | 1.5000 | 1.1250 | 11.08% | 75.00% | 2.4999 | 1.3750 | 28.43% |
| Grouped, lane 0 first (ScalarGroupedOA) | 2.0994 | 1.0497 | 4.08% | 42.98% | 4.9134 | 1.2284 | 16.33% |
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

Counted in the disassembly of the measured binary, including hashing and the benchmark loop:

| Path | SIMD per-lane masks | SIMD combined mask |
| --- | ---: | ---: |
| Hit in the home group | 34 | 29 |
| Miss that ends in the home group | 39 | 26 |
| Each further full group | 27 | 13 |

After the two compares, the per-lane-mask kernel moves four 64-bit lanes to general registers and
rebuilds a 4-bit mask with `extr`, `and`, and `bfi`: eight instructions before the branch. It does the
same again to find an empty lane. The combined-mask kernel narrows both compare results with `uzp1`
and `xtn`, moves them once, and branches once. A hit then needs `clz`, `sub`, and `ldr`, plus one
`add` that recomputes the group address. A miss tests `keys[3]` with one scalar load.

## Earlier experiments

These ran on a shared `m8g.24xlarge` development machine before this measurement. Their absolute
times are not comparable with the tables above. The full log is in this file's git history.

- **Cyclic start lane.** Lane-zero grouping loses the hash's choice of slot. Starting the scan at
  `hash & 3` raised the share of hits found by the first compare from 43% to 75%, and cut compares
  per hit from 2.10 to 1.52.
- **One loop for the cyclic scan.** A single loop that returns on the first match or the first empty
  lane replaced a version with a separate first-lane test. At 2^24 keys it cut hit time from 18.8 ns
  to 16.3 ns.
- **Cheaper group address.** Keeping the probe position as a cell index lets Clang address a group
  with one `add ..., lsl #4` instead of `lsr` plus `add ..., lsl #6`. That saved about 1 ns per lookup
  at 2^28 keys in a paired test. In the same test, one extra independent `and` on the hit path of
  linear probing cost 0.42 ns at 2^28 keys, and three cost 2.03 ns.
- **Two combined-mask variants that lost.** Indexing values with `3 - countl_zero / 16` made Clang add
  a sign extension and keep a null check, for 33 instructions per hit. Building the value offset with
  xsimd `select` and `reduce_add` needed two 128-bit constants. AArch64 calls do not preserve 128-bit
  vector registers, so the constants were reloaded from the stack on every probe.

## Reproduce

Build as described in the README, then run the full matrix on one machine:

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

To split the work across machines as above, give each machine a `--benchmark_filter` with whole
rows, such as `'/keys:268435456/hit_percent:0/'`. Then pass all the JSON files to both scripts.
