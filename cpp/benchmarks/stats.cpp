#include "stats.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <thread>

namespace lob::bench {

namespace {

std::uint64_t percentile(const std::vector<std::uint64_t>& sorted, double p) {
    if (sorted.empty()) {
        return 0;
    }
    const double rank = p * static_cast<double>(sorted.size() - 1);
    const auto lo = static_cast<std::size_t>(std::floor(rank));
    const auto hi = static_cast<std::size_t>(std::ceil(rank));
    if (lo == hi) {
        return sorted[lo];
    }
    const double frac = rank - static_cast<double>(lo);
    return static_cast<std::uint64_t>(static_cast<double>(sorted[lo]) +
                                       frac * (static_cast<double>(sorted[hi]) - static_cast<double>(sorted[lo])));
}

} // namespace

LatencyStats compute_latency_stats(std::vector<std::uint64_t>& latencies_ns, double elapsed_seconds) {
    LatencyStats stats;
    stats.events_processed = latencies_ns.size();
    stats.elapsed_seconds = elapsed_seconds;
    stats.events_per_second =
        elapsed_seconds > 0.0 ? static_cast<double>(stats.events_processed) / elapsed_seconds : 0.0;

    std::sort(latencies_ns.begin(), latencies_ns.end());
    stats.p50_ns = percentile(latencies_ns, 0.50);
    stats.p95_ns = percentile(latencies_ns, 0.95);
    stats.p99_ns = percentile(latencies_ns, 0.99);
    stats.p999_ns = percentile(latencies_ns, 0.999);
    return stats;
}

void print_report(const std::vector<BenchmarkResult>& results) {
    std::cout << std::left << std::setw(24) << "operation" << std::setw(22) << "workload" << std::setw(10)
              << "target_n" << std::setw(10) << "actual_n" << std::setw(8) << "levels" << std::setw(10) << "events"
              << std::setw(14) << "events/sec" << std::setw(10) << "p50(ns)" << std::setw(10) << "p95(ns)"
              << std::setw(10) << "p99(ns)" << std::setw(12) << "p99.9(ns)" << '\n';

    for (const auto& r : results) {
        std::cout << std::left << std::setw(24) << r.operation << std::setw(22) << r.workload_config << std::setw(10)
                   << r.active_order_target << std::setw(10) << r.active_orders_actual << std::setw(8)
                   << r.price_levels << std::setw(10) << r.stats.events_processed << std::setw(14)
                   << static_cast<std::uint64_t>(r.stats.events_per_second) << std::setw(10) << r.stats.p50_ns
                   << std::setw(10) << r.stats.p95_ns << std::setw(10) << r.stats.p99_ns << std::setw(12)
                   << r.stats.p999_ns << '\n';
    }
}

void write_csv(const std::string& path, const std::vector<BenchmarkResult>& results) {
    std::ofstream out(path, std::ios::trunc);
    out << "operation,workload_config,active_order_target,active_orders_actual,price_levels,"
           "events_processed,events_per_second,p50_ns,p95_ns,p99_ns,p999_ns\n";
    for (const auto& r : results) {
        out << r.operation << ',' << r.workload_config << ',' << r.active_order_target << ','
            << r.active_orders_actual << ',' << r.price_levels << ',' << r.stats.events_processed << ','
            << r.stats.events_per_second << ',' << r.stats.p50_ns << ',' << r.stats.p95_ns << ',' << r.stats.p99_ns
            << ',' << r.stats.p999_ns << '\n';
    }
}

void print_environment_info() {
    std::cout << "# Benchmark environment\n";
#if defined(_MSC_VER)
    std::cout << "compiler: MSVC " << _MSC_VER << '\n';
#elif defined(__clang__)
    std::cout << "compiler: Clang " << __clang_version__ << '\n';
#elif defined(__GNUC__)
    std::cout << "compiler: GCC " << __VERSION__ << '\n';
#else
    std::cout << "compiler: unknown\n";
#endif

#if defined(NDEBUG)
    std::cout << "build_type: Release (NDEBUG defined)\n";
#else
    std::cout << "build_type: Debug (NDEBUG not defined) -- results below are not representative\n";
#endif

#if defined(_WIN32)
    std::cout << "os: Windows\n";
#elif defined(__linux__)
    std::cout << "os: Linux\n";
#elif defined(__APPLE__)
    std::cout << "os: macOS\n";
#else
    std::cout << "os: unknown\n";
#endif

    std::cout << "hardware_concurrency: " << std::thread::hardware_concurrency() << " logical cores\n";
    std::cout << "note: CPU model, frequency configuration, and core pinning are not captured\n"
                 "      automatically here -- record these manually when publishing results.\n"
                 "      See docs/benchmark_methodology.md.\n";
}

} // namespace lob::bench
