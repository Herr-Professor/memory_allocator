#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <random>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#if defined(ALLOCATOR_SYSTEM)
#include "allocators/system.h"
#elif defined(ALLOCATOR_JEMALLOC)
#include "allocators/jemalloc.h"
#elif defined(ALLOCATOR_TCMALLOC)
#include "allocators/tcmalloc.h"
#elif defined(ALLOCATOR_MEMPOOL)
#include "allocators/mempool.h"
#else
#error "Define allocator macro (ALLOCATOR_SYSTEM / ALLOCATOR_JEMALLOC / ALLOCATOR_TCMALLOC / ALLOCATOR_MEMPOOL)"
#endif

struct WorkloadSpec {
    std::string name;
    std::vector<size_t> sizes;
    std::vector<double> weights;
    double alloc_prob;
    size_t max_live;
    size_t alignment;
};

struct AllocationRecord {
    void* ptr;
    size_t requested;
    size_t usable;
    bool aligned;
};

struct ThreadStats {
    uint64_t ops = 0;
    uint64_t alloc_ops = 0;
    uint64_t free_ops = 0;
    uint64_t total_requested = 0;
    uint64_t total_usable = 0;
    uint64_t live_requested = 0;
    uint64_t live_usable = 0;
    uint64_t peak_live_requested = 0;
    uint64_t peak_live_usable = 0;
    std::vector<uint64_t> alloc_samples;
    std::vector<uint64_t> free_samples;
};

static std::vector<int> parse_int_list(const std::string& value) {
    std::vector<int> out;
    size_t start = 0;
    while (start < value.size()) {
        size_t end = value.find(',', start);
        if (end == std::string::npos) {
            end = value.size();
        }
        out.push_back(std::stoi(value.substr(start, end - start)));
        start = end + 1;
    }
    return out;
}

static std::vector<std::string> parse_string_list(const std::string& value) {
    std::vector<std::string> out;
    size_t start = 0;
    while (start < value.size()) {
        size_t end = value.find(',', start);
        if (end == std::string::npos) {
            end = value.size();
        }
        out.push_back(value.substr(start, end - start));
        start = end + 1;
    }
    return out;
}

static uint64_t percentile_ns(std::vector<uint64_t>& samples, double pct) {
    if (samples.empty()) {
        return 0;
    }
    std::sort(samples.begin(), samples.end());
    size_t index = static_cast<size_t>((pct / 100.0) * (samples.size() - 1));
    return samples[index];
}

static std::vector<WorkloadSpec> default_workloads() {
    return {
        {
            "rl_small",
            {16, 32, 64, 128, 256, 512},
            {0.25, 0.25, 0.2, 0.15, 0.1, 0.05},
            0.65,
            4096,
            0
        },
        {
            "rl_medium",
            {128, 256, 512, 1024, 2048, 4096},
            {0.2, 0.25, 0.25, 0.15, 0.1, 0.05},
            0.6,
            2048,
            0
        },
        {
            "fragmentation_mix",
            {16, 32, 64, 128, 256, 512, 1024, 2048, 4096},
            {0.1, 0.1, 0.1, 0.12, 0.12, 0.12, 0.12, 0.12, 0.1},
            0.55,
            8192,
            0
        },
        {
            "alignment64",
            {64, 128, 256, 512, 1024},
            {0.35, 0.25, 0.2, 0.15, 0.05},
            0.6,
            2048,
            64
        }
    };
}

static const WorkloadSpec* find_workload(const std::vector<WorkloadSpec>& specs, const std::string& name) {
    for (const auto& spec : specs) {
        if (spec.name == name) {
            return &spec;
        }
    }
    return nullptr;
}

