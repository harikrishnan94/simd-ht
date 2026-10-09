#pragma once

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <new>
#include <stdexcept>
#include <vector>

#if !defined(SIMD_HT_AVX2) && !defined(SIMD_HT_NEON64)
#error "Select the NEON64 or AVX2 SIMD backend in Meson."
#endif

namespace simd_ht
{

using Key = uint64_t;
using Value = uint64_t;

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

template <typename T>
struct CacheLineAllocator
{
    using value_type = T;

    T * allocate(size_t n)
    {
        void * p = nullptr;
        if (posix_memalign(&p, 64, n * sizeof(T)) != 0)
            throw std::bad_alloc();
        return static_cast<T *>(p);
    }
    void deallocate(T * p, size_t) noexcept { std::free(p); }
    bool operator==(const CacheLineAllocator &) const = default;
};

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
    std::vector<Cell, CacheLineAllocator<Cell>> cells;
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

    const Value * findSimd(Key key) const
    {
        if (key == 0)
            return nullptr;
#if defined(SIMD_HT_NEON64)
        using KeyPair = Key __attribute__((vector_size(16), aligned(16), may_alias));
        using Words = uint32_t __attribute__((vector_size(16)));
        using Fields = uint16_t __attribute__((vector_size(8)));
        const KeyPair needle = {key, key};
#else
        using KeyQuad = Key __attribute__((vector_size(32), aligned(32), may_alias));
        using LaneBits = bool __attribute__((ext_vector_type(4)));
        const KeyQuad needle = {key, key, key, key};
#endif
        const size_t group_mask = cell_mask >> 2;
        size_t pos = (hashKey(key) & cell_mask) >> 2;
        for (;;)
        {
            const auto & group = groups[pos];
#if defined(SIMD_HT_NEON64)
            const auto low = std::bit_cast<Words>(*reinterpret_cast<const KeyPair *>(group.keys) == needle);
            const auto high = std::bit_cast<Words>(*reinterpret_cast<const KeyPair *>(group.keys + 2) == needle);
            // Lane i's comparison fills bits [16 * i, 16 * i + 16).
            const Words even = __builtin_shufflevector(low, high, 0, 2, 4, 6);
            const uint64_t matches = std::bit_cast<uint64_t>(__builtin_convertvector(even, Fields));
            // A key occupies at most one slot, so a match in lane i has 48 - 16 * i leading zeros.
            // Indexing values by 3 - countl_zero / 16 instead adds a sign extension and a null check.
            if (matches)
                return reinterpret_cast<const Value *>(
                    reinterpret_cast<const std::byte *>(&group.values[3]) - static_cast<size_t>(std::countl_zero(matches)) / 2);
#else
            const auto equal = __builtin_convertvector(*reinterpret_cast<const KeyQuad *>(group.keys) == needle, LaneBits);
            // Bits 4 to 7 of a four-lane bool vector are padding, so std::bit_cast to a byte would read them.
            const unsigned matches = unsigned{equal[0]} | unsigned{equal[1]} << 1 | unsigned{equal[2]} << 2 | unsigned{equal[3]} << 3;
            if (matches)
                return &group.values[std::countr_zero(matches)];
#endif
            // Insertion fills the lowest empty lane and never deletes, so a group with a free slot has lane 3 empty.
            if (group.keys[3] == 0)
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
