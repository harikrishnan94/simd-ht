#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <system_error>

#if defined(__linux__)
#include <cerrno>
#include <unistd.h>
#include <linux/perf_event.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#endif

namespace simd_ht
{

/// User-space hardware counters around timed lookup batches. Setting OA_PMU to any non-empty value enables them.
class PerfCounters
{
public:
    PerfCounters()
    {
        // Benchmarks run on one thread, and nothing modifies the environment.
        const char * mode = std::getenv("OA_PMU"); // NOLINT(concurrency-mt-unsafe)
        if (!mode || !*mode)
            return;
#if defined(__linux__)
        for (size_t i = 0; i < events.size(); ++i)
        {
            perf_event_attr attr{};
            attr.size = sizeof(attr);
            attr.type = PERF_TYPE_HARDWARE;
            attr.config = events[i].config;
            attr.disabled = i == 0;
            attr.pinned = i == 0;
            attr.exclude_kernel = 1;
            attr.exclude_hv = 1;
            attr.read_format = PERF_FORMAT_GROUP | PERF_FORMAT_TOTAL_TIME_ENABLED | PERF_FORMAT_TOTAL_TIME_RUNNING;
            fds[i] = static_cast<int>(syscall(SYS_perf_event_open, &attr, 0, -1, i == 0 ? -1 : fds[0], PERF_FLAG_FD_CLOEXEC));
            if (fds[i] < 0)
            {
                const std::string error = std::string("perf_event_open ") + events[i].name + ": " + std::system_category().message(errno);
                closeAll();
                throw std::runtime_error(error);
            }
        }
#else
        throw std::runtime_error("OA_PMU requires Linux perf events");
#endif
    }

    PerfCounters(const PerfCounters &) = delete;
    PerfCounters & operator=(const PerfCounters &) = delete;
    ~PerfCounters() { closeAll(); }

    [[gnu::noinline]] void start()
    {
#if defined(__linux__)
        if (fds[0] < 0)
            return;
        if (ioctl(fds[0], PERF_EVENT_IOC_RESET, PERF_IOC_FLAG_GROUP) != 0 || ioctl(fds[0], PERF_EVENT_IOC_ENABLE, PERF_IOC_FLAG_GROUP) != 0)
            throw std::runtime_error("Cannot enable the perf counter group");
#endif
        asm volatile("" ::: "memory");
    }

    [[gnu::noinline]] void stop()
    {
        asm volatile("" ::: "memory");
#if defined(__linux__)
        if (fds[0] < 0)
            return;
        if (ioctl(fds[0], PERF_EVENT_IOC_DISABLE, PERF_IOC_FLAG_GROUP) != 0)
            throw std::runtime_error("Cannot disable the perf counter group");
        // Group read layout: event count, time enabled, time running, then one value per event.
        std::array<uint64_t, 3 + event_count> data{};
        if (read(fds[0], data.data(), sizeof(data)) != static_cast<ssize_t>(sizeof(data)) || data[0] != event_count)
            throw std::runtime_error("Incomplete perf counter group read");
        if (data[1] == 0 || data[1] != data[2])
            throw std::runtime_error("Perf counters were multiplexed; results would be scaled estimates");
        for (size_t i = 0; i < event_count; ++i)
            totals[i] += data[3 + i];
#endif
    }

    /// Adds per-lookup averages to the benchmark's counters.
    template <class State>
    void report(State & state) const
    {
        if (fds[0] < 0)
            return;
        for (size_t i = 0; i < event_count; ++i)
            state.counters[events[i].name] = static_cast<double>(totals[i]) / static_cast<double>(state.iterations());
    }

private:
    struct Event
    {
        const char * name;
        uint64_t config;
    };

    static constexpr size_t event_count = 3;
#if defined(__linux__)
    static constexpr std::array<Event, event_count> events
        = {{{"pmu_cycles", PERF_COUNT_HW_CPU_CYCLES},
            {"pmu_instructions", PERF_COUNT_HW_INSTRUCTIONS},
            {"pmu_branch_misses", PERF_COUNT_HW_BRANCH_MISSES}}};
#else
    static constexpr std::array<Event, event_count> events = {{{"pmu_cycles", 0}, {"pmu_instructions", 0}, {"pmu_branch_misses", 0}}};
#endif
    std::array<int, event_count> fds{-1, -1, -1};
    std::array<uint64_t, event_count> totals{};

    void closeAll()
    {
#if defined(__linux__)
        for (int & fd : fds)
        {
            if (fd >= 0)
                close(fd);
            fd = -1;
        }
#endif
    }
};

}