static ThreadStats run_thread(const WorkloadSpec& spec, uint64_t ops, uint64_t seed,
                              std::atomic<int>& ready, std::atomic<bool>& start) {
    ThreadStats stats;
    stats.alloc_samples.reserve(static_cast<size_t>(ops / 1024));
    stats.free_samples.reserve(static_cast<size_t>(ops / 1024));

    std::mt19937_64 rng(seed);
    std::uniform_real_distribution<double> prob_dist(0.0, 1.0);
    std::discrete_distribution<size_t> size_dist(spec.weights.begin(), spec.weights.end());

    std::vector<AllocationRecord> live;
    live.reserve(spec.max_live / 4);

    ready.fetch_add(1, std::memory_order_relaxed);
    while (!start.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }

    constexpr uint64_t sample_mask = 1023;

    for (uint64_t i = 0; i < ops; ++i) {
        bool can_alloc = live.size() < spec.max_live;
        bool can_free = !live.empty();
        bool do_alloc = can_alloc && (prob_dist(rng) < spec.alloc_prob || !can_free);

        if (do_alloc) {
            const size_t size = spec.sizes[size_dist(rng)];
            const bool aligned = spec.alignment > 0;
            auto start_time = std::chrono::steady_clock::time_point{};
            bool sample = (rng() & sample_mask) == 0;
            if (sample) {
                start_time = std::chrono::steady_clock::now();
            }

            void* ptr = aligned ? alloc_aligned(size, spec.alignment) : alloc(size);
            if (!ptr) {
                continue;
            }

            if (sample) {
                auto end_time = std::chrono::steady_clock::now();
                uint64_t ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time).count();
                stats.alloc_samples.push_back(ns);
            }

            size_t usable = usable_size(ptr, size, aligned);

            live.push_back({ptr, size, usable, aligned});
            stats.alloc_ops++;
            stats.total_requested += size;
            stats.total_usable += usable;
            stats.live_requested += size;
            stats.live_usable += usable;
            stats.peak_live_requested = std::max(stats.peak_live_requested, stats.live_requested);
            stats.peak_live_usable = std::max(stats.peak_live_usable, stats.live_usable);
        } else {
            std::uniform_int_distribution<size_t> pick_dist(0, live.size() - 1);
            size_t index = pick_dist(rng);
            AllocationRecord record = live[index];
            live[index] = live.back();
            live.pop_back();

            auto start_time = std::chrono::steady_clock::time_point{};
            bool sample = (rng() & sample_mask) == 0;
            if (sample) {
                start_time = std::chrono::steady_clock::now();
            }

            dealloc(record.ptr, record.aligned);

            if (sample) {
                auto end_time = std::chrono::steady_clock::now();
                uint64_t ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time).count();
                stats.free_samples.push_back(ns);
            }

            stats.free_ops++;
            stats.live_requested -= record.requested;
            stats.live_usable -= record.usable;
        }

        stats.ops++;
    }

    for (const auto& record : live) {
        dealloc(record.ptr, record.aligned);
    }
    stats.live_requested = 0;
    stats.live_usable = 0;

    return stats;
}

static void print_csv_header() {
    std::cout << "allocator,workload,threads,ops_per_thread,total_ops,seconds,throughput_ops_s,"
              << "alloc_p50_ns,alloc_p99_ns,free_p50_ns,free_p99_ns,avg_overhead_ratio,"
              << "peak_live_requested,peak_live_usable,alignment" << std::endl;
}

