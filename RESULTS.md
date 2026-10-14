# Results

This page is the full record behind the [README](README.md). It covers three open-addressing lookups of 64-bit keys, each run one key at a time and in batches (AMAC, asynchronous memory access chaining), on ARM and on x86.

The main run used four AWS `m8g.4xlarge` instances (ARM, Arm Neoverse V2 cores) and four `m7i.4xlarge` instances (x86, Intel Xeon Platinum 8488C), all in `ap-south-2a`. Both platforms ran the same source.

The designs are numbered as steps below. Steps 2 and 4 are not in the current source. Their numbers come from a second run, on four `m8g.4xlarge` instances, that measured five one-at-a-time designs. This page calls it the five-design run. Every table from that run says so, and designs are compared only inside one run.

The question behind all of it is how fast a large open-addressing table can answer a lookup. Linear probing, one lookup at a time, takes 39.79 ns on ARM and 65.14 ns on x86 to find that a key is absent in an 8 GiB table. Two changes bring that to 11.10 ns and 19.92 ns: groups of four keys per cache line, compared with one SIMD instruction, and 32 lookups in flight at once.

- [Setup](#setup-one-binary-per-platform-on-four-dedicated-instances-each)
- [The steps](#the-steps-simd-alone-cuts-a-miss-from-1087-to-405-cycles-amac-alone-to-365-both-to-309)
- [The explanation](#the-explanation-amac-hides-memory-waits-and-it-costs-instructions)
- [ARM, by table size](#arm-simd-with-amac-has-the-lowest-median-from-2-mib-up-and-amac-loses-at-512-kib-and-below)
- [x86, by table size](#x86-simd-with-amac-has-the-lowest-median-from-512-kib-up-and-amac-loses-at-128-kib-and-below)
- [How the results were checked](#every-run-checked-its-results-on-one-binary-per-platform)
- [Data](#data): counters, ring sizes, probe statistics, tuning experiments, benchmark names
- [Reproduce](#reproduce)

## Setup: one binary per platform on four dedicated instances each

### The input is random 64-bit keys at load factor 0.5, and the timer covers only the lookups

The keys and the values are 64-bit integers, and key 0 is reserved: a slot that holds key 0 is empty. The hash XORs the two 32-bit halves of the key, then applies MurmurHash3's final mix, `fmix32`.

A table for N keys has `2 * bit_ceil(N)` slots. `bit_ceil(N)` rounds N up to a power of two. N is always a power of two here, so the load factor is exactly 0.5. Each table holds `32 * N` bytes, never deletes a key and never resizes. N runs from 2^10 to 2^28, so the tables take 32 KiB to 8 GiB.

The stored keys are the integers 1 through N, passed through the splitmix64 finalizer. The finalizer is a bijection, so the keys are distinct, and none of them is 0. Absent keys are the integers N + 1 through 2N, passed through the same finalizer. The query trace is the list of keys to look up. It holds N keys, shuffled with a fixed seed, and 0%, 50% or 100% of them are in the table.

One timed pass looks up every key of the trace and sums the values it finds. The timer covers the hash, the probe, the load of the value and the sum, and for the AMAC lookups it also covers the ring. It does not cover building the table or shuffling the trace. After timing, the run checks the hit count and the checksum of the values, and it fails if either differs. The lookups do not depend on each other, so the result is throughput on one thread, not the latency of one lookup running alone.

Google Benchmark counts each lookup as one iteration. Each case warms up for 0.05 s, runs for at least 0.1 s, and runs five times. The runs of all cases go in one random order, and this page calls that "interleaved". The tables report the median. Automatic vectorization is off, so the scalar lookups stay scalar.

### ARM and x86 differ in cache sizes and in where the binary was built

| | ARM | x86 |
| --- | --- | --- |
| Instances | 4 x `m8g.4xlarge` | 4 x `m7i.4xlarge` |
| CPU | Arm Neoverse V2, 16 cores | Intel Xeon Platinum 8488C, 8 cores, 16 logical CPUs |
| Caches | 64 KiB L1d and 2 MiB L2 per core, 36 MiB L3 shared | 48 KiB L1d and 2 MiB L2 per core, 105 MiB L3 shared |
| Memory | 61.6 GiB | 61.8 GiB |
| OS | Ubuntu, Linux 7.0.0-1008-aws | Ubuntu, Linux 7.0.0-1004-aws |
| Compiler | Clang 22.1.8, `-O3`, `-fno-vectorize -fno-slp-vectorize`, NEON through Clang vector types | Same Clang release, same flags plus `-mavx2`, AVX2 through Clang vector types |
| Harness | Google Benchmark 1.9.4, one thread pinned to CPU 8 with `taskset` | Same |
| Binary | One `oa_bench`, built on the `m8g.24xlarge` development host and copied unchanged to all four instances. Its tests pass 4 of 4 (run for this page) | One `oa_bench`, built on instance 1 with the official Clang 22.1.8 release and copied unchanged to all four instances. Its tests passed 4 of 4 there |
| Source | Same source as x86 | Same source as ARM |
| Load average when the sweep started | 0.00 to 0.33 | 0.00 to 0.98 |

The five-design run and its binary are used for steps 2 and 4, for the small-table counters and for the comparison of the two runs. That run used four `m8g.4xlarge` instances for ARM and eight `m7i.4xlarge` instances for x86. It used Clang 22.1.8, Google Benchmark 1.9.4 and the vector library xsimd 14.3.0. I did not record `/etc/os-release` in the main run. The five-design run reported Ubuntu 26.04 on the same kernel versions.

An `m8g.4xlarge` or `m7i.4xlarge` is part of a larger processor, so other tenants can share its L3 cache and its memory bandwidth. Address-space randomization (ASLR) was on, and Google Benchmark warned about it. I did not turn it off.

On ARM, every core has one hardware thread. On x86, logical CPU 8 shares a core with another logical CPU. Both stayed online, and the benchmark ran only on CPU 8. I did not record which logical CPU is its sibling in the main run.

A kernel is one lookup design, run as one benchmark, such as `SIMDGroupedOA_AMAC32`. A row is one table size with one hit rate. Every kernel of a row ran on one instance, in one interleaved run. The rows were split across the four instances of each platform like this:

| Instance | Sweep (five runs per case) | Counter run (one run per case) |
| --- | --- | --- |
| 1 | 2^28 keys, 0% hits | 2^28 keys, 0% hits |
| 2 | 2^28 keys, 50% hits | 2^28 keys, 50% hits |
| 3 | 2^28 keys, 100% hits | 2^28 keys, 100% hits |
| 4 | 2^10 to 2^26 keys, every hit rate | 2^24 keys, every hit rate |

Each 2^24 and 2^28 row holds 15 kernels: three one-at-a-time lookups, three AMAC lookups with a ring of 32, and nine ring-size lookups (rings of 8, 16 and 64). The other rows hold the six kernels with no ring sweep. The sweep has 234 cases per platform.

Hardware counters come from a separate run of the same binary with `OA_PMU=1`. It takes one run per case and reads the counters once per pass. Reading them adds time, so I use the counts of that run and never its times. `kernel.perf_event_paranoid` was 2, so the counters cover the benchmark process in user space and leave out the kernel. Branch misses are Linux's generic `branch-misses` event.

### The two runs gave different one-at-a-time times

The same one-at-a-time code ran in both runs, on the same instance type, and the times differ. I do not know why. The instances are different virtual machines on shared hardware. The table shows `main / five-design - 1`, over the medians of each run.

| Platform | Rows | Linear probing | Cyclic scalar | SIMD |
| --- | --- | --- | --- | --- |
| ARM | 8 GiB, the 9 headline numbers | +8.9% (misses), -7.6% (50% hits), +0.6% (hits) | within 1.1% | within 1.1% |
| ARM | 2 MiB to 8 GiB, all 21 rows | -9.5% to +8.9% | -4.6% to +1.8% | -6.7% to +0.8% |
| x86 | 8 GiB, the 9 headline numbers | -5.7% to +6.0% | -5.5% to +6.5% | -4.1% to +11.6% |
| x86 | 2 MiB to 8 GiB, all 21 rows | -5.7% to +9.5% | -7.5% to +6.5% | -11.6% to +11.6% |

For example, ARM linear probing at 8 GiB took 36.53 ns on misses and 40.82 ns on half hits in the five-design run, and 39.79 ns and 37.73 ns in the main run. At 512 KiB the two runs differ more. ARM cyclic scalar took 9.68 ns on misses and 9.52 ns on half hits in the five-design run, and 7.66 ns and 4.62 ns in the main run. x86 SIMD took 2.87 ns on misses, against 4.82 ns in the main run.

At that size the position of the loop in the binary changes the time, as [the section on small tables](#small-tables-mispredict-almost-nothing-and-the-loops-address-moves-the-512-kib-times) shows. The two binaries place their loops at different addresses, and I did not repeat the address experiment on the main-run binary. Because of these differences I compare designs only inside one run, and the tables that use two runs say so.

## The steps: SIMD alone cuts a miss from 108.7 to 40.5 cycles, AMAC alone to 36.5, both to 30.9

At 8 GiB with every lookup missing, linear probing takes 39.79 ns per lookup on ARM, or 108.7 cycles. No single cause explains that. A probe reads 1.375 cache lines, it mispredicts 1.412 times, and each lookup waits for memory by itself. The eight designs below attack these costs one at a time. Each one is a numbered step that changes a single thing about an earlier step. Steps 1 to 5 change the layout, the probe order and the compare, and steps 6 to 8 add AMAC to step 1, step 3 and step 5.

Steps 1, 3 and 5 are in both runs, and steps 2 and 4 are only in the five-design run. The first table is the five-design run, with all five steps. The second is the main run, with the steps the current code has. Compare rows only inside one table.

Five-design run, ARM. 8 GiB table, every lookup missing. Cache lines and key compares come from replaying the lookups. The other counts come from a separate counter run. Times are nanoseconds per lookup, the median of 5 runs.

| Step | Design | Lines | Key compares | Branch mispredictions | Instructions | Cycles | ns |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | Linear probing | 1.375 | 2.50 | 1.42 | 28.50 | 100.6 | 36.53 |
| 2 | Four keys per line, scalar scan from lane 0 | 1.228 | 4.91 | 1.36 | 34.68 | 98.4 | 35.31 |
| 3 | Scalar scan from lane `hash & 3` | 1.228 | 2.56 | 1.30 | 34.15 | 75.0 | 26.87 |
| 4 | SIMD compare, one mask per lane | 1.228 | 4 per group | 0.25 | 45.17 | 79.5 | 28.46 |
| 5 | SIMD compare, one combined mask | 1.228 | 4 per group | 0.29 | 28.97 | 40.5 | 14.57 |

Main run, ARM. 8 GiB table, every lookup missing. Cache lines come from the replay. For steps 6 to 8 they are the lines of the design the ring runs, because the ring walks the same slots. Instructions, cycles and branch misses come from the counter run. Times are nanoseconds per lookup, the median of 5 runs.

| Step | Change | Layer | Cache lines | Instructions | Cycles | Branch misses | ns | Kept |
| --- | --- | --- | ---: | ---: | ---: | ---: | ---: | --- |
| 1 | Linear probing (`ScalarOA`) | Baseline | 1.375 | 28.50 | 108.7 | 1.412 | 39.79 | Yes |
| 3 | Four keys per cache line, scan from lane `hash & 3` (`ScalarCyclicGroupedOA`) | Memory layout, probe order | 1.228 | 34.15 | 75.1 | 1.293 | 26.98 | Yes |
| 5 | Step 3 with a SIMD compare and one combined mask (`SIMDGroupedOA`) | Branches, instructions | 1.228 | 28.97 | 40.5 | 0.284 | 14.57 | Yes |
| 6 | Step 1 with AMAC (`ScalarOA_AMAC32`) | Core pipeline | 1.375 | 50.38 | 36.5 | 1.618 | 13.02 | Yes |
| 7 | Step 3 with AMAC (`ScalarCyclicGroupedOA_AMAC32`) | Core pipeline | 1.228 | 54.69 | 31.9 | 1.190 | 11.44 | Yes |
| 8 | Step 5 with AMAC (`SIMDGroupedOA_AMAC32`) | Core pipeline | 1.228 | 47.02 | 30.9 | 0.323 | 11.10 | Yes |

Steps 2 and 4 were slower than the step they changed, so they were dropped. Steps 6 to 8 all cut cycles, and step 8 has the lowest time in the second table. The counts of the five-design run for hits and for the other sizes are in [Data](#data).

### Step 1: linear probing reads 1.375 lines and mispredicts 1.412 times per miss

```text
16-byte cells, four per 64-byte cache line
  | key | value | key | value | key | value | key | value |
  start at cell  hash & (capacity - 1),  then try the next cell
```

Step 1 is linear probing (`ScalarOA`), the baseline. The lookup starts at the cell the hash selects, its home cell, and tries each next cell. It stops at the key or at an empty cell. At load factor 0.5, a hit compares 1.5 keys on average and a miss compares 2.5 (Knuth, *The Art of Computer Programming*, Vol. 3, §6.4). On this benchmark's keys, `tools/probe_stats.cpp` measures 1.50 compares per hit and 2.50 per miss. The number of compares on a miss varies a lot:

| Linear-probing misses that stop at | 1st cell | 2nd | 3rd | 4th | 5th or later |
| --- | ---: | ---: | ---: | ---: | ---: |
| Share | 50.0% | 19.7% | 10.5% | 6.3% | 13.6% |

That variation makes the branch that ends the loop hard to predict. A run of filled cells can also carry a miss into a second cache line. A miss reads 1.375 cache lines on average, and 28% of misses read two or more. Linear probing mispredicts 1.412 times per miss (counters, main run, ARM, 8 GiB).

At that size a miss takes 108.7 cycles and a hit takes 44.0. A miss reads 22% more cache lines than a hit: 1.375 against 1.125. It also mispredicts 2.5 times as often: 1.412 against 0.571.

### Step 2: four keys per cache line cut lines per miss to 1.228 and made hits 30% slower

```text
One 64-byte group per cache line
  | key 0 | key 1 | key 2 | key 3 | value 0 | value 1 | value 2 | value 3 |
  start at group  (hash & (capacity - 1)) / 4,  then try the next group
```

A group holds four keys, then their four values. The group is 64 bytes and starts on a cache line. The four slots are called lanes, numbered 0 to 3. Both layouts use 16 bytes per slot, and the same hash bits select the slot, so a key's slot lands on the same cache line in both layouts.

The home group is the first group the hash selects. Insertion puts the key there when it has a free lane, and otherwise in the next group that has one. This removes the line crossings that step 1 pays for. At load factor 0.5, 16.3% of groups are full, so 83.7% of misses end on their home cache line. A miss reads 1.228 lines, against 1.375 for linear probing. A hit reads 1.050 lines, against 1.125.

Step 2 scans lanes 0, 1, 2 and 3 in that order. Insertion takes the lowest empty lane, so a key sits in whichever lane was free. The first compare finds only 43% of hits, where linear probing finds 75%. A hit compares 2.10 keys. A miss compares all four lanes before it checks for an empty lane, so it makes 4.91 compares on average.

In the five-design run (ARM, 8 GiB), misses were 3% faster than linear probing, at 35.31 ns. Hits were 30% slower, at 20.51 ns against 15.74 ns. A hit took 1.05 mispredictions. The step was dropped.

### Step 3: starting the scan at lane `hash & 3` cut compares per miss from 4.91 to 2.56

Step 2 compares four lanes on a miss and finds only 43% of hits on the first compare. Step 3 starts both insertion and lookup at lane `hash & 3`, and the scan wraps inside the group: after lane 3 comes lane 0. A scan that wraps like this is called cyclic, so this design is the cyclic scalar lookup, `ScalarCyclicGroupedOA`.

With this scan, the first compare finds 75% of hits, the same share as linear probing, and a hit compares 1.52 keys. Insertion fills the lanes in the order the lookup scans them, and the table never deletes a key. So the lookup stops at the first empty lane, and a miss compares 2.56 keys. The count still varies:

| Cyclic scalar misses that stop at | 1st compare | 2nd | 3rd | 4th | 5th or later |
| --- | ---: | ---: | ---: | ---: | ---: |
| Share | 50.0% | 19.4% | 9.6% | 4.6% | 16.3% |

Step 3 mispredicts 1.293 times per miss, where linear probing mispredicts 1.412 times (counters, main run, 8 GiB, ARM). In the five-design run, step 3 misses were 24% faster than step 2: 26.87 ns against 35.31 ns. Hits took 16.08 ns, 2% slower than linear probing.

### Step 4: a mask bit per lane cut mispredictions to 0.25 per miss and made misses 6% slower

Step 3 still mispredicts 1.30 times per miss, in the five-design run. Step 4 attacks the branches. Two 128-bit NEON compares test all four keys against the search key, and each compare sets every bit of a 64-bit lane, or none. The SIMD lookups use the table that step 2 builds. That table fills lanes from lane 0.

Step 4 moves each 64-bit compare result into a general-purpose register. `extr`, `and` and `bfi` build a 4-bit mask, with one bit per lane. They take 8 instructions, and they finish before the branch. The lookup then repeats the work, with two more compares, to look for an empty lane.

In the five-design run, mispredictions fell from 1.30 per miss to 0.25, and instructions rose from 34.15 to 45.17. A miss took 28.46 ns, against 26.87 ns for step 3. A hit took 22.69 ns, the slowest hit time of the five steps. The step was dropped.

### Step 5: one combined mask cut instructions per miss from 45.17 to 28.97

Step 4 spends 8 instructions per group on the mask and runs a second compare for the empty lane. In all, a miss runs 11 more instructions than in step 3. `SIMDGroupedOA` does the same two compares, and the rest of the lookup is shorter. Two properties of the table make that possible.

A key occupies at most one slot, so at most one of the four lanes can match, and the lookup does not need a separate bit per lane. It narrows both compare results into one 64-bit register. `uzp1` keeps one 32-bit half of each compare result, and `xtn` narrows that half to one 16-bit field per key. The lookup then branches once.

A match in lane 0 leaves 48 leading zeros, and each later lane leaves 16 fewer, so for lane `i` the count is `48 - 16 * i`. The values sit in order, 8 bytes each, and the compare result uses 16 bits per key. The code divides the leading-zero count by 2 and uses the result as a number of bytes. The load starts at value 3 and moves back by that many bytes, to value `i`. A hit uses `clz`, `sub` and `ldr`, plus one `add` that recomputes the group address.

The filled lanes also always come first. Insertion takes the lowest empty lane and nothing is deleted, so a group with any free slot has an empty lane 3. When no key matches, the lookup reads `keys[3]`, and a zero there means the search key is absent. That branch goes the same way in the 83.7% of groups that are not full.

Clang 22.1.8 generates this NEON code for one group (from the one-at-a-time SIMD loop in the ARM binary of the main run).

```asm
ldp   q1, q2, [x15]          // load keys 0-1 and keys 2-3
cmeq  v1.2d, v1.2d, v0.2d    // keys 0-1 == search key: all ones or all zeros per lane
cmeq  v2.2d, v2.2d, v0.2d    // keys 2-3 == search key
uzp1  v1.4s, v1.4s, v2.4s    // keep one 32-bit half of each result
xtn   v1.4h, v1.4s           // narrow to one 16-bit field per key
fmov  x14, d1                // move the four fields to one general register
cbnz  x14, hit               // one branch for four keys
```

The table below gives the instruction counts of the one-at-a-time lookup, from the disassembly of the timed loop. They include the hash and the benchmark loop. The step 4 column comes from the five-design binary, and I counted the step 5 column again in the ARM binary of the main run.

| Path | Step 4 | Step 5 |
| --- | ---: | ---: |
| Hit in the home group | 34 | 29 |
| Miss that ends in the home group | 39 | 26 |
| Each further full group | 27 | 13 |

In the five-design run, at 8 GiB, step 5 ran 28.97 instructions and 40.5 cycles per miss. That is 36% fewer instructions than step 4 and 49% fewer cycles, with the same cache lines read. It mispredicted 0.29 times per miss, against 0.25 for step 4. In the main run it also runs 28.97 instructions and 40.5 cycles. Its miss takes 14.57 ns, 2.73 times faster than linear probing's 39.79 ns.

The path lengths model the instruction count. A miss that ends in the home group runs 26 instructions, and each further full group adds 13. A miss reads 1.2284 groups, so the model gives 26 + 0.2284 x 13 = 28.97. The counter run measured 28.97.

On x86-64, one `vpcmpeqq` compares all 4 keys, and `vmovmskpd` moves the result to a general-purpose register. The lookup keeps the probe position as a byte offset, because x86 addressing cannot scale a group index by 64. In the x86 binary each further full group costs 9 instructions. On ARM it costs 13.

### Steps 6 to 8: AMAC cut cycles per miss to 36.5, 31.9 and 30.9, and raised instructions by 77%, 60% and 62%

Each miss in steps 1 to 5 still waits for memory alone. The core can start later lookups while it waits, but only up to its out-of-order window: a limit on how many later instructions it may start. At 8 GiB step 1 takes 108.7 cycles per miss and step 5 takes 40.5, so a large part of every miss is a wait that the window does not hide.

AMAC, asynchronous memory access chaining, hides it in software (Kocberber, Falsafi and Grot, "Asynchronous Memory Access Chaining", PVLDB 9(4):252-263, 2015, https://doi.org/10.14778/2856318.2856321). This code is adapted from the ClickHouse hash join (https://github.com/ClickHouse/ClickHouse/pull/115254, ring in https://github.com/ClickHouse/ClickHouse/pull/120515). That join uses AMAC with one dependent load per visit, so a visit reads one cell.

Here a ring of R lookups is in flight, and a visit to a ring slot reads one 64-byte cache line. The visit then either finishes the lookup, or prefetches the line of the next visit. When a lookup finishes, the slot takes the next key of the batch: it hashes the key and prefetches the key's first line. While one slot waits for its line, the visits to the other slots run, so their misses overlap. R is 32 on every table size, and rings of 8, 16 and 64 also run at 2^24 and 2^28 keys.

```text
ring of R slots, each holding   position (the line it reads next), key
sweep over the slots, one visit each:
    visit   read the line at the slot's position
            key found, or empty slot found -> result goes out, slot takes the next key, prefetch its first line
            otherwise                      -> position = next line, prefetch it
```

The visit is the same compare as the one-at-a-time lookup, one line at a time. In `ScalarOA_AMAC32` (step 6) a visit reads the rest of one cache line, from the cell it stands on to the next 64-byte boundary. A miss there makes 1.375 visits on average. In `ScalarCyclicGroupedOA_AMAC32` (step 7) a visit scans one group from lane `hash & 3`, and the ring also stores that lane in one byte per slot. In `SIMDGroupedOA_AMAC32` (step 8) a visit does the step 5 compare on one group.

The prefetch is `__builtin_prefetch(address, 0, 3)`, and on ARM Clang compiles it to `prfm pldl1keep`. Locality 1 would compile to `prfm pldl3keep` and bring the line only as far as L3. I did not time that variant. The table is read-only during a batch, so in-flight lookups cannot interact. It is also at most half full, so every walk ends at an empty slot or group.

On ARM at 8 GiB with every lookup missing, the times fall from 39.79 ns to 13.02 ns for linear probing, from 26.98 ns to 11.44 ns for cyclic scalar, and from 14.57 ns to 11.10 ns for SIMD. Cycles per lookup fall from 108.7 to 36.5, from 75.1 to 31.9, and from 40.5 to 30.9. Instructions per lookup rise from 28.50 to 50.38, from 34.15 to 54.69, and from 28.97 to 47.02. The second step table above shows the branch misses. On x86 the times fall from 65.14 ns to 27.26 ns, from 61.13 ns to 23.91 ns, and from 30.73 ns to 19.92 ns.

The loop of the SIMD lookup shows where the instructions go. The listing below is that loop while all 32 slots are busy, from the disassembly of the ARM binary (`llvm-objdump-22 -d --no-show-raw-insn -C build/oa_bench`). It has no calls. It issues one `prfm` for each lookup it starts and for each visit that moves on.

The hit count and the checksum stay in registers (`x22` and `x28`). Each ring slot has a position and a key (`x24` and `x27` point at the two arrays), and the ring does not store the key's index in the batch. A visit that moves on to the next group runs 21 instructions:

```asm
loop:
  lsl   x18, x15, #3               // slot s, as a byte offset
  ldr   x11, [x24, x18]            // ring.pos[s]: the group to read
  add   x18, x27, x18              // &ring.key[s]
  ld1r  {v1.2d}, [x18]             // broadcast the key
  ldp   q2, q0, [x11]              // keys 0-1 and 2-3, prefetched by an earlier visit
  cmeq  v2.2d, v2.2d, v1.2d
  cmeq  v1.2d, v0.2d, v1.2d
  uzp1  v1.4s, v2.4s, v1.4s
  xtn   v1.4h, v1.4s
  fmov  x18, d1
  cbz   x18, no_match              // one branch for four keys
  ...                              // hit: clz, sub, ldr, two adds (hit count, checksum), then the next key
no_match:
  mov   x18, v0.d[1]               // keys[3]
  cbz   x18, absent                // lane 3 empty: the key is absent, take the next key
  add   x11, x11, #0x40            // next group
  cmp   x11, x9                    // wrap at the end of the table
  csel  x11, x8, x11, eq
  str   x11, [x24, x15, lsl #3]    // ring.pos[s] = next group
  prfm  pldl1keep, [x11]           // prefetch it
  add   x15, x15, #0x1             // next slot
  cmp   x15, #0x20                 // 32 slots
  b.ne  loop
```

I counted the paths in the listing. A visit that moves on runs 21 instructions. A visit that finds the key runs 45. That count includes the value load, the hit count, the checksum and the start of the next lookup (hash, first line, prefetch). A visit that finds an empty lane 3 runs 42, with the same start of the next lookup. For comparison, a hit in the one-at-a-time SIMD lookup runs 29.

These path lengths model the instruction count of the ring, and the counters agree. A miss makes 1.2284 visits, so 0.2284 of them move on. The model then gives 42 + 0.2284 x 21 = 46.80 instructions per lookup. A hit makes 1.0497 visits, and the model gives 45 + 0.0497 x 21 = 46.04. The counter run measured 47.02 for all misses and 46.24 for all hits, so the model is 0.5% below the counters on both. The visit counts come from the replay of the lookups, and I have not found where the other 0.2 instructions per lookup go.

AMAC lost on small tables, and the ARM and x86 sections below show where. It adds instructions, and a small table has no memory waits to hide.

## The explanation: AMAC hides memory waits, and it costs instructions

Two readings explain the measurements, one for lookups made one at a time and one for AMAC. One at a time, the lookups do not depend on each other. While one waits for memory, the core executes later lookups and starts their loads, up to the out-of-order window. Fewer instructions per lookup then fit more lookups into the window. A mispredicted branch throws away the loads that later lookups already started. In this reading, that is why SIMD (steps 4 and 5) wins without AMAC.

A control experiment tests this reading by changing only the instruction count. It ran on the shared `m8g.24xlarge` development machine, with a load average of 0.93 at the start. I added `and` instructions to linear probing before the first load of a key. Nothing uses their results, they add no memory access and no branch, and nothing waits for them. Every query is a hit.

Each case ran five times, in a random interleave. The table shows the median nanoseconds per lookup and the change against linear probing. "Separate" means the ranges of the five runs do not overlap, and "overlap" means they do.

| Keys | Linear probing | One extra `and` | Three extra `and` | One: change | Three: change |
| --- | ---: | ---: | ---: | ---: | ---: |
| 2^10 | 1.578 | 1.659 | 1.855 | +0.082 ns (+5.17%), separate | +0.278 ns (+17.59%), separate |
| 2^16 | 5.670 | 5.629 | 5.987 | -0.041 ns (-0.72%), overlap | +0.317 ns (+5.60%), separate |
| 2^24 | 15.246 | 15.582 | 16.969 | +0.336 ns (+2.20%), separate | +1.723 ns (+11.30%), separate |
| 2^28 | 15.880 | 16.299 | 17.906 | +0.418 ns (+2.63%), separate | +2.026 ns (+12.76%), separate |

At 2^10 keys the table fits in the L1 cache, and the extra instructions add their own time there. At 2^16 keys one extra instruction made no measurable difference. At 2^24 and 2^28 keys the tables are far larger than the L3 cache, and three extra instructions still cost 11% to 13% per lookup: 12.76% at 2^28 keys. Instruction count matters for lookups made one at a time, as the window reading says.

AMAC does not rely on the window, because it issues the loads itself. The counters show a different trade. On ARM at 2^28 keys with every lookup missing, I measured that AMAC raises instructions per lookup by 77% for linear probing, 60% for cyclic scalar and 62% for SIMD. It cuts cycles by 66%, 58% and 24% (counters, main run). The time follows the overlap of memory waits, not the instruction count.

The ring size gives a second view of the same trade. ARM linear-probing misses at 2^28 keys take 20.40 ns with a ring of 8, 14.35 ns with 16, 13.02 ns with 32 and 13.11 ns with 64. More lookups in flight help, up to about 32.

Ring 32 is within 3.0% of the best ring in all 18 rows on ARM and within 1.9% on x86. A ring of 8 is 14% to 57% slower than the best on ARM and 24% to 50% on x86. On ARM, SIMD prefers a ring of 16 in four of six rows, by 0.9% to 2.8%. The ring-size tables are in [Data](#data).

The same measurements explain why SIMD's lead shrinks. On ARM the three designs finish a miss in 36.5, 31.9 and 30.9 cycles with AMAC, within 5.6 cycles of each other. The time of linear probing over the time of SIMD falls from 2.73 to 1.17 at 2^28 keys. I infer that the lookups there reach a limit set by memory, of about 31 to 37 cycles per lookup, and that the instructions of the probe stop mattering. I did not measure misses in flight, memory bandwidth or TLB misses, so this stays an inference.

On x86 the reading does not hold. The designs need 102.1, 88.8 and 74.7 cycles, 27 cycles apart, and the lead falls from 2.12 to 1.37.

AMAC loses where nothing waits. At 32 KiB and 128 KiB the table fits in L2, and linear probing mispredicts about 0.001 times per miss (counts from [the section on small tables](#small-tables-mispredict-almost-nothing-and-the-loops-address-moves-the-512-kib-times)). I infer that the extra instructions of AMAC show directly in the time there. The counters exist only at 2^24 and 2^28 keys, and on ARM they show 56% to 82% more instructions with AMAC.

At 512 KiB on ARM, SIMD with half hits takes 2.34 ns per key, and 7.60 ns with AMAC. I have no counters at that size. My guess is that the branch predictor learns the 16,384-key trace in the one-at-a-time loop, and not in the ring's interleaved order. I did not test it.

I did not measure lines or lookups in flight, memory bandwidth, TLB misses, or the AMAC counts at table sizes other than 2^24 and 2^28. Neither the explanation of the SIMD lead nor the memory limit on ARM is proven.

## ARM: SIMD with AMAC has the lowest median from 2 MiB up, and AMAC loses at 512 KiB and below

Which of the six lookups is fastest depends on the table size and the hit rate. The tables show the median nanoseconds per lookup over five runs, on ARM (`m8g.4xlarge`), main run. Bold marks the lowest median in the row. The sizes are grouped by the cache level the table fits in. The ARM L2 is 2 MiB and the L3 is 36 MiB.

### 32 KiB to 512 KiB: one key at a time is faster, and AMAC loses

| Keys | Table | Hits | Linear probing | Cyclic scalar | SIMD | Linear probing, AMAC | Cyclic scalar, AMAC | SIMD, AMAC |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 2^10 | 32 KiB | 0% | 1.91 | 2.38 | **1.61** | 2.59 | 2.84 | 2.42 |
| 2^10 | 32 KiB | 50% | **1.71** | 2.09 | 1.85 | 2.47 | 3.00 | 2.50 |
| 2^10 | 32 KiB | 100% | **1.57** | 1.80 | 1.98 | 2.12 | 2.92 | 2.59 |
| 2^12 | 128 KiB | 0% | 2.17 | 2.60 | **1.75** | 2.70 | 2.97 | 2.51 |
| 2^12 | 128 KiB | 50% | **1.89** | 2.23 | 1.91 | 2.52 | 2.88 | 2.51 |
| 2^12 | 128 KiB | 100% | **1.68** | 1.88 | 2.11 | 2.14 | 2.83 | 2.57 |
| 2^14 | 512 KiB | 0% | 11.61 | 7.66 | **2.42** | 9.26 | 8.61 | 3.98 |
| 2^14 | 512 KiB | 50% | 6.79 | 4.62 | **2.34** | 7.89 | 8.98 | 7.60 |
| 2^14 | 512 KiB | 100% | 2.80 | **2.56** | 2.63 | 3.55 | 4.35 | 2.59 |

At 32 KiB and 128 KiB, the one-at-a-time lookups win every row. The fastest AMAC lookup takes 1.27 to 1.50 times as long as the fastest one-at-a-time lookup (last column of the factor table below). The query trace holds only 1,024 or 4,096 keys, and it repeats on every pass, so the branch predictor learns it. At these sizes, linear probing mispredicts about 0.001 times per miss. The bytes a lookup needs are already in a cache, so the instruction count decides the time. Linear probing is fastest on hits and on half hits, and SIMD is fastest on misses.

At 512 KiB the trace has 16,384 keys, and the branch predictor learns part of it. SIMD without AMAC wins misses and half hits: 2.42 ns and 2.34 ns, against 3.98 ns and 7.60 ns for SIMD with AMAC. With every lookup hitting, the lowest median is cyclic scalar at 2.56 ns, and its range overlaps the ranges of SIMD with AMAC, SIMD and linear probing. How much the predictor learns depends on where the loop sits in memory, as [the section on small tables](#small-tables-mispredict-almost-nothing-and-the-loops-address-moves-the-512-kib-times) shows.

### 2 MiB to 32 MiB: AMAC with SIMD wins, by a small margin at 2 MiB

| Keys | Table | Hits | Linear probing | Cyclic scalar | SIMD | Linear probing, AMAC | Cyclic scalar, AMAC | SIMD, AMAC |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 2^16 | 2 MiB | 0% | 13.43 | 11.67 | 4.80 | 11.80 | 9.93 | **4.78** |
| 2^16 | 2 MiB | 50% | 13.24 | 12.38 | 9.79 | 11.23 | 10.49 | **9.25** |
| 2^16 | 2 MiB | 100% | 5.64 | 5.53 | 3.35 | 6.13 | 5.93 | **3.07** |
| 2^18 | 8 MiB | 0% | 17.05 | 14.21 | 6.00 | 12.01 | 10.23 | **4.84** |
| 2^18 | 8 MiB | 50% | 16.97 | 14.46 | 11.31 | 11.55 | 10.79 | **9.53** |
| 2^18 | 8 MiB | 100% | 7.50 | 7.25 | 4.65 | 6.26 | 6.17 | **3.30** |
| 2^20 | 32 MiB | 0% | 20.91 | 17.05 | 9.04 | 12.13 | 10.30 | **5.28** |
| 2^20 | 32 MiB | 50% | 21.17 | 17.10 | 13.27 | 11.66 | 10.89 | **9.79** |
| 2^20 | 32 MiB | 100% | 9.73 | 9.82 | 9.01 | 6.50 | 6.36 | **4.12** |

SIMD with AMAC has the lowest median in all nine rows. At 2 MiB with every lookup missing, its range overlaps SIMD without AMAC (4.78 ns against 4.80 ns), so I do not name a winner there. In the other eight rows its range overlaps no other range.

### 128 MiB to 8 GiB: AMAC with SIMD wins every row, and AMAC cuts linear probing to a third

| Keys | Table | Hits | Linear probing | Cyclic scalar | SIMD | Linear probing, AMAC | Cyclic scalar, AMAC | SIMD, AMAC |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 2^22 | 128 MiB | 0% | 29.32 | 23.07 | 13.04 | 12.20 | 10.39 | **9.30** |
| 2^22 | 128 MiB | 50% | 30.25 | 22.65 | 17.16 | 11.72 | 10.94 | **10.07** |
| 2^22 | 128 MiB | 100% | 14.23 | 14.35 | 13.67 | 9.33 | 9.39 | **8.96** |
| 2^24 | 512 MiB | 0% | 35.79 | 25.19 | 13.89 | 12.39 | 10.73 | **10.57** |
| 2^24 | 512 MiB | 50% | 35.65 | 24.66 | 18.41 | 11.96 | 11.45 | **10.79** |
| 2^24 | 512 MiB | 100% | 15.00 | 15.37 | 14.67 | 10.68 | 10.84 | **10.29** |
| 2^26 | 2 GiB | 0% | 38.87 | 25.96 | 14.06 | 12.78 | 11.27 | **10.91** |
| 2^26 | 2 GiB | 50% | 37.12 | 25.14 | 18.67 | 12.43 | 11.85 | **11.04** |
| 2^26 | 2 GiB | 100% | 15.14 | 15.61 | 14.81 | 11.01 | 11.17 | **10.56** |
| 2^28 | 8 GiB | 0% | 39.79 | 26.98 | 14.57 | 13.02 | 11.44 | **11.10** |
| 2^28 | 8 GiB | 50% | 37.73 | 26.11 | 19.18 | 12.65 | 11.97 | **11.26** |
| 2^28 | 8 GiB | 100% | 15.84 | 16.21 | 15.38 | 11.29 | 11.46 | **10.86** |

SIMD with AMAC has the lowest median in all twelve rows, and its range overlaps no other range. The next fastest is cyclic scalar with AMAC on misses and half hits (its median is 1.5% to 11.7% above the lowest), and linear probing with AMAC on hits (3.8% to 4.2% above).

### The factors behind the ARM tables

Each factor is a quotient of two medians in the tables above. AMAC gain is the one-at-a-time time over the time with AMAC, so a value below 1 means AMAC is slower. SIMD lead is the time of linear probing over the time of SIMD. SIMD AMAC over linear probing is the one-at-a-time time of linear probing over the time of SIMD with AMAC. The last column is the best AMAC time over the best one-at-a-time time, so a value above 1 means AMAC loses the row.

| Keys | Table | Hits | AMAC gain, linear probing | AMAC gain, cyclic scalar | AMAC gain, SIMD | SIMD lead, one at a time | SIMD lead, AMAC | SIMD AMAC over linear probing | Best AMAC over best one at a time |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 2^10 | 32 KiB | 0% | 0.74 | 0.84 | 0.67 | 1.18 | 1.07 | 0.79 | 1.50 |
| 2^10 | 32 KiB | 50% | 0.69 | 0.70 | 0.74 | 0.92 | 0.99 | 0.68 | 1.44 |
| 2^10 | 32 KiB | 100% | 0.74 | 0.62 | 0.77 | 0.79 | 0.82 | 0.61 | 1.35 |
| 2^12 | 128 KiB | 0% | 0.81 | 0.87 | 0.70 | 1.24 | 1.07 | 0.86 | 1.44 |
| 2^12 | 128 KiB | 50% | 0.75 | 0.77 | 0.76 | 0.99 | 1.00 | 0.75 | 1.33 |
| 2^12 | 128 KiB | 100% | 0.79 | 0.67 | 0.82 | 0.80 | 0.83 | 0.65 | 1.27 |
| 2^14 | 512 KiB | 0% | 1.25 | 0.89 | 0.61 | 4.80 | 2.33 | 2.92 | 1.65 |
| 2^14 | 512 KiB | 50% | 0.86 | 0.52 | 0.31 | 2.91 | 1.04 | 0.89 | 3.25 |
| 2^14 | 512 KiB | 100% | 0.79 | 0.59 | 1.02 | 1.07 | 1.37 | 1.08 | 1.01 |
| 2^16 | 2 MiB | 0% | 1.14 | 1.17 | 1.00 | 2.80 | 2.47 | 2.81 | 1.00 |
| 2^16 | 2 MiB | 50% | 1.18 | 1.18 | 1.06 | 1.35 | 1.21 | 1.43 | 0.95 |
| 2^16 | 2 MiB | 100% | 0.92 | 0.93 | 1.09 | 1.68 | 2.00 | 1.84 | 0.92 |
| 2^18 | 8 MiB | 0% | 1.42 | 1.39 | 1.24 | 2.84 | 2.48 | 3.52 | 0.81 |
| 2^18 | 8 MiB | 50% | 1.47 | 1.34 | 1.19 | 1.50 | 1.21 | 1.78 | 0.84 |
| 2^18 | 8 MiB | 100% | 1.20 | 1.18 | 1.41 | 1.61 | 1.90 | 2.28 | 0.71 |
| 2^20 | 32 MiB | 0% | 1.72 | 1.66 | 1.71 | 2.31 | 2.30 | 3.96 | 0.58 |
| 2^20 | 32 MiB | 50% | 1.82 | 1.57 | 1.36 | 1.60 | 1.19 | 2.16 | 0.74 |
| 2^20 | 32 MiB | 100% | 1.50 | 1.54 | 2.19 | 1.08 | 1.58 | 2.37 | 0.46 |
| 2^22 | 128 MiB | 0% | 2.40 | 2.22 | 1.40 | 2.25 | 1.31 | 3.15 | 0.71 |
| 2^22 | 128 MiB | 50% | 2.58 | 2.07 | 1.70 | 1.76 | 1.16 | 3.01 | 0.59 |
| 2^22 | 128 MiB | 100% | 1.52 | 1.53 | 1.53 | 1.04 | 1.04 | 1.59 | 0.66 |
| 2^24 | 512 MiB | 0% | 2.89 | 2.35 | 1.31 | 2.58 | 1.17 | 3.39 | 0.76 |
| 2^24 | 512 MiB | 50% | 2.98 | 2.15 | 1.71 | 1.94 | 1.11 | 3.30 | 0.59 |
| 2^24 | 512 MiB | 100% | 1.40 | 1.42 | 1.43 | 1.02 | 1.04 | 1.46 | 0.70 |
| 2^26 | 2 GiB | 0% | 3.04 | 2.30 | 1.29 | 2.76 | 1.17 | 3.56 | 0.78 |
| 2^26 | 2 GiB | 50% | 2.99 | 2.12 | 1.69 | 1.99 | 1.13 | 3.36 | 0.59 |
| 2^26 | 2 GiB | 100% | 1.38 | 1.40 | 1.40 | 1.02 | 1.04 | 1.43 | 0.71 |
| 2^28 | 8 GiB | 0% | 3.06 | 2.36 | 1.31 | 2.73 | 1.17 | 3.59 | 0.76 |
| 2^28 | 8 GiB | 50% | 2.98 | 2.18 | 1.70 | 1.97 | 1.12 | 3.35 | 0.59 |
| 2^28 | 8 GiB | 100% | 1.40 | 1.41 | 1.42 | 1.03 | 1.04 | 1.46 | 0.71 |

At 8 GiB the AMAC gain on misses is 3.06 for linear probing, 2.36 for cyclic scalar and 1.31 for SIMD. SIMD's lead over linear probing on misses is 2.73 one at a time and 1.17 with AMAC. With every lookup hitting, the lead is 1.03 one at a time and 1.04 with AMAC.

### Small tables mispredict almost nothing, and the loop's address moves the 512 KiB times

This section's counters and times come from the shared `m8g.24xlarge` development machine, with a load average of 12 to 14. The process was pinned to one core. The counts are branch misses per linear-probing miss, given as the range over five runs. `OA_PMU=1` turned the counters on.

The tables compare two builds of the same loop instructions. One is the five-design binary, and the other is a build of the source that uses the vector types of Clang. The loops sit at different addresses.

| Build | 32 KiB | 128 KiB | 512 KiB | 2 MiB |
| --- | ---: | ---: | ---: | ---: |
| Five-design binary | 0.0011 to 0.0012 | 0.0007 to 0.0015 | 0.90 to 0.97 | 1.26 to 1.30 |
| Rebuilt from source | 0.0011 to 0.0012 | 0.0004 to 0.0005 | 0.46 to 0.55 | 1.22 to 1.26 |

At 32 KiB and 128 KiB both builds almost never mispredict. At 2 MiB both mispredict more than once per miss. At 512 KiB the rebuilt binary mispredicts about half as often. The miss times move with the branch misses. The next table gives the median nanoseconds per miss, over five interleaved runs on that machine.

| Table | Build | Linear probing | Cyclic scalar | SIMD |
| --- | --- | ---: | ---: | ---: |
| 512 KiB | Five-design binary | 11.32 | 9.73 | 2.16 |
| 512 KiB | Rebuilt from source | 6.95 | 7.77 | 2.31 |
| 2 MiB | Five-design binary | 14.71 | 13.29 | 5.44 |
| 2 MiB | Rebuilt from source | 14.69 | 13.28 | 5.42 |

At 512 KiB linear probing differs by 39% between the builds, and SIMD by about 7%. SIMD misses rarely mispredict. At 2 MiB the two builds agree within 0.4%. The five-design binary's times on this machine match the dedicated instances: linear probing is 11.32 ns here against 11.17 ns there, and SIMD is 2.16 ns against 2.25 ns.

### ARM run-to-run variation stays below 8.3%

Variation is the coefficient of variation of the five times: their standard deviation divided by their mean, as a percent. These are the cases above 5%, from the output of `tools/summarize.py`:

Maximum coefficient of variation: 8.27%.
- SIMDGroupedOA, 2^20 keys, 100% hits: 8.27%
- ScalarOA, 2^14 keys, 100% hits: 7.62%
- ScalarCyclicGroupedOA, 2^20 keys, 100% hits: 7.51%
- SIMDGroupedOA, 2^20 keys, 0% hits: 7.09%
- ScalarOA_AMAC32, 2^14 keys, 100% hits: 6.38%
- SIMDGroupedOA_AMAC32, 2^20 keys, 100% hits: 6.27%
- SIMDGroupedOA, 2^14 keys, 100% hits: 5.25%
- SIMDGroupedOA, 2^14 keys, 50% hits: 5.21%
- ScalarCyclicGroupedOA, 2^14 keys, 100% hits: 5.06%

The largest variation is 8.27%, for SIMD at 32 MiB with all hits. Nine cases exceed 5%, and all nine are at 512 KiB or at 32 MiB. At 32 MiB, the range of the lowest median still overlaps no other range, in all three rows. At 512 KiB with all hits the ranges overlap, as the 512 KiB rows above show.

## x86: SIMD with AMAC has the lowest median from 512 KiB up, and AMAC loses at 128 KiB and below

The x86 tables use the same layout as the ARM tables, and the setup is in the table of machines above. This is the main run on four `m7i.4xlarge` instances (Intel Xeon Platinum 8488C). The x86 L2 is 2 MiB and the L3 is 105 MiB.

AVX2 is the x86 SIMD path. Its lookup compares a group with one 256-bit compare, the instruction `vpcmpeqq`, and `vmovmskpd` moves the compare result to a general-purpose register. The lookup tests lane 3 the same way as the NEON lookup, and it keeps the probe position as a byte offset. The AMAC lookups use the same compare for each visit.

### 32 KiB to 512 KiB: one key at a time wins up to 128 KiB

| Keys | Table | Hits | Linear probing | Cyclic scalar | SIMD | Linear probing, AMAC | Cyclic scalar, AMAC | SIMD, AMAC |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 2^10 | 32 KiB | 0% | 2.02 | 3.09 | **1.88** | 2.72 | 3.02 | 2.48 |
| 2^10 | 32 KiB | 50% | 1.79 | 2.53 | **1.78** | 2.61 | 2.93 | 2.31 |
| 2^10 | 32 KiB | 100% | **1.60** | 1.95 | 1.78 | 2.30 | 2.83 | 2.20 |
| 2^12 | 128 KiB | 0% | 4.86 | 3.56 | 2.80 | 4.37 | 4.34 | **2.79** |
| 2^12 | 128 KiB | 50% | 2.19 | 2.89 | **2.14** | 3.98 | 2.96 | 2.31 |
| 2^12 | 128 KiB | 100% | **1.82** | 2.20 | 2.04 | 2.29 | 2.80 | 2.20 |
| 2^14 | 512 KiB | 0% | 13.68 | 13.46 | 4.82 | 11.51 | 10.32 | **4.21** |
| 2^14 | 512 KiB | 50% | 11.10 | 12.78 | 9.78 | 10.92 | 10.00 | **7.92** |
| 2^14 | 512 KiB | 100% | 5.73 | 3.16 | 2.41 | 5.25 | 5.02 | **2.32** |

At 32 KiB and 128 KiB one-at-a-time lookups win five of the six rows. In those five rows, the fastest AMAC lookup takes 1.08 to 1.38 times as long as the fastest one-at-a-time lookup. In the sixth row, 128 KiB with every lookup missing, SIMD takes 2.80 ns and SIMD with AMAC takes 2.79 ns, and their ranges overlap. At 512 KiB SIMD with AMAC has the lowest median in all three rows. With every lookup hitting, its range overlaps SIMD without AMAC (2.32 ns against 2.41 ns).

### 2 MiB to 32 MiB: AMAC with SIMD wins every row

| Keys | Table | Hits | Linear probing | Cyclic scalar | SIMD | Linear probing, AMAC | Cyclic scalar, AMAC | SIMD, AMAC |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 2^16 | 2 MiB | 0% | 16.91 | 18.21 | 6.32 | 13.54 | 12.14 | **5.10** |
| 2^16 | 2 MiB | 50% | 18.19 | 19.02 | 13.20 | 12.55 | 12.18 | **9.20** |
| 2^16 | 2 MiB | 100% | 7.90 | 8.00 | 4.10 | 7.05 | 6.73 | **2.64** |
| 2^18 | 8 MiB | 0% | 23.79 | 23.64 | 8.61 | 13.81 | 12.64 | **5.39** |
| 2^18 | 8 MiB | 50% | 26.51 | 25.28 | 15.57 | 12.84 | 12.57 | **9.54** |
| 2^18 | 8 MiB | 100% | 10.69 | 10.76 | 5.79 | 7.36 | 7.11 | **3.62** |
| 2^20 | 32 MiB | 0% | 26.44 | 26.26 | 10.06 | 13.99 | 12.81 | **5.72** |
| 2^20 | 32 MiB | 50% | 29.57 | 27.67 | 16.79 | 13.01 | 12.81 | **9.65** |
| 2^20 | 32 MiB | 100% | 12.30 | 12.20 | 6.87 | 7.65 | 7.45 | **4.37** |

SIMD with AMAC has the lowest median in all nine rows, and its range overlaps no other range.

### 128 MiB to 8 GiB: AMAC with SIMD wins every row, at 8 GiB 3.27 times faster than linear probing on misses and 4.31 times on half hits

| Keys | Table | Hits | Linear probing | Cyclic scalar | SIMD | Linear probing, AMAC | Cyclic scalar, AMAC | SIMD, AMAC |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 2^22 | 128 MiB | 0% | 40.48 | 41.62 | 18.17 | 16.70 | 15.09 | **10.32** |
| 2^22 | 128 MiB | 50% | 47.15 | 42.88 | 24.16 | 15.90 | 15.19 | **11.16** |
| 2^22 | 128 MiB | 100% | 20.01 | 19.75 | 15.09 | 11.46 | 10.95 | **9.75** |
| 2^24 | 512 MiB | 0% | 55.10 | 52.31 | 24.59 | 22.05 | 19.47 | **15.15** |
| 2^24 | 512 MiB | 50% | 66.69 | 56.40 | 29.47 | 20.98 | 19.45 | **14.86** |
| 2^24 | 512 MiB | 100% | 26.48 | 25.53 | 20.05 | 15.96 | 15.89 | **13.90** |
| 2^26 | 2 GiB | 0% | 61.51 | 55.92 | 27.66 | 24.87 | 22.03 | **18.04** |
| 2^26 | 2 GiB | 50% | 72.78 | 60.21 | 31.86 | 23.87 | 22.09 | **17.51** |
| 2^26 | 2 GiB | 100% | 28.76 | 27.59 | 22.26 | 18.53 | 18.38 | **16.40** |
| 2^28 | 8 GiB | 0% | 65.14 | 61.13 | 30.73 | 27.26 | 23.91 | **19.92** |
| 2^28 | 8 GiB | 50% | 78.75 | 64.24 | 33.27 | 25.00 | 22.96 | **18.28** |
| 2^28 | 8 GiB | 100% | 31.89 | 31.29 | 26.23 | 20.73 | 20.49 | **18.57** |

SIMD with AMAC has the lowest median in all twelve rows, and its range overlaps no other range. The next fastest is cyclic scalar with AMAC in all twelve rows (its median is 10.4% to 46.2% above the lowest).

### The factors behind the x86 tables

The columns are the same as in the ARM section.

| Keys | Table | Hits | AMAC gain, linear probing | AMAC gain, cyclic scalar | AMAC gain, SIMD | SIMD lead, one at a time | SIMD lead, AMAC | SIMD AMAC over linear probing | Best AMAC over best one at a time |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 2^10 | 32 KiB | 0% | 0.74 | 1.02 | 0.76 | 1.08 | 1.10 | 0.82 | 1.32 |
| 2^10 | 32 KiB | 50% | 0.69 | 0.86 | 0.77 | 1.01 | 1.13 | 0.78 | 1.30 |
| 2^10 | 32 KiB | 100% | 0.69 | 0.69 | 0.81 | 0.90 | 1.05 | 0.73 | 1.38 |
| 2^12 | 128 KiB | 0% | 1.11 | 0.82 | 1.01 | 1.73 | 1.57 | 1.74 | 0.99 |
| 2^12 | 128 KiB | 50% | 0.55 | 0.98 | 0.92 | 1.02 | 1.72 | 0.95 | 1.08 |
| 2^12 | 128 KiB | 100% | 0.80 | 0.79 | 0.93 | 0.89 | 1.04 | 0.83 | 1.21 |
| 2^14 | 512 KiB | 0% | 1.19 | 1.30 | 1.15 | 2.84 | 2.74 | 3.25 | 0.87 |
| 2^14 | 512 KiB | 50% | 1.02 | 1.28 | 1.24 | 1.13 | 1.38 | 1.40 | 0.81 |
| 2^14 | 512 KiB | 100% | 1.09 | 0.63 | 1.04 | 2.38 | 2.27 | 2.47 | 0.96 |
| 2^16 | 2 MiB | 0% | 1.25 | 1.50 | 1.24 | 2.68 | 2.65 | 3.32 | 0.81 |
| 2^16 | 2 MiB | 50% | 1.45 | 1.56 | 1.44 | 1.38 | 1.36 | 1.98 | 0.70 |
| 2^16 | 2 MiB | 100% | 1.12 | 1.19 | 1.55 | 1.93 | 2.67 | 3.00 | 0.64 |
| 2^18 | 8 MiB | 0% | 1.72 | 1.87 | 1.60 | 2.76 | 2.56 | 4.41 | 0.63 |
| 2^18 | 8 MiB | 50% | 2.07 | 2.01 | 1.63 | 1.70 | 1.35 | 2.78 | 0.61 |
| 2^18 | 8 MiB | 100% | 1.45 | 1.51 | 1.60 | 1.85 | 2.03 | 2.95 | 0.63 |
| 2^20 | 32 MiB | 0% | 1.89 | 2.05 | 1.76 | 2.63 | 2.45 | 4.62 | 0.57 |
| 2^20 | 32 MiB | 50% | 2.27 | 2.16 | 1.74 | 1.76 | 1.35 | 3.06 | 0.57 |
| 2^20 | 32 MiB | 100% | 1.61 | 1.64 | 1.57 | 1.79 | 1.75 | 2.81 | 0.64 |
| 2^22 | 128 MiB | 0% | 2.42 | 2.76 | 1.76 | 2.23 | 1.62 | 3.92 | 0.57 |
| 2^22 | 128 MiB | 50% | 2.97 | 2.82 | 2.17 | 1.95 | 1.42 | 4.23 | 0.46 |
| 2^22 | 128 MiB | 100% | 1.75 | 1.80 | 1.55 | 1.33 | 1.18 | 2.05 | 0.65 |
| 2^24 | 512 MiB | 0% | 2.50 | 2.69 | 1.62 | 2.24 | 1.46 | 3.64 | 0.62 |
| 2^24 | 512 MiB | 50% | 3.18 | 2.90 | 1.98 | 2.26 | 1.41 | 4.49 | 0.50 |
| 2^24 | 512 MiB | 100% | 1.66 | 1.61 | 1.44 | 1.32 | 1.15 | 1.91 | 0.69 |
| 2^26 | 2 GiB | 0% | 2.47 | 2.54 | 1.53 | 2.22 | 1.38 | 3.41 | 0.65 |
| 2^26 | 2 GiB | 50% | 3.05 | 2.73 | 1.82 | 2.28 | 1.36 | 4.16 | 0.55 |
| 2^26 | 2 GiB | 100% | 1.55 | 1.50 | 1.36 | 1.29 | 1.13 | 1.75 | 0.74 |
| 2^28 | 8 GiB | 0% | 2.39 | 2.56 | 1.54 | 2.12 | 1.37 | 3.27 | 0.65 |
| 2^28 | 8 GiB | 50% | 3.15 | 2.80 | 1.82 | 2.37 | 1.37 | 4.31 | 0.55 |
| 2^28 | 8 GiB | 100% | 1.54 | 1.53 | 1.41 | 1.22 | 1.12 | 1.72 | 0.71 |

At 8 GiB the AMAC gain on misses is 2.39 for linear probing, 2.56 for cyclic scalar and 1.54 for SIMD. SIMD's lead over linear probing on misses is 2.12 one at a time and 1.37 with AMAC. With every lookup hitting, the lead is 1.22 one at a time and 1.12 with AMAC.

### x86 run-to-run variation stays below 5.7%

Maximum coefficient of variation: 5.66%.
- ScalarOA, 2^14 keys, 100% hits: 5.66%
- SIMDGroupedOA_AMAC32, 2^22 keys, 100% hits: 5.48%

The largest variation is 5.66%, for linear probing at 512 KiB with all hits. Two cases exceed 5%.

## Every run checked its results, on one binary per platform

Correctness comes first.

- The tests check every lookup against a reference table. They cover keys that hash to the same first slot, full groups, and wraparound: the probe can pass the last slot and continue at the first slot. They also cover keys with high bits set, a second insert of a key that is already present, the limits on table size, and an empty table. They check that the first linear-probing cell starts on a 64-byte boundary.
- Every batched lookup is compared with its one-at-a-time lookup, for rings of 1, 4, 8, 16, 32 and 64. A batch must return one result per key, and each result must be the pointer that the one-at-a-time lookup returns. Both callback forms are checked, with and without the key's index in the batch. Batches around the ring size are covered, and so are key 0, which marks an empty slot, and absent keys.
- The tests pass under AddressSanitizer and UndefinedBehaviorSanitizer.
- A second test binary runs the same checks on the other backend's SIMD lookup, compiled for the CPU that runs the test. This works because both backends use only Clang's vector types.
- After timing, every benchmark run checks its hit count and its value checksum, and fails on a mismatch. All runs on both platforms passed.
- The probe statistics come from replaying insertion and lookup on plain key arrays. Before counting, the replay is checked against the real tables: every key must land in the same slot. Its linear-probing averages of 1.50 and 2.50 compares match Knuth's formulas.

Then the measurement.

- Every case ran five times, in one random interleave with the other kernels of its row. The tables report the median. The largest coefficient of variation is 8.27% on ARM and 5.66% on x86.
- One binary per platform was copied unchanged to four dedicated instances. All kernels of a row ran on one instance. The process was pinned to one CPU.
- The ARM and x86 instances ran the same source. The x86 binary was built on one of its own instances, with the official Clang 22.1.8 release, which comes from the same LLVM source as the ARM compiler.
- The hardware counters come from a separate run. Reading the counters adds time. Counts come only from the counter runs, and times come only from the timed sweeps.
- Each explanation is labelled in its sentence as measured, modelled or inferred, in [the explanation](#the-explanation-amac-hides-memory-waits-and-it-costs-instructions).
- The two runs gave different one-at-a-time times ([see Setup](#setup-one-binary-per-platform-on-four-dedicated-instances-each)), so I compare designs only inside one run.

A run of the five one-at-a-time designs on the shared 96-core `m8g.24xlarge` development machine was too noisy to use. Other work was running on it, and its load average was 8.0 at the start. Run-to-run variation reached 54.36%, for per-lane masks at 8 MiB with all hits.

Of the 150 cases, 23 varied by more than 10%, all at 8 MiB or at 32 MiB. In two rows the noise changed which design was fastest. No time from that run is used on this page or in the README.

## Data

### Hardware counters, ARM, main run

Counts are per lookup, from one `OA_PMU=1` run per case on the instance that ran that size. They leave out the kernel. The times of this run are not used.

| Keys | Hits | Kernel | Instructions | Cycles | Branch misses |
| --- | ---: | --- | ---: | ---: | ---: |
| 2^24 | 0% | Linear probing | 28.50 | 100.9 | 1.409 |
| 2^24 | 0% | Cyclic scalar | 34.15 | 70.4 | 1.309 |
| 2^24 | 0% | SIMD | 28.97 | 38.7 | 0.286 |
| 2^24 | 0% | Linear probing, AMAC | 50.38 | 34.6 | 1.569 |
| 2^24 | 0% | Cyclic scalar, AMAC | 54.69 | 30.2 | 1.174 |
| 2^24 | 0% | SIMD, AMAC | 47.02 | 29.5 | 0.321 |
| 2^24 | 50% | Linear probing | 26.62 | 96.6 | 1.494 |
| 2^24 | 50% | Cyclic scalar | 30.74 | 68.7 | 1.412 |
| 2^24 | 50% | SIMD | 29.33 | 51.2 | 0.875 |
| 2^24 | 50% | Linear probing, AMAC | 45.98 | 33.6 | 1.571 |
| 2^24 | 50% | Cyclic scalar, AMAC | 52.20 | 32.2 | 1.304 |
| 2^24 | 50% | SIMD, AMAC | 46.63 | 30.1 | 0.948 |
| 2^24 | 100% | Linear probing | 24.75 | 41.9 | 0.586 |
| 2^24 | 100% | Cyclic scalar | 27.34 | 42.9 | 0.543 |
| 2^24 | 100% | SIMD | 29.69 | 40.6 | 0.053 |
| 2^24 | 100% | Linear probing, AMAC | 41.58 | 30.1 | 0.777 |
| 2^24 | 100% | Cyclic scalar, AMAC | 49.73 | 30.2 | 0.536 |
| 2^24 | 100% | SIMD, AMAC | 46.24 | 28.6 | 0.075 |
| 2^28 | 0% | Linear probing | 28.50 | 108.7 | 1.412 |
| 2^28 | 0% | Cyclic scalar | 34.15 | 75.1 | 1.293 |
| 2^28 | 0% | SIMD | 28.97 | 40.5 | 0.284 |
| 2^28 | 0% | Linear probing, AMAC | 50.38 | 36.5 | 1.618 |
| 2^28 | 0% | Cyclic scalar, AMAC | 54.69 | 31.9 | 1.190 |
| 2^28 | 0% | SIMD, AMAC | 47.02 | 30.9 | 0.323 |
| 2^28 | 50% | Linear probing | 26.63 | 105.1 | 1.496 |
| 2^28 | 50% | Cyclic scalar | 30.75 | 72.8 | 1.396 |
| 2^28 | 50% | SIMD | 29.33 | 53.6 | 0.867 |
| 2^28 | 50% | Linear probing, AMAC | 45.99 | 35.2 | 1.618 |
| 2^28 | 50% | Cyclic scalar, AMAC | 52.21 | 33.3 | 1.320 |
| 2^28 | 50% | SIMD, AMAC | 46.63 | 31.4 | 0.959 |
| 2^28 | 100% | Linear probing | 24.75 | 44.0 | 0.571 |
| 2^28 | 100% | Cyclic scalar | 27.34 | 45.1 | 0.535 |
| 2^28 | 100% | SIMD | 29.69 | 42.8 | 0.052 |
| 2^28 | 100% | Linear probing, AMAC | 41.59 | 31.4 | 0.782 |
| 2^28 | 100% | Cyclic scalar, AMAC | 49.73 | 31.9 | 0.537 |
| 2^28 | 100% | SIMD, AMAC | 46.24 | 30.3 | 0.073 |

The next table shows how the counts change with AMAC, from the table above.

| Keys | Hits | Design | Instructions | Cycles | Branch misses |
| --- | ---: | --- | ---: | ---: | ---: |
| 2^24 | 0% | Linear probing | +77% | -66% | +11% |
| 2^24 | 0% | Cyclic scalar | +60% | -57% | -10% |
| 2^24 | 0% | SIMD | +62% | -24% | +12% |
| 2^24 | 50% | Linear probing | +73% | -65% | +5% |
| 2^24 | 50% | Cyclic scalar | +70% | -53% | -8% |
| 2^24 | 50% | SIMD | +59% | -41% | +8% |
| 2^24 | 100% | Linear probing | +68% | -28% | +33% |
| 2^24 | 100% | Cyclic scalar | +82% | -29% | -1% |
| 2^24 | 100% | SIMD | +56% | -30% | +42% |
| 2^28 | 0% | Linear probing | +77% | -66% | +15% |
| 2^28 | 0% | Cyclic scalar | +60% | -58% | -8% |
| 2^28 | 0% | SIMD | +62% | -24% | +14% |
| 2^28 | 50% | Linear probing | +73% | -66% | +8% |
| 2^28 | 50% | Cyclic scalar | +70% | -54% | -5% |
| 2^28 | 50% | SIMD | +59% | -42% | +11% |
| 2^28 | 100% | Linear probing | +68% | -29% | +37% |
| 2^28 | 100% | Cyclic scalar | +82% | -29% | +0% |
| 2^28 | 100% | SIMD | +56% | -29% | +39% |

### Hardware counters, x86, main run

| Keys | Hits | Kernel | Instructions | Cycles | Branch misses |
| --- | ---: | --- | ---: | ---: | ---: |
| 2^24 | 0% | Linear probing | 44.00 | 197.3 | 1.019 |
| 2^24 | 0% | Cyclic scalar | 66.16 | 182.0 | 1.048 |
| 2^24 | 0% | SIMD | 33.06 | 89.9 | 0.235 |
| 2^24 | 0% | Linear probing, AMAC | 62.26 | 77.9 | 1.355 |
| 2^24 | 0% | Cyclic scalar, AMAC | 69.36 | 68.7 | 1.084 |
| 2^24 | 0% | SIMD, AMAC | 53.56 | 54.2 | 0.269 |
| 2^24 | 50% | Linear probing | 39.50 | 234.0 | 1.059 |
| 2^24 | 50% | Cyclic scalar | 54.05 | 201.0 | 1.082 |
| 2^24 | 50% | SIMD | 33.25 | 106.6 | 0.699 |
| 2^24 | 50% | Linear probing, AMAC | 57.23 | 75.3 | 1.246 |
| 2^24 | 50% | Cyclic scalar, AMAC | 65.21 | 69.4 | 1.066 |
| 2^24 | 50% | SIMD, AMAC | 51.35 | 54.0 | 0.710 |
| 2^24 | 100% | Linear probing | 35.00 | 93.4 | 0.480 |
| 2^24 | 100% | Cyclic scalar | 41.96 | 89.9 | 0.486 |
| 2^24 | 100% | SIMD | 33.45 | 72.0 | 0.050 |
| 2^24 | 100% | Linear probing, AMAC | 52.21 | 56.3 | 0.643 |
| 2^24 | 100% | Cyclic scalar, AMAC | 61.07 | 56.0 | 0.522 |
| 2^24 | 100% | SIMD, AMAC | 49.14 | 49.2 | 0.067 |
| 2^28 | 0% | Linear probing | 44.00 | 244.8 | 1.018 |
| 2^28 | 0% | Cyclic scalar | 66.16 | 223.7 | 1.042 |
| 2^28 | 0% | SIMD | 33.06 | 115.2 | 0.235 |
| 2^28 | 0% | Linear probing, AMAC | 62.26 | 102.1 | 1.357 |
| 2^28 | 0% | Cyclic scalar, AMAC | 69.36 | 88.8 | 1.088 |
| 2^28 | 0% | SIMD, AMAC | 53.56 | 74.7 | 0.269 |
| 2^28 | 50% | Linear probing | 39.50 | 273.2 | 1.057 |
| 2^28 | 50% | Cyclic scalar | 54.06 | 222.6 | 1.083 |
| 2^28 | 50% | SIMD | 33.25 | 115.2 | 0.698 |
| 2^28 | 50% | Linear probing, AMAC | 57.24 | 86.6 | 1.246 |
| 2^28 | 50% | Cyclic scalar, AMAC | 65.22 | 79.4 | 1.068 |
| 2^28 | 50% | SIMD, AMAC | 51.35 | 63.4 | 0.711 |
| 2^28 | 100% | Linear probing | 35.00 | 119.4 | 0.480 |
| 2^28 | 100% | Cyclic scalar | 41.96 | 117.2 | 0.486 |
| 2^28 | 100% | SIMD | 33.45 | 98.2 | 0.050 |
| 2^28 | 100% | Linear probing, AMAC | 52.21 | 77.7 | 0.643 |
| 2^28 | 100% | Cyclic scalar, AMAC | 61.08 | 76.6 | 0.523 |
| 2^28 | 100% | SIMD, AMAC | 49.14 | 69.5 | 0.066 |

| Keys | Hits | Design | Instructions | Cycles | Branch misses |
| --- | ---: | --- | ---: | ---: | ---: |
| 2^24 | 0% | Linear probing | +41% | -61% | +33% |
| 2^24 | 0% | Cyclic scalar | +5% | -62% | +3% |
| 2^24 | 0% | SIMD | +62% | -40% | +15% |
| 2^24 | 50% | Linear probing | +45% | -68% | +18% |
| 2^24 | 50% | Cyclic scalar | +21% | -65% | -2% |
| 2^24 | 50% | SIMD | +54% | -49% | +2% |
| 2^24 | 100% | Linear probing | +49% | -40% | +34% |
| 2^24 | 100% | Cyclic scalar | +46% | -38% | +7% |
| 2^24 | 100% | SIMD | +47% | -32% | +32% |
| 2^28 | 0% | Linear probing | +41% | -58% | +33% |
| 2^28 | 0% | Cyclic scalar | +5% | -60% | +4% |
| 2^28 | 0% | SIMD | +62% | -35% | +15% |
| 2^28 | 50% | Linear probing | +45% | -68% | +18% |
| 2^28 | 50% | Cyclic scalar | +21% | -64% | -1% |
| 2^28 | 50% | SIMD | +54% | -45% | +2% |
| 2^28 | 100% | Linear probing | +49% | -35% | +34% |
| 2^28 | 100% | Cyclic scalar | +46% | -35% | +7% |
| 2^28 | 100% | SIMD | +47% | -29% | +32% |

### Hardware counters, ARM, five-design run

This is the only evidence for steps 2 and 4. The run counted instructions, cycles and branch misses at 2^24 and 2^28 keys for the five designs. Steps 1, 3 and 5 also have counts in the main run above, and those are the ones I use for them. Compare rows only inside one table.

| Keys | Hits | Design | Instructions | Cycles | Branch misses |
| --- | ---: | --- | ---: | ---: | ---: |
| 2^24 | 0% | Linear probing | 28.50 | 92.7 | 1.409 |
| 2^24 | 0% | Lane 0 scan | 34.68 | 91.4 | 1.362 |
| 2^24 | 0% | Cyclic scalar | 34.15 | 70.2 | 1.311 |
| 2^24 | 0% | Per-lane masks | 45.17 | 75.3 | 0.249 |
| 2^24 | 0% | SIMD | 28.97 | 38.8 | 0.287 |
| 2^24 | 50% | Linear probing | 26.62 | 105.9 | 1.500 |
| 2^24 | 50% | Lane 0 scan | 30.94 | 78.6 | 1.410 |
| 2^24 | 50% | Cyclic scalar | 30.74 | 68.3 | 1.413 |
| 2^24 | 50% | Per-lane masks | 40.25 | 80.4 | 0.728 |
| 2^24 | 50% | SIMD | 29.33 | 51.3 | 0.879 |
| 2^24 | 100% | Linear probing | 24.75 | 41.9 | 0.584 |
| 2^24 | 100% | Lane 0 scan | 27.21 | 54.6 | 1.057 |
| 2^24 | 100% | Cyclic scalar | 27.34 | 42.9 | 0.544 |
| 2^24 | 100% | Per-lane masks | 35.34 | 59.6 | 0.052 |
| 2^24 | 100% | SIMD | 29.69 | 41.0 | 0.054 |
| 2^28 | 0% | Linear probing | 28.50 | 100.6 | 1.419 |
| 2^28 | 0% | Lane 0 scan | 34.68 | 98.4 | 1.356 |
| 2^28 | 0% | Cyclic scalar | 34.15 | 75.0 | 1.301 |
| 2^28 | 0% | Per-lane masks | 45.17 | 79.5 | 0.249 |
| 2^28 | 0% | SIMD | 28.97 | 40.5 | 0.286 |
| 2^28 | 50% | Linear probing | 26.63 | 111.5 | 1.498 |
| 2^28 | 50% | Lane 0 scan | 30.95 | 83.5 | 1.398 |
| 2^28 | 50% | Cyclic scalar | 30.75 | 72.2 | 1.396 |
| 2^28 | 50% | Per-lane masks | 40.25 | 85.5 | 0.728 |
| 2^28 | 50% | SIMD | 29.33 | 53.2 | 0.865 |
| 2^28 | 100% | Linear probing | 24.75 | 43.7 | 0.569 |
| 2^28 | 100% | Lane 0 scan | 27.21 | 57.1 | 1.050 |
| 2^28 | 100% | Cyclic scalar | 27.34 | 45.0 | 0.534 |
| 2^28 | 100% | Per-lane masks | 35.34 | 63.6 | 0.052 |
| 2^28 | 100% | SIMD | 29.69 | 43.1 | 0.055 |

The next table has the all-hits counts at 2^28 keys from the same run.

| Step | Design | Instructions | Cycles | Branch mispredictions | ns |
| --- | --- | ---: | ---: | ---: | ---: |
| 1 | Linear probing | 24.75 | 43.7 | 0.569 | 15.74 |
| 2 | Scalar scan from lane 0 | 27.21 | 57.1 | 1.050 | 20.51 |
| 3 | Scalar scan from lane `hash & 3` | 27.34 | 45.0 | 0.534 | 16.08 |
| 4 | SIMD, one mask per lane | 35.34 | 63.6 | 0.052 | 22.69 |
| 5 | SIMD, one combined mask | 29.69 | 43.1 | 0.055 | 15.30 |

Steps 1, 3 and 5 need 43 to 45 cycles per hit. Step 5 mispredicts a tenth as often as linear probing. It runs 20% more instructions and is 2.8% faster. Step 2 is about 5 ns slower than linear probing, and step 4 about 7 ns slower.

Steps 2 and 4 also have times at every size, in median nanoseconds per lookup, from the same run:

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

With these two designs included, the largest variation of that run is 11.40%. That case is per-lane masks at 32 MiB with all hits. The next is 10.36%, for per-lane masks at 32 MiB with half the lookups hitting.

### Ring sizes, ARM and x86

The ring tables give the median nanoseconds per lookup, from the sweep at 2^24 and 2^28 keys. "No ring" is the one-at-a-time lookup. Bold marks the lowest in the row. First ARM:

| Keys | Hits | Design | No ring | Ring 8 | Ring 16 | Ring 32 | Ring 64 |
| --- | ---: | --- | ---: | ---: | ---: | ---: | ---: |
| 2^24 | 0% | Linear probing | 35.79 | 19.51 | 13.91 | **12.39** | 12.68 |
| 2^24 | 0% | Cyclic scalar | 25.19 | 16.39 | 11.82 | **10.73** | 11.22 |
| 2^24 | 0% | SIMD | 13.89 | 12.88 | **10.48** | 10.57 | 10.82 |
| 2^24 | 50% | Linear probing | 35.65 | 18.64 | 13.22 | **11.96** | 12.35 |
| 2^24 | 50% | Cyclic scalar | 24.66 | 16.12 | 11.82 | **11.45** | 11.89 |
| 2^24 | 50% | SIMD | 18.41 | 15.39 | 11.69 | 10.79 | **10.76** |
| 2^24 | 100% | Linear probing | 15.00 | 13.24 | 10.69 | **10.68** | 10.79 |
| 2^24 | 100% | Cyclic scalar | 15.37 | 12.99 | **10.56** | 10.84 | 10.58 |
| 2^24 | 100% | SIMD | 14.67 | 11.56 | **10.15** | 10.29 | 10.31 |
| 2^28 | 0% | Linear probing | 39.79 | 20.40 | 14.35 | **13.02** | 13.11 |
| 2^28 | 0% | Cyclic scalar | 26.98 | 17.12 | 12.23 | **11.44** | 11.53 |
| 2^28 | 0% | SIMD | 14.57 | 13.39 | **10.95** | 11.10 | 11.37 |
| 2^28 | 50% | Linear probing | 37.73 | 19.51 | 13.62 | **12.65** | 12.71 |
| 2^28 | 50% | Cyclic scalar | 26.11 | 16.88 | 12.09 | **11.97** | 12.21 |
| 2^28 | 50% | SIMD | 19.18 | 15.89 | 12.05 | 11.26 | **11.22** |
| 2^28 | 100% | Linear probing | 15.84 | 13.88 | **11.26** | 11.29 | 11.40 |
| 2^28 | 100% | Cyclic scalar | 16.21 | 13.67 | **11.12** | 11.46 | 11.13 |
| 2^28 | 100% | SIMD | 15.38 | 12.13 | **10.57** | 10.86 | 10.95 |

Largest gap between ring 32 and the best ring: 3.0% (2^28 keys, 100% hits, Cyclic scalar).

Then x86:

| Keys | Hits | Design | No ring | Ring 8 | Ring 16 | Ring 32 | Ring 64 |
| --- | ---: | --- | ---: | ---: | ---: | ---: | ---: |
| 2^24 | 0% | Linear probing | 55.10 | 32.03 | 24.32 | 22.05 | **21.88** |
| 2^24 | 0% | Cyclic scalar | 52.31 | 27.93 | 20.91 | **19.47** | 19.53 |
| 2^24 | 0% | SIMD | 24.59 | 19.55 | 16.33 | **15.15** | 15.31 |
| 2^24 | 50% | Linear probing | 66.69 | 31.23 | 23.19 | 20.98 | **20.87** |
| 2^24 | 50% | Cyclic scalar | 56.40 | 28.31 | 20.83 | **19.45** | 19.46 |
| 2^24 | 50% | SIMD | 29.47 | 21.96 | 16.87 | **14.86** | 15.17 |
| 2^24 | 100% | Linear probing | 26.48 | 20.96 | 17.15 | **15.96** | 16.13 |
| 2^24 | 100% | Cyclic scalar | 25.53 | 20.98 | 16.82 | 15.89 | **15.60** |
| 2^24 | 100% | SIMD | 20.05 | 18.17 | 15.60 | 13.90 | **13.85** |
| 2^28 | 0% | Linear probing | 65.14 | 38.11 | 29.76 | 27.26 | **27.03** |
| 2^28 | 0% | Cyclic scalar | 61.13 | 33.31 | 25.54 | 23.91 | **23.65** |
| 2^28 | 0% | SIMD | 30.73 | 24.66 | 20.78 | **19.92** | 19.93 |
| 2^28 | 50% | Linear probing | 78.75 | 35.05 | 26.55 | 25.00 | **24.53** |
| 2^28 | 50% | Cyclic scalar | 64.24 | 31.71 | 23.55 | 22.96 | **22.72** |
| 2^28 | 50% | SIMD | 33.27 | 25.36 | 19.91 | **18.28** | 18.33 |
| 2^28 | 100% | Linear probing | 31.89 | 26.30 | 21.92 | 20.73 | **20.62** |
| 2^28 | 100% | Cyclic scalar | 31.29 | 26.25 | 21.21 | 20.49 | **20.31** |
| 2^28 | 100% | SIMD | 26.23 | 23.59 | 20.36 | 18.57 | **18.55** |

Largest gap between ring 32 and the best ring: 1.9% (2^28 keys, 50% hits, Linear probing).

### Probe statistics

`build/oa_probe_stats` replays insertion and lookup on plain key arrays. It counts what each lookup reads. A line is one 64-byte cache line. These counts are for 2^28 keys, and they do not depend on the machine.

| Probe order | Hit: key compares | Hit: lines | Hit: 2+ lines | Hit: first compare matches | Miss: key compares | Miss: lines | Miss: 2+ lines |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Linear (step 1) | 1.5000 | 1.1250 | 11.08% | 75.00% | 2.4999 | 1.3750 | 28.43% |
| Grouped, lane 0 first (step 2) | 2.0994 | 1.0497 | 4.08% | 42.98% | 4.9134 | 1.2284 | 16.33% |
| Grouped, lane `hash & 3` first (step 3) | 1.5177 | 1.0497 | 4.08% | 75.00% | 2.5586 | 1.2284 | 16.33% |

Full groups are 16.33% of all groups. Steps 4 and 5 search the lane-0 table one whole group at a time, so they read the same lines as step 2. The next table is the share of lookups by how many key compares happen before the loop exits.

| Lookups | 1 | 2 | 3 | 4 | 5+ |
| --- | ---: | ---: | ---: | ---: | ---: |
| Linear hits | 75.00% | 14.35% | 5.15% | 2.36% | 3.14% |
| Linear misses | 50.00% | 19.67% | 10.48% | 6.29% | 13.56% |
| Cyclic hits | 75.00% | 14.25% | 4.85% | 1.82% | 4.08% |
| Cyclic misses | 50.01% | 19.42% | 9.62% | 4.62% | 16.33% |

Before counting, `--verify` built the real tables at 2^20 keys. For each of the three tables, the replay put all 1,048,576 keys in the same slots as the real table.

### Tuning experiments

These experiments ran on the shared `m8g.24xlarge` development machine. Their times cannot be compared with the tables above.

- Starting the scalar scan at `hash & 3` raised the share of hits found by the first compare from 43% to 75%. It cut compares per hit from 2.10 to 1.52.
- The cyclic scan returns from a single loop, on the first match or on the first empty lane. At 2^24 keys a hit takes 16.3 ns, against 18.8 ns for a version with a separate test for the first lane.
- The cyclic scalar scan keeps its probe position as a cell index, so Clang can address a group with one `add ..., lsl #4`. The other form starts with `lsr`, a right shift, then does `add ..., lsl #6`. In a paired test at 2^28 keys, the cell index saved about 1 ns per lookup. The same run held the control experiment above.
- Two SIMD variants were slower. One indexed the values with `3 - countl_zero / 16`. A hit took 33 instructions, because Clang added a sign extension and kept a null check on the pointer. The other used xsimd's `select` and `reduce_add` to compute the value offset, and it needed two 128-bit constants. A function call on AArch64 does not keep the vector registers, so the constants were loaded from the stack again on every probe.
- With a callback that takes the key's index in the batch, the ring stores the index each time a lookup starts. Clang keeps those stores even when the callback ignores the index. In a separate compile of both forms (Clang 22.1.8, ARM), the form with the index has one more store at each start. A callback that takes only the value makes the ring skip the index. I did not time the difference.

### Benchmark names

The names in the JSON files and in `--benchmark_filter` have the form `ScalarOA_AMAC32/keys:268435456/hit_percent:0/real_time`. These are the kernels of the main run:

| Design | One at a time | AMAC, ring of 32 (every size) | AMAC, rings of 8, 16 and 64 (2^24 and 2^28 keys) |
| --- | --- | --- | --- |
| Linear probing (step 1) | `ScalarOA` | `ScalarOA_AMAC32` | `ScalarOA_AMAC8`, `_AMAC16`, `_AMAC64` |
| Cyclic scalar (step 3) | `ScalarCyclicGroupedOA` | `ScalarCyclicGroupedOA_AMAC32` | `ScalarCyclicGroupedOA_AMAC8`, `_AMAC16`, `_AMAC64` |
| SIMD (step 5) | `SIMDGroupedOA` | `SIMDGroupedOA_AMAC32` | `SIMDGroupedOA_AMAC8`, `_AMAC16`, `_AMAC64` |

The five-design binary and its raw JSON use these names. The current source does not build that binary.

| Step | Name in the tables | Benchmark in the five-design binary | Benchmark in the current source |
| --- | --- | --- | --- |
| 1 | Linear probing | `ScalarOA` | `ScalarOA` |
| 2 | Lane 0 scan | `ScalarGroupedOA` | Not in the source |
| 3 | Cyclic scalar | `ScalarCyclicGroupedOA` | `ScalarCyclicGroupedOA` |
| 4 | Per-lane masks | `SIMDGroupedOA` | Not in the source |
| 5 | SIMD | `SIMDCombinedGroupedOA` | `SIMDGroupedOA` |

## Reproduce

The build needs Clang and the C++20 language, and it was tested with Clang 22.1.8. Meson configures the build, Ninja runs the compile, and CMake builds Google Benchmark. clang-format and clang-tidy check the source text, and Meson fails at setup without them. The scripts in `tools/` need only Python 3.

On first setup, Meson downloads Google Benchmark. The download is one fixed archive, and Meson checks its hash. The `.clang-format` and `.clang-tidy` files were copied from the ClickHouse database project.

```sh
CXX=clang++-22 meson setup build --buildtype=release
meson compile -C build
meson test -C build --print-errorlogs
meson compile -C build format-check tidy
```

To also test an 8 GiB grouped table, run the command below. That table is the smallest one whose group index uses hash bit 28. Each test binary builds its own table, and the run needs 16 GiB of memory.

```sh
OA_LARGE_TESTS=1 meson test -C build
```

The sanitizer build is separate:

```sh
CXX=clang++-22 meson setup build-sanitize --buildtype=debug -Db_sanitize=address,undefined
meson compile -C build-sanitize
meson test -C build-sanitize --print-errorlogs
```

The full matrix covers 2^10, 2^12, ..., 2^28 keys. Each size runs with 0%, 50% and 100% hits, for the three one-at-a-time lookups and the three AMAC lookups with a ring of 32. Rings of 8, 16 and 64 run at 2^24 and 2^28 keys. Run it on an idle core of a quiet machine, because other processes share the L3 cache and the memory bandwidth. The sweeps of the four instances of each platform added up to 103 minutes on ARM and 184 minutes on x86, so one machine needs about that long.

```sh
taskset -c 8 build/oa_bench \
    --benchmark_min_time=0.1s --benchmark_min_warmup_time=0.05 \
    --benchmark_repetitions=5 --benchmark_enable_random_interleaving=true \
    --benchmark_display_aggregates_only=true \
    --benchmark_out=sweep.json --benchmark_out_format=json
python3 tools/summarize.py sweep.json
python3 tools/plot.py docs/lookup-time-arm.svg --platform arm sweep.json
```

To run a subset, pass `--benchmark_filter`. To run only the AMAC lookups with a ring of 32 at 2^28 keys, use `--benchmark_filter='_AMAC32/keys:268435456/'`.

To split the work across machines, give each machine a filter that selects whole rows, so that every kernel of a row stays on one machine. The main run used four machines per platform. Each machine ran this command with its own filter, from the directory of the binary:

```sh
taskset -c 8 ./oa_bench --benchmark_filter='<filter>' \
    --benchmark_min_time=0.1s --benchmark_min_warmup_time=0.05 \
    --benchmark_repetitions=5 --benchmark_enable_random_interleaving=true \
    --benchmark_display_aggregates_only=true \
    --benchmark_out=sweep.json --benchmark_out_format=json
```

| Machine | Sweep `<filter>` | Counter run `<filter>` |
| --- | --- | --- |
| 1 | `/keys:268435456/hit_percent:0/` | `/keys:268435456/hit_percent:0/` |
| 2 | `/keys:268435456/hit_percent:50/` | `/keys:268435456/hit_percent:50/` |
| 3 | `/keys:268435456/hit_percent:100/` | `/keys:268435456/hit_percent:100/` |
| 4 | `/keys:(1024\|4096\|16384\|65536\|262144\|1048576\|4194304\|16777216\|67108864)/` | `/keys:16777216/` |

Then pass all the JSON files to both scripts. The tables of this page come from these commands. In them, `<run>` holds one directory per machine, each with `sweep.json` and `counters.json`:

```sh
python3 tools/summarize.py <run>/*/sweep.json --counters <run>/*/counters.json
python3 tools/plot.py docs/lookup-time-arm.svg --platform arm <run>/*/sweep.json
python3 tools/plot.py docs/lookup-time-x86.svg --platform x86 <run>/*/sweep.json
```

Hardware counters come from a separate run with `OA_PMU=1`. `kernel.perf_event_paranoid` is the Linux switch for who may read those counters. The run needs it set to 2 or lower, and otherwise it needs root. It reads the counters once per pass. Reading them adds time, so use the counts of this run and never its times. The main run took one counter run per case, with no repetitions:

```sh
OA_PMU=1 taskset -c 8 ./oa_bench --benchmark_filter='<filter>' \
    --benchmark_min_time=0.1s --benchmark_out=counters.json --benchmark_out_format=json
```

`oa_probe_stats` counts compares and cache lines by replaying insertion and lookup on plain key arrays. With `--verify`, it first checks that the replay puts every key in the same slot as the real tables.

```sh
build/oa_probe_stats --verify 20
build/oa_probe_stats 28
```

The current source does not contain steps 2 and 4, so these commands cannot measure them. To read the disassembly of the AMAC loop on ARM:

```sh
llvm-objdump-22 -d --no-show-raw-insn -C build/oa_bench > oa_bench.dis
```
