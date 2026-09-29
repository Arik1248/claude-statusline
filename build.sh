#!/usr/bin/env bash
# Build the native statusline and prove it still matches the reference shell
# implementation before it goes live.
#
#   ./build.sh          build + run the differential test suite
#   ./build.sh --quick  build only
set -euo pipefail
cd "$(dirname "$0")"

echo "==> building"
clang -O2 -Wall -o statusline statusline.c
echo "    statusline"

if [ "${1:-}" = "--quick" ]; then
  echo "==> skipping tests (--quick)"
  exit 0
fi

echo "==> differential tests"
bash statusline.test.sh | tail -2
