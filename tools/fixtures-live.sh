#!/usr/bin/env bash
# Derive a LIVE fixture set from the real one captured by fixtures.sh.
#
# College football is played one day a week, so the situation block -- down,
# distance, possession, timeouts, last play -- is absent from every payload you
# can fetch on a Tuesday, and that block is most of the board. This takes a
# real finished game and rewinds it: status back to the third quarter, scores
# back to what they were, and a situation added in the shape ESPN publishes.
#
#   tools/fixtures.sh && tools/fixtures-live.sh
#   ./probe.sh 2754 8 fixtures-live
#
# Better still, when a game is actually on -- any game -- capture the real
# thing over the top and re-run:
#   curl -sS -A "curl/8.7.1 rmpp-scoreboard/1.0" \
#     ".../college-football/scoreboard/<liveEventId>" \
#     -o tools/fixtures-live/scoreboard-<sameIdAsBelow>.json
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
SRC="$HERE/fixtures"
DST="$HERE/fixtures-live"
GAME=401867895        # Duquesne at Youngstown State, a real finished game

[ -d "$SRC" ] || { echo "run tools/fixtures.sh first"; exit 1; }
rm -rf "$DST"; cp -r "$SRC" "$DST"

python3 - "$DST" "$GAME" <<'PY'
import json, sys, glob, os
dst, game = sys.argv[1], sys.argv[2]

# The live status, shared by the schedule entry and the single-event payload.
LIVE = {"clock": 432.0, "displayClock": "7:12", "period": 3,
        "type": {"id": "2", "name": "STATUS_IN_PROGRESS", "state": "in",
                 "completed": False, "description": "In Progress",
                 "detail": "7:12 - 3rd Quarter", "shortDetail": "7:12 - 3rd"}}

# Exactly the fields ESPN puts under competitions[0].situation while a game is
# being played. yardLine is measured from the team in possession's own goal
# line, which is what the field strip draws against.
SITUATION = {
    "lastPlay": {
        "id": f"{game}301",
        "type": {"id": "68", "text": "Rush", "abbreviation": "RUSH"},
        "text": "Beau Brungard run for 9 yards to the Duq 24 for a 1ST down",
        "scoringPlay": False,
        "statYardage": 9,
        "team": {"id": "2754"},
    },
    "down": 1, "yardLine": 76, "distance": 10,
    "downDistanceText": "1st & 10 at DUQ 24",
    "shortDownDistanceText": "1st & 10",
    "possessionText": "DUQ 24",
    "isRedZone": False,
    "homeTimeouts": 3, "awayTimeouts": 2,
    "possession": "2754",
}

def rewind(competitor):
    # Third quarter: drop the fourth-quarter line and the winner flag, and put
    # the score back to the sum of what is left.
    lines = [l for l in competitor.get("linescores", []) if l.get("period", 9) <= 3][:3]
    competitor["linescores"] = lines
    total = sum(int(round(l.get("value", float(l.get("displayValue", 0))))) for l in lines)
    if isinstance(competitor.get("score"), dict):
        competitor["score"] = {"value": float(total), "displayValue": str(total)}
    else:
        competitor["score"] = str(total)
    competitor.pop("winner", None)
    return competitor

# The single-event payload the board polls.
p = os.path.join(dst, f"scoreboard-{game}.json")
ev = json.load(open(p))
ev["status"] = LIVE
comp = ev["competitions"][0]
comp["status"] = LIVE
comp["situation"] = SITUATION
comp["competitors"] = [rewind(c) for c in comp["competitors"]]
json.dump(ev, open(p, "w"))

# And the same game inside each team's season, so the list shows it live and
# the feed picks it as the current game.
for s in glob.glob(os.path.join(dst, "teams-*-schedule.json")):
    d = json.load(open(s))
    hit = False
    for e in d.get("events", []):
        if e.get("id") != game:
            continue
        e["status"] = LIVE
        c = e["competitions"][0]
        c["status"] = LIVE
        c["situation"] = SITUATION
        c["competitors"] = [rewind(x) for x in c["competitors"]]
        hit = True
    if hit:
        json.dump(d, open(s, "w"))

print(f"live fixture: event {game}, 3rd quarter, 7:12, Youngstown State with the ball")
PY

echo "now: ./probe.sh 2754 8 fixtures-live"
