#!/usr/bin/env bash
# Builds the engine in Release and runs the C++ benchmark suite against the
# workloads defined in configs/, writing results under results/.
#
# TODO(Milestone 4/5): pre-generated workload benchmarks (add/cancel/replace/
# match, passive-heavy/balanced/match-heavy, 1k/10k/100k active orders),
# percentile reporting, baseline-vs-optimized comparison.
set -euo pipefail

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target lob_benchmarks

echo "TODO: workload-driven benchmarks not implemented yet."
echo "See docs/benchmark_methodology.md."
