#pragma once

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

#include <xsimd/xsimd.hpp>

namespace simd_ht
{

using Key = uint64_t;
using Value = uint64_t;

#if defined(SIMD_HT_AVX2)
using LookupArch = xsimd::avx2;
#elif defined(SIMD_HT_NEON64)
using LookupArch = xsimd::neon64;
#else
#error "Select the NEON64 or AVX2 SIMD backend in Meson."
#endif

inline uint32_t hashKey(Key key)
{
    uint32_t hash = static_cast<uint32_t>(key) ^ static_cast<uint32_t>(key >> 32);
    hash ^= hash >> 16;
    hash *= 0x85ebca6bU;
    hash ^= hash >> 13;
    hash *= 0xc2b2ae35U;
    return hash ^ (hash >> 16);
}

inline size_t capacityFor(size_t distinct_keys)
{
    static_assert(sizeof(size_t) >= 8);
    if (distinct_keys > (size_t{1} << 31))
        throw std::length_error("Capacity exceeds the 32-bit hash address space");
    return std::max(size_t{4}, 2 * std::bit_ceil(distinct_keys));
}

struct Cell
{
    Key key = 0;
    Value value = 0;
};

struct alignas(64) Group
{
    Key keys[4]{};
    Value values[4]{};
};

static_assert(sizeof(Cell) == 16);
static_assert(sizeof(Group) == 64 && alignof(Group) == 64);
static_assert(offsetof(Group, values) == 32);

class ScalarTable
{
public:
    explicit ScalarTable(size_t distinct_keys)
        : cells(capacityFor(distinct_keys))
        , cell_mask(cells.size() - 1)
    {
    }

    void insert(Key key, Value value)
    {
        if (key == 0)
            throw std::invalid_argument("Key zero is reserved for empty cells");
        size_t pos = hashKey(key) & cell_mask;
        for (;;)
        {
            auto & cell = cells[pos];
            if (cell.key == key)
            {
                cell.value = value;
                return;
            }
            if (cell.key == 0)
            {
                if (key_count == cells.size() / 2)
                    throw std::length_error("Table reached its maximum load factor");
                cell = {key, value};
                ++key_count;
                return;
            }
            pos = (pos + 1) & cell_mask;
        }
    }

    const Value * find(Key key) const
    {
        if (key == 0)
            return nullptr;
        size_t pos = hashKey(key) & cell_mask;
        while (cells[pos].key != key)
        {
            if (cells[pos].key == 0)
                return nullptr;
            pos = (pos + 1) & cell_mask;
        }
        return &cells[pos].value;
    }

    size_t size() const { return key_count; }
    size_t capacity() const { return cells.size(); }
    size_t allocatedBytes() const { return cells.size() * sizeof(Cell); }

private:
    std::vector<Cell, xsimd::aligned_allocator<Cell, 64>> cells;
    size_t cell_mask;
    size_t key_count = 0;
};

class GroupedTable
{
public:
    explicit GroupedTable(size_t distinct_keys)
        : groups(capacityFor(distinct_keys) / 4)
        , cell_mask(groups.size() * 4 - 1)
    {
    }

    void insert(Key key, Value value)
    {
        if (key == 0)
            throw std::invalid_argument("Key zero is reserved for empty cells");
        size_t pos = (hashKey(key) & cell_mask) >> 2;
        for (;;)
        {
            auto & group = groups[pos];
            size_t empty_lane = 4;
            for (size_t lane = 0; lane < 4; ++lane)
            {
                if (group.keys[lane] == key)
                {
                    group.values[lane] = value;
                    return;
                }
                if (group.keys[lane] == 0 && empty_lane == 4)
                    empty_lane = lane;
            }
            if (empty_lane != 4)
            {
                if (key_count == capacity() / 2)
                    throw std::length_error("Table reached its maximum load factor");
                group.keys[empty_lane] = key;
                group.values[empty_lane] = value;
                ++key_count;
                return;
            }
            pos = (pos + 1) & (cell_mask >> 2);
        }
    }

