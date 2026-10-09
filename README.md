# simd-ht

How fast can one CPU core look up 64-bit keys in a hash table that is far larger than its caches?
This repository measures five lookup kernels for an open-addressing hash table on an Arm Neoverse V2
core, with tables from 32 KiB to 8 GiB.

The fastest kernel tests four keys with two NEON compares and one branch. In an 8 GiB table it
finishes a miss in **14.57 ns**. Textbook linear probing needs **36.53 ns** for the same miss.
Hits take 15.30 ns against 15.74 ns.

![Lookup time against table size for the five kernels](docs/lookup-time.svg)

Median nanoseconds per lookup in an 8 GiB table (2^28 keys), lower is better:

| Kernel | All misses | Half hits | All hits |
| --- | ---: | ---: | ---: |
| Linear probing (`ScalarOA`) | 36.53 | 40.82 | 15.74 |
| Grouped, scalar (`ScalarGroupedOA`) | 35.31 | 30.55 | 20.51 |
| Grouped, cyclic scalar (`ScalarCyclicGroupedOA`) | 26.87 | 26.39 | 16.08 |
| Grouped, SIMD per-lane masks (`SIMDGroupedOA`) | 28.46 | 31.10 | 22.69 |
| **Grouped, SIMD combined mask** (`SIMDCombinedGroupedOA`) | **14.57** | **19.36** | **15.30** |

Full results for all ten table sizes, the hardware counters, and the method are in
[RESULTS.md](RESULTS.md).

Three ideas carry the result:

1. **Probe one cache line, not one cell.** Four keys and their values share a 64-byte line, so most
   lookups touch exactly one line.
2. **Make the loop exit predictable.** Linear probing stops after a different number of cells for
   each miss, and the core mispredicts 1.42 times per miss. The grouped SIMD lookup makes one
   decision per cache line and mispredicts 0.29 times per miss.
3. **Spend few instructions per lookup.** Independent lookups overlap inside the core. The fewer
   instructions each one needs, the more cache misses are in flight at once.

## The table

Keys and values are 64-bit integers. Key 0 marks an empty slot. Capacity is twice the key count,
rounded up to a power of two, so every benchmark runs at load factor 0.5. There is no deletion and
no resizing. The hash folds a key's two 32-bit halves together and applies MurmurHash3's `fmix32`
finalizer.

Two memory layouts share that hash:

```text
Linear probing: 16-byte cells, four per 64-byte cache line
  | key | value | key | value | key | value | key | value |
  start at cell  hash & (capacity - 1),  then try the next cell

Grouped: one 64-byte group per cache line
  | key 0 | key 1 | key 2 | key 3 | value 0 | value 1 | value 2 | value 3 |
  start at group (hash & (capacity - 1)) / 4,  then try the next group
```

Both layouts use 16 bytes per slot, so a key's home group starts at the same offset as its home
cell's cache line would in the linear layout. Inserting into a group takes the first empty lane.
When the group is full, the key goes to the next group.

## Why linear probing loses on misses

At load factor 0.5, linear probing compares 1.5 keys per hit and 2.5 keys per miss on average. That
is Knuth's classic result, and `tools/probe_stats.cpp` measures 1.50 and 2.50 on the benchmark's
keys. The averages hide two costs.

First, the number of compares varies from miss to miss:

| Linear-probing misses that stop at | 1st cell | 2nd | 3rd | 4th | 5th or later |
| --- | ---: | ---: | ---: | ---: | ---: |
| Share | 50.0% | 19.7% | 10.5% | 6.3% | 13.6% |

The branch that ends the loop cannot be predicted. The hardware counters show 1.42 branch
mispredictions per miss. Each misprediction throws away the work the core had already started on
the lookups that come after it.

Second, a run of occupied cells crosses cache-line boundaries. 28% of misses read two or more
lines, and a miss reads 1.375 lines on average.

## Grouping: one cache line per probe

At load factor 0.5, 16.3% of groups are full. So a miss ends in its home cache line 83.7% of the
time, and a hit reads 1.05 lines on average, against 1.125 for linear probing.

The grouped layout alone does not make the scalar code fast. `ScalarGroupedOA` scans lanes 0 to 3
in order, but the wanted key sits in whichever lane was free when it was inserted. Its first
compare matches only 43% of hits, against 75% for linear probing, and a hit compares 2.10 keys.

## Cyclic: start where the hash points

`ScalarCyclicGroupedOA` starts both insertion and lookup at lane `hash & 3` and wraps around the
group. That restores linear probing's first-compare match rate (75%) and compare count (1.52 per
hit), while each probe still reads one cache line. The lookup can stop at the first
empty lane in that order, because insertion filled the lanes in the same order and nothing is ever
deleted.

