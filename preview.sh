#!/usr/bin/env bash
# Build and run the scoreboard natively on macOS (or Linux) so you can iterate
# on the layout without touching the tablet. This is the only way to exercise
# the ESPN parsing without an SDK in the loop -- start here.
#
#   brew install qt cmake ninja
#   ./preview.sh                              # live data, portrait
#   ./preview.sh --orient landscape           # live data, landscape
#   ./preview.sh --demo live --orient landscape
#   ./preview.sh --demo live --shot out.png --panel 954x1696
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
BUILD="$HERE/build-preview"

PREFIX=""
if command -v brew >/dev/null 2>&1 && brew --prefix qt >/dev/null 2>&1; then
    PREFIX="-DCMAKE_PREFIX_PATH=$(brew --prefix qt)"
fi

cmake -S "$HERE" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release $PREFIX
cmake --build "$BUILD"

exec "$BUILD/cfb_scoreboard" "$@"
