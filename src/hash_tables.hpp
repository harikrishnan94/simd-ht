#pragma once

#include "amac.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <new>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <utility>
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

/// An on_result(value) callback needs no row, so the ring stores none. Clang does not remove the dead row stores itself.
template <class OnResult>
inline constexpr bool amac_tracks_rows = std::is_invocable_v<OnResult &, size_t, const Value *>;

template <class OnResult>
[[gnu::always_inline]] inline void callOnResult(OnResult & on_result, size_t row, const Value * value)
{
    if constexpr (amac_tracks_rows<OnResult>)
        on_result(row, value);
    else
        on_result(value);
}

/// Each slot holds the line it reads next, its key and, if tracked, its row. A null position marks an inactive slot.
template <class Position, size_t ring_size, bool track_rows>
struct AmacRing
{
    std::array<const Position *, ring_size> pos{};
    std::array<Key, ring_size> key{};
    std::array<size_t, track_rows ? ring_size : 0> rows{};

    bool isActive(size_t s) const { return pos[s] != nullptr; }
    void deactivate(size_t s) { pos[s] = nullptr; }

    size_t rowOf(size_t s) const
    {
        if constexpr (track_rows)
            return rows[s];
        else
            return 0;
    }

    void setRow(size_t s, size_t row)
    {
        if constexpr (track_rows)
            rows[s] = row;
    }
};

/// A visit reads the rest of one cache line, so visits per lookup are the lines the lookup touches.
template <size_t ring_size_, class OnResult>
struct LinearAmacPolicy
{
    static constexpr size_t ring_size = ring_size_;
    using Ring = AmacRing<Cell, ring_size, amac_tracks_rows<OnResult>>;

    std::span<const Key> keys;
    const Cell * cells = nullptr;
    const Cell * cells_end = nullptr;
    size_t cell_mask = 0;
    OnResult on_result;

    bool start(Ring & ring, size_t s, size_t row)
    {
        const Key key = keys[row];
        if (key == 0)
        {
            callOnResult(on_result, row, nullptr);
            return false;
        }
        const Cell * home = cells + (hashKey(key) & cell_mask);
        ring.pos[s] = home;
        ring.key[s] = key;
        ring.setRow(s, row);
        prefetchLine(home);
        return true;
    }

    AmacStep step(Ring & ring, size_t s)
    {
        const Cell * cell = ring.pos[s];
        const Key key = ring.key[s];
        do
        {
            if (cell->key == key)
            {
                callOnResult(on_result, ring.rowOf(s), &cell->value);
                return AmacStep::Done;
            }
            if (cell->key == 0)
            {
                callOnResult(on_result, ring.rowOf(s), nullptr);
                return AmacStep::Done;
            }
            if (++cell == cells_end)
                cell = cells;
        } while (reinterpret_cast<uintptr_t>(cell) % 64 != 0);
        ring.pos[s] = cell;
        prefetchLine(cell);
        return AmacStep::Advance;
    }
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

    /// Calls on_result(row, find(keys[row])) once per row, in completion order. on_result(find(keys[row])) is also accepted.
    template <size_t ring_size, class OnResult>
    void findAmac(std::span<const Key> keys, OnResult on_result) const
    {
        amacRun(
            LinearAmacPolicy<ring_size, OnResult>{keys, cells.data(), cells.data() + cells.size(), cell_mask, std::move(on_result)},
            keys.size());
    }

    size_t size() const { return key_count; }
    size_t capacity() const { return cells.size(); }
    size_t allocatedBytes() const { return cells.size() * sizeof(Cell); }

private:
    std::vector<Cell, CacheLineAllocator<Cell>> cells;
    size_t cell_mask;
    size_t key_count = 0;
};

/// Puts the key in the first group, from its home group on, that holds the key or has an empty lane.
/// Each group's lanes are scanned from lane hash & lane_mask, wrapping around.
inline void insertIntoGroups(std::vector<Group> & groups, size_t & key_count, Key key, Value value, uint32_t lane_mask)
{
    if (key == 0)
        throw std::invalid_argument("Key zero is reserved for empty cells");
    const uint32_t hash = hashKey(key);
    const size_t first_lane = hash & lane_mask;
    const size_t group_mask = groups.size() - 1;
    size_t pos = (hash >> 2) & group_mask;
    for (;;)
    {
        auto & group = groups[pos];
        size_t empty_lane = 4;
        for (size_t offset = 0; offset < 4; ++offset)
        {
            const size_t lane = (first_lane + offset) & 3;
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
            if (key_count == groups.size() * 2)
                throw std::length_error("Table reached its maximum load factor");
            group.keys[empty_lane] = key;
            group.values[empty_lane] = value;
            ++key_count;
            return;
        }
        pos = (pos + 1) & group_mask;
    }
}

