#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
build_dir="$(mktemp -d "${TMPDIR:-/tmp}/mlrs-parser-budget-tests.XXXXXX")"
trap 'rm -rf -- "$build_dir"' EXIT

cxx="${CXX:-c++}"
"$cxx" \
    -std=c++17 \
    -O1 \
    -g \
    -Wall \
    -Wextra \
    -Werror \
    -fsanitize=address,undefined \
    -fno-omit-frame-pointer \
    -I"$repo_root" \
    "$repo_root/tests/host/test_parser_budget.cpp" \
    -o "$build_dir/test_parser_budget"

ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 "$build_dir/test_parser_budget"
