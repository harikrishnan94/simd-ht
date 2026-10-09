# simd-ht

This page reports timed lookups on three hash tables. Each key and each value is a 64-bit integer.

The key is stored in the table, next to its value. A separate list of keys for one slot is called a bucket. This table has no buckets. This layout is called open addressing.

The runs used one core of the Arm Neoverse V2 design. Graviton4 is the name of that core in these runs.

Each table is half full. The share of filled slots is the load factor. Here the load factor is 0.5.

A KiB is 1,024 bytes. A MiB is 1,024 KiB. A GiB is 1,024 MiB. The smallest table is 32 KiB. The largest table is 8 GiB.

Three lookups are measured. The name in parentheses is the benchmark name.

- The table is a row of 16-byte cells. A cell holds one key and one value. The lookup starts at the cell the hash selects. It then tries each next cell. It stops when it finds the key or an empty cell. This is linear probing (`ScalarOA`).

- Keys and values sit in groups of 64 bytes. A group holds 4 keys, then 4 values. Each of those 4 positions is a lane, numbered 0 to 3. The scan starts at lane `hash & 3`. After the scan checks lane 3, it checks lane 0 next, in the same group. This is the cyclic grouped scan (`ScalarCyclicGroupedOA`).

SIMD means one instruction works on several keys at once. On this core those instructions are called NEON.

- The group has the same 64-byte shape. Insertion fills the lowest free lane. One branch then tests whether any of the 4 keys matched. This is the SIMD grouped lookup (`SIMDGroupedOA`).

## Results

These times are for the hash table of 8 GiB. That hash table holds 2^28 keys. The unit is nanoseconds per lookup. Each number is the median of 5 runs.

A hit means the key is in the table. A miss means the key is absent. The middle column is half hits and half misses.

| | All misses | 50% hits | All hits |
| --- | ---: | ---: | ---: |
| Linear probing (`ScalarOA`) | 36.53 | 40.82 | 15.74 |
| Cyclic grouped scan (`ScalarCyclicGroupedOA`) | 26.87 | 26.39 | 16.08 |
| SIMD grouped lookup (`SIMDGroupedOA`) | 14.57 | 19.36 | 15.30 |

![Lookup time vs table size](docs/lookup-time.svg)

The chart shows the same three lookups at every table size.

From 2 MiB up, the SIMD lookup is 2.1-2.8x faster than linear probing on misses. With 50% hits, it is 1.4-2.1x faster. When every lookup hits, SIMD is only 2-3% faster than linear probing from 128 MiB up.

A cache line is the 64-byte block the CPU loads from memory. Every hit reads about one cache line, in all three layouts. The all-hit times stay close for that reason.

[RESULTS.md](RESULTS.md) lists every table size. It records how many instructions the CPU ran. A cycle is one tick of the core's clock. It also records how many cycles those instructions took.

It also records how often the CPU guessed a branch wrong. Those records are the hardware counters. It also counts the keys each lookup compares. It counts the cache lines each lookup reads.

Those key and line counts are the probe statistics. That page also describes two earlier designs. One compares the lanes from lane 0, one key at a time. A mask is a few bits that record which lanes matched. The other design builds one bit of that mask per lane. The page records how the numbers were checked.

## Why the SIMD lookup is faster on misses

At load factor 0.5, a linear-probing miss compares 2.5 keys on average. The count varies a lot. 50% of misses stop at the first cell. 13.6% of misses need 5 or more cells.

The loop stops on the key or on an empty cell. The CPU often guesses wrong about when the loop will stop. A wrong guess is a branch misprediction. At 8 GiB, each miss has 1.42 branch mispredictions.

My first SIMD version removed most of those mispredictions. It had 0.25 mispredictions per miss. It was still slower than the cyclic grouped scan. That scan checks one key at a time.

A mask is a few bits that record which lanes matched. That version built a 4-bit mask from the compare results. Building it took 8 instructions per group. The lookup built a second mask to find an empty lane.

On an average miss, the current version runs 29 instructions. The first version runs 45. The table below shows the same counts as 28.97 and 45.17. The misprediction rate is about the same.

Branch misses in this table are mispredicted branches. Each column is per lookup.

| 8 GiB, all misses, per lookup | Branch misses | Instructions | Cycles | ns |
| --- | ---: | ---: | ---: | ---: |
| Linear probing (`ScalarOA`) | 1.42 | 28.50 | 100.6 | 36.53 |
| Cyclic grouped scan (`ScalarCyclicGroupedOA`) | 1.30 | 34.15 | 75.0 | 26.87 |
| First SIMD version, one mask per lane | 0.25 | 45.17 | 79.5 | 28.46 |
| SIMD grouped lookup (`SIMDGroupedOA`) | 0.29 | 28.97 | 40.5 | 14.57 |

My reading is that the lookups do not depend on each other. One lookup can be waiting on memory while the core starts later instructions. The core may start only a limited number of those instructions. That limit is the out-of-order window.

A cache miss is a load that does not find its data in the cache. Fewer instructions per lookup let more lookups fit in that window. More cache misses can then be waiting at once. A cache miss that has started, and has not finished, is a miss in flight.

Adding 3 unused `and` instructions to linear probing cost 12.8% at 8 GiB, with all hits. That fits this reading. I have not measured the number of misses in flight.

## How the SIMD probe works

The table never deletes a key. A table that only inserts is called insert-only. A key sits in at most one slot.