/// Compares the four keys of a group with one key. A key occupies at most one slot, so at most one lane matches.
#if defined(SIMD_HT_NEON64)
using Needle = Key __attribute__((vector_size(16), aligned(16), may_alias));
using LaneMatches = uint64_t;

[[gnu::always_inline]] inline Needle broadcastKey(Key key)
{
    return {key, key};
}

/// Lane i's comparison fills bits [16 * i, 16 * i + 16).
[[gnu::always_inline]] inline LaneMatches matchLanes(const Group & group, Needle needle)
{
    using Words = uint32_t __attribute__((vector_size(16)));
    using Fields = uint16_t __attribute__((vector_size(8)));
    const auto low = std::bit_cast<Words>(*reinterpret_cast<const Needle *>(group.keys) == needle);
    const auto high = std::bit_cast<Words>(*reinterpret_cast<const Needle *>(group.keys + 2) == needle);
    const Words even = __builtin_shufflevector(low, high, 0, 2, 4, 6);
    return std::bit_cast<uint64_t>(__builtin_convertvector(even, Fields));
}

/// A match in lane i has 48 - 16 * i leading zeros.
/// Indexing values by 3 - countl_zero / 16 instead adds a sign extension and a null check.
[[gnu::always_inline]] inline const Value * matchedValue(const Group & group, LaneMatches matches)
{
    return reinterpret_cast<const Value *>(
        reinterpret_cast<const std::byte *>(&group.values[3]) - static_cast<size_t>(std::countl_zero(matches)) / 2);
}
#else
using Needle = Key __attribute__((vector_size(32), aligned(32), may_alias));
using LaneMatches = unsigned;

[[gnu::always_inline]] inline Needle broadcastKey(Key key)
{
    return {key, key, key, key};
}

/// Bit i is set when lane i matches.
[[gnu::always_inline]] inline LaneMatches matchLanes(const Group & group, Needle needle)
{
    using LaneBits = bool __attribute__((ext_vector_type(4)));
    const auto equal = __builtin_convertvector(*reinterpret_cast<const Needle *>(group.keys) == needle, LaneBits);
    // Bits 4 to 7 of a four-lane bool vector are padding, so std::bit_cast to a byte gives an indeterminate value.
    return unsigned{equal[0]} | unsigned{equal[1]} << 1 | unsigned{equal[2]} << 2 | unsigned{equal[3]} << 3;
}

[[gnu::always_inline]] inline const Value * matchedValue(const Group & group, LaneMatches matches)
{
    return &group.values[std::countr_zero(matches)];
}
#endif

/// A visit compares the four keys of one group, as findSimd does.
template <size_t ring_size_, class OnResult>
struct SimdAmacPolicy
{
    static constexpr size_t ring_size = ring_size_;
    using Ring = AmacRing<Group, ring_size, amac_tracks_rows<OnResult>>;

    std::span<const Key> keys;
    const Group * groups = nullptr;
    const Group * groups_end = nullptr;
    size_t cell_mask = 0;
    OnResult on_result;

    bool start(Ring & ring, size_t s, size_t row)
    {
        const Key key = keys[row];
        if (key == 0)
        {
            callOnResult(on_result, row, nullptr);
            return false;
        }
        const Group * home = groups + ((hashKey(key) & cell_mask) >> 2);
        ring.pos[s] = home;
        ring.key[s] = key;
        ring.setRow(s, row);
        prefetchLine(home);
        return true;
    }

    AmacStep step(Ring & ring, size_t s)
    {
        const Group * group = ring.pos[s];
        if (const LaneMatches matches = matchLanes(*group, broadcastKey(ring.key[s])))
        {
            callOnResult(on_result, ring.rowOf(s), matchedValue(*group, matches));
            return AmacStep::Done;
        }
        if (group->keys[3] == 0)
        {
            callOnResult(on_result, ring.rowOf(s), nullptr);
            return AmacStep::Done;
        }
        if (++group == groups_end)
            group = groups;
        ring.pos[s] = group;
        prefetchLine(group);
        return AmacStep::Advance;
    }
};

class GroupedTable
{
public:
    explicit GroupedTable(size_t distinct_keys)
        : groups(capacityFor(distinct_keys) / 4)
        , cell_mask(groups.size() * 4 - 1)
    {
    }

    void insert(Key key, Value value) { insertIntoGroups(groups, key_count, key, value, 0); }

