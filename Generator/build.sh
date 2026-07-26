#!/usr/bin/env bash

set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
cd "$script_dir"
./mads no_name.asq -o:output.xex
echo "Generated: $script_dir/output.xex"
