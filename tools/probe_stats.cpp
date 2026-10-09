#include "hash_tables.hpp"
#include "workload.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{

using simd_ht::Key;

/// Totals over one set of lookups. A cache line is 64 bytes: four 16-byte cells, or one group.
struct Counts
{
    uint64_t lookups = 0;
    uint64_t key_compares = 0;
    uint64_t lines = 0;
    uint64_t multi_line = 0;
    uint64_t first_compare_hits = 0;
    std::vector<uint64_t> compares_histogram = std::vector<uint64_t>(6);

    void add(uint64_t compares, uint64_t touched_lines, bool first_compare_hit)
    {
        ++lookups;
        key_compares += compares;
        lines += touched_lines;
        multi_line += touched_lines > 1;
        first_compare_hits += first_compare_hit;
        ++compares_histogram[std::min<uint64_t>(compares, compares_histogram.size() - 1)];
    }
};

struct Probe
{
    uint64_t compares = 0;
    uint64_t lines = 1;
    bool found = false;
    size_t slot = 0;
};

/// Linear probing over 16-byte cells, as in ScalarTable.
class LinearKeys
{
public:
    explicit LinearKeys(size_t distinct_keys)
        : keys(simd_ht::capacityFor(distinct_keys))
        , mask(keys.size() - 1)
    {
    }

    void insert(Key key)
    {
        size_t pos = simd_ht::hashKey(key) & mask;
        while (keys[pos] != 0)
            pos = (pos + 1) & mask;
        keys[pos] = key;
    }

    Probe find(Key key) const
    {
        Probe probe;
        size_t pos = simd_ht::hashKey(key) & mask;
        for (;;)
        {
            ++probe.compares;
            if (keys[pos] == key || keys[pos] == 0)
            {
                probe.found = keys[pos] == key;
                probe.slot = pos;
                return probe;
            }
            pos = (pos + 1) & mask;
            probe.lines += pos % 4 == 0;
        }
    }

private:
    std::vector<Key> keys;
    size_t mask;
};

/// Four-key groups. Insertion fills the first empty lane in scan order; Cyclic starts that order at hash & 3.
template <bool Cyclic>
class GroupedKeys
{
public:
    explicit GroupedKeys(size_t distinct_keys)
        : keys(simd_ht::capacityFor(distinct_keys))
        , group_mask(keys.size() / 4 - 1)
    {
    }

    void insert(Key key)
    {
        const uint32_t hash = simd_ht::hashKey(key);
        for (size_t group = ((hash & (keys.size() - 1)) >> 2);; group = (group + 1) & group_mask)
            for (size_t offset = 0; offset < 4; ++offset)
                if (Key & slot = keys[group * 4 + lane(hash, offset)]; slot == 0)
                {
                    slot = key;
                    return;
                }
    }

    /// Scalar scan in lane order. The lane-zero scan compares a whole group before testing for an empty lane;
    /// the cyclic scan stops at the first empty lane, like CyclicGroupedTable::findScalar.
    Probe find(Key key) const
    {
        Probe probe;
        probe.lines = 0;
        const uint32_t hash = simd_ht::hashKey(key);
        for (size_t group = ((hash & (keys.size() - 1)) >> 2);; group = (group + 1) & group_mask)
        {
            ++probe.lines;
            bool empty = false;
            for (size_t offset = 0; offset < 4; ++offset)
            {
                const size_t slot = group * 4 + lane(hash, offset);
                ++probe.compares;
                if (keys[slot] == key)
                {
                    probe.found = true;
                    probe.slot = slot;
                    return probe;
                }
                empty |= keys[slot] == 0;
                if (Cyclic && empty)
                    return probe;
            }
            if (empty)
                return probe;
        }
    }

    double fullGroupFraction() const
    {
        uint64_t full = 0;
        for (size_t group = 0; group <= group_mask; ++group)
            full += keys[group * 4] != 0 && keys[group * 4 + 1] != 0 && keys[group * 4 + 2] != 0 && keys[group * 4 + 3] != 0;
        return static_cast<double>(full) / static_cast<double>(group_mask + 1);
    }

private:
    std::vector<Key> keys;
    size_t group_mask;

    static size_t lane(uint32_t hash, size_t offset) { return Cyclic ? (hash + offset) & 3 : offset; }
};

void require(bool condition, const std::string & message)
{
    if (!condition)
        throw std::runtime_error(message);
}

/// Checks that the replay puts every key where the real table does. Value addresses differ by 16 bytes per
/// cell, or by 64 bytes per group plus 8 bytes per lane, so differences from the first key locate each slot.
template <class Table, auto Find, class Replay>
void verify(size_t distinct_keys, const Replay & replay, bool grouped, const char * name)
{
    Table table(distinct_keys);
    for (size_t i = 0; i < distinct_keys; ++i)
        table.insert(simd_ht::keyForIndex(i), 0);
    const auto address = [&](size_t slot) { return static_cast<std::intptr_t>(grouped ? slot / 4 * 64 + slot % 4 * 8 : slot * 16); };
    const auto actual = [&](Key key) { return reinterpret_cast<std::intptr_t>((table.*Find)(key)); };
    const Key first = simd_ht::keyForIndex(0);
    const std::intptr_t first_actual = actual(first);
    const std::intptr_t first_expected = address(replay.find(first).slot);
    for (size_t i = 0; i < distinct_keys; ++i)
    {
        const Key key = simd_ht::keyForIndex(i);
        const Probe probe = replay.find(key);
        require(probe.found, std::string(name) + ": replay lost a key");
        require(
            actual(key) - first_actual == address(probe.slot) - first_expected, std::string(name) + ": replay slot differs from the table");
    }
    std::cout << "Verified " << distinct_keys << " slots for " << name << '\n';
}