## SIMD: four keys, one branch

`SIMDCombinedGroupedOA` compares all four keys of a group at once. The NEON code that Clang 22
generates for one group is:

```asm
ldp   q1, q2, [x15]          // load keys 0-1 and 2-3
cmeq  v1.2d, v1.2d, v0.2d    // keys 0-1 == search key: all-ones or zero per lane
cmeq  v2.2d, v2.2d, v0.2d    // keys 2-3 == search key
uzp1  v1.4s, v1.4s, v2.4s    // keep one 32-bit half of each result
xtn   v1.4h, v1.4s           // narrow to one 16-bit field per key
fmov  x14, d1                // the four fields, in one general register
cbnz  x14, hit               // one branch for four keys
```

Two facts about the table make the rest cheap:

- **A key occupies at most one slot.** At most one 16-bit field is set, so a single count of
  leading zeros locates the match. A match in lane `i` leaves `48 - 16 * i` leading zeros, and half
  of that is the byte distance back from `values[3]` to `values[i]`. On a hit, the value costs
  `clz`, `sub`, and `ldr`, plus one `add` that recomputes the group address.
- **Occupied lanes are always a prefix of the group.** Insertion takes the lowest empty lane and
  nothing is deleted, so a group with any free slot has lane 3 empty. When no key matches, one
  scalar load of `keys[3]` decides whether the key is absent. That branch goes the same way for the
  83.7% of groups that are not full.

A hit in the home group runs 29 instructions, including hashing and the benchmark loop. A miss
that ends in the home group runs 26.

The first SIMD kernel, `SIMDGroupedOA`, makes the same two compares. It then moves each 64-bit
result to a general register separately and rebuilds a 4-bit mask, which takes eight instructions
before the branch. It repeats all of that to test for an empty lane. A hit costs 34 instructions
and a home-group miss 39. In the 8 GiB table it takes 22.69 ns per hit, against 15.74 ns for linear
probing.

On x86-64, the same code compiles to one 256-bit `vpcmpeqq`, one `vmovmskpd`, and the same lane-3
exit. That build compiles but has not been run or measured.

## Instructions are the budget

Hardware counters per lookup in the 8 GiB table:

| Kernel | Miss: instructions | Miss: cycles | Miss: branch misses | Hit: instructions | Hit: cycles | Hit: branch misses |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Linear probing | 28.50 | 100.6 | 1.42 | 24.75 | 43.7 | 0.57 |
| Grouped, scalar | 34.68 | 98.4 | 1.36 | 27.21 | 57.1 | 1.05 |
| Grouped, cyclic scalar | 34.15 | 75.0 | 1.30 | 27.34 | 45.0 | 0.53 |
| Grouped, SIMD per-lane masks | 45.17 | 79.5 | 0.25 | 35.34 | 63.6 | 0.05 |
| Grouped, SIMD combined mask | 28.97 | 40.5 | 0.29 | 29.69 | 43.1 | 0.05 |

The two SIMD kernels mispredict about as often (0.25 and 0.29 times per miss), touch the same
cache lines, and run the same hashing. The combined-mask kernel runs 36% fewer instructions per
miss and needs 49% fewer cycles. The counters fit a simple picture: the core overlaps independent
lookups up to the size of its out-of-order window, so fewer instructions per lookup and fewer
mispredictions mean more cache misses in flight at once. The counters do not prove that cause.

On hits, the combined-mask kernel runs 20% more instructions than linear probing (29.69 against
24.75) but mispredicts a tenth as often. Both need about 43 cycles per hit.

## Where each kernel wins

- **32 KiB and 128 KiB tables.** The query trace holds only 1,024 or 4,096 keys and repeats on every
  pass, and the branch predictor learns it. Linear probing mispredicts 0.001 to 0.003 times per miss
  at these sizes, against 1.0 at 512 KiB. With no mispredictions and no cache misses, instruction
  count decides. Linear probing wins hits (1.58 ns against 1.99 ns at 32 KiB) and half hits. The
  combined-mask kernel still wins misses (1.61 ns against 1.91 ns). Treat these two sizes as a best
  case for linear probing.
- **512 KiB.** The trace is now too long to learn. Linear-probing misses jump to 11.17 ns, while the
  combined-mask kernel takes 2.25 ns, five times less. With all hits, linear probing, cyclic scalar,
  and combined-mask lookups are within run-to-run noise (2.41 to 2.48 ns).