int main(int argc, char** argv) {
    uint64_t ops_per_thread = 200000;
    std::vector<int> thread_counts = {1, 2, 4, 8};
    std::vector<std::string> workload_names;
    uint64_t seed = 42;
    bool print_header = true;

    for (int i = 1; i < argc; ++i) {
        if (std::strncmp(argv[i], "--ops=", 6) == 0) {
            ops_per_thread = std::stoull(argv[i] + 6);
        } else if (std::strncmp(argv[i], "--threads=", 10) == 0) {
            thread_counts = parse_int_list(argv[i] + 10);
        } else if (std::strncmp(argv[i], "--workloads=", 12) == 0) {
            workload_names = parse_string_list(argv[i] + 12);
        } else if (std::strncmp(argv[i], "--seed=", 7) == 0) {
            seed = std::stoull(argv[i] + 7);
        } else if (std::strcmp(argv[i], "--no-header") == 0) {
            print_header = false;
        }
    }

    auto workloads = default_workloads();
    if (workload_names.empty()) {
        for (const auto& spec : workloads) {
            workload_names.push_back(spec.name);
        }
    }

    if (print_header) {
        print_csv_header();
    }

    for (const auto& workload_name : workload_names) {
        const WorkloadSpec* spec = find_workload(workloads, workload_name);
        if (!spec) {
            std::cerr << "Unknown workload: " << workload_name << std::endl;
            continue;
        }

        for (int threads : thread_counts) {
            std::cerr << "[bench] start allocator=" << allocator_name()
                      << " workload=" << spec->name
                      << " threads=" << threads
                      << " ops=" << ops_per_thread
                      << std::endl;
            std::atomic<int> ready{0};
            std::atomic<bool> start{false};

            std::vector<std::thread> workers;
            std::vector<ThreadStats> stats(threads);
            workers.reserve(threads);

            for (int t = 0; t < threads; ++t) {
                workers.emplace_back([&, t]() {
                    thread_init();
                    stats[t] = run_thread(*spec, ops_per_thread, seed + static_cast<uint64_t>(t) * 1315423911u,
                                          ready, start);
                    thread_teardown();
                });
            }

            while (ready.load(std::memory_order_relaxed) < threads) {
                std::this_thread::yield();
            }

            auto bench_start = std::chrono::steady_clock::now();
            start.store(true, std::memory_order_release);

            for (auto& worker : workers) {
                worker.join();
            }
            auto bench_end = std::chrono::steady_clock::now();

            uint64_t total_ops = 0;
            uint64_t total_alloc_ops = 0;
            uint64_t total_free_ops = 0;
            uint64_t total_requested = 0;
            uint64_t total_usable = 0;
            uint64_t peak_live_requested = 0;
            uint64_t peak_live_usable = 0;
            std::vector<uint64_t> alloc_samples;
            std::vector<uint64_t> free_samples;

            for (const auto& stat : stats) {
                total_ops += stat.ops;
                total_alloc_ops += stat.alloc_ops;
                total_free_ops += stat.free_ops;
                total_requested += stat.total_requested;
                total_usable += stat.total_usable;
                peak_live_requested = std::max(peak_live_requested, stat.peak_live_requested);
                peak_live_usable = std::max(peak_live_usable, stat.peak_live_usable);
                alloc_samples.insert(alloc_samples.end(), stat.alloc_samples.begin(), stat.alloc_samples.end());
                free_samples.insert(free_samples.end(), stat.free_samples.begin(), stat.free_samples.end());
            }

            double seconds = std::chrono::duration_cast<std::chrono::duration<double>>(bench_end - bench_start).count();
            double throughput = seconds > 0.0 ? static_cast<double>(total_ops) / seconds : 0.0;
            double overhead_ratio = total_requested > 0 ? static_cast<double>(total_usable) / static_cast<double>(total_requested) : 0.0;

            uint64_t alloc_p50 = percentile_ns(alloc_samples, 50.0);
            uint64_t alloc_p99 = percentile_ns(alloc_samples, 99.0);
            uint64_t free_p50 = percentile_ns(free_samples, 50.0);
            uint64_t free_p99 = percentile_ns(free_samples, 99.0);

            std::cout << allocator_name() << ','
                      << spec->name << ','
                      << threads << ','
                      << ops_per_thread << ','
                      << total_ops << ','
                      << seconds << ','
                      << throughput << ','
                      << alloc_p50 << ','
                      << alloc_p99 << ','
                      << free_p50 << ','
                      << free_p99 << ','
                      << overhead_ratio << ','
                      << peak_live_requested << ','
                      << peak_live_usable << ','
                      << spec->alignment
                      << std::endl;

            std::cerr << "[bench] done allocator=" << allocator_name()
                      << " workload=" << spec->name
                      << " threads=" << threads
                      << " seconds=" << seconds
                      << " throughput_ops_s=" << throughput
                      << std::endl;

            allocator_reset();
        }
    }

    return 0;
}
