#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace lob::bench {

struct LatencyStats {
    std::uint64_t events_processed{};
    double elapsed_seconds{};
    double events_per_second{};
    std::uint64_t p50_ns{};
    std::uint64_t p95_ns{};
    std::uint64_t p99_ns{};
    std::uint64_t p999_ns{};
};

// Sorts `latencies_ns` in place and computes throughput + percentiles.
LatencyStats compute_latency_stats(std::vector<std::uint64_t>& latencies_ns, double elapsed_seconds);

struct BenchmarkResult {
    std::string operation;        // e.g. "add_existing_level", "mixed_event_stream"
    std::string workload_config;  // e.g. "n/a" or "balanced (50/30/20)"
    std::size_t active_order_target{};
    std::size_t active_orders_actual{};
    std::size_t price_levels{};
    LatencyStats stats;
};

void print_report(const std::vector<BenchmarkResult>& results);
void write_csv(const std::string& path, const std::vector<BenchmarkResult>& results);
void print_environment_info();

} // namespace lob::bench