- **2 MiB to 8 GiB.** The combined-mask kernel is fastest in every row. Its range of runs is separate
  from every other kernel's in every row except all hits at 32 MiB. Against linear probing it is 2.1
  to 2.8 times faster on misses and 1.4 to 2.1 times faster with half hits. With all hits the gap
  shrinks from 1.7 times at 2 MiB to 2 to 3% at 128 MiB and above.

## Limits

- One processor: Arm Neoverse V2 (AWS Graviton4), on four `m8g.4xlarge` instances, with Clang
  22.1.8 on Linux. Each core has 64 KiB of L1 data cache and 2 MiB of L2; the 36 MiB L3 is shared.
- One input distribution: distinct pseudo-random 64-bit keys, one fixed shuffle seed, load factor
  exactly 0.5.
- Throughput of independent lookups on one thread. This is not the latency of one isolated lookup.
- Insert-only tables. The lane-3 exit and the cyclic early exit both rely on there being no
  deletion.
- The AVX2 path compiles, but it has not been run.

## Build and test

Requires Clang with C++20, Meson, Ninja, CMake, clang-format and clang-tidy. Python 3 runs the
scripts in `tools/`, with no extra packages. The setup was validated with Clang 22.1.8. Meson
downloads pinned, checksummed archives of Google Benchmark and xsimd on first setup.

```sh
CXX=clang++-22 meson setup build --buildtype=release
meson compile -C build
meson test -C build --print-errorlogs
meson compile -C build format-check tidy
```

Meson picks NEON on AArch64 and AVX2 on x86-64. Automatic loop and SLP vectorization are disabled for
project code, so the scalar kernels stay scalar. The `.clang-format` and `.clang-tidy` files are
copies of ClickHouse's.

Sanitizer build:

```sh
CXX=clang++-22 meson setup build-sanitize --buildtype=debug -Db_sanitize=address,undefined
meson compile -C build-sanitize
meson test -C build-sanitize --print-errorlogs
```

The tests compare every lookup against a reference map. They cover collisions, full groups,
wraparound, high key bits, duplicate updates, sizing limits, and empty tables.

## Measure

The benchmark covers 2^10, 2^12, ..., 2^28 keys, with 0%, 50%, and 100% hits. At 2^28 keys the
table takes 8 GiB and the query trace 2 GiB, and a full run takes about 45 minutes. Pin it to an
idle core on a quiet machine. Other processes share the L3 cache and memory bandwidth; on a busy
host, results for 8 MiB to 32 MiB tables varied by up to 54% between runs.

```sh
taskset -c 8 build/oa_bench \
    --benchmark_min_time=0.1s --benchmark_min_warmup_time=0.05 \
    --benchmark_repetitions=5 --benchmark_enable_random_interleaving=true \
    --benchmark_display_aggregates_only=true \
    --benchmark_out=sweep.json --benchmark_out_format=json
python3 tools/summarize.py sweep.json
python3 tools/plot.py docs/lookup-time.svg sweep.json
```

Each case builds its table, then times full passes over a shuffled query trace with one query per
distinct key. Timing covers hashing, probing, the value load, and summing the values. After timing,
the harness checks the hit count and the value checksum. Use `--benchmark_filter` to run a subset,
for example `--benchmark_filter='.*keys:268435456/'` for the largest tables.

Hardware counters come from a separate run with `OA_PMU=1`. It needs root or
`kernel.perf_event_paranoid` at 2 or lower. The counter syscalls run once per pass, so use the
counts from that run, not its times:

```sh
sudo env OA_PMU=1 taskset -c 8 build/oa_bench --benchmark_filter='/keys:268435456/' \
    --benchmark_min_time=0.1s --benchmark_out=counters.json --benchmark_out_format=json
python3 tools/summarize.py sweep.json --counters counters.json
```

Compare and cache-line counts come from replaying insertion and lookup on bare key arrays.
`--verify` first checks that the replay puts every key in the same slot as the real tables:

```sh
build/oa_probe_stats --verify 20
build/oa_probe_stats 28
```

## Repository layout

| Path | Contents |
| --- | --- |
| `src/hash_tables.hpp` | The two table layouts and all five lookups |
| `src/workload.hpp` | Key generation and shuffled query traces |
| `src/benchmark.cpp` | Google Benchmark harness |
| `src/perf_counters.hpp` | Optional Linux perf counters, enabled by `OA_PMU=1` |
| `tests/hash_tables_test.cpp` | Correctness tests against a reference map |
| `tools/probe_stats.cpp` | Compares and cache lines per lookup |
| `tools/summarize.py`, `tools/plot.py` | Tables and the chart from benchmark JSON |
| `docs/lookup-time.svg` | The chart above |

## License

[Apache License 2.0](LICENSE).
