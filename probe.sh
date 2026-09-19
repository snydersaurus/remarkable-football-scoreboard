#!/usr/bin/env bash
# Run the feed for real -- against live ESPN, or against saved payloads -- and
# print the state map the QML reads. No device, and no Qt on the Mac needed.
#
#   ./probe.sh                    # Ohio State, live ESPN
#   ./probe.sh 2754               # Youngstown State, live ESPN
#   LEAGUE=nfl ./probe.sh         # the Browns, live ESPN
#   ./probe.sh 194 20 fixtures    # parse tools/fixtures instead of the network
#   ./probe.sh 2754 8 fixtures-live   # ... including a game in progress
#
# This is the check worth doing before anything is deployed: the layout can be
# argued about from a screenshot, but whether the parsing survives a real
# payload cannot be. Fixture mode is for the situation block -- down, distance,
# possession, last play -- which exists in no payload you can fetch on a day
# with no game on. See tools/fixtures.sh.
#
# It builds against Ubuntu's Qt in its own small image rather than the
# reMarkable SDK's host Qt, which ships no TLS plugin and needs a newer glibc
# than the SDK image has.
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
IMAGE="${IMAGE:-cfb-probe}"
TEAM="${1:-0}"          # 0 = the league's first followed team
LEAGUE="${LEAGUE:-cfb}"
# A team's season is 210KB and the container is emulated, so the live fetch
# wants longer than it looks like it should. Ten seconds was not enough and the
# board came back empty, which reads exactly like a parsing failure.
SECS="${2:-25}"
MODE="${3:-live}"

if ! docker image inspect "$IMAGE" >/dev/null 2>&1; then
    echo "==> building the probe image (once)"
    docker build --platform linux/amd64 -t "$IMAGE" -f "$HERE/docker/probe.Dockerfile" "$HERE/docker"
fi

FIXTURES=""
if [ "$MODE" != "live" ]; then
    [ -d "$HERE/tools/$MODE" ] || { echo "no tools/$MODE — run tools/fixtures.sh first"; exit 1; }
    FIXTURES="-e CFB_FIXTURES=/src/tools/$MODE"
fi

# shellcheck disable=SC2086
docker run --rm --platform linux/amd64 -v "$HERE":/src -e LEAGUE="$LEAGUE" -e PROBE_EVENT="${PROBE_EVENT:-}" $FIXTURES "$IMAGE" bash -lc "
  set -euo pipefail
  # Configure fresh: a cache left by another image points at a toolchain that
  # is not in this one, and the failure looks nothing like the cause.
  rm -rf /src/build-probe
  cmake -S /src/tools -B /src/build-probe -G Ninja -DCMAKE_BUILD_TYPE=Release >/dev/null
  cmake --build /src/build-probe >/dev/null
  /src/build-probe/cfb_probe $TEAM $SECS
"
