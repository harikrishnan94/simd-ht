# Docs rules for experiment repos

These rules are for repos that take a known algorithm or data structure and improve it in measured steps. A step can change the algorithm, the data layout, memory access, branches, instructions, threads, or the allocator.

Such a repo has two documents. The README states the result and shows the evidence a reader needs to believe it. The full record, usually RESULTS.md, holds every measurement, the method, the checks, and how to reproduce them.

The examples come from two projects: a SIMD probe for an open-addressing hash table, and a parallel hash-join table.

## Readers

The README has two readers.

- The skimmer is a senior engineer or engineering manager with about two minutes. They read the first screen, the headings and the tables. Then they decide whether the work is deep, careful and honest.
- The checker is an engineer in the field who reads everything and may rerun it.

Write the first screen for the skimmer and the rest for the checker. Both are convinced by evidence. Neither is convinced by adjectives.

Define a term at first use only if the checker may not know it. Terms the project invented get a definition; field basics do not.

- Example: define "lane" and "home group". Do not define KiB, cache line or branch misprediction.

## What a senior reader looks for

Each signal needs a place in the README where the skimmer will see it.

| Signal | Where it shows |
| --- | --- |
| A real problem and a fair baseline: the known algorithm, cited, not a weakened copy | Summary, starting point |
| A result with its number and conditions | First screen |
| Depth: steps that cross layers, from the algorithm down to the core pipeline | Step table |
| Cause, not just correlation: counters that explain the time, and a control experiment | Why it wins |
| Judgment: failed steps kept, losses and limits stated, guesses labeled | Claims, step table, limits |
| Care: repeated interleaved runs, one binary, checked results, a second platform | How the numbers were checked |
| A repo that works: a clean clone builds and runs with the listed commands | Build and run |

## Plan the experiment before writing it up

- Name the starting point: the known algorithm, cited, and the implementation it is measured against.
  - "Birler, Schmidt, Fent and Neumann, 'Simple, Efficient, and Robust Hash Tables for Join Processing', DaMoN '24, https://doi.org/10.1145/3662010.3663442"
- Number the designs as steps. The baseline is step 1, and each later step makes one change. If a step makes two changes, split it or say so.
- For each step, state the layer it changes, what you expect, and the measurement that tests it. Use the measurement that belongs to the layer:

| Layer | Typical change | Measure |
| --- | --- | --- |
| Algorithm | probe order, partitioning, what is compared | operations per lookup, from a replay or a model |
| Data layout and memory | cell size, grouping, alignment, contiguous or linked storage | cache lines or cache misses per operation, bytes per key |
| Branches | loop exits, data-dependent tests | branch mispredictions per operation |
| Instructions | SIMD, mask building, address arithmetic | instructions per operation, path lengths from the disassembly |
| Core pipeline | overlap of independent work | cycles and stall cycles per operation, control experiments |
| Threads | partitioning, locks, merge steps | time against thread count, each phase timed alone |
| Allocator and OS | allocation size, page reuse | page faults, first run against later runs |
| Code placement | where a hot loop sits in memory | two builds of the same instructions |

- Keep the steps that failed. A step that made things slower is evidence for the explanation of the step that worked.
  - "Step 4 cut mispredictions to 0.25 per miss but ran 45.17 instructions, and was 6% slower than step 3."
- Test an explanation with a control experiment that changes only the suspected cause.
  - "Adding 3 unused `and` instructions to linear probing cost 12.8% at 8 GiB, with all hits."
- When adapting an algorithm to a new setting, write each obstacle as a problem and its fix.
  - Problem: "A key's probe can run past its partition's range, into cells another thread writes."
  - Fix: "The probe stops at the range end and puts the key on the partition's overflow list. One thread places those keys after all threads finish."
- Where a model predicts a number, report the model and the measurement side by side.
  - "Measured: 53 overflow keys. Model: 55.6 (Janson and Viola, arXiv:1410.5967, eq. 8.14)."

## README: the first screen

The first screen is what GitHub shows before any scrolling: about 20 lines of text and one image. It holds these three parts, in this order. No definitions, setup paragraphs or badges come before them.

1. The project name as the only H1. Then two or three sentences: what was built, what it is compared against, and the headline number.
   - Bad: "This page reports timed lookups on three hash tables."
   - Good: "Three open-addressing lookups on 64-bit keys. The SIMD one compares four keys at once and tests them with one branch. Its misses are 2.1-2.8x faster than linear probing, from 2 MiB to 8 GiB."
2. Three to five bullets on what the work shows. Each opens with the claim in bold, then the number and its conditions. One bullet shows which layer the gain came from. One bullet is a case where the design loses.
   - "**Fewer branch misses alone did not pay**: a SIMD lookup at 0.25 mispredictions per miss was 6% slower than the scalar scan. It ran 45 instructions; the final design runs 29."
   - "**Builds with heavy key skew are still slower at 16 threads and up**: up to 1.37x."
3. The chart. Without a chart, the headline table.

## README: the rest, in order

