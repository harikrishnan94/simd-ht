#pragma once

#include <cstddef>
#include <cstdint>

namespace simd_ht
{

enum class AmacStep : uint8_t
{
    Advance, // No match yet; the slot moved to the next line and prefetched it.
    Done, // The lookup finished; the slot can take a new row.
};

[[gnu::always_inline]] inline void prefetchLine(const void * address)
{
    // Locality 3 keeps the line in L1. On AArch64 locality 1 stages it in L3 only, and the visit's load then pays the full L1 miss.
    __builtin_prefetch(address, 0, 3);
}

/// AMAC (asynchronous memory access chaining), after Kocberber et al., PVLDB 2015.
/// The ring holds ring_size lookups in flight. A visit to a slot reads one cache line, then either finishes the lookup
/// or prefetches the line its next visit reads. The misses of the other slots overlap with that wait.
/// The table is read-only during a run, so in-flight lookups cannot interact. It is at most half full, so every walk
/// ends at an empty cell or group. A policy supplies:
///   - ring_size and Ring: parallel per-slot arrays, all slots inactive when value-initialized, with isActive(s) and deactivate(s).
///   - start(ring, s, row): admits the row into slot s and prefetches. False means the row finished without a walk,
///     so the slot stays free.
///   - step(ring, s): makes one visit and returns an AmacStep.
template <class Policy>
void amacRun(Policy policy, size_t rows)
{
    constexpr size_t ring_size = Policy::ring_size;
    typename Policy::Ring ring{};
    size_t next = 0;
    size_t active = 0;

    // Admits rows into slot s until one needs a walk or the rows run out.
    auto refill = [&](size_t s) __attribute__((always_inline))
    {
        while (next < rows)
        {
            const size_t row = next++;
            if (policy.start(ring, s, row))
                break;
        }
    };

    for (size_t s = 0; s < ring_size; ++s)
    {
        refill(s);
        active += ring.isActive(s);
    }

    // While every slot is active, a sweep needs no active check. A refill that finds no row ends this phase.
    while (active == ring_size)
    {
        for (size_t s = 0; s < ring_size; ++s)
        {
            if (policy.step(ring, s) == AmacStep::Advance)
                continue;
            ring.deactivate(s);
            refill(s);
            if (!ring.isActive(s))
                --active;
        }
    }

    while (active > 0)
    {
        for (size_t s = 0; s < ring_size; ++s)
        {
            if (!ring.isActive(s))
                continue;
            if (policy.step(ring, s) == AmacStep::Advance)
                continue;
            ring.deactivate(s);
            --active;
        }
    }
}

}