1) At most one lane can match. The lookup does not need a separate mask bit for each lane.

The first instruction compares keys 0 and 1. The second compares keys 2 and 3. Both write their results into one register. Each key uses 16 bits of that register.

`cbnz` branches when that register is not zero. One `cbnz` then tests all 4 keys.

Leading zeros are the zero bits before the first 1 bit. A match in lane 0 leaves 48 leading zeros. Each later lane leaves 16 fewer than the lane before it. For lane `i`, the code writes the count as `48 - 16 * i`.

The values sit in order, from value 0 through value 3. Each value occupies 8 bytes. The compare result uses 16 bits per key. Two leading zeros make one byte of distance. The code divides the whole count by 2.

The load starts at value 3. It moves back by that byte count. It arrives at value `i`.

2) Insertion fills the lowest free lane. The lanes therefore fill from lane 0 toward lane 3. If any lane is free, lane 3 is free as well. `keys[3]` is then 0.

A miss does one extra load of that key. That load reads a single key. 83.7% of groups are not full. The branch on `keys[3]` nearly always goes the same way.

The compiler is Clang. Version 22 generates these instructions for one group. In the listing, a general register is a normal integer register. `uzp1` keeps one 32-bit half of each compare result. `xtn` narrows that half to 16 bits.

```asm
ldp   q1, q2, [x15]          // load keys 0-1 and keys 2-3
cmeq  v1.2d, v1.2d, v0.2d    // keys 0-1 == search key: all ones or all zeros per lane
cmeq  v2.2d, v2.2d, v0.2d    // keys 2-3 == search key
uzp1  v1.4s, v1.4s, v2.4s    // keep one 32-bit half of each result
xtn   v1.4h, v1.4s           // narrow to one 16-bit field per key
fmov  x14, d1                // move the four fields to one general register
cbnz  x14, hit               // one branch for four keys
```

The home group is the first group the hash selects. A hit that stops there is 29 instructions in the compiled code. A miss that stops there is 26 instructions in the compiled code. Both counts include the hash and the benchmark loop.

These path counts are not the averages in the table above. Some lookups search past the home group. Those lookups run more instructions.

The code is in [`src/hash_tables.hpp`](src/hash_tables.hpp). A vector type holds several keys in one variable. The code uses only vector types that are built into Clang. It does not use a separate SIMD library.

x86-64 is the 64-bit instruction set for Intel and AMD processors. On x86-64, one instruction compares the keys in the group. Its name is `vpcmpeqq`. A second instruction moves that result into a normal integer register. Its name is `vmovmskpd`.

## Sapphire Rapids

A second run used one logical CPU of an Intel Xeon Platinum 8488C. That chip is Sapphire Rapids. The machines were eight AWS `m7i.4xlarge` instances in `ap-south-2a`. The date was 2026-10-09. The SIMD instructions on this CPU are AVX2.

The same three lookups ran again. The table below is the 8 GiB hash table, in nanoseconds per lookup. Each number is the median of 5 runs.

| | All misses | 50% hits | All hits |
| --- | ---: | ---: | ---: |
| Linear probing (`ScalarOA`) | 61.46 | 83.53 | 30.82 |
| Cyclic grouped scan (`ScalarCyclicGroupedOA`) | 58.54 | 67.98 | 29.39 |
| SIMD grouped lookup (`SIMDGroupedOA`) | 28.55 | 34.68 | 23.50 |

From 2 MiB up, the SIMD lookup is 2.1-2.8x faster than linear probing on misses. With 50% hits, it is 1.4-2.4x faster. With every lookup hitting, it is 1.2-1.8x faster.

At 32 KiB and at 128 KiB, with every lookup hitting, linear probing is faster.

[RESULTS.md](RESULTS.md#x86-64) lists every table size for this run, plus the hardware counters.

## Caveats

- The tables in [Results](#results) come from Graviton4. The compiler is Clang 22.1.8. The keys follow one distribution. The load factor is 0.5. The Sapphire Rapids times are in the section above.

- The benchmark times a stream of lookups that do not depend on each other. That rate is throughput. It does not time one lookup running alone. That single wait is latency.

- Below 2 MiB, the results are mixed.

- The branch predictor is the part of the core that guesses which way a branch will go. The query trace is the list of keys to look up. At 32 KiB and at 128 KiB, that predictor learns the list. Linear probing is then faster on hits.

- At 512 KiB, the time depends on where the loop sits in memory. Two builds use the same loop instructions. On misses, linear probing differs by 39% between those builds.

- The table is insert-only. The `keys[3]` test does not work if a key can be deleted.

- The x86-64 SIMD path is AVX2. It was run on eight `m7i.4xlarge` instances. The times are in [RESULTS.md](RESULTS.md#x86-64).

## Build

The code uses the C++20 language. Clang compiles it. Meson, Ninja, and CMake are the build tools. On AArch64, Meson selects NEON. On x86-64, Meson selects AVX2.

```sh
CXX=clang++-22 meson setup build --buildtype=release
meson compile -C build
meson test -C build
taskset -c 8 build/oa_bench --benchmark_min_time=0.1s --benchmark_repetitions=5 \
    --benchmark_enable_random_interleaving=true
```

The full run takes about 45 minutes. It needs memory for an 8 GiB table plus a 2 GiB query trace.

The exact commands for the published numbers are in [RESULTS.md](RESULTS.md#reproduce).

## License

The license is the [Apache License 2.0](LICENSE).