4. The starting point and the designs, side by side: the baseline first, the final design last. Each gets the same diagram and one line on what it changes. Name a design by what it does, with its code or benchmark name in code font.
5. Results: one setup line (hardware, compiler, workload, runs, statistic), then the headline table. A second platform gets its own heading and the same table layout.
6. The step table: one row per step, with the change, its layer, that layer's measurement, the time, and whether the step was kept. One sentence after it says what the steps show.
7. Why it wins: the measurements that explain the result, and the control experiment. Label each explanation as described under Evidence.
8. How it works: the property the design relies on, then a worked example on the smallest input that shows it. Use a short sequence of text diagrams. Each one changes one thing and marks it. Then the hot loop, copied from the disassembly. Problems and fixes from the adaptation go here.
9. How the numbers were checked: four to six bullets on correctness, runs, binary, counters, spread and the second platform. Link the matching section of the full record.
10. Limits: where the design loses or does not apply, stated as facts.
    - "The table is insert-only. The `keys[3]` test does not work if a key can be deleted."
11. Build and run: at most five commands from a clean clone. Then a link to the reproduce section of the full record, then License.

A reader who reads only the headings should get the story. Where a section has a finding, its heading states it.

- Bad: "Analysis". Good: "Fewer branch misses alone did not pay".

## Tone

- No self-praise: novel, blazing, significant, dramatically, state-of-the-art. Replace each adjective with the number it stands for.
  - Bad: "A blazing-fast SIMD hash table." Good: "Misses are 2.1-2.8x faster than linear probing."
- Use "I" for choices and guesses. The reader then sees who made each call.
  - "On AVX2, I keep the probe position as a byte offset."
  - "My reading is that …"
- Put the cost next to the gain: "The `keys[3]` test needs a table that never deletes."
- State what is not known as a fact. Do not promise to find out.
- No emoji, no wall of badges, no "Key features" list and no "Why this matters" heading.

## Full record: section order

1. What the page covers, and the date and machines of the main run. Any section that uses another run says so. Then a list of sections.
2. Setup: how the input is generated, what the timer covers and leaves out, and the check after each run. Then a table of machines, compiler, harness and binary, and which machine ran which cases.
3. The steps: the step table with every layer measurement, then one subsection per step. Each subsection has a diagram, the change, its layer measurement, and the effect. For a step the current code no longer has, name the commit that does.
4. The explanation: what the measurements suggest, the control experiment, and what was not measured.
5. Results for every input size, with the best result in each row in bold. Group the sizes by regime, such as "fits in L2" and "larger than the last-level cache". Then the run-to-run variation.
6. Each other platform: its own setup table and the same table layout.
7. How the results were checked: correctness first, then measurement.
8. Data: hardware counters, operation statistics, tuning experiments, and a map from names in the measured binary to names in the current code.
9. Reproduce: the exact commands, how to split the cases across machines, the counter run, and the analysis tools.

## Method: what every run states

The README gives the setup in one line. The full record gives all of it.

- Date, machine type, CPU model, core count, cache sizes, memory, OS and kernel.
- Compiler, version and flags, including whether auto-vectorization is on. Library versions.
- One binary for every machine in a run. If the current code builds differently, say how the disassembly differs.
- Thread pinning, and what runs on the other hardware thread of the same core.
- Warm-up, minimum run time, repetitions, and run order. Run every design of one comparison on the same machine, in one randomly interleaved run.
- The statistic and the spread: the median, and the coefficient of variation or the range. When two designs' ranges overlap, say so and do not name a winner.
- Hardware counters come from a separate run. Reading counters adds time, so use that run's counts and never its times.
- A correctness check after every timed run, such as a checksum and a hit count. Tests against a reference and sanitizer runs go in the full record.
- A run on a shared or noisy machine is reported as noisy, and none of its numbers are used elsewhere.
- Effects of the environment, with numbers. Examples: a branch predictor that learns a short input, a loop whose speed depends on its address, an allocator that returns pages between runs.
  - "A warm-up run does not mean warm pages. jemalloc can purge the table after any query, so a timed run can still fault it back in."

## Evidence

- Every number in the README is also in the full record, with the same rounding. Copy numbers from the output of the tool that produced them.
- The line above each table names the platform, input size, unit and statistic: "8 GiB table, Graviton4, ns per lookup, median of 5 runs."
- Back each claim with the measurement of its layer. A claim about memory cites cache lines or misses; a claim about branches cites mispredictions.
  - "Stall cycles per L2 miss fell from 30 to 25 (duplicate keys, Graviton4, 32 threads, probe phase)."
- Label each explanation as measured, modeled or inferred. Measured: name the run. Modeled: give the formula and its source. Inferred: say so, and say what was not measured: "I have not measured the number of misses in flight."
- Cite outside work where it is used, with author, title, and section or URL: "(Knuth, *The Art of Computer Programming*, Vol. 3, §6.4)".
- Keep the results where the design loses. Bold marks the best result in a row, whichever design it is.

## What stays out

- History: earlier versions, "my first version", what changed since the last run. Git history keeps it. Describe a rejected design by what it does.
  - Bad: "My first SIMD version removed most of those mispredictions."
  - Good: "A SIMD lookup that builds one mask bit per lane removes most of those mispredictions."
- Superseded numbers. Replace them, and do not keep a note of what the page used to say.
- Test names. Say what the tests check; the command that runs them is enough.
- Plans and promises of future work. State a limit as a fact.
- From the README only: per-size tables, raw counters, checks and tuning experiments. Link the section of the full record instead.

## The repo around the docs

A reviewer who likes the README opens the code and the log next.

- A clean clone builds, tests and runs with the README's commands. Try it before publishing.
- No scratch files, raw dumps or commented-out experiments in the tree. A removed design lives in a named commit.
- One change per commit, each with a short imperative message.
- A license file.