template <class Replay>
std::pair<Counts, Counts> measure(size_t distinct_keys, const Replay & replay)
{
    Counts hits;
    Counts misses;
    for (size_t i = 0; i < distinct_keys; ++i)
    {
        const Probe hit = replay.find(simd_ht::keyForIndex(i));
        require(hit.found, "Inserted key was not found");
        hits.add(hit.compares, hit.lines, hit.compares == 1);
        const Probe miss = replay.find(simd_ht::keyForIndex(distinct_keys + i));
        require(!miss.found, "Absent key was found");
        misses.add(miss.compares, miss.lines, false);
    }
    return {hits, misses};
}

double perLookup(uint64_t total, const Counts & counts)
{
    return static_cast<double>(total) / static_cast<double>(counts.lookups);
}

double percent(uint64_t part, const Counts & counts)
{
    return 100.0 * perLookup(part, counts);
}

void printRow(const char * name, const Counts & hits, const Counts & misses)
{
    std::printf(
        "| %s | %.4f | %.4f | %.2f%% | %.2f%% | %.4f | %.4f | %.2f%% |\n",
        name,
        perLookup(hits.key_compares, hits),
        perLookup(hits.lines, hits),
        percent(hits.multi_line, hits),
        percent(hits.first_compare_hits, hits),
        perLookup(misses.key_compares, misses),
        perLookup(misses.lines, misses),
        percent(misses.multi_line, misses));
}

void printHistogram(const char * name, const Counts & counts)
{
    std::printf("| %s |", name);
    for (size_t i = 1; i < counts.compares_histogram.size(); ++i)
        std::printf(" %.2f%% |", percent(counts.compares_histogram[i], counts));
    std::printf("\n");
}

}

int main(int argc, char ** argv)
{
    try
    {
        bool verify_slots = false;
        int log2_keys = -1;
        for (int i = 1; i < argc; ++i)
        {
            if (std::string_view(argv[i]) == "--verify")
                verify_slots = true;
            else
                log2_keys = std::stoi(argv[i]);
        }
        require(log2_keys >= 1 && log2_keys <= 28, "Usage: oa_probe_stats [--verify] LOG2_KEYS (1 to 28)");
        const size_t distinct_keys = size_t{1} << log2_keys;

        LinearKeys linear(distinct_keys);
        GroupedKeys<false> lane_zero(distinct_keys);
        GroupedKeys<true> cyclic(distinct_keys);
        for (size_t i = 0; i < distinct_keys; ++i)
        {
            const Key key = simd_ht::keyForIndex(i);
            linear.insert(key);
            lane_zero.insert(key);
            cyclic.insert(key);
        }
        if (verify_slots)
        {
            verify<simd_ht::ScalarTable, &simd_ht::ScalarTable::find>(distinct_keys, linear, false, "ScalarTable");
            verify<simd_ht::GroupedTable, &simd_ht::GroupedTable::findSimd>(distinct_keys, lane_zero, true, "GroupedTable");
            verify<simd_ht::CyclicGroupedTable, &simd_ht::CyclicGroupedTable::findScalar>(
                distinct_keys, cyclic, true, "CyclicGroupedTable");
        }

        const auto [linear_hits, linear_misses] = measure(distinct_keys, linear);
        const auto [lane_zero_hits, lane_zero_misses] = measure(distinct_keys, lane_zero);
        const auto [cyclic_hits, cyclic_misses] = measure(distinct_keys, cyclic);

        std::printf("2^%d keys, load factor 0.5. Per lookup; a line is one 64-byte cache line.\n\n", log2_keys);
        std::printf(
            "| Probe order | Hit: key compares | Hit: lines | Hit: 2+ lines | Hit: first compare matches "
            "| Miss: key compares | Miss: lines | Miss: 2+ lines |\n");
        std::printf("| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |\n");
        printRow("Linear (ScalarOA)", linear_hits, linear_misses);
        printRow("Grouped, lane 0 first (layout of SIMDGroupedOA)", lane_zero_hits, lane_zero_misses);
        printRow("Grouped, lane hash & 3 first (ScalarCyclicGroupedOA)", cyclic_hits, cyclic_misses);
        std::printf("\nSIMDGroupedOA reads the lane 0 layout one whole group at a time, so its line counts match that row.\n");
        std::printf("Full groups: %.2f%% of all groups.\n\n", 100.0 * lane_zero.fullGroupFraction());

        std::printf("Share of lookups by key compares before the loop exits:\n\n");
        std::printf("| Lookups | 1 | 2 | 3 | 4 | 5+ |\n| --- | ---: | ---: | ---: | ---: | ---: |\n");
        printHistogram("Linear hits", linear_hits);
        printHistogram("Linear misses", linear_misses);
        printHistogram("Cyclic hits", cyclic_hits);
        printHistogram("Cyclic misses", cyclic_misses);
        return 0;
    }
    catch (const std::exception & error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
