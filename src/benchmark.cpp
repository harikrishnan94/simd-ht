#include "hash_tables.hpp"
#include "workload.hpp"

#include <benchmark/benchmark.h>

#include <cstddef>
#include <cstdint>
#include <exception>
#include <string>

namespace
{

bool benchmark_failed = false;

void fail(benchmark::State & state, const std::string & message)
{
    benchmark_failed = true;
    state.SkipWithError(message);
}

template <class Table, auto Find>
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
        benchmark::DoNotOptimize(table);
        benchmark::DoNotOptimize(query_data);
        // Each batch scans the full trace; benchmark iterations count individual lookups.
        while (state.KeepRunningBatch(static_cast<benchmark::IterationCount>(workload.queries.size())))
        {
            benchmark::ClobberMemory();
            uint64_t hits = 0;
            uint64_t checksum = 0;
            for (Key key : workload.queries)
            {
                if (const Value * value = (table.*Find)(key))
                {
                    ++hits;
                    checksum += *value;
                }
            }
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
        state.counters["capacity"] = static_cast<double>(table.capacity());
        state.counters["load_factor"] = static_cast<double>(table.size()) / static_cast<double>(table.capacity());
        state.counters["table_bytes"] = static_cast<double>(table.allocatedBytes());
        state.counters["query_bytes"] = static_cast<double>(workload.queries.size() * sizeof(Key));
    }
    catch (const std::exception & exception)
    {
        fail(state, std::string("Table or workload setup failed: ") + exception.what());
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
    benchmark::RegisterBenchmark("ScalarOA", lookup<simd_ht::ScalarTable, &simd_ht::ScalarTable::find>)->Apply(arguments);
    benchmark::RegisterBenchmark("ScalarGroupedOA", lookup<simd_ht::GroupedTable, &simd_ht::GroupedTable::findScalar>)->Apply(arguments);
    benchmark::RegisterBenchmark("SIMDGroupedOA", lookup<simd_ht::GroupedTable, &simd_ht::GroupedTable::findSimd>)->Apply(arguments);
    benchmark::RegisterBenchmark("SIMDCombinedGroupedOA", lookup<simd_ht::GroupedTable, &simd_ht::GroupedTable::findSimdCombined>)
        ->Apply(arguments);
    benchmark::RegisterBenchmark("ScalarCyclicGroupedOA", lookup<simd_ht::CyclicGroupedTable, &simd_ht::CyclicGroupedTable::findScalar>)
        ->Apply(arguments);
    benchmark::RunSpecifiedBenchmarks();
    benchmark::Shutdown();
    return benchmark_failed ? 1 : 0;
}
