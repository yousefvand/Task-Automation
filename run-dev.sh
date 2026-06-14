#!/usr/bin/env bash
set -euo pipefail

if [[ ! -x ./build/taskautomation ]]; then
  ./build-arch.sh
fi

./build/taskautomation