    const Value * findScalar(Key key) const
    {
        if (key == 0)
            return nullptr;
        size_t pos = (hashKey(key) & cell_mask) >> 2;
        for (;;)
        {
            const auto & group = groups[pos];
            bool empty = false;
            for (size_t lane = 0; lane < 4; ++lane)
            {
                if (group.keys[lane] == key)
                    return &group.values[lane];
                empty |= group.keys[lane] == 0;
            }
            if (empty)
                return nullptr;
            pos = (pos + 1) & (cell_mask >> 2);
        }
    }

    const Value * findSimd(Key key) const
    {
        if (key == 0)
            return nullptr;
        using Batch = xsimd::batch<Key, LookupArch>;
        static_assert(Batch::size == 2 || Batch::size == 4);
        const Batch needle(key);
        const Batch zero(Key{0});
        const size_t group_mask = cell_mask >> 2;
        size_t pos = (hashKey(key) & cell_mask) >> 2;
        for (;;)
        {
            const auto & group = groups[pos];
            uint64_t matches = 0;
            uint64_t empty = 0;
            for (size_t lane = 0; lane < 4; lane += Batch::size)
            {
                const auto keys = Batch::load_aligned(group.keys + lane);
                matches |= (keys == needle).mask() << lane;
                empty |= (keys == zero).mask() << lane;
            }
            if (matches)
                return &group.values[std::countr_zero(matches)];
            if (empty)
                return nullptr;
            pos = (pos + 1) & group_mask;
        }
    }

    size_t size() const { return key_count; }
    size_t capacity() const { return groups.size() * 4; }
    size_t allocatedBytes() const { return groups.size() * sizeof(Group); }

private:
    std::vector<Group> groups;
    size_t cell_mask;
    size_t key_count = 0;
};

class CyclicGroupedTable
{
public:
    explicit CyclicGroupedTable(size_t distinct_keys)
        : groups(capacityFor(distinct_keys) / 4)
        , cell_mask(groups.size() * 4 - 1)
    {
    }

    void insert(Key key, Value value)
    {
        if (key == 0)
            throw std::invalid_argument("Key zero is reserved for empty cells");
        const uint32_t hash = hashKey(key);
        const size_t home_lane = hash & 3;
        size_t pos = hash & cell_mask & ~size_t{3};
        for (;;)
        {
            auto & group = groups[pos / 4];
            size_t empty_lane = 4;
            for (size_t offset = 0; offset < 4; ++offset)
            {
                const size_t lane = (home_lane + offset) & 3;
                if (group.keys[lane] == key)
                {
                    group.values[lane] = value;
                    return;
                }
                if (group.keys[lane] == 0 && empty_lane == 4)
                    empty_lane = lane;
            }
            if (empty_lane != 4)
            {
                if (key_count == capacity() / 2)
                    throw std::length_error("Table reached its maximum load factor");
                group.keys[empty_lane] = key;
                group.values[empty_lane] = value;
                ++key_count;
                return;
            }
            pos = (pos + 4) & cell_mask;
        }
    }

    const Value * findScalar(Key key) const
    {
        if (key == 0)
            return nullptr;
        const uint32_t hash = hashKey(key);
        const size_t home_lane = hash & 3;
        // Keep pos a lane-0 cell index: a group index costs an extra shift on the first probe.
        size_t pos = hash & cell_mask & ~size_t{3};
        for (;;)
        {
            const auto & group = groups[pos / 4];
            for (size_t offset = 0; offset < 4; ++offset)
            {
                const size_t lane = (home_lane + offset) & 3;
                if (group.keys[lane] == key)
                    return &group.values[lane];
                if (group.keys[lane] == 0)
                    return nullptr;
            }
            pos = (pos + 4) & cell_mask;
        }
    }

    size_t size() const { return key_count; }
    size_t capacity() const { return groups.size() * 4; }
    size_t allocatedBytes() const { return groups.size() * sizeof(Group); }

private:
    std::vector<Group> groups;
    size_t cell_mask;
    size_t key_count = 0;
};

}
