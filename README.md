# simd-ht

This repository measures three lookups for an open-addressing hash table with 64-bit keys. It runs
them on one Arm Neoverse V2 core, for tables from 32 KiB to 8 GiB at load factor 0.5.

In an 8 GiB table, the SIMD grouped lookup takes 14.57 ns per miss and 15.30 ns per hit. Linear
probing takes 36.53 ns and 15.74 ns. Most SIMD lookups read one cache line. Per miss, the SIMD
lookup mispredicts 0.29 times and runs 28.97 instructions, against 1.42 and 28.50 for linear
probing. From 2 MiB to 8 GiB it has the lowest median time in every measured case.

| Lookup | Benchmark | Layout | Scan |
| --- | --- | --- | --- |
| Linear probing | `ScalarOA` | 16-byte key/value cells | One cell at a time, from the home cell |
| Cyclic scalar | `ScalarCyclicGroupedOA` | 64-byte groups of four keys | One lane at a time, from lane `hash & 3` |
| SIMD | `SIMDGroupedOA` | 64-byte groups of four keys | All four lanes with two NEON compares |

![Lookup time against table size for the three lookups](docs/lookup-time.svg)

Median nanoseconds per lookup in an 8 GiB table (2^28 keys):

| Lookup | All misses | Half hits | All hits |
| --- | ---: | ---: | ---: |
| Linear probing | 36.53 | 40.82 | 15.74 |
| Cyclic scalar | 26.87 | 26.39 | 16.08 |
| SIMD | 14.57 | 19.36 | 15.30 |

[RESULTS.md](RESULTS.md) has the results for all ten table sizes, the hardware counters, the probe
statistics, and the method.

## Table design

Keys and values are 64-bit integers, and key 0 marks an empty slot. The capacity is twice the key
count, rounded up to a power of two, so every benchmark runs at load factor 0.5. The table never
deletes or resizes. The hash XORs the two 32-bit halves of a key and then applies MurmurHash3's
`fmix32` finalizer.

The lookups use two memory layouts:

```text
Linear probing: 16-byte cells, four per 64-byte cache line
  | key | value | key | value | key | value | key | value |
  start at cell  hash & (capacity - 1),  then try the next cell

Grouped: one 64-byte group per cache line
  | key 0 | key 1 | key 2 | key 3 | value 0 | value 1 | value 2 | value 3 |
  start at group (hash & (capacity - 1)) / 4,  then try the next group
```

The cell or group where a lookup starts is the key's home. Both layouts use 16 bytes per slot, so
the same hash bits pick the same cache line in either layout.

A group's four slots are lanes 0 to 3. Insertion puts a key in the first empty lane of its home
group, or of the next group when the home group is full. The two grouped tables fill lanes in a
different order. The SIMD table fills lanes 0, 1, 2, 3. The cyclic table starts at lane `hash & 3`
and wraps around.

## Analysis

### Linear probing

At load factor 0.5, linear probing compares 1.5 keys per hit and 2.5 keys per miss on average. This
is Knuth's result. `tools/probe_stats.cpp` measures 1.50 and 2.50 on the benchmark's keys. The
averages hide two costs.

The number of compares varies from miss to miss:

| Linear-probing misses that stop at | 1st cell | 2nd | 3rd | 4th | 5th or later |
| --- | ---: | ---: | ---: | ---: | ---: |
| Share | 50.0% | 19.7% | 10.5% | 6.3% | 13.6% |

The core therefore cannot predict the branch that ends the loop. The hardware counters show 1.42
branch mispredictions per miss. Each misprediction discards the work the core had already started on
the lookups after it.

A run of occupied cells also crosses into the next cache line. A miss reads 1.375 lines on average,
and 28% of misses read two lines or more.

### Grouped layout

At load factor 0.5, 16.3% of groups are full. A miss stops at the first group with a free slot, so
83.7% of misses end in their home cache line. A hit reads 1.05 lines on average, against 1.125 for
linear probing.

### Cyclic scalar lookup

`ScalarCyclicGroupedOA` starts both insertion and lookup at lane `hash & 3` and wraps around the
group. Its first compare finds 75% of hits, the same share as linear probing. A hit compares 1.52
keys on average, and each probe reads one cache line. The lookup stops at the first empty lane,
because insertion filled the lanes in the same order and nothing is deleted.

