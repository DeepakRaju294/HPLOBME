#!/usr/bin/env bash
# Builds the engine in Release and runs the baseline benchmark suite
# (add/cancel/replace/match, passive-heavy/balanced/match-heavy mixed
# streams, 1k/10k/100k active orders), writing results/baseline_benchmark.csv.
#
# Must be run from the repository root: the harness writes its CSV to a
# path relative to the current working directory.
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target lob_benchmarks --config Release

BENCH_BIN="build/cpp/benchmarks/lob_benchmarks"
if [ -f "build/cpp/benchmarks/Release/lob_benchmarks.exe" ]; then
  BENCH_BIN="build/cpp/benchmarks/Release/lob_benchmarks.exe"
elif [ -f "build/cpp/benchmarks/lob_benchmarks.exe" ]; then
  BENCH_BIN="build/cpp/benchmarks/lob_benchmarks.exe"
fi

"./${BENCH_BIN}"

echo "See docs/benchmark_methodology.md for methodology and controls, and"
echo "docs/performance_analysis.md for the baseline-vs-optimized story (Milestone 5)."
