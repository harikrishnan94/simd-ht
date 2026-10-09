# Results

This page is the full record behind the [README](README.md). It covers five lookup designs for one hash table.

It includes the CPU's counts of instructions, cycles, and wrong branch guesses. Those counts are the hardware counters. It includes every table size. It also records how the numbers were checked.

The main run was on 2026-10-09. It used four AWS `m8g.4xlarge` instances in `ap-south-2a`. The CPU design is Arm Neoverse V2. Graviton4 is the name of that core in these runs.

A section that uses a different run says so.

- [Setup](#setup)
- [The five designs](#the-five-designs)
- [Why fewer instructions help](#why-fewer-instructions-help)
- [Hits](#hits)
- [Results by table size](#results-by-table-size)
- [x86-64](#x86-64)
- [How the results were checked](#how-the-results-were-checked)
- [Data](#data): counters, probe statistics, tuning experiments, benchmark names
- [Reproduce](#reproduce)

## Setup

All five designs use the same keys. They use the same hash, the same table sizes, and the same machines. Only the memory layout changes. The code that walks the table to find the key changes with the layout. That walk is the probe.

- The keys and the values are 64-bit integers. Key 0 is reserved. A slot that holds key 0 is empty.

- The hash is computed in two steps. The first step XORs the two 32-bit halves of the key. MurmurHash3 is a known hash function. The last mixing step of a hash is called its final mix. The second step is MurmurHash3's final mix. That mix is called `fmix32`.

- `bit_ceil(N)` rounds N up to a power of two. The table has `2 * bit_ceil(N)` slots for N keys. The load factor is the share of slots that hold a key. Here the load factor is exactly 0.5.

- Each table holds `32 * N` bytes. The tables never delete a key. They never resize.

- A KiB is 1,024 bytes. A MiB is 1,024 KiB. A GiB is 1,024 MiB. The tables hold from 2^10 keys to 2^28 keys. That range is 32 KiB to 8 GiB.

- The stored keys start as the integers 1 through N. The last mixing step of a hash is called its finalizer. splitmix64 is a known hash. Its finalizer maps each of these keys to a different output.

- A function that maps each input to a different output is a bijection. This finalizer is a bijection. The keys are distinct. None of them is 0.

- Absent keys are the integers N + 1 through 2N, passed through the same finalizer.

- The query trace is the list of keys to look up. It holds N keys. The list is shuffled with a fixed seed. Either 0%, 50%, or 100% of those keys are in the table.

- A lookup of a present key is a hit. A lookup of an absent key is a miss.

- One timed pass looks up every key in the trace. It sums the values it finds. The timer covers the hash, the probe, the load of the value, and the sum. The timer does not cover building the table. The timer does not cover shuffling the trace.

- After timing, the run checks the hit count and the checksum of the values. It fails if either check does not match.

- The lookups do not depend on each other. The measured result is how many lookups finish per unit of time on one thread. That rate is called throughput. It is not the time for one lookup running alone. That single time is called latency.

- Google Benchmark is the timing library. It counts each lookup as one iteration. Each case warms up for 0.05 s. It then runs for at least 0.1 s. Each case runs five times.

- The runs of every case are mixed into one random order. That mix is what "interleaved" means below.

- SIMD means one instruction works on several keys at once.

- The compiler must not turn a one-key lookup into a SIMD lookup on its own. That automatic rewrite is called vectorization. It is off for this project's code. Scalar means one key at a time. The scalar lookups stay scalar.

L1, L2, and L3 in the table are CPU caches. L1 is the smallest, and it is private to one core. L3 is the largest, and the whole processor shares it. NEON is Arm's SIMD instruction set. xsimd is the library the measured binary used to call NEON.

| | |
| --- | --- |
| Instances | 4 x `m8g.4xlarge`: 16 Neoverse V2 cores, 64 GiB, Ubuntu 26.04, Linux 7.0.0-1008-aws |
| Caches | 64 KiB L1 data and 2 MiB L2 per core, 36 MiB L3 shared by the whole processor |
| Compiler | Clang 22.1.8, `-O3`, `-fno-vectorize -fno-slp-vectorize`, NEON through xsimd 14.3.0 |
| Harness | Google Benchmark 1.9.4, one thread pinned to CPU 8 |
| Binary | One `oa_bench` build, copied unchanged to all four instances |

The main case below is the 8 GiB table. It is more than 200 times the size of the L3 cache.

A vector type holds several keys in one variable. The measured binary called NEON through xsimd 14.3.0. The current source uses only the vector types built into Clang.

Disassembly is the compiled instructions, written out one by one. In that listing, the timed loops match between the measured binary and a build of the current source. The addresses differ. The untimed loop that builds the table also differs. That loop now calls a shared function to insert a key. The call passes two more arguments.

A row is one table size together with one hit rate. The cases were split across the instances by row. Every design in a row ran on the same machine. Those runs were interleaved.

| Instance | Cases |
| --- | --- |
| 1 | 2^28 keys, 0% hits; counters at 2^24 keys |
| 2 | 2^28 keys, 50% hits; probe statistics |
| 3 | 2^28 keys, 100% hits; counters at 2^28 keys |
| 4 | All other table sizes |

An `m8g.4xlarge` uses part of a larger processor. Other virtual machines can share its L3 cache. They can also share its memory bandwidth. The variation tables below show how far the five runs of each case spread.

## The five designs

The CPU loads memory in blocks of 64 bytes. A block of that size is a cache line. In the tables, a line means one cache line.

Steps 2 to 5 store 4 keys and then 4 values in one group. The group is 64 bytes. It fills one cache line. The four slots are lanes. The lane numbers are 0, 1, 2, and 3.

A mask is a bit pattern that records which lanes matched the search key. Step 4 keeps one bit for each lane. Step 5 keeps both compare results in one mask. Scalar means the lookup compares one key at a time.

Each row below is one design. The table is 8 GiB. Every query key is absent. All five designs ran in one interleaved benchmark. The times come from that run.

A cell is one slot that holds a key and its value. The first design starts at the cell the hash selects. It then tries the next cell. It stops when it finds the key or an empty cell. That design is linear probing. It is step 1.

A branch misprediction is a wrong guess about which way a branch will go. Instruction counts, cycle counts, and branch mispredictions come from a separate counter run. Cache lines and key compares come from replaying the lookups. Every column is the count per lookup.

| Step | Design | Lines | Key compares | Branch mispredictions | Instructions | Cycles | ns |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | Linear probing | 1.375 | 2.50 | 1.42 | 28.50 | 100.6 | 36.53 |
| 2 | Four keys per line, scalar scan from lane 0 | 1.228 | 4.91 | 1.36 | 34.68 | 98.4 | 35.31 |
| 3 | Scalar scan from lane `hash & 3` | 1.228 | 2.56 | 1.30 | 34.15 | 75.0 | 26.87 |
| 4 | SIMD compare, one mask per lane | 1.228 | 4 per group | 0.25 | 45.17 | 79.5 | 28.46 |
| 5 | SIMD compare, one combined mask | 1.228 | 4 per group | 0.29 | 28.97 | 40.5 | 14.57 |

Steps 2 and 3 read fewer cache lines than step 1. They still mispredict 1.30 or more times per miss. Step 2 makes 4.91 key compares, against 2.50 for step 1. Step 3 makes 2.56 key compares.

Step 4 mispredicts 0.25 times per miss. It runs 11 more instructions than step 3. It is 6% slower than step 3. Step 5 has about the same misprediction count as step 4. Its instruction count matches linear probing. It takes half as many cycles as step 4.

The current source contains steps 1, 3, and 5. Steps 2 and 4 were removed after this run. Commit `73ab522` still contains them.

### Step 1: linear probing

```text
16-byte cells, four per 64-byte cache line
  | key | value | key | value | key | value | key | value |
  start at cell  hash & (capacity - 1),  then try the next cell
```

The home cell is the cell the hash selects. Linear probing starts there. The lookup then tries the next cell. It stops when it finds the key or an empty cell.

At load factor 0.5, a hit compares 1.5 keys on average. A miss compares 2.5 keys on average. Knuth published these two averages for this load factor. On this benchmark's keys, `tools/probe_stats.cpp` measures 1.50 compares per hit and 2.50 compares per miss.

The number of compares on a miss varies a lot.

| Linear-probing misses that stop at | 1st cell | 2nd | 3rd | 4th | 5th or later |
| --- | ---: | ---: | ---: | ---: | ---: |
| Share | 50.0% | 19.7% | 10.5% | 6.3% | 13.6% |

The branch that ends the loop is hard to predict. At 8 GiB it is wrong 1.42 times per miss.

The filled cells can continue past the end of one cache line. A miss then has to read the next line too. A miss reads 1.375 lines on average. 28% of misses read two lines or more.

At 8 GiB, a miss takes 100.6 cycles. A hit takes 43.7 cycles. A miss reads 22% more cache lines than a hit. The line counts are 1.375 and 1.125. A miss also mispredicts 2.5 times as often. The misprediction counts are 1.42 and 0.57.

### Step 2: four keys per cache line

```text
One 64-byte group per cache line
  | key 0 | key 1 | key 2 | key 3 | value 0 | value 1 | value 2 | value 3 |
  start at group  (hash & (capacity - 1)) / 4,  then try the next group
```

The grouped layout puts four keys and their values in one group. The group starts at the beginning of a cache line. A start on that boundary is called aligned.

Both layouts use 16 bytes per slot. The same hash bits select the slot in either layout. Those bits select the same cache line in both layouts.

The home group is the first group the hash selects. Insertion puts the key there when that group has a free lane. Otherwise it uses the next group that has a free lane.

At load factor 0.5, 16.3% of groups are full. So 83.7% of misses end on their home cache line. A miss reads 1.228 lines, against 1.375 for linear probing. A hit reads 1.050 lines, against 1.125.

This first grouped scan compares lanes 0, 1, 2, and 3 in that order. Insertion takes the lowest empty lane. A key sits in whichever lane was free when it was inserted.

The first compare finds only 43% of hits. Linear probing finds 75% of hits on the first compare. A hit compares 2.10 keys. A miss compares all four lanes before it checks for an empty lane. A miss makes 4.91 compares on average.

Misses are 3% faster, at 35.31 ns. Hits are 30% slower, at 20.51 ns against 15.74 ns. A hit takes 1.05 mispredictions.

### Step 3: start the scan at lane `hash & 3`

Insertion and lookup both start at lane `hash & 3`. The scan wraps inside the group. After lane 3, the next lane is lane 0. A scan that wraps like that is called cyclic. This is the cyclic scalar lookup, `ScalarCyclicGroupedOA`.

The first compare finds 75% of hits, the same share as linear probing. A hit compares 1.52 keys.

Insertion fills the lanes in the order the lookup scans them. The table never deletes a key. The lookup can therefore stop at the first empty lane. A miss compares 2.56 keys.

Misses are 24% faster than step 2, at 26.87 ns. Hits take 16.08 ns. That is 2% slower than linear probing.

The number of compares on a miss still varies.

| Cyclic scalar misses that stop at | 1st compare | 2nd | 3rd | 4th | 5th or later |
| --- | ---: | ---: | ---: | ---: | ---: |
| Share | 50.0% | 19.4% | 9.6% | 4.6% | 16.3% |

The result is 1.30 mispredictions per miss. Linear probing has 1.42.

### Step 4: compare all four keys at once

The SIMD lookups use the table built by step 2. That table fills lanes starting at lane 0.

Two 128-bit NEON compares test all four keys against the search key. Each compare sets every bit of a 64-bit lane, or it sets none. One branch then decides for all four keys.

The first SIMD lookup moved each 64-bit compare result into a normal integer register, also called a general-purpose register. `extr` pulls bits out of a register. `and` keeps selected bits. `bfi` inserts bits. `extr`, `and`, and `bfi` built the 4-bit mask. They took 8 instructions, and they finished before the branch.

It then repeated the work, with two more compares, to look for an empty lane.

Mispredictions fall from 1.30 per miss to 0.25. Instructions rise from 34.15 to 45.17. A miss takes 28.46 ns, against 26.87 ns for step 3. A hit takes 22.69 ns, the slowest hit time of the five designs.

### Step 5: one combined mask

`SIMDGroupedOA` does the same two compares. The rest of the lookup is shorter. Two properties of the table make that possible.

1. A key occupies at most one slot. At most one of the four lanes can match. The lookup does not need a separate bit per lane.

The lookup narrows both compare results into one 64-bit register. `uzp1` keeps one 32-bit half of each compare result. `xtn` narrows that half to one 16-bit field per key. The lookup then branches once.

Leading zeros are the zero bits before the first 1 bit. A match in lane 0 leaves 48 leading zeros. Each later lane leaves 16 fewer than the lane before it. For lane `i`, the code writes the count as `48 - 16 * i`.

The values sit in order, from value 0 through value 3. Each value occupies 8 bytes. The compare result uses 16 bits per key. The code divides the leading-zero count by 2 and uses the result as a number of bytes.

The load starts at value 3. It moves back by that byte count. It arrives at value `i`.

`clz` counts the leading zeros. `sub` subtracts. `ldr` loads the bytes at an address. A hit uses those three instructions, plus one `add`. The `add` recomputes the group address.

2. The filled lanes always come first. Insertion takes the lowest empty lane. Nothing is deleted. A group with any free slot therefore has an empty lane 3.

When no key matches, the lookup reads `keys[3]` as one key. A zero in that slot means the search key is absent. That branch goes the same way in the 83.7% of groups that are not full.

Clang 22 generates this NEON code for one group.

```asm
ldp   q1, q2, [x15]          // load keys 0-1 and keys 2-3
cmeq  v1.2d, v1.2d, v0.2d    // keys 0-1 == search key: all ones or all zeros per lane
cmeq  v2.2d, v2.2d, v0.2d    // keys 2-3 == search key
uzp1  v1.4s, v1.4s, v2.4s    // keep one 32-bit half of each result
xtn   v1.4h, v1.4s           // narrow to one 16-bit field per key
fmov  x14, d1                // move the four fields to one general register
cbnz  x14, hit               // one branch for four keys
```

The instruction counts below come from the disassembly. They include the hash and the benchmark loop.

| Path | Step 4 | Step 5 |
| --- | ---: | ---: |
| Hit in the home group | 34 | 29 |
| Miss that ends in the home group | 39 | 26 |
| Each further full group | 27 | 13 |

At 8 GiB, step 5 runs 28.97 instructions per miss. It also runs 40.5 cycles per miss. That is 36% fewer instructions than step 4. The cycle count is 49% lower. It reads the same cache lines. It mispredicts 0.29 times per miss, against 0.25 for step 4.

A miss takes 14.57 ns. That is 2.5 times faster than linear probing.

## Why fewer instructions help

The lookups do not depend on each other. While one lookup waits for memory, the core executes later lookups. It also starts their loads. It can run only a limited distance past the oldest lookup that is still unfinished. That limit is a fixed number of instructions. The name for the limit is the out-of-order window.

A cache miss is a load that does not find its data in the cache. Fewer instructions per lookup let more lookups fit in that window. More cache misses fit in it as well. A miss in flight is a cache miss that has started and has not finished.

A mispredicted branch throws away the work after it. That includes loads already started for later lookups.

This matches three results.

1. Step 4 removed most mispredictions. It also added instructions. The lookup got slower.

2. Step 5 kept the misprediction rate of step 4. It removed 36% of the instructions. Cycles per miss fell by half. The cache lines read did not change.

3. A control experiment added `and` instructions to linear probing. The added instructions sit before the first load of a key. Nothing uses their results. They add no memory access. They add no branch. Nothing waits for them.

The control experiment ran on 2026-10-08, on the shared `m8g.24xlarge` development machine. Load average is the Unix measure of how busy the machine is. At the start it was 0.93. Every query is a hit. Each case ran five times, in a random interleave.

The table shows the median nanoseconds per lookup. It also shows the change against linear probing. "Separate" means the ranges of the five runs do not overlap. "Overlap" means those ranges do overlap.

| Keys | Linear probing | One extra `and` | Three extra `and` | One: change | Three: change |
| --- | ---: | ---: | ---: | ---: | ---: |
| 2^10 | 1.578 | 1.659 | 1.855 | +0.082 ns (+5.17%), separate | +0.278 ns (+17.59%), separate |
| 2^16 | 5.670 | 5.629 | 5.987 | -0.041 ns (-0.72%), overlap | +0.317 ns (+5.60%), separate |
| 2^24 | 15.246 | 15.582 | 16.969 | +0.336 ns (+2.20%), separate | +1.723 ns (+11.30%), separate |
| 2^28 | 15.880 | 16.299 | 17.906 | +0.418 ns (+2.63%), separate | +2.026 ns (+12.76%), separate |

At 2^10 keys the table fits in the L1 cache. The extra instructions add their own time there. At 2^16 keys, one extra instruction made no measurable difference. At 2^24 and at 2^28 keys, the tables are far larger than the L3 cache. Three extra instructions still cost 11 to 13% per lookup.

None of the counters used here measure misses in flight. This explanation is not proven.

## Hits

When every lookup hits, the designs are close. Every hit reads 1.05 to 1.125 cache lines. The counts below are per lookup, at 8 GiB.

| Step | Design | Instructions | Cycles | Branch mispredictions | ns |
| --- | --- | ---: | ---: | ---: | ---: |
| 1 | Linear probing | 24.75 | 43.7 | 0.569 | 15.74 |
| 2 | Scalar scan from lane 0 | 27.21 | 57.1 | 1.050 | 20.51 |
| 3 | Scalar scan from lane `hash & 3` | 27.34 | 45.0 | 0.534 | 16.08 |
| 4 | SIMD, one mask per lane | 35.34 | 63.6 | 0.052 | 22.69 |
| 5 | SIMD, one combined mask | 29.69 | 43.1 | 0.055 | 15.30 |

Steps 1, 3, and 5 need 43 to 45 cycles per hit. Step 5 mispredicts a tenth as often as linear probing. It runs 20% more instructions. It is 2.8% faster. Step 2 is about 5 ns slower than linear probing. Step 4 is about 7 ns slower than linear probing.

## Results by table size

The current source contains three of the designs. They are linear probing (step 1), the cyclic scalar scan (step 3), and SIMD (step 5). The times fall into three ranges.

**32 KiB and 128 KiB.** The branch predictor is the part of the core that guesses which way a branch will go. The query trace holds only 1,024 keys, or only 4,096 keys. The trace repeats on every pass. The predictor learns that repeated list.

At these sizes, linear probing mispredicts about 0.001 times per miss. At 2 MiB it mispredicts more than 1.2 times per miss. At 32 KiB and at 128 KiB, the bytes the lookup needs are already in a cache. Mispredictions are near zero as well. The instruction count decides the time.

When every lookup hits, linear probing is fastest. At 32 KiB, linear probing takes 1.58 ns. SIMD takes 1.99 ns. Linear probing is also fastest when half the lookups hit.

When every lookup misses, SIMD is fastest. At 32 KiB, SIMD takes 1.61 ns. Linear probing takes 1.91 ns.

These two sizes are a best case for every lookup whose branches depend on the data. The branch predictor has learned the trace.

**512 KiB.** The table still fits in the L2 cache. The trace has 16,384 keys. The branch predictor learns part of that trace. How much it learns depends on where the loop sits in memory.

In the measured binary, linear-probing misses take 11.17 ns. A build of the current source runs the same loop instructions. The instructions sit at other addresses. On the development machine, linear-probing misses in that build took about 7 ns.

SIMD misses take 2.25 ns. Between the two builds, the SIMD miss time changed by about 7%. When every lookup hits, the three designs sit between 2.41 ns and 2.48 ns. That gap is inside the run-to-run noise.

**2 MiB to 8 GiB.** SIMD has the lowest median in every row. Its range of five runs stays apart from the other two ranges. The exception is all hits at 32 MiB.

When every lookup misses, SIMD is 2.1 to 2.8 times faster than linear probing. When half the lookups hit, SIMD is 1.4 to 2.1 times faster. When every lookup hits, the lead is 1.7 times at 2 MiB. From 128 MiB up, that lead is 2 to 3%.

### Lookup time, current designs

The table is the median nanoseconds per lookup, over five runs. Bold marks the lowest median in the row.

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

Run-to-run variation is the spread of the five times divided by their average. That spread is the standard deviation. The table writes that ratio as a percent. That percent is the coefficient of variation.

The maximum here is 6.83%. Four cases exceed 5%. All four are at 512 KiB or at 32 MiB.

| Lookup | Table | Hits | Variation |
| --- | --- | ---: | ---: |
| SIMD | 512 KiB | 0% | 6.83% |
| Linear probing | 512 KiB | 100% | 6.65% |
| SIMD | 32 MiB | 100% | 6.45% |
| SIMD | 32 MiB | 0% | 5.46% |

From 2 MiB up, the fastest design's range of runs overlaps another range in one row only. That row is all hits at 32 MiB. In that row, the three ranges overlap. The three ranges also overlap at 512 KiB when every lookup hits.

### Lookup time, steps 2 and 4

These two designs ran in the same interleaved run as the three above. The numbers are median nanoseconds per lookup.

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

With these two designs included, the largest variation is 11.40%. That case is per-lane masks at 32 MiB with all hits. The next is 10.36%, for per-lane masks at 32 MiB with half the lookups hitting.

### Small tables and the branch predictor

At 2^10 and at 2^12 keys, the query trace has only 1,024 or 4,096 entries. The trace repeats on every pass. The branch predictor learns that sequence. At 2^14 keys, the predictor learns part of the sequence. How much it learns depends on where the loop sits in memory.

The branch misses below are per linear-probing miss. `OA_PMU=1` turns on the CPU performance counters for a run. The figures below come from runs like that on 2026-10-09.

The runs used the `m8g.24xlarge` development machine. The process was pinned to one core. The load average was 12 to 14. Each cell is the range over five runs.

The current source compiles to the same loop instructions as the measured binary. The instructions sit at other addresses.

| Build | 32 KiB | 128 KiB | 512 KiB | 2 MiB |
| --- | ---: | ---: | ---: | ---: |
| Measured binary | 0.0011 to 0.0012 | 0.0007 to 0.0015 | 0.90 to 0.97 | 1.26 to 1.30 |
| Current source | 0.0011 to 0.0012 | 0.0004 to 0.0005 | 0.46 to 0.55 | 1.22 to 1.26 |

At 32 KiB and at 128 KiB, both builds almost never mispredict. At 2 MiB, both mispredict more than once per miss. At 512 KiB, the current source mispredicts about half as often as the measured binary.

The miss times move with the branch misses. The medians below are nanoseconds per miss, over five interleaved runs, from the same machine and the same day.

| Table | Build | Linear probing | Cyclic scalar | SIMD |
| --- | --- | ---: | ---: | ---: |
| 512 KiB | Measured binary | 11.32 | 9.73 | 2.16 |
| 512 KiB | Current source | 6.95 | 7.77 | 2.31 |
| 2 MiB | Measured binary | 14.71 | 13.29 | 5.44 |
| 2 MiB | Current source | 14.69 | 13.28 | 5.42 |

The measured binary's 512 KiB times on this machine match the dedicated instances. Linear probing is 11.32 ns here, against 11.17 ns there. SIMD is 2.16 ns here, against 2.25 ns there. The 512 KiB row of the main table is therefore correct for that binary.

Another build of the same code can differ by 39% for linear probing. SIMD misses rarely mispredict. Their time changed by about 7%. At 2 MiB the two builds agree within 0.4%.

Earlier versions of this page gave 0.001, 0.003, 1.00, and 1.18 branch misses for these four sizes. Those figures came from a run whose raw output was not kept. The measurements above replace them.

## x86-64

AVX2 is the x86-64 SIMD path. Its lookup compares a group with one 256-bit compare, the instruction `vpcmpeqq`. `vmovmskpd` moves the compare result to a general-purpose register. The lookup uses the same test of lane 3.

It keeps the probe position as a byte offset. Multiplying an index while forming an address is called scaling. x86 addressing cannot scale a group index by 64.

The NEON lookup keeps the group index. AArch64 is the 64-bit Arm instruction set. On AArch64 the address add can shift the index left by 6 bits. That shift is written `lsl #6`. It multiplies the index by 64.

In the x86-64 disassembly, each further group costs 9 instructions with the byte offset. A group index costs 13 instructions.

This run was on 2026-10-09. It used eight AWS `m7i.4xlarge` instances in `ap-south-2a`. The CPU is an Intel Xeon Platinum 8488C. That chip is Sapphire Rapids.

| | |
| --- | --- |
| Instances | 8 x `m7i.4xlarge`: 8 cores, 16 logical CPUs, 64 GiB |
| Caches | 48 KiB L1 data and 2 MiB L2 per core. `lscpu` reports 105 MiB of L3 |
| Compiler | Clang 22.1.8, `-O3`, `-fno-vectorize -fno-slp-vectorize`, `-mavx2` |
| OS | Ubuntu 26.04, Linux 7.0.0-1004-aws |
| Harness | Google Benchmark 1.9.4. One thread, pinned to logical CPU 8 |
| Binary | One `oa_bench` build, copied unchanged to all eight instances |

An `m7i.4xlarge` uses part of a larger processor. Other tenants can share its L3 cache and its memory bandwidth.

Logical CPU 8 is the second thread of core 0. Logical CPU 0 is the other thread of that core. Both threads stayed online. The benchmark process ran only on CPU 8.

Every design of one table row ran on one instance. The three designs in a row were interleaved. The flags match the [reproduce](#reproduce) section: five runs, 0.1 s minimum, 0.05 s warmup.

| Instance | Cases |
| --- | --- |
| 1 | 2^28 keys, 0% hits |
| 2 | 2^28 keys, 50% hits |
| 3 | 2^28 keys, 100% hits |
| 4 | 2^26 and 2^24 keys, 0% hits |
| 5 | 2^26 and 2^24 keys, 50% hits |
| 6 | 2^26 and 2^24 keys, 100% hits |
| 7 | 2^22, 2^20, and 2^18 keys. Counters at 2^24 keys |
| 8 | 2^16, 2^14, 2^12, and 2^10 keys. Counters at 2^28 keys |

The build machine passed `meson test` before the binary was copied. That is four tests: the hash tables, the other backend's source, probe placement, and a short benchmark.

### Lookup time

The table is the median nanoseconds per lookup, over five runs. Bold marks the lowest median in the row.

| Keys | Table | Hits | Linear probing | Cyclic scalar | SIMD |
| --- | --- | ---: | ---: | ---: | ---: |
| 2^10 | 32 KiB | 0% | 2.23 | 2.93 | **1.66** |
| 2^10 | 32 KiB | 50% | 1.79 | 2.40 | **1.68** |
| 2^10 | 32 KiB | 100% | **1.52** | 1.85 | 1.69 |
| 2^12 | 128 KiB | 0% | 4.75 | 3.36 | **2.07** |
| 2^12 | 128 KiB | 50% | 2.07 | 2.71 | **2.05** |
| 2^12 | 128 KiB | 100% | **1.73** | 2.09 | 1.94 |
| 2^14 | 512 KiB | 0% | 12.87 | 13.17 | **2.87** |
| 2^14 | 512 KiB | 50% | 10.47 | 12.31 | **9.32** |
| 2^14 | 512 KiB | 100% | 5.59 | 3.00 | **2.20** |
| 2^16 | 2 MiB | 0% | 16.89 | 18.39 | **6.05** |
| 2^16 | 2 MiB | 50% | 18.06 | 19.14 | **12.91** |
| 2^16 | 2 MiB | 100% | 7.69 | 7.85 | **4.17** |
| 2^18 | 8 MiB | 0% | 22.92 | 23.62 | **8.43** |
| 2^18 | 8 MiB | 50% | 25.33 | 24.73 | **15.21** |
| 2^18 | 8 MiB | 100% | 10.38 | 10.48 | **5.84** |
| 2^20 | 32 MiB | 0% | 25.38 | 26.61 | **10.21** |
| 2^20 | 32 MiB | 50% | 29.19 | 27.70 | **17.22** |
| 2^20 | 32 MiB | 100% | 12.26 | 12.34 | **7.56** |
| 2^22 | 128 MiB | 0% | 40.95 | 43.59 | **19.02** |
| 2^22 | 128 MiB | 50% | 45.59 | 43.54 | **24.65** |
| 2^22 | 128 MiB | 100% | 20.59 | 20.26 | **16.09** |
| 2^24 | 512 MiB | 0% | 51.09 | 50.79 | **23.87** |
| 2^24 | 512 MiB | 50% | 62.07 | 56.17 | **30.18** |
| 2^24 | 512 MiB | 100% | 27.47 | 27.09 | **22.37** |
| 2^26 | 2 GiB | 0% | 56.19 | 56.54 | **27.39** |
| 2^26 | 2 GiB | 50% | 69.16 | 60.94 | **32.96** |
| 2^26 | 2 GiB | 100% | 30.44 | 29.81 | **25.18** |
| 2^28 | 8 GiB | 0% | 61.46 | 58.54 | **28.55** |
| 2^28 | 8 GiB | 50% | 83.53 | 67.98 | **34.68** |
| 2^28 | 8 GiB | 100% | 30.82 | 29.39 | **23.50** |

At 8 GiB, linear probing takes 61.46 ns per miss. SIMD takes 28.55 ns. SIMD is 2.2 times faster. With half the lookups hitting, the times are 83.53 ns and 34.68 ns. SIMD is 2.4 times faster. With every lookup hitting, the times are 30.82 ns and 23.50 ns. SIMD is 1.3 times faster.

From 2 MiB up, SIMD has the lowest median in every row. Its range of five runs stays apart from the other two ranges.

On misses, from 2 MiB to 8 GiB, SIMD is 2.1 to 2.8 times faster than linear probing. With half the lookups hitting, SIMD is 1.4 to 2.4 times faster. With every lookup hitting, SIMD is 1.2 to 1.8 times faster.

At 32 KiB, with every lookup hitting, linear probing takes 1.52 ns. SIMD takes 1.69 ns. At 128 KiB those times are 1.73 ns and 1.94 ns.

At 128 KiB, with half the lookups hitting, the SIMD median is 2.05 ns. The linear-probing median is 2.07 ns. The ranges of those five runs overlap.

The largest coefficient of variation is 12.87%. That case is SIMD at 32 MiB with every lookup hitting. The five times are 7.11, 7.56, 7.56, 8.04, and 9.75 ns. The slow run is 9.75 ns. In that same row, linear probing's median is 12.26 ns and the cyclic scan's median is 12.34 ns.

Five cases vary by more than 5%. All five are at 32 MiB or at 128 MiB.

| Lookup | Table | Hits | Variation |
| --- | --- | ---: | ---: |
| SIMD | 32 MiB | 100% | 12.87% |
| Linear probing | 128 MiB | 50% | 6.97% |
| Cyclic scalar | 128 MiB | 100% | 6.31% |
| SIMD | 128 MiB | 100% | 5.78% |
| Linear probing | 128 MiB | 100% | 5.15% |

### Hardware counters

These counts are per lookup. They come from one `OA_PMU=1` pass on the instance that owned that size. The pass reads the counters once per batch, which adds time, so its times are not used. The instruction counts at 2^24 keys and at 2^28 keys agree within 0.01 per lookup.

| Keys | Hits | Design | Instructions | Cycles | Branch misses |
| --- | ---: | --- | ---: | ---: | ---: |
| 2^24 | 0% | Linear probing | 44.00 | 201.7 | 1.020 |
| 2^24 | 0% | Cyclic scalar | 66.16 | 195.0 | 1.044 |
| 2^24 | 0% | SIMD | 33.06 | 94.4 | 0.233 |
| 2^24 | 50% | Linear probing | 39.50 | 218.0 | 1.075 |
| 2^24 | 50% | Cyclic scalar | 54.05 | 209.1 | 1.085 |
| 2^24 | 50% | SIMD | 33.25 | 109.7 | 0.701 |
| 2^24 | 100% | Linear probing | 35.00 | 97.7 | 0.488 |
| 2^24 | 100% | Cyclic scalar | 41.96 | 96.2 | 0.485 |
| 2^24 | 100% | SIMD | 33.45 | 76.0 | 0.050 |
| 2^28 | 0% | Linear probing | 44.00 | 231.5 | 1.022 |
| 2^28 | 0% | Cyclic scalar | 66.16 | 218.5 | 1.041 |
| 2^28 | 0% | SIMD | 33.06 | 108.6 | 0.233 |
| 2^28 | 50% | Linear probing | 39.50 | 246.8 | 1.074 |
| 2^28 | 50% | Cyclic scalar | 54.06 | 232.6 | 1.087 |
| 2^28 | 50% | SIMD | 33.25 | 123.5 | 0.702 |
| 2^28 | 100% | Linear probing | 35.00 | 111.8 | 0.480 |
| 2^28 | 100% | Cyclic scalar | 41.96 | 107.4 | 0.486 |
| 2^28 | 100% | SIMD | 33.45 | 89.0 | 0.050 |

At 8 GiB, a SIMD miss runs 33.06 instructions and misses the branch predictor 0.233 times. Linear probing runs 44.00 instructions and misses the predictor 1.022 times. The SIMD miss takes 108.6 cycles. The linear-probing miss takes 231.5 cycles.

## How the results were checked

Correctness:

- The tests check every lookup against a reference table. They cover keys that hash to the same first slot. They cover full groups. The probe can pass the last slot and continue at the first slot. That case is called wraparound. The tests cover that case too.

- They also cover keys with high bits set. They cover a second insert of a key that is already present. They cover the limits on table size, and an empty table. They check that the first linear-probing cell starts on a 64-byte boundary.

- One tool reports invalid memory access. That tool is AddressSanitizer. Another tool reports undefined behavior. That tool is UndefinedBehaviorSanitizer. The same tests pass under both tools.

- A second test binary runs the same checks on the SIMD lookup for the other instruction set. The SIMD code for one instruction set is called a backend. This lookup is the other backend. The host CPU is the CPU that runs the test. The lookup is compiled for that CPU. This works because both backends use only Clang's vector types.

- After timing, every benchmark run checks its hit count and its value checksum. It fails on a mismatch.

- The probe statistics come from replaying insertion and lookup on plain key arrays. Before counting, the replay is checked against the real tables. Every key must land in the same slot. Its linear-probing averages of 1.50 and 2.50 compares match Knuth's formulas.

Measurement:

- Every case ran five times. The order of all runs was randomly interleaved. The tables report the median. Among the three current designs, the largest coefficient of variation is 6.83%.

- From 2 MiB up, the fastest design's range of five runs overlaps another design in one row only. That row is all hits at 32 MiB.

- One binary was copied unchanged to four dedicated instances. All designs of a table row ran on the same instance. The process was pinned to one core.

- The current source compiles to the same timed loops as the measured binary. At 2 MiB, the code addresses do not matter. Both builds gave the same miss times within 0.4%. At 512 KiB, the addresses do matter, as shown above.

- The hardware counters come from a separate run. Reading the counters adds time. Counts come only from that run. Times come only from the main run.

- The final numbers come from the dedicated instances. A first run on a shared machine was too noisy. The next section describes it.

### Shared-machine run

Before the dedicated instances, the full run of all five designs went on the 96-core `m8g.24xlarge` development machine. Other work was running on it. Its load average was 8.0 at the start.

Run-to-run variation reached 54.36%. That case is per-lane masks, at 8 MiB, with all hits. Of the 150 cases, 23 varied by more than 10%. All 23 were at 8 MiB or at 32 MiB.

In two rows, that noise changed which design was fastest.

For 8 MiB with all hits, the shared machine measured SIMD at 11.90 ns. It measured per-lane masks at 7.22 ns. The dedicated instances measured SIMD at 4.78 ns. They measured per-lane masks at 6.90 ns.

For 8 GiB with all hits, the shared machine put linear probing ahead of SIMD. Linear probing took 15.88 ns. SIMD took 16.18 ns. On the dedicated instances, linear probing took 15.74 ns. SIMD took 15.30 ns. Those two ranges do not overlap.

No time from this run is used anywhere else on this page. None of those times are used in the README.

## Data

### Hardware counters

These counts are for the benchmark process. They leave out the kernel. Each figure is per lookup. They come from a separate `OA_PMU=1` run. Each case is run once. Branch misses are Linux's generic `branch-misses` event.

A counter run reads the counters once per pass. That read adds time. The times from this run are not used.

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

### Probe statistics

`build/oa_probe_stats` replays insertion and lookup on plain key arrays. It counts what each lookup reads. A line is one 64-byte cache line. These counts are for 2^28 keys.

| Probe order | Hit: key compares | Hit: lines | Hit: 2+ lines | Hit: first compare matches | Miss: key compares | Miss: lines | Miss: 2+ lines |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Linear (step 1) | 1.5000 | 1.1250 | 11.08% | 75.00% | 2.4999 | 1.3750 | 28.43% |
| Grouped, lane 0 first (step 2) | 2.0994 | 1.0497 | 4.08% | 42.98% | 4.9134 | 1.2284 | 16.33% |
| Grouped, lane `hash & 3` first (step 3) | 1.5177 | 1.0497 | 4.08% | 75.00% | 2.5586 | 1.2284 | 16.33% |

Full groups are 16.33% of all groups. Steps 4 and 5 search the lane-0 table one whole group at a time. They read the same lines as step 2.

The next table is the share of lookups by how many key compares happen before the loop exits.

| Lookups | 1 | 2 | 3 | 4 | 5+ |
| --- | ---: | ---: | ---: | ---: | ---: |
| Linear hits | 75.00% | 14.35% | 5.15% | 2.36% | 3.14% |
| Linear misses | 50.00% | 19.67% | 10.48% | 6.29% | 13.56% |
| Cyclic hits | 75.00% | 14.25% | 4.85% | 1.82% | 4.08% |
| Cyclic misses | 50.01% | 19.42% | 9.62% | 4.62% | 16.33% |

Before counting, `--verify` built the real tables at 2^20 keys. For each of the three tables, the replay put all 1,048,576 keys in the same slots as the real table.

### Tuning experiments

These experiments ran on the shared `m8g.24xlarge` development machine. Their times cannot be compared with the tables above. The full log is in the git history of this file.

- Cyclic start lane. Starting the scalar scan at `hash & 3` raised the share of hits found by the first compare from 43% to 75%. It cut compares per hit from 2.10 to 1.52.

- One loop for the cyclic scan. The lookup returns from a single loop. It returns on the first match, or on the first empty lane. This replaced a version with a separate test for the first lane. At 2^24 keys, hit time fell from 18.8 ns to 16.3 ns.

- Cheaper group address: the probe position is kept as a cell index. Clang can then address a group with one `add ..., lsl #4`. The other form starts with `lsr`, a shift of bits to the right. It then does `add ..., lsl #6`. In a paired test at 2^28 keys, this saved about 1 ns per lookup. The same run held the control experiment above.

- Two SIMD variants were slower. `countl_zero` counts leading zero bits. One variant indexed the values with `3 - countl_zero / 16`. A hit took 33 instructions.

- Clang widened a signed number by copying its sign bit. That widening is a sign extension. Clang also kept a test that the pointer is not null. That test is a null check.

- `select` picks one of two values. `reduce_add` adds a vector's lanes into one number. The other variant used both, from xsimd, to compute the value offset, and it needed two 128-bit constants. A vector register is where those SIMD values sit. A function call on AArch64 does not keep the vector registers. The constants were loaded from the stack again on every probe.

### Benchmark names

The measured binary held all five designs. Commit `7eed6d0` later removed steps 2 and 4. It also renamed step 5. The benchmark names in the raw JSON differ from the names in the current source.

| Step | Name in the tables | Benchmark in the measured binary | Benchmark in the current source |
| --- | --- | --- | --- |
| 1 | Linear probing | `ScalarOA` | `ScalarOA` |
| 2 | Lane 0 scan | `ScalarGroupedOA` | Removed |
| 3 | Cyclic scalar | `ScalarCyclicGroupedOA` | `ScalarCyclicGroupedOA` |
| 4 | Per-lane masks | `SIMDGroupedOA` | Removed |
| 5 | SIMD | `SIMDCombinedGroupedOA` | `SIMDGroupedOA` |

## Reproduce

The build needs Clang and the C++20 language. Meson configures the build. Ninja runs the compile. CMake is another build tool the setup needs. clang-format and clang-tidy check the source text.

The scripts in `tools/` need only Python 3. The build was tested with Clang 22.1.8.

On first setup, Meson downloads Google Benchmark. The download is one fixed archive. Meson checks the hash of that archive. The `.clang-format` and `.clang-tidy` files were copied from a database project called ClickHouse.

```sh
CXX=clang++-22 meson setup build --buildtype=release
meson compile -C build
meson test -C build --print-errorlogs
meson compile -C build format-check tidy
```

Run the command below to also test an 8 GiB grouped table. That table is the smallest one whose group index uses hash bit 28. Each test binary builds its own table. The run needs 16 GiB of memory.

```sh
OA_LARGE_TESTS=1 meson test -C build
```

Sanitizer build:

```sh
CXX=clang++-22 meson setup build-sanitize --buildtype=debug -Db_sanitize=address,undefined
meson compile -C build-sanitize
meson test -C build-sanitize --print-errorlogs
```

The full matrix covers 2^10, 2^12, ..., 2^28 keys. Each size is run with 0% hits, with 50% hits, and with 100% hits. On one machine it takes about 45 minutes. Run it on an idle core of a quiet machine. Other processes share the L3 cache and the memory bandwidth.

```sh
taskset -c 8 build/oa_bench \
    --benchmark_min_time=0.1s --benchmark_min_warmup_time=0.05 \
    --benchmark_repetitions=5 --benchmark_enable_random_interleaving=true \
    --benchmark_display_aggregates_only=true \
    --benchmark_out=sweep.json --benchmark_out_format=json
python3 tools/summarize.py sweep.json
python3 tools/plot.py docs/lookup-time.svg sweep.json
```

To run a subset, pass `--benchmark_filter`. For the largest tables, one filter is `--benchmark_filter='.*keys:268435456/'`.

To split the work across machines, give each machine a filter that selects whole rows. One such filter is `'/keys:268435456/hit_percent:0/'`. This keeps every design of a row on one machine. Then pass all the JSON files to both scripts.

Hardware counters come from a separate run with `OA_PMU=1`. `kernel.perf_event_paranoid` is the Linux switch for who may read those counters. The run needs that switch set to 2 or lower. Otherwise the run needs root.

It reads the counters once per pass. The read adds time. Use its counts. Do not use its times.

```sh
sudo env OA_PMU=1 taskset -c 8 build/oa_bench --benchmark_filter='/keys:(16777216|268435456)/' \
    --benchmark_min_time=0.1s --benchmark_out=counters.json --benchmark_out_format=json
python3 tools/summarize.py sweep.json --counters counters.json
```

`oa_probe_stats` counts compares and cache lines by replaying insertion and lookup on plain key arrays. With `--verify`, it first checks that the replay puts every key in the same slot as the real tables.

```sh
build/oa_probe_stats --verify 20
build/oa_probe_stats 28
```

To measure steps 2 and 4, build commit `73ab522`. That commit still contains them.
