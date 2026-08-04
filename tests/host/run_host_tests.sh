#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd -- "${script_dir}/../.." && pwd)"

cd "${repo_root}"
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s tests/host -p 'test_*.py'
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover \
    -s mLRS/modules/stm32ll-lib/tests -p 'test_*.py'
"${script_dir}/run_arq_tests.sh"
"${script_dir}/run_fifo_tests.sh"
"${script_dir}/run_radio_irq_tests.sh"
"${script_dir}/run_mavlinkx_tests.sh"
"${script_dir}/run_serial_startup_tests.sh"
"${script_dir}/run_udp_drain_tests.sh"