    const Value * findSimd(Key key) const
    {
        if (key == 0)
            return nullptr;
        // Insertion fills the lowest empty lane and never deletes, so a lookup stops at the first group with lane 3 empty.
        const Needle needle = broadcastKey(key);
#if defined(SIMD_HT_NEON64)
        const size_t group_mask = cell_mask >> 2;
        size_t pos = (hashKey(key) & cell_mask) >> 2;
        for (;;)
        {
            const auto & group = groups[pos];
            if (const LaneMatches matches = matchLanes(group, needle))
                return matchedValue(group, matches);
            if (group.keys[3] == 0)
                return nullptr;
            pos = (pos + 1) & group_mask;
        }
#else
        // x86 addressing cannot scale a group index by 64, so keep the probe position in bytes.
        // Widen before shifting: a 32-bit shift drops hash bits 28 to 31, which tables of 2^27 or more groups use.
        const size_t byte_mask = (groups.size() - 1) * sizeof(Group);
        size_t offset = (static_cast<size_t>(hashKey(key)) << 4) & byte_mask;
        const auto * base = reinterpret_cast<const std::byte *>(groups.data());
        for (;;)
        {
            const auto & group = *reinterpret_cast<const Group *>(base + offset);
            if (const LaneMatches matches = matchLanes(group, needle))
                return matchedValue(group, matches);
            if (group.keys[3] == 0)
                return nullptr;
            offset = (offset + sizeof(Group)) & byte_mask;
        }
#endif
    }

    /// Calls on_result(row, findSimd(keys[row])) once per row, in completion order. on_result(findSimd(keys[row])) is also accepted.
    template <size_t ring_size, class OnResult>
    void findSimdAmac(std::span<const Key> keys, OnResult on_result) const
    {
        amacRun(
            SimdAmacPolicy<ring_size, OnResult>{keys, groups.data(), groups.data() + groups.size(), cell_mask, std::move(on_result)},
            keys.size());
    }

    size_t size() const { return key_count; }
    size_t capacity() const { return groups.size() * 4; }
    size_t allocatedBytes() const { return groups.size() * sizeof(Group); }

private:
    std::vector<Group> groups;
    size_t cell_mask;
    size_t key_count = 0;
};

/// A visit scans one group from the key's home lane, as findScalar does. Each group of a walk starts at that lane.
template <size_t ring_size_, class OnResult>
struct CyclicAmacPolicy
{
    static constexpr size_t ring_size = ring_size_;

    struct Ring : AmacRing<Group, ring_size, amac_tracks_rows<OnResult>>
    {
        std::array<uint8_t, ring_size> lane{};
    };

    std::span<const Key> keys;
    const Group * groups = nullptr;
    const Group * groups_end = nullptr;
    size_t cell_mask = 0;
    OnResult on_result;

    bool start(Ring & ring, size_t s, size_t row)
    {
        const Key key = keys[row];
        if (key == 0)
        {
            callOnResult(on_result, row, nullptr);
            return false;
        }
        const uint32_t hash = hashKey(key);
        const Group * home = groups + ((hash & cell_mask) >> 2);
        ring.pos[s] = home;
        ring.key[s] = key;
        ring.setRow(s, row);
        ring.lane[s] = static_cast<uint8_t>(hash & 3);
        prefetchLine(home);
        return true;
    }

    AmacStep step(Ring & ring, size_t s)
    {
        const Group * group = ring.pos[s];
        const Key key = ring.key[s];
        const size_t home_lane = ring.lane[s];
        for (size_t offset = 0; offset < 4; ++offset)
        {
            const size_t lane = (home_lane + offset) & 3;
            if (group->keys[lane] == key)
            {
                callOnResult(on_result, ring.rowOf(s), &group->values[lane]);
                return AmacStep::Done;
            }
            if (group->keys[lane] == 0)
            {
                callOnResult(on_result, ring.rowOf(s), nullptr);
                return AmacStep::Done;
            }
        }
        if (++group == groups_end)
            group = groups;
        ring.pos[s] = group;
        prefetchLine(group);
        return AmacStep::Advance;
    }
};

class CyclicGroupedTable
{
public:
    explicit CyclicGroupedTable(size_t distinct_keys)
        : groups(capacityFor(distinct_keys) / 4)
        , cell_mask(groups.size() * 4 - 1)
    {
    }

    void insert(Key key, Value value) { insertIntoGroups(groups, key_count, key, value, 3); }

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

    /// Calls on_result(row, findScalar(keys[row])) once per row, in completion order. on_result(findScalar(keys[row])) is also accepted.
    template <size_t ring_size, class OnResult>
    void findScalarAmac(std::span<const Key> keys, OnResult on_result) const
    {
        amacRun(
            CyclicAmacPolicy<ring_size, OnResult>{keys, groups.data(), groups.data() + groups.size(), cell_mask, std::move(on_result)},
            keys.size());
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
