# ARM lookup results

The latest four-way comparison, including simplified cyclic lookup, is in
[Simplified cyclic full sweep](#simplified-cyclic-full-sweep). The newest cyclic
change is in [Cyclic group addressing](#cyclic-group-addressing); it was measured
at selected sizes, not as a full sweep. Earlier sections retain their original
measurements.

Measured on 2026-10-07 on an ARM Neoverse-V2 host, pinned to CPU 8, using Clang
22.1.8 and the NEON64 backend. All 90 cases completed three interleaved measured
repetitions, with no hit-count, value-checksum, or iteration-count failures.

At `2^28` distinct keys, SIMD lowers miss time by **20.6%** versus normal OA.
For 100% hits, it raises lookup time by **45.8%**. The scalar grouped control
shows why the full comparison matters: grouping improves mixed queries, while
SIMD itself is 3.2% slower than scalar lookup on that grouped layout.

## Largest table

Median nanoseconds per lookup; lower is better. Each table occupies 8 GiB and
the query trace occupies 2 GiB. The change columns compare SIMD lookup time with
the indicated scalar baseline; negative means less time.

| Hits | Scalar OA | Scalar grouped OA | SIMD grouped OA | SIMD vs scalar OA | SIMD vs scalar grouped |
| --- | ---: | ---: | ---: | ---: | ---: |
| 0% | 36.43 | 35.60 | 28.91 | -20.6% | -18.8% |
| 50% | 38.69 | 30.18 | 31.15 | -19.5% | +3.2% |
| 100% | 15.73 | 20.49 | 22.93 | +45.8% | +11.9% |

Against normal OA, those changes are 7.52 ns less per miss, 7.54 ns less per mixed
query, and 7.20 ns more per hit. Against scalar grouped OA, SIMD saves 6.69 ns
per miss but costs 0.97 ns per mixed query and 2.44 ns per hit.

The coefficient of variation across the three repetitions is below 0.6% for
each of these nine largest-table cases. This is one machine and one deterministic
input seed. These measurements establish behavior for this workload, not a
general winner across processors or key distributions.

## Full sweep

Median nanoseconds per lookup. Table size is `32 * distinct_keys` bytes; queries
add `8 * distinct_keys` bytes. All cases have exactly 0.5 load.

| Distinct keys | Hits | Scalar OA | Scalar grouped OA | SIMD grouped OA |
| --- | --- | ---: | ---: | ---: |
| 2^10 | 0% | 1.91 | 2.47 | 4.60 |
| 2^10 | 50% | 1.71 | 2.03 | 3.90 |
| 2^10 | 100% | 1.57 | 1.63 | 3.25 |
| 2^12 | 0% | 2.16 | 2.81 | 5.01 |
| 2^12 | 50% | 1.89 | 2.25 | 4.18 |
| 2^12 | 100% | 1.69 | 1.73 | 3.44 |
| 2^14 | 0% | 11.44 | 5.63 | 5.16 |
| 2^14 | 50% | 6.88 | 8.66 | 4.46 |
| 2^14 | 100% | 2.65 | 7.69 | 3.66 |
| 2^16 | 0% | 13.02 | 12.78 | 7.70 |
| 2^16 | 50% | 13.14 | 13.33 | 12.54 |
| 2^16 | 100% | 5.70 | 10.39 | 4.70 |
| 2^18 | 0% | 16.56 | 15.86 | 10.89 |
| 2^18 | 50% | 17.20 | 15.59 | 14.70 |
| 2^18 | 100% | 7.56 | 11.82 | 6.97 |
| 2^20 | 0% | 19.04 | 20.51 | 16.87 |
| 2^20 | 50% | 20.89 | 19.04 | 17.95 |
| 2^20 | 100% | 9.37 | 14.39 | 14.36 |
| 2^22 | 0% | 28.05 | 29.31 | 25.71 |
| 2^22 | 50% | 31.90 | 25.61 | 27.54 |
| 2^22 | 100% | 14.29 | 18.27 | 20.71 |
| 2^24 | 0% | 34.02 | 33.19 | 27.54 |
| 2^24 | 50% | 36.73 | 28.52 | 29.36 |
| 2^24 | 100% | 15.13 | 19.65 | 21.63 |
| 2^26 | 0% | 35.03 | 34.21 | 27.93 |
| 2^26 | 50% | 38.10 | 29.20 | 29.97 |
| 2^26 | 100% | 15.23 | 19.93 | 22.10 |
| 2^28 | 0% | 36.43 | 35.60 | 28.91 |
| 2^28 | 50% | 38.69 | 30.18 | 31.15 |
| 2^28 | 100% | 15.73 | 20.49 | 22.93 |

## Reproduce and inspect

```sh
taskset -c 8 build/oa_bench \
    --benchmark_min_time=0.1s \
    --benchmark_min_warmup_time=0.05 \
    --benchmark_repetitions=3 \
    --benchmark_enable_random_interleaving=true \
    --benchmark_out=results/neon64.json \
    --benchmark_out_format=json
```

The host has 64 KiB L1 data cache and 2 MiB L2 per core, plus a shared 36 MiB L3.
ASLR remained enabled. Allocation, construction, shuffling, and verification are
outside timing. Timed work includes hashing, probing, reading values, and
accumulating results. The numbers describe independent-query throughput, not
serialized access latency. Google Benchmark's warmup precedes the measured
repetitions; each invocation rebuilds its table and query trace.

Artifacts from this run are local and excluded from Git:

- [Raw measurements and aggregates](results/neon64.json)
- [Median comparison CSV](results/neon64-medians.csv)
- [Console output](results/neon64.log)
- [Source and executable SHA256 manifest](results/neon64.sha256)
- [ARM executable disassembly](build/neon64.disassembly.txt)
- [AVX2 cross-compiled assembly](build-cross/benchmark-avx2.s)

Release and ASan/UBSan/LeakSanitizer tests passed, including a nine-case benchmark
smoke test. ClickHouse formatting and clang-tidy checks passed. ARM disassembly
contains paired 128-bit loads and 64-bit vector comparisons for SIMD, with scalar
comparisons in both controls. AVX2 assembly contains a 256-bit aligned load,
four-key compare, and lane-mask extraction. The AVX2 test translation unit also
cross-compiles, but x86 runtime correctness and performance have not been tested.

## Hash-selected scalar lanes

The separate `ScalarCyclicGroupedOA` experiment starts insertion and lookup at
`hash & 3`, then scans all four lanes cyclically in every group. Storage, group
selection, group-level empty termination, hashing, capacity, and workload are
unchanged. No SIMD kernel was changed.

All three scalar variants were rebuilt and measured together on CPU 8 with the
same compiler flags, seed, warmup, and repetition settings as above. The original
lookup loops retain identical ARM instruction sequences. The cyclic loop is
fully unrolled and scalar, with no inner-loop spills, but adds lane-address
arithmetic.

For `2^28` keys and 100% hits, cyclic grouping takes **20.102 ns**, compared with
**20.581 ns** for lane-zero grouping and **15.871 ns** for normal OA. It saves
**0.478 ns (2.32%)** versus lane-zero grouping, but still costs **4.231 ns
(26.66%)** versus normal OA.

The three-repeat ranges are 20.094-20.106 ns for cyclic, 20.558-20.582 ns for
lane-zero grouped, and 15.871-15.900 ns for normal OA. The ranges do not overlap;
each largest-case coefficient of variation is below 0.11%.

### All-hit sweep

Median nanoseconds per lookup. The change column compares cyclic with lane-zero
grouped lookup time; negative means less time.

| Distinct keys | Normal OA | Lane-zero grouped | Cyclic grouped | Cyclic vs lane-zero |
| --- | ---: | ---: | ---: | ---: |
| 2^10 | 1.571 | 1.632 | 2.156 | +32.14% |
| 2^12 | 1.681 | 1.732 | 2.233 | +28.90% |
| 2^14 | 2.227 | 7.755 | 2.768 | -64.31% |
| 2^16 | 5.500 | 10.387 | 5.984 | -42.39% |
| 2^18 | 7.601 | 11.825 | 8.210 | -30.57% |
| 2^20 | 9.700 | 13.198 | 11.943 | -9.51% |
| 2^22 | 14.346 | 18.354 | 17.638 | -3.90% |
| 2^24 | 15.140 | 19.649 | 18.957 | -3.53% |
| 2^26 | 15.360 | 19.991 | 19.228 | -3.82% |
| 2^28 | 15.871 | 20.581 | 20.102 | -2.32% |

The `2^20` cyclic-versus-lane-zero result is inconclusive from these three
repetitions: cyclic spans 10.784-14.685 ns, overlapping lane-zero grouping's
13.098-14.143 ns. Its coefficient of variation is 16.06%. Normal OA is faster
than cyclic in every measured repetition at every sampled size.

### Miss and mixed controls

The control run covered 0% and 50% hits at four representative sizes, again with
all three scalar variants and three interleaved repetitions. All 24 cases and
72 measured repetitions passed their correctness and normalization checks.

| Distinct keys | Hits | Normal OA | Lane-zero grouped | Cyclic grouped | Cyclic vs lane-zero |
| --- | --- | ---: | ---: | ---: | ---: |
| 2^10 | 0% | 1.919 | 2.464 | 2.746 | +11.45% |
| 2^10 | 50% | 1.718 | 2.057 | 2.407 | +17.04% |
| 2^16 | 0% | 13.907 | 13.378 | 12.616 | -5.70% |
| 2^16 | 50% | 14.004 | 13.731 | 12.985 | -5.44% |
| 2^20 | 0% | 20.662 | 18.175 | 17.301 | -4.81% |
| 2^20 | 50% | 21.005 | 18.661 | 19.646 | +5.28% |
| 2^28 | 0% | 36.233 | 35.099 | 30.819 | -12.19% |
| 2^28 | 50% | 38.425 | 30.486 | 28.212 | -7.46% |

At `2^28`, cyclic grouping saves 4.280 ns per miss and 2.274 ns per mixed query
versus lane-zero grouping. Against normal OA, it saves 5.415 ns (14.94%) per miss
and 10.213 ns (26.58%) per mixed query. Largest-table repetition ranges do not
overlap, and all six control cases have coefficients of variation below 0.35%.

At `2^20`, the cyclic-versus-lane-zero ranges overlap for both hit rates, so
those comparisons remain inconclusive. Cyclic miss times span 17.093-21.026 ns,
with 11.98% variation. This is one host and one fixed input seed; gains are not
uniform across sizes or hit rates.

Both grouped kernels still compare all four keys on a group miss. Their
compiled empty tests then stop at the first zero in probe order. Cyclic
placement can reach that zero sooner than lane-zero placement, reducing some
miss-path work. No hardware-counter measurement isolates its runtime share.

To reproduce the controls, use the all-hit command below with these two options:

```sh
--benchmark_filter='^Scalar(OA|GroupedOA|CyclicGroupedOA)/keys:(1024|65536|1048576|268435456)/hit_percent:(0|50)/'
--benchmark_out=results/cyclic-controls.json
```

### Probe counts

An occupancy replay of the exact `2^28` key/hash/insertion sequence found:

| Per successful lookup | Normal OA | Lane-zero grouped | Cyclic grouped |
| --- | ---: | ---: | ---: |
| Key comparisons | 1.499996 | 2.099416 | 1.517692 |
| Logical cacheline visits | 1.125016 | 1.049662 | 1.049662 |
| First-comparison hits | 75.0018% | 42.9777% | 75.0010% |

Cyclic placement restores the starting-slot hint and reduces comparisons by
27.71% versus lane-zero grouping. It does not eliminate the measured runtime
gap with normal OA. These are algorithmic counts, not cache misses or a causal
breakdown of runtime. Every simulated cyclic destination and value was checked
against the actual table at `2^20`. Hardware counters remain unavailable under
the host's current permissions.

### Reproduce the all-hit experiment

```sh
taskset -c 8 build/oa_bench \
    --benchmark_filter='^Scalar(OA|GroupedOA|CyclicGroupedOA)/keys:[0-9]+/hit_percent:100/' \
    --benchmark_min_time=0.1s \
    --benchmark_min_warmup_time=0.05 \
    --benchmark_repetitions=3 \
    --benchmark_enable_random_interleaving=true \
    --benchmark_out=results/cyclic-hits.json \
    --benchmark_out_format=json
```

All 30 cases and 90 measured repetitions passed their correctness and
normalization checks. Release and ASan/UBSan/LeakSanitizer tests passed, including
the expanded twelve-case benchmark smoke test. Formatting and clang-tidy passed.
The new benchmark and test translation units cross-compile for AVX2; x86 runtime
remains untested.

Local artifacts, excluded from Git:

- [All-hit measurements](results/cyclic-hits.json)
- [All-hit median CSV](results/cyclic-hits-medians.csv)
- [Miss and mixed measurements](results/cyclic-controls.json)
- [Miss and mixed median CSV](results/cyclic-controls-medians.csv)
- [Source and executable SHA256 manifest](results/cyclic.sha256)
- [Cyclic probe diagnostic](results/cyclic-probe-diagnostics.cpp) and [counts](results/cyclic-probe-diagnostics.csv)
- [JSON validation and summary script](results/summarize-cyclic.py)
- [Updated ARM disassembly](build/cyclic-neon64.disassembly.txt)
- [Updated AVX2 assembly](build-cross/benchmark-cyclic-avx2.s)

The original executable is preserved as `results/oa_bench-original`, matching
the binary fingerprint in `results/neon64.sha256`. The original measurements
above have not been replaced by this experiment.

## Simplified cyclic lookup

On 2026-10-08, `ScalarCyclicGroupedOA` was simplified to one cyclic lane loop.
It returns on either a match or the first empty lane. Insertion uses that same
order, and entries are never deleted or moved, so an empty lane proves absence.
There is no separate first-lane fast path or accumulated empty flag.

The following before/after check uses `2^24` keys, a 512 MiB table, a 128 MiB
trace, and three randomly interleaved repetitions per executable on CPU 8.
The before and after executables ran sequentially; these are medians in ns/lookup.

| Hit rate | Original cyclic | Simplified cyclic |
| --- | ---: | ---: |
| 0% | 29.058 | 26.481 |
| 50% | 26.716 | 26.060 |
| 100% | 18.838 | 16.302 |

The unchanged normal-OA all-hit control measured 15.211 ns before and 15.184 ns
after. The simplified cyclic lookup still trails normal OA at this size.
This check does not replace the earlier full-size sweep.

Raw measurements and executable snapshots are in `results/cyclic-home-first/`:
`before-n24.json` is the original cyclic loop, `peeled-n24.json` is the
superseded first-lane-special-case version, and `simple-n24.json` is the current
implementation. Release and ASan/UBSan/LeakSanitizer tests, formatting, and
clang-tidy passed. The current tests cross-compile for AVX2; x86 runtime remains
untested.

## Simplified cyclic full sweep

Measured on 2026-10-08 with all four implementations in the same executable,
pinned to CPU 8 on the ARM Neoverse-V2 host. Clang 22.1.8, NEON64, hashing,
compiler flags, seed, and workload are unchanged. All 120 cases and 360 measured
repetitions passed correctness and normalization checks. Source and executable
fingerprints match before and after the run.

Normal is conventional scalar linear probing. Grouped is the original lane-zero
scalar lookup. SIMD uses that same grouped placement with NEON comparisons.
Cyclic is the current simplified scalar loop, starting at `hash & 3` and
returning on the first matching or empty lane.

Median nanoseconds per lookup; lower is better. Bold marks the lowest observed
median, not a statistical significance claim. Each case has three randomly
interleaved repetitions at exactly 0.5 load. Table size excludes the query trace,
which adds one quarter of the table's bytes. Timing includes hashing, probing,
value reads, and result accumulation; construction and shuffling are excluded.

### 0% hits

| Keys | Table size | Normal | Grouped | SIMD | Cyclic |
| --- | --- | ---: | ---: | ---: | ---: |
| 2^10 | 32 KiB | **1.906** | 2.463 | 4.604 | 2.543 |
| 2^12 | 128 KiB | **2.198** | 2.813 | 5.004 | 2.762 |
| 2^14 | 512 KiB | 11.416 | 5.711 | **5.250** | 7.957 |
| 2^16 | 2 MiB | 14.259 | 13.625 | **8.416** | 12.959 |
| 2^18 | 8 MiB | 16.806 | 15.816 | **10.903** | 14.955 |
| 2^20 | 32 MiB | 20.618 | 20.275 | **15.996** | 18.861 |
| 2^22 | 128 MiB | 28.546 | 28.985 | 26.172 | **24.601** |
| 2^24 | 512 MiB | 33.435 | 32.808 | 27.442 | **26.699** |
| 2^26 | 2 GiB | 35.064 | 33.631 | 28.094 | **27.076** |
| 2^28 | 8 GiB | 36.389 | 35.055 | 28.971 | **28.187** |

### 50% hits

| Keys | Table size | Normal | Grouped | SIMD | Cyclic |
| --- | --- | ---: | ---: | ---: | ---: |
| 2^10 | 32 KiB | **1.724** | 2.030 | 3.899 | 2.237 |
| 2^12 | 128 KiB | **1.890** | 2.249 | 4.181 | 2.380 |
| 2^14 | 512 KiB | 6.791 | 8.898 | **4.616** | 8.368 |
| 2^16 | 2 MiB | 14.385 | 13.929 | **12.604** | 13.583 |
| 2^18 | 8 MiB | 17.271 | 15.659 | **14.651** | 15.352 |
| 2^20 | 32 MiB | 20.789 | 17.721 | 19.231 | **17.366** |
| 2^22 | 128 MiB | 31.571 | 26.529 | 28.033 | **24.334** |
| 2^24 | 512 MiB | 36.528 | 28.953 | 29.998 | **26.009** |
| 2^26 | 2 GiB | 37.910 | 29.456 | 30.539 | **26.464** |
| 2^28 | 8 GiB | 38.445 | 30.509 | 31.939 | **27.391** |

### 100% hits

| Keys | Table size | Normal | Grouped | SIMD | Cyclic |
| --- | --- | ---: | ---: | ---: | ---: |
| 2^10 | 32 KiB | **1.583** | 1.632 | 3.253 | 1.937 |
| 2^12 | 128 KiB | **1.682** | 1.732 | 3.431 | 2.034 |
| 2^14 | 512 KiB | 2.320 | 7.758 | 3.773 | **2.305** |
| 2^16 | 2 MiB | 5.954 | 10.701 | **5.349** | 6.124 |
| 2^18 | 8 MiB | 7.655 | 11.848 | **7.034** | 7.797 |
| 2^20 | 32 MiB | **9.479** | 13.590 | 12.331 | 11.323 |
| 2^22 | 128 MiB | **14.631** | 18.576 | 20.013 | 15.294 |
| 2^24 | 512 MiB | **15.234** | 19.640 | 21.667 | 16.379 |
| 2^26 | 2 GiB | **15.326** | 19.985 | 22.168 | 16.636 |
| 2^28 | 8 GiB | **15.862** | 20.578 | 22.996 | 17.270 |

At `2^28`, cyclic lookup reduces time versus normal OA by 8.202 ns (22.54%)
for misses and 11.053 ns (28.75%) for mixed queries. For all hits, cyclic takes
1.407 ns (8.87%) longer than normal OA, but saves 3.308 ns (16.08%) versus
scalar grouped and 5.726 ns (24.90%) versus SIMD grouped. All largest-table
pairwise sample ranges are separated, and the largest coefficient of variation
there is below 0.52%.

The `2^20` cases are noisy: seven have sample coefficients of variation above
5%, with a maximum of 14.40%. The lowest-median miss result overlaps all three
other variants' ranges; the lowest-median mixed result overlaps scalar grouped.
At `2^14` and 100% hits, cyclic and normal OA also have overlapping ranges.
These results describe one host and one deterministic input seed, not every
processor or key distribution. AVX2 runtime remains untested.

### Reproduce the full comparison

```sh
taskset -c 8 build/oa_bench \
    --benchmark_min_time=0.1s \
    --benchmark_min_warmup_time=0.05 \
    --benchmark_repetitions=3 \
    --benchmark_enable_random_interleaving=true \
    --benchmark_display_aggregates_only=true \
    --benchmark_out=results/neon64-simple-full.json \
    --benchmark_out_format=json
python3 results/summarize-simple-full.py results/neon64-simple-full.json
```

`--benchmark_display_aggregates_only` affects the console only; the JSON retains
all individual measurements. Artifacts are local and excluded from Git:

- [Raw measurements](results/neon64-simple-full.json)
- [Medians, ranges, and coefficients of variation](results/neon64-simple-full-medians.csv)
- [Generated tables and variation report](results/neon64-simple-full-tables.md)
- [Console output](results/neon64-simple-full.log)
- [Source and executable fingerprints](results/neon64-simple-full.sha256)
- [Validation and summary script](results/summarize-simple-full.py)

The executable and header fingerprinted there were replaced by the change below.
Byte-identical copies remain as `results/cyclic-home-first/oa_bench-simple` and
`results/cyclic-home-first/hash_tables-simple.hpp`.

## Cyclic group addressing

On 2026-10-08, `ScalarCyclicGroupedOA` insertion and lookup started keeping the
probe position as the cell index of the group's first lane,
`hash & cell_mask & ~size_t{3}`. They read `groups[pos / 4]` and advance with
`(pos + 4) & cell_mask`. This is the same group index as before, so group
selection, lane order, placement, and termination are unchanged. Rebuilding the
driver below against the new header showed that the new insertion puts every key
in the same physical slot as the previous one, up to `2^20` keys
([log](results/cyclic-address/verify-new-insert.log)). Normal, lane-zero
grouped, and SIMD lookup are unchanged.

The previous lookup executed 24 instructions per query on a first-lane hit,
versus 21 for normal OA. Its first group address took `and`, `lsr #2`, and
`add ..., lsl #6`. Arm's
[Neoverse V2 optimization guide](https://documentation-service.arm.com/static/668bc0a369e89f01e39c4668)
lists an `add` with a left shift above 4 at 2 cycles on the M pipes, versus
1 cycle for shifts up to 4. With the cell index, Clang 22.1.8 emits a hoisted
mask `and` followed by `add ..., lsl #4`. The first-lane hit path drops to
23 instructions. The two `and` instructions that extract the home lane remain;
one is a register-allocation duplicate. Each overflow group now costs one more
instruction than before. The same source change does not alter the lane-zero
grouped or SIMD loops: Clang turns it back into `lsr` and `lsl #6`.

### Paired measurement

[`cyclic-address.cpp`](results/cyclic-address/cyclic-address.cpp) builds one
executable with five lookups, compiled against the pre-change header:

- Normal OA: production `ScalarTable`.
- Normal OA with one or three padding instructions: independent `and`s of the
  hash before the first probe. An empty `asm` keeps them without consuming
  their results.
- Previous cyclic: a copy of the previous insertion and lookup.
- New cyclic: the new lookup on the same table, so placement is identical.

Disassembly confirmed that the normal and previous-cyclic loops match the
previous production loops instruction for instruction. Each padding loop adds
exactly the stated instructions and keeps the table loads hoisted. The new
cyclic loop matches the new production loop. Before timing, every lookup was
checked against production slots, values, misses, wraparound, and key zero.

All 44 cases and 220 measured repetitions passed hit, checksum, and
normalization checks. Five randomly interleaved repetitions ran on CPU 8, with
the same timing options as earlier sections. Medians are ns per lookup with
min-max ranges. All twelve ranges are separated. The largest coefficient of
variation is 1.23% for the cyclic lookups and 2.05% overall, for three padding
instructions at `2^16`.

| Keys | Hits | Previous cyclic | New cyclic | Change |
| --- | --- | ---: | ---: | ---: |
| 2^10 | 0% | 2.523 (2.523-2.531) | 2.384 (2.376-2.392) | -0.140 (-5.54%) |
| 2^10 | 50% | 2.228 (2.225-2.251) | 2.088 (2.082-2.092) | -0.140 (-6.29%) |
| 2^10 | 100% | 1.932 (1.931-1.943) | 1.795 (1.794-1.825) | -0.137 (-7.11%) |
| 2^16 | 0% | 12.437 (12.331-12.510) | 11.455 (11.207-11.481) | -0.983 (-7.90%) |
| 2^16 | 50% | 12.598 (12.499-12.719) | 12.282 (12.069-12.314) | -0.317 (-2.51%) |
| 2^16 | 100% | 5.601 (5.524-5.699) | 5.444 (5.353-5.489) | -0.158 (-2.82%) |
| 2^24 | 0% | 26.581 (26.349-26.709) | 25.281 (25.188-25.580) | -1.301 (-4.89%) |
| 2^24 | 50% | 26.051 (25.952-26.189) | 24.976 (24.971-25.152) | -1.075 (-4.13%) |
| 2^24 | 100% | 16.404 (16.331-16.429) | 15.528 (15.494-15.588) | -0.876 (-5.34%) |
| 2^28 | 0% | 28.059 (27.988-28.080) | 26.990 (26.956-27.005) | -1.068 (-3.81%) |
| 2^28 | 50% | 27.473 (27.433-27.491) | 26.366 (26.361-26.392) | -1.107 (-4.03%) |
| 2^28 | 100% | 17.248 (17.240-17.255) | 16.204 (16.188-16.208) | -1.045 (-6.06%) |

The padding lookups measure what extra work before the first probe costs normal
OA on all-hit queries:

| Keys | Normal | One extra `and` | Three extra `and`s | Previous cyclic minus normal |
| --- | ---: | ---: | ---: | ---: |
| 2^10 | 1.578 | 1.659 (+0.082, +5.17%) | 1.855 (+0.278, +17.59%) | +0.355 |
| 2^16 | 5.670 | 5.629 (-0.041, overlapping) | 5.987 (+0.317, +5.60%) | -0.069 (overlapping) |
| 2^24 | 15.246 | 15.582 (+0.336, +2.20%) | 16.969 (+1.723, +11.30%) | +1.158 |
| 2^28 | 15.880 | 16.299 (+0.418, +2.63%) | 17.906 (+2.026, +12.76%) | +1.368 |

Unless marked, each padded range is separated from normal OA's. Independent
instructions on the hit path cost time at every size, but not in proportion to
their number. At `2^28`, the address change saved 1.045 ns, about 2.5 times the
cost of one padding instruction. Instruction count alone therefore does not
explain the saving. The padding instructions sit off the load-address chain,
whereas the removed `lsr` was on it, and the removed `add ..., lsl #6` used an
M pipe. These controls measure only the cost of extra independent instructions;
address-chain latency was not controlled, so the experiment does not identify
the mechanism.

The normal-OA miss medians in this executable are 35.598 ns at `2^24` and
38.824 ns at `2^28`. Production measured 33.645 and 36.164 ns below, with the
same instructions at different code addresses. Both miss loops fit within one
64-byte block; the cause was not investigated. Use the production run below
for cyclic-versus-normal comparisons. The cyclic results do not show this
sensitivity: at `2^24` and `2^28`, the new cyclic lookup agrees with production
within 1.41% at every hit rate. The previous cyclic lookup agrees with the
earlier sweep's executable within 0.46%.

### Production confirmation

The rebuilt `build/oa_bench` ran normal and cyclic lookup at `2^24` and `2^28`,
with five randomly interleaved repetitions. All 12 cases and 60 repetitions
passed. The earlier-sweep columns come from a different executable and run,
with three repetitions, so the last column is context rather than a paired
comparison. All ranges in both comparison columns are separated. The largest
coefficient of variation in this run is 0.74%.

| Keys | Hits | Normal | Cyclic | Cyclic vs normal | Earlier normal | Earlier cyclic | Cyclic vs earlier cyclic |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 2^24 | 0% | 33.645 | 25.642 | -8.003 (-23.79%) | 33.435 | 26.699 | -1.056 (-3.96%) |
| 2^24 | 50% | 35.928 | 25.044 | -10.884 (-30.29%) | 36.528 | 26.009 | -0.965 (-3.71%) |
| 2^24 | 100% | 15.212 | 15.643 | +0.431 (+2.83%) | 15.234 | 16.379 | -0.736 (-4.49%) |
| 2^28 | 0% | 36.164 | 27.312 | -8.852 (-24.48%) | 36.389 | 28.187 | -0.875 (-3.10%) |
| 2^28 | 50% | 38.461 | 26.382 | -12.079 (-31.41%) | 38.445 | 27.391 | -1.009 (-3.68%) |
| 2^28 | 100% | 15.901 | 16.323 | +0.422 (+2.65%) | 15.862 | 17.270 | -0.947 (-5.48%) |

At `2^28`, cyclic lookup now takes 0.422 ns (2.65%) longer than normal OA for
all hits, down from 1.407 ns (8.87%) in the earlier sweep. It saves 8.852 ns
(24.48%) per miss and 12.079 ns (31.41%) per mixed query. Other sizes were not
re-measured with the production executable. The four-way full sweep above has
not been refreshed. This is one host and one input seed.

Release and ASan/UBSan Meson tests passed, as did formatting and clang-tidy.
The test and benchmark translation units cross-compile for AVX2; x86 runtime
remains untested.

### Reproduce the group-addressing measurements

```sh
clang++-22 -std=c++20 -O3 -DNDEBUG -D_FILE_OFFSET_BITS=64 \
    -DBENCHMARK_STATIC_DEFINE -Wall -Wextra -Wpedantic \
    -fno-vectorize -fno-slp-vectorize -DSIMD_HT_NEON64=1 \
    -I results/cyclic-address/previous-header -isystem subprojects/xsimd-14.3.0/include \
    -isystem subprojects/benchmark-1.9.4/include \
    results/cyclic-address/cyclic-address.cpp \
    build/subprojects/benchmark-1.9.4/libcm_benchmark.a -pthread \
    -o /tmp/cyclic-address
/tmp/cyclic-address --verify-only
taskset -c 8 /tmp/cyclic-address \
    --benchmark_min_time=0.1s --benchmark_min_warmup_time=0.05 \
    --benchmark_repetitions=5 --benchmark_enable_random_interleaving=true \
    --benchmark_display_aggregates_only=true \
    --benchmark_out=/tmp/paired.json --benchmark_out_format=json
python3 results/cyclic-address/summarize.py /tmp/paired.json

# Placement check of the new insertion: the same driver against the new header.
clang++-22 -std=c++20 -O3 -DNDEBUG -D_FILE_OFFSET_BITS=64 \
    -DBENCHMARK_STATIC_DEFINE -fno-vectorize -fno-slp-vectorize \
    -DSIMD_HT_NEON64=1 -I src -isystem subprojects/xsimd-14.3.0/include \
    -isystem subprojects/benchmark-1.9.4/include \
    results/cyclic-address/cyclic-address.cpp \
    build/subprojects/benchmark-1.9.4/libcm_benchmark.a -pthread \
    -o /tmp/cyclic-address-new
/tmp/cyclic-address-new --verify-only

taskset -c 8 build/oa_bench \
    --benchmark_filter='^(ScalarOA|ScalarCyclicGroupedOA)/keys:(16777216|268435456)/' \
    --benchmark_min_time=0.1s --benchmark_min_warmup_time=0.05 \
    --benchmark_repetitions=5 --benchmark_enable_random_interleaving=true \
    --benchmark_display_aggregates_only=true \
    --benchmark_out=/tmp/production.json --benchmark_out_format=json
python3 results/cyclic-address/summarize-production.py /tmp/production.json results/neon64-simple-full.json
```

`previous-header` holds the pre-change `hash_tables.hpp` and `workload.hpp`,
which the measured driver was compiled against. Artifacts are local and
excluded from Git:

- [Driver source](results/cyclic-address/cyclic-address.cpp) and [executable](results/cyclic-address/cyclic-address)
- [Paired raw measurements](results/cyclic-address/paired-n5.json), [console output](results/cyclic-address/paired-n5.log), [tables](results/cyclic-address/paired-n5-summary.md), and [per-case samples](results/cyclic-address/paired-n5-summary.csv)
- [Production raw measurements](results/cyclic-address/production-n5.json), [console output](results/cyclic-address/production-n5.log), and [tables](results/cyclic-address/production-n5-summary.md)
- [Summary scripts](results/cyclic-address/summarize.py) and [production comparison](results/cyclic-address/summarize-production.py)
- [Driver disassembly](results/cyclic-address/cyclic-address.disassembly.txt), [production disassembly](results/cyclic-address/oa_bench.disassembly.txt), and [loop extractor](results/cyclic-address/hot-loop.py)
- [New production executable](results/cyclic-address/oa_bench-cyclic-address) and [header](results/cyclic-address/hash_tables-cyclic-address.hpp)
- [New-insertion placement check](results/cyclic-address/verify-new-insert.log)
- [Fingerprints](results/cyclic-address/SHA256SUMS)
