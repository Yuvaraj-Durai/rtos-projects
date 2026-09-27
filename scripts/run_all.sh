#!/usr/bin/env bash
# Configure, build and test every project in one CMake tree.
#
#   scripts/run_all.sh [build-dir]        (default: build/)
#
# Extra CMake options can be passed through CMAKE_ARGS, e.g.
#   CMAKE_ARGS="-DFREERTOS_KERNEL_PATH=$HOME/FreeRTOS-Kernel" scripts/run_all.sh
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${1:-$repo_root/build}"
jobs="$(nproc 2>/dev/null || echo 4)"

# shellcheck disable=SC2086
cmake -S "$repo_root" -B "$build_dir" ${CMAKE_ARGS:-}
cmake --build "$build_dir" -j "$jobs"
cd "$build_dir"
ctest --output-on-failure --timeout 120
