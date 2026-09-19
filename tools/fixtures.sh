#!/usr/bin/env bash
# Capture a set of real ESPN payloads for probe.sh to parse. Run this on the
# Mac (it needs nothing but curl) whenever you want to check the parsing
# against what the API is serving right now.
#
#   tools/fixtures.sh                 # both teams and their current games
#   tools/fixtures.sh 401858225       # add one more event by id
#
# File names follow fixtureName() in GameFeed.cpp: everything after
# /college-football/, punctuation folded to dashes.
#
# To exercise the live path out of season, capture an event WHILE a game is
# being played -- any game, not just these teams -- and copy it over
# scoreboard-<id>.json for the game the board is on. That situation block is
# the only part of the parsing that cannot be checked on a quiet day.
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
DIR="$HERE/fixtures"
API="https://site.api.espn.com/apis/site/v2/sports/football/college-football"
TEAMS="194 2754"

mkdir -p "$DIR"

grab() {   # grab <url-tail>
    local tail="$1"
    local name
    name="$(printf '%s' "$tail" | sed 's/[^A-Za-z0-9]\{1,\}/-/g').json"
    # Same header the app sends; see src/EspnUserAgent.h for why it
    # has to lead with curl's token.
    curl -sS -A "curl/8.7.1 rmpp-scoreboard/1.0" "$API/$tail" -o "$DIR/$name"
    printf '  %-34s %8s bytes\n' "$name" "$(wc -c < "$DIR/$name" | tr -d ' ')"
}

echo "==> teams"
for t in $TEAMS; do
    grab "teams/$t"
    grab "teams/$t/schedule"
done

echo "==> each team's live, last and next game"
for t in $TEAMS; do
    # Capture all three candidates the feed might land on -- whatever is live,
    # the game just played, and the next one up -- because which one it picks
    # depends on the day you run the probe.
    ids="$(python3 - "$DIR/teams-$t-schedule.json" <<'PICK'
import json, sys, datetime
d = json.load(open(sys.argv[1]))
now = datetime.datetime.now(datetime.timezone.utc)
live = recent = nxt = None
for e in d.get("events", []):
    st = e["competitions"][0].get("status", e.get("status", {})).get("type", {})
    when = datetime.datetime.strptime(e["date"], "%Y-%m-%dT%H:%MZ").replace(
        tzinfo=datetime.timezone.utc)
    if st.get("state") == "in":
        live = e["id"]
    elif st.get("state") == "post":
        recent = e["id"]
    elif nxt is None and when > now:
        nxt = e["id"]
print(" ".join(i for i in (live, recent, nxt) if i))
PICK
)"
    for id in $ids; do
        grab "scoreboard/$id"
    done
done

echo "==> today's league-wide scoreboard (the LIVE NOW page)"
grab "scoreboard?dates=$(TZ=America/New_York date +%Y%m%d)"

for extra in "$@"; do
    grab "scoreboard/$extra"
done

echo
echo "captured into $DIR"
