#pragma once

#include "hash_tables.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace simd_ht
{

inline constexpr uint64_t query_seed = 0x6a09e667f3bcc909ULL;

inline uint64_t mix64(uint64_t value)
{
    value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31);
}

// The finalizer is a bijection; index + 1 keeps supported keys distinct and nonzero.
inline Key keyForIndex(uint64_t index)
{
    return mix64(index + 1);
}

inline Value valueForKey(Key key)
{
    return mix64(key ^ 0xd1b54a32d192ed03ULL);
}

struct Workload
{
    std::vector<Key> queries;
    uint64_t expected_hits = 0;
    uint64_t expected_checksum = 0;
};

inline Workload makeWorkload(size_t distinct_keys, unsigned hit_percent)
{
    if (distinct_keys == 0 || distinct_keys > (size_t{1} << 28))
        throw std::invalid_argument("Workload size must be between 1 and 2^28");
    if (hit_percent != 0 && hit_percent != 50 && hit_percent != 100)
        throw std::invalid_argument("Hit percentage must be 0, 50, or 100");

    Workload workload;
    workload.queries.resize(distinct_keys);
    workload.expected_hits = distinct_keys * hit_percent / 100;
    for (size_t i = 0; i < distinct_keys; ++i)
    {
        if (i < workload.expected_hits)
        {
            // Spread hits across insertion order, including the mixed workload.
            const Key key = keyForIndex(i * distinct_keys / workload.expected_hits);
            workload.queries[i] = key;
            workload.expected_checksum += valueForKey(key);
        }
        else
            workload.queries[i] = keyForIndex(distinct_keys + i - workload.expected_hits);
    }

    // Rejection sampling makes the shuffle unbiased and independent of the standard library.
    uint64_t random_state = query_seed;
    for (size_t remaining = workload.queries.size(); remaining > 1; --remaining)
    {
        const uint64_t bound = remaining;
        const uint64_t reject_below = -bound % bound;
        uint64_t random = 0;
        do
        {
            random_state += 0x9e3779b97f4a7c15ULL;
            random = mix64(random_state);
        } while (random < reject_below);
        std::swap(workload.queries[remaining - 1], workload.queries[random % bound]);
    }
    return workload;
}

}
