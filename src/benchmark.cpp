#include "hash_tables.hpp"
#include "perf_counters.hpp"
#include "workload.hpp"

#include <benchmark/benchmark.h>

#include <cstddef>
#include <cstdint>
#include <exception>
#include <string>
#include <vector>

namespace
{

bool benchmark_failed = false;

void fail(benchmark::State & state, const std::string & message)
{
    benchmark_failed = true;
    state.SkipWithError(message);
}

// The timed passes add the hits and the value checksum of every query. The AMAC passes ignore rows, so their rings store none.
template <auto Find>
struct PerKey
{
    template <class Table>
    [[gnu::always_inline]] static void
    run(const Table & table, const std::vector<simd_ht::Key> & queries, uint64_t & hits, uint64_t & checksum)
    {
        for (simd_ht::Key key : queries)
        {
            if (const simd_ht::Value * value = (table.*Find)(key))
            {
                ++hits;
                checksum += *value;
            }
        }
    }
};

template <size_t ring_size>
struct Amac
{
    [[gnu::always_inline]] static void
    run(const simd_ht::ScalarTable & table, const std::vector<simd_ht::Key> & queries, uint64_t & hits, uint64_t & checksum)
    {
        table.findAmac<ring_size>(queries, sum(hits, checksum));
    }

    [[gnu::always_inline]] static void
    run(const simd_ht::CyclicGroupedTable & table, const std::vector<simd_ht::Key> & queries, uint64_t & hits, uint64_t & checksum)
    {
        table.findScalarAmac<ring_size>(queries, sum(hits, checksum));
    }

    [[gnu::always_inline]] static void
    run(const simd_ht::GroupedTable & table, const std::vector<simd_ht::Key> & queries, uint64_t & hits, uint64_t & checksum)
    {
        table.findSimdAmac<ring_size>(queries, sum(hits, checksum));
    }

private:
    [[gnu::always_inline]] static auto sum(uint64_t & hits, uint64_t & checksum)
    {
        return [&](const simd_ht::Value * value)
        {
            if (value)
            {
                ++hits;
                checksum += *value;
            }
        };
    }
};

template <class Table, class Pass>
void lookup(benchmark::State & state)
{
    using namespace simd_ht;
    const auto distinct_keys = static_cast<size_t>(state.range(0));
    const auto hit_percent = static_cast<unsigned>(state.range(1));
    try
    {
        Table table(distinct_keys);
        for (size_t i = 0; i < distinct_keys; ++i)
        {
            const Key key = keyForIndex(i);
            table.insert(key, valueForKey(key));
        }
        const Workload workload = makeWorkload(distinct_keys, hit_percent);
        if (table.size() != distinct_keys)
        {
            fail(state, "Table size differs from the number of inserted keys");
            return;
        }

        uint64_t batches = 0;
        uint64_t total_hits = 0;
        uint64_t total_checksum = 0;
        const auto * query_data = workload.queries.data();
        PerfCounters counters;
        benchmark::DoNotOptimize(table);
        benchmark::DoNotOptimize(query_data);
        // Each batch scans the full trace; benchmark iterations count individual lookups.
        while (state.KeepRunningBatch(static_cast<benchmark::IterationCount>(workload.queries.size())))
        {
            benchmark::ClobberMemory();
            uint64_t hits = 0;
            uint64_t checksum = 0;
            counters.start();
            Pass::run(table, workload.queries, hits, checksum);
            counters.stop();
            total_hits += hits;
            total_checksum += checksum;
            ++batches;
        }

        if (total_hits != workload.expected_hits * batches || total_checksum != workload.expected_checksum * batches)
        {
            fail(state, "Lookup hit count or value checksum differs from the expected result");
            return;
        }
        if (static_cast<uint64_t>(state.iterations()) != batches * workload.queries.size())
        {
            fail(state, "Benchmark iteration count differs from the number of lookups");
            return;
        }

        state.SetItemsProcessed(state.iterations());
        counters.report(state);
        state.counters["capacity"] = static_cast<double>(table.capacity());
        state.counters["load_factor"] = static_cast<double>(table.size()) / static_cast<double>(table.capacity());
        state.counters["table_bytes"] = static_cast<double>(table.allocatedBytes());
        state.counters["query_bytes"] = static_cast<double>(workload.queries.size() * sizeof(Key));
    }
    catch (const std::exception & exception)
    {
        fail(state, std::string("Benchmark setup or perf counters failed: ") + exception.what());
    }
}

void arguments(benchmark::internal::Benchmark * benchmark)
{
    benchmark->ArgNames({"keys", "hit_percent"});
    for (unsigned exponent = 10; exponent <= 28; exponent += 2)
        for (int64_t hit_percent : {0, 50, 100})
            benchmark->Args({int64_t{1} << exponent, hit_percent});
    benchmark->Unit(benchmark::kNanosecond)->UseRealTime();
}

// The ring-size sweep covers only 2^24 and 2^28 keys, the sizes of the counter runs.
void ringSweepArguments(benchmark::internal::Benchmark * benchmark)
{
    benchmark->ArgNames({"keys", "hit_percent"});
    for (unsigned exponent : {24, 28})
        for (int64_t hit_percent : {0, 50, 100})
            benchmark->Args({int64_t{1} << exponent, hit_percent});
    benchmark->Unit(benchmark::kNanosecond)->UseRealTime();
}

template <size_t ring_size>
void registerAmac(void (*apply)(benchmark::internal::Benchmark *))
{
    using namespace simd_ht;
    const std::string suffix = "_AMAC" + std::to_string(ring_size);
    benchmark::RegisterBenchmark("ScalarOA" + suffix, lookup<ScalarTable, Amac<ring_size>>)->Apply(apply);
    benchmark::RegisterBenchmark("ScalarCyclicGroupedOA" + suffix, lookup<CyclicGroupedTable, Amac<ring_size>>)->Apply(apply);
    benchmark::RegisterBenchmark("SIMDGroupedOA" + suffix, lookup<GroupedTable, Amac<ring_size>>)->Apply(apply);
}

}

