#!/usr/bin/env bash
# End-to-end Milestone 8 reproduction from a clean clone.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

python -m pip install -r requirements.txt
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DLOB_BUILD_PYTHON_BINDINGS=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
python -m pytest python/tests
# Replay determinism is exercised explicitly by the replay test group.
ctest --test-dir build -C Release -R Replay --output-on-failure
bash scripts/run_benchmarks.sh
python scripts/run_simulation.py --config configs/baseline.yaml
python scripts/generate_charts.py
