#include "hash_tables.hpp"
#include "workload.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace
{

using simd_ht::CyclicGroupedTable;
using simd_ht::GroupedTable;
using simd_ht::Key;
using simd_ht::ScalarTable;
using simd_ht::Value;

static_assert(sizeof(simd_ht::Cell) == 16);
static_assert(sizeof(simd_ht::Group) == 64);
static_assert(alignof(simd_ht::Group) == 64);
static_assert(offsetof(simd_ht::Group, values) == 32);

void require(bool condition, std::string_view message)
{
    if (!condition)
        throw std::runtime_error(std::string(message));
}

template <typename Exception, typename Function>
void expectThrows(Function function, std::string_view message)
{
    try
    {
        function();
    }
    catch (const Exception &)
    {
        return;
    }
    throw std::runtime_error(std::string(message));
}

void checkLookup(
    const ScalarTable & scalar, const GroupedTable & grouped, const CyclicGroupedTable & cyclic, Key key, const Value * expected)
{
    const auto check = [&](const Value * actual, std::string_view name)
    {
        const bool matches = expected ? actual && *actual == *expected : actual == nullptr;
        if (!matches)
            throw std::runtime_error(std::string(name) + " lookup mismatch for key " + std::to_string(key));
    };
    check(scalar.find(key), "Scalar");
    check(grouped.findSimd(key), "SIMD grouped");
    check(cyclic.findScalar(key), "Scalar cyclic grouped");
}

void checkContents(
    const ScalarTable & scalar,
    const GroupedTable & grouped,
    const CyclicGroupedTable & cyclic,
    const std::unordered_map<Key, Value> & expected)
{
    require(scalar.size() == expected.size(), "Scalar table size differs from reference");
    require(grouped.size() == expected.size(), "Grouped table size differs from reference");
    require(cyclic.size() == expected.size(), "Cyclic grouped table size differs from reference");
    for (const auto & [key, value] : expected)
        checkLookup(scalar, grouped, cyclic, key, &value);
    checkLookup(scalar, grouped, cyclic, 0, nullptr);
}

void testSizing()
{
    constexpr std::pair<std::size_t, std::size_t> cases[]
        = {{0, 4}, {1, 4}, {2, 4}, {3, 8}, {4, 8}, {5, 16}, {7, 16}, {8, 16}, {9, 32}, {16, 32}};
    for (const auto & [count, capacity] : cases)
    {
        require(simd_ht::capacityFor(count) == capacity, "Unexpected rounded capacity");
        const ScalarTable scalar(count);
        const GroupedTable grouped(count);
        const CyclicGroupedTable cyclic(count);
        require(scalar.capacity() == capacity, "Unexpected scalar capacity");
        require(grouped.capacity() == capacity, "Unexpected grouped capacity");
        require(cyclic.capacity() == capacity, "Unexpected cyclic grouped capacity");
        require(scalar.allocatedBytes() == 16 * capacity, "Unexpected scalar allocation size");
        require(grouped.allocatedBytes() == 16 * capacity, "Unexpected grouped allocation size");
        require(cyclic.allocatedBytes() == 16 * capacity, "Unexpected cyclic grouped allocation size");
        require(scalar.size() == 0 && grouped.size() == 0 && cyclic.size() == 0, "New tables must be empty");
        checkLookup(scalar, grouped, cyclic, 0, nullptr);
        checkLookup(scalar, grouped, cyclic, 1, nullptr);
        checkLookup(scalar, grouped, cyclic, std::numeric_limits<Key>::max(), nullptr);
    }

    require(simd_ht::capacityFor(std::size_t{1} << 28) == (std::size_t{1} << 29), "Incorrect capacity for the largest benchmark");
    require(simd_ht::capacityFor(std::size_t{1} << 31) == (std::size_t{1} << 32), "Incorrect maximum supported capacity");
    expectThrows<std::length_error>([] { simd_ht::capacityFor((std::size_t{1} << 31) + 1); }, "Oversized key count was accepted");
    expectThrows<std::length_error>(
        [] { simd_ht::capacityFor(std::numeric_limits<std::size_t>::max()); }, "Overflowing key count was accepted");
}

void testSpecialKeys()
{
    ScalarTable scalar(32);
    GroupedTable grouped(32);
    CyclicGroupedTable cyclic(32);
    std::unordered_map<Key, Value> expected;
    const std::array<Key, 7> keys
        = {1,
           Key{1} << 32,
           Key{1} << 63,
           0xffffffffULL,
           0xffffffff00000000ULL,
           std::numeric_limits<Key>::max(),
           std::numeric_limits<Key>::max() - 1};
    for (Key key : keys)
    {
        const Value value = key ^ std::numeric_limits<Value>::max();
        scalar.insert(key, value);
        grouped.insert(key, value);
        cyclic.insert(key, value);
        expected[key] = value;
    }

    require(simd_ht::hashKey(1) == 0x514e28b7U, "Hash does not match the MurmurHash3 finalizer");
    require(simd_ht::hashKey(1) == simd_ht::hashKey(Key{1} << 32), "Hash does not fold both key halves");
    for (Key half = 1; half <= 16; ++half)
    {
        const Key key = (half << 32) | half;
        require(simd_ht::hashKey(key) == 0, "Equal key halves must fold to zero");
        scalar.insert(key, half);
        grouped.insert(key, half);
        cyclic.insert(key, half);
        expected[key] = half;
    }
    checkContents(scalar, grouped, cyclic, expected);
    checkLookup(scalar, grouped, cyclic, (Key{17} << 32) | 17, nullptr);

    expectThrows<std::invalid_argument>([&] { scalar.insert(0, 123); }, "Scalar table accepted the empty key");
    expectThrows<std::invalid_argument>([&] { grouped.insert(0, 123); }, "Grouped table accepted the empty key");
    expectThrows<std::invalid_argument>([&] { cyclic.insert(0, 123); }, "Cyclic grouped table accepted the empty key");
    checkContents(scalar, grouped, cyclic, expected);
}

void testInsertionLimit()
{
    ScalarTable scalar(3);
    GroupedTable grouped(3);
    CyclicGroupedTable cyclic(3);
    std::unordered_map<Key, Value> expected;
    for (Key key = 1; key <= 4; ++key)
    {
        scalar.insert(key, key);
        grouped.insert(key, key);
        cyclic.insert(key, key);
        expected[key] = key;
    }

    expectThrows<std::length_error>([&] { scalar.insert(5, 5); }, "Scalar table exceeded half capacity");
    expectThrows<std::length_error>([&] { grouped.insert(5, 5); }, "Grouped table exceeded half capacity");
    expectThrows<std::length_error>([&] { cyclic.insert(5, 5); }, "Cyclic grouped table exceeded half capacity");
    scalar.insert(2, 0);
    grouped.insert(2, 0);
    cyclic.insert(2, 0);
    expected[2] = 0;
    checkContents(scalar, grouped, cyclic, expected);
    checkLookup(scalar, grouped, cyclic, 5, nullptr);
}

void testWraparound()
{
    ScalarTable scalar(16);
    GroupedTable grouped(16);
    CyclicGroupedTable cyclic(16);
    std::vector<Key> keys;
    for (Key key = 1; key < 10000 && keys.size() < 13; ++key)
    {
        if ((simd_ht::hashKey(key) & 31) == 31)
            keys.push_back(key);
    }
    require(keys.size() == 13, "Could not find keys whose home cell is the final cell");

    std::unordered_map<Key, Value> expected;
    for (std::size_t i = 0; i < 12; ++i)
    {
        const Value value = i;
        scalar.insert(keys[i], value);
        grouped.insert(keys[i], value);
        cyclic.insert(keys[i], value);
        expected[keys[i]] = value;
        checkContents(scalar, grouped, cyclic, expected);
        checkLookup(scalar, grouped, cyclic, keys.back(), nullptr);
    }

    for (std::size_t i = 0; i < 12; ++i)
    {
        const Key key = keys[i];
        scalar.insert(key, key);
        grouped.insert(key, key);
        cyclic.insert(key, key);
        expected[key] = key;
    }
    checkContents(scalar, grouped, cyclic, expected);
    checkLookup(scalar, grouped, cyclic, keys.back(), nullptr);
}

void testCyclicPlacement()
{
    for (std::size_t start_lane = 0; start_lane < 4; ++start_lane)
    {
        CyclicGroupedTable table(16);
        std::vector<Key> keys;
        for (Key key = 1; key < 10000 && keys.size() < 10; ++key)
        {
            if ((simd_ht::hashKey(key) & 31) == 28 + start_lane)
                keys.push_back(key);
        }
        require(keys.size() == 10, "Could not find keys for the cyclic placement test");

        std::uintptr_t base = 0;
        for (std::size_t i = 0; i < 9; ++i)
        {
            table.insert(keys[i], i);
            const Value * found = table.findScalar(keys[i]);
            require(found && *found == i, "Cyclic lookup failed with gaps in a partially occupied group");
            const std::size_t lane = (start_lane + i) & 3;
            const std::size_t value_offset = offsetof(simd_ht::Group, values) + lane * sizeof(Value);
            const auto address = reinterpret_cast<std::uintptr_t>(found);
            require(address % sizeof(simd_ht::Group) == value_offset, "Cyclic insertion used the wrong physical lane");
            if (i == 0)
                base = address - 7 * sizeof(simd_ht::Group) - value_offset;
            const std::size_t group = (7 + i / 4) & 7;
            require(address == base + group * sizeof(simd_ht::Group) + value_offset, "Cyclic group overflow or wraparound is incorrect");

            for (std::size_t j = 0; j <= i; ++j)
            {
                const Value * previous = table.findScalar(keys[j]);
                require(previous && *previous == j, "Cyclic insertion displaced an existing key");
            }
            require(table.findScalar(keys.back()) == nullptr, "Cyclic lookup returned an absent key");
        }
    }
}

void testRandomReference()
{
    // A fixed seed makes test failures reproducible.
    // NOLINTNEXTLINE(bugprone-random-generator-seed,cert-msc32-c,cert-msc51-cpp)
    std::mt19937_64 random(0x73ba94f81904eab1ULL);
    for (std::size_t count : {0, 1, 2, 3, 7, 16, 31, 128, 1025, 4096})
    {
        ScalarTable scalar(count);
        GroupedTable grouped(count);
        CyclicGroupedTable cyclic(count);
        std::unordered_map<Key, Value> expected;
        std::vector<Key> keys;
        while (expected.size() < count)
        {
            const Key key = random();
            if (key == 0 || expected.contains(key))
                continue;
            const Value value = random();
            scalar.insert(key, value);
            grouped.insert(key, value);
            cyclic.insert(key, value);
            expected[key] = value;
            keys.push_back(key);
            checkLookup(scalar, grouped, cyclic, key, &value);
        }

        for (std::size_t i = 0; i < keys.size(); i += 7)
        {
            const Value value = i % 2 ? random() : 0;
            scalar.insert(keys[i], value);
            grouped.insert(keys[i], value);
            cyclic.insert(keys[i], value);
            expected[keys[i]] = value;
        }
        checkContents(scalar, grouped, cyclic, expected);

        for (std::size_t i = 0; i < 4096; ++i)
        {
            const Key key = i % 2 == 0 && !keys.empty() ? keys[random() % keys.size()] : random();
            const auto it = expected.find(key);
            checkLookup(scalar, grouped, cyclic, key, it == expected.end() ? nullptr : &it->second);
        }
    }
}

void testWorkloads()
{
    for (std::size_t count : {1, 2, 3, 4, 7, 64, 257, 1024})
    {
        ScalarTable scalar(count);
        GroupedTable grouped(count);
        CyclicGroupedTable cyclic(count);
        std::unordered_map<Key, Value> expected;
        for (std::size_t i = 0; i < count; ++i)
        {
            const Key key = simd_ht::keyForIndex(i);
            const Value value = simd_ht::valueForKey(key);
            require(key != 0, "Workload generated the empty key");
            require(expected.emplace(key, value).second, "Workload generated duplicate insertion keys");
            scalar.insert(key, value);
            grouped.insert(key, value);
            cyclic.insert(key, value);
        }

        for (unsigned hit_percent : {0, 50, 100})
        {
            const auto workload = simd_ht::makeWorkload(count, hit_percent);
            const auto repeated = simd_ht::makeWorkload(count, hit_percent);
            require(workload.queries == repeated.queries, "Query traces are not reproducible");
            require(workload.queries.size() == count, "Query trace does not cover the requested count");
            std::unordered_set<Key> queries;
            uint64_t hits = 0;
            uint64_t checksum = 0;
            for (Key key : workload.queries)
            {
                require(key != 0, "Query trace contains the empty key");
                require(queries.insert(key).second, "Query trace contains duplicate keys");
                const auto it = expected.find(key);
                checkLookup(scalar, grouped, cyclic, key, it == expected.end() ? nullptr : &it->second);
                if (it != expected.end())
                {
                    ++hits;
                    checksum += it->second;
                }
            }
            require(hits == count * hit_percent / 100, "Query trace has an incorrect hit rate");
            require(workload.expected_hits == hits, "Workload expected hit count is incorrect");
            require(workload.expected_checksum == checksum, "Workload expected value checksum is incorrect");
        }
    }

    std::unordered_set<Key> boundary_keys;
    for (uint64_t index : {uint64_t{0}, uint64_t{1}, (uint64_t{1} << 28) - 1, uint64_t{1} << 28, (uint64_t{1} << 29) - 1})
    {
        const Key key = simd_ht::keyForIndex(index);
        require(key != 0 && boundary_keys.insert(key).second, "Key generation failed at a benchmark size boundary");
    }

    expectThrows<std::invalid_argument>([] { simd_ht::makeWorkload(0, 50); }, "Empty workload was accepted");
    expectThrows<std::invalid_argument>([] { simd_ht::makeWorkload((std::size_t{1} << 28) + 1, 50); }, "Oversized workload was accepted");
    expectThrows<std::invalid_argument>([] { simd_ht::makeWorkload(16, 25); }, "Unsupported hit percentage was accepted");
}

}

int main()
{
    try
    {
        testSizing();
        testSpecialKeys();
        testInsertionLimit();
        testWraparound();
        testCyclicPlacement();
        testRandomReference();
        testWorkloads();
        std::cout << "Hash table tests passed\n";
        return 0;
    }
    catch (const std::exception & error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