int main(int argc, char ** argv)
{
    benchmark::Initialize(&argc, argv);
    if (benchmark::ReportUnrecognizedArguments(argc, argv))
        return 1;

#if defined(SIMD_HT_AVX2)
    benchmark::AddCustomContext("simd_target", "AVX2");
#elif defined(SIMD_HT_NEON64)
    benchmark::AddCustomContext("simd_target", "NEON64");
#endif
#if defined(__clang__)
    benchmark::AddCustomContext("compiler", "Clang " __clang_version__);
#else
    benchmark::AddCustomContext("compiler", __VERSION__);
#endif
    benchmark::AddCustomContext("hash", "XOR-fold 64-bit key, MurmurHash3 fmix32");
    benchmark::AddCustomContext("query_seed", std::to_string(simd_ht::query_seed));
    benchmark::AddCustomContext("workload", "Distinct full-width keys; one shuffled N-key trace per batch");
    benchmark::AddCustomContext("amac", "Ring of 32 lookups on every size; rings of 8, 16 and 64 at 2^24 and 2^28 keys");
    benchmark::RegisterBenchmark("ScalarOA", lookup<simd_ht::ScalarTable, PerKey<&simd_ht::ScalarTable::find>>)->Apply(arguments);
    benchmark::RegisterBenchmark(
        "ScalarCyclicGroupedOA", lookup<simd_ht::CyclicGroupedTable, PerKey<&simd_ht::CyclicGroupedTable::findScalar>>)
        ->Apply(arguments);
    benchmark::RegisterBenchmark("SIMDGroupedOA", lookup<simd_ht::GroupedTable, PerKey<&simd_ht::GroupedTable::findSimd>>)
        ->Apply(arguments);
    registerAmac<32>(arguments);
    registerAmac<8>(ringSweepArguments);
    registerAmac<16>(ringSweepArguments);
    registerAmac<64>(ringSweepArguments);
    benchmark::RunSpecifiedBenchmarks();
    benchmark::Shutdown();
    return benchmark_failed ? 1 : 0;
}
