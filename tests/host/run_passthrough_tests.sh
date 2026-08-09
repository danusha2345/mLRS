#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
build_dir="$(mktemp -d "${TMPDIR:-/tmp}/mlrs-passthrough-tests.XXXXXX")"
trap 'rm -rf -- "$build_dir"' EXIT

cxx="${CXX:-c++}"
"$cxx" \
    -std=c++17 \
    -O1 \
    -g \
    -Wall \
    -Wextra \
    -Werror \
    -Wno-address-of-packed-member \
    -fsanitize=address,undefined,float-cast-overflow \
    -fno-omit-frame-pointer \
    -I"$repo_root" \
    "$repo_root/tests/host/test_passthrough.cpp" \
    "$repo_root/mLRS/Common/thirdparty/thirdparty.cpp" \
    -o "$build_dir/test_passthrough"

ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 "$build_dir/test_passthrough"
