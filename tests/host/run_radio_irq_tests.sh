#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
build_dir="$(mktemp -d "${TMPDIR:-/tmp}/mlrs-radio-irq-tests.XXXXXX")"
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
    "$repo_root/tests/host/test_radio_irq.cpp" \
    -o "$build_dir/test_radio_irq"

ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 "$build_dir/test_radio_irq"