An earlier scalar lookup scanned lanes 0 to 3 of the SIMD table. A key sits in whichever lane was
free when it was inserted. So that scan found only 43% of hits with its first compare, and it
compared 2.10 keys per hit. In the 8 GiB table it took 20.51 ns per hit, against 16.08 ns for the
cyclic scan. [RESULTS.md](RESULTS.md#earlier-designs) lists its other numbers.

### SIMD lookup

`SIMDGroupedOA` compares all four keys of a group at once. Clang 22 generates this NEON code for one
group:

```asm
ldp   q1, q2, [x15]          // load keys 0-1 and keys 2-3
cmeq  v1.2d, v1.2d, v0.2d    // keys 0-1 == search key: all ones or all zeros per lane
cmeq  v2.2d, v2.2d, v0.2d    // keys 2-3 == search key
uzp1  v1.4s, v1.4s, v2.4s    // keep one 32-bit half of each result
xtn   v1.4h, v1.4s           // narrow to one 16-bit field per key
fmov  x14, d1                // move the four fields to one general register
cbnz  x14, hit               // one branch for four keys
```

Two properties of the table keep the rest short:

1. A key occupies at most one slot. At most one 16-bit field is set, so one count of leading zeros
   finds the match. A match in lane `i` leaves `48 - 16 * i` leading zeros. Half of that count is the
   distance in bytes from `values[3]` back to `values[i]`. A hit loads its value with `clz`, `sub`,
   and `ldr`, plus one `add` that recomputes the group address.
2. The filled lanes always come first. Insertion takes the lowest empty lane, and nothing is
   deleted, so a group with any free slot has an empty lane 3. When no key matches, one scalar load
   of `keys[3]` decides whether the key is absent. That branch goes the same way in the 83.7% of
   groups that are not full.

A hit in the home group runs 29 instructions, counting the hash and the benchmark loop. A miss that
ends in the home group runs 26.

An earlier SIMD lookup made the same two compares. It then moved each 64-bit result to a general
register and rebuilt a 4-bit mask, which took eight instructions before the branch. It repeated that
work to look for an empty lane. A hit cost 34 instructions, and a miss in the home group cost 39. In
the 8 GiB table it took 22.69 ns per hit and 28.46 ns per miss.

On x86-64, the same source compiles to one 256-bit `vpcmpeqq`, one `vmovmskpd`, and the same lane 3
test. That build compiles, but it has not been run or measured.

### Hardware counters

Counts per lookup in the 8 GiB table:

| Lookup | Miss: instructions | Miss: cycles | Miss: branch misses | Hit: instructions | Hit: cycles | Hit: branch misses |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Linear probing | 28.50 | 100.6 | 1.42 | 24.75 | 43.7 | 0.57 |
| Cyclic scalar | 34.15 | 75.0 | 1.30 | 27.34 | 45.0 | 0.53 |
| SIMD | 28.97 | 40.5 | 0.29 | 29.69 | 43.1 | 0.05 |

The cyclic scalar and SIMD lookups read the same cache lines, because their tables hold the same
number of keys in each group. On a miss, the cyclic lookup mispredicts 1.30 times against 0.29. It
runs 34.15 instructions against 28.97, and it needs 75.0 cycles against 40.5.

The earlier SIMD lookup read the same lines and mispredicted about as often: 0.25 times per miss,
against 0.29. It ran 45.17 instructions and 79.5 cycles per miss. The current lookup runs 36% fewer
instructions per miss and needs 49% fewer cycles.

Interpretation: while one lookup waits for memory, the core starts the following lookups, up to a
fixed number of instructions ahead. Fewer instructions per lookup let more lookups start, so more
cache misses overlap. A misprediction discards the lookups started after it. The counters are
consistent with this model, but they do not prove it.

On hits, the SIMD lookup runs 20% more instructions than linear probing (29.69 against 24.75), and it
mispredicts a tenth as often. Both need about 43 cycles per hit.

### Results by table size

- 32 KiB and 128 KiB. The query trace holds only 1,024 or 4,096 keys and repeats on every pass, and
  the branch predictor learns it. At these sizes, linear probing mispredicts 0.001 to 0.003 times per
  miss, against 1.0 at 512 KiB. Without mispredictions and cache misses, the instruction count
  decides. Linear probing has the lowest time for hits (1.58 ns against 1.99 ns at 32 KiB) and for
  half hits. The SIMD lookup has the lowest time for misses (1.61 ns against 1.91 ns). These two
  sizes are a best case for linear probing.
- 512 KiB. The trace is too long to learn. Linear-probing misses take 11.17 ns, and SIMD misses take
  2.25 ns. With all hits, the three lookups are within run-to-run noise, between 2.41 and 2.48 ns.
- 2 MiB to 8 GiB. The SIMD lookup has the lowest median in every row. Its range of runs is separate
  from the other two ranges, except with all hits at 32 MiB. Against linear probing it is 2.1 to 2.8
  times faster on misses, and 1.4 to 2.1 times faster with half hits. With all hits, the difference
  falls from 1.7 times at 2 MiB to 2 to 3% at 128 MiB and above.

## Limitations

- One processor: Arm Neoverse V2 (AWS Graviton4), on four `m8g.4xlarge` instances, with Clang 22.1.8
  on Linux. Each core has 64 KiB of L1 data cache and 2 MiB of L2. The 36 MiB L3 is shared.
- One input distribution: distinct pseudo-random 64-bit keys, one fixed shuffle seed, and a load
  factor of exactly 0.5.
- Throughput of independent lookups on one thread. The results are not the latency of one isolated
  lookup.
- Insert-only tables. The lane 3 test and the cyclic early exit both rely on there being no deletion.
- The AVX2 code compiles, but it has not been run.

## Build and test

The build needs Clang with C++20, Meson, Ninja, CMake, clang-format, and clang-tidy. The scripts in
`tools/` need only Python 3. The build was tested with Clang 22.1.8. On first setup, Meson downloads
Google Benchmark and xsimd as pinned archives and checks their hashes.

```sh
CXX=clang++-22 meson setup build --buildtype=release
meson compile -C build
meson test -C build --print-errorlogs
meson compile -C build format-check tidy
```

Meson selects NEON on AArch64 and AVX2 on x86-64. Automatic vectorization is off for project code,
so the scalar lookups stay scalar. The `.clang-format` and `.clang-tidy` files are copies of
ClickHouse's.

Sanitizer build:

```sh
CXX=clang++-22 meson setup build-sanitize --buildtype=debug -Db_sanitize=address,undefined
meson compile -C build-sanitize
meson test -C build-sanitize --print-errorlogs
```

The tests check every lookup against a reference map. They cover collisions, full groups,
wraparound, high key bits, duplicate updates, sizing limits, and empty tables.

## Measurement

The benchmark covers 2^10, 2^12, ..., 2^28 keys, each with 0%, 50%, and 100% hits. At 2^28 keys the
table takes 8 GiB and the query trace 2 GiB. A full run takes about 45 minutes.

Run it on an idle core of a quiet machine. Other processes share the L3 cache and the memory
bandwidth. On a busy machine, results for 8 MiB to 32 MiB tables varied by up to 54% between runs.

```sh
taskset -c 8 build/oa_bench \
    --benchmark_min_time=0.1s --benchmark_min_warmup_time=0.05 \
    --benchmark_repetitions=5 --benchmark_enable_random_interleaving=true \
    --benchmark_display_aggregates_only=true \
    --benchmark_out=sweep.json --benchmark_out_format=json
python3 tools/summarize.py sweep.json
python3 tools/plot.py docs/lookup-time.svg sweep.json
```

Each case first builds its table. It then times full passes over a shuffled query trace with one
query per distinct key. The timing covers hashing, probing, loading the value, and summing the
values. After timing, the benchmark checks the hit count and the value checksum. To run a subset,
use `--benchmark_filter`, for example `--benchmark_filter='.*keys:268435456/'` for the largest
tables.

Hardware counters come from a separate run with `OA_PMU=1`. That run needs root, or
`kernel.perf_event_paranoid` set to 2 or lower. It reads the counters once per pass, which adds
time, so use its counts and not its times:

```sh
sudo env OA_PMU=1 taskset -c 8 build/oa_bench --benchmark_filter='/keys:268435456/' \
    --benchmark_min_time=0.1s --benchmark_out=counters.json --benchmark_out_format=json
python3 tools/summarize.py sweep.json --counters counters.json
```

`oa_probe_stats` counts compares and cache lines by replaying insertion and lookup on plain key
arrays. With `--verify`, it first checks that the replay puts every key in the same slot as the
real tables:

```sh
build/oa_probe_stats --verify 20
build/oa_probe_stats 28
```

## Repository layout

| Path | Contents |
| --- | --- |
| `src/hash_tables.hpp` | The two table layouts and the three lookups |
| `src/workload.hpp` | Key generation and shuffled query traces |
| `src/benchmark.cpp` | Google Benchmark harness |
| `src/perf_counters.hpp` | Optional Linux perf counters, turned on by `OA_PMU=1` |
| `tests/hash_tables_test.cpp` | Correctness tests against a reference map |
| `tools/probe_stats.cpp` | Compares and cache lines per lookup |
| `tools/summarize.py`, `tools/plot.py` | Tables and the chart from benchmark JSON |
| `docs/lookup-time.svg` | The chart above |

## License

[Apache License 2.0](LICENSE).
