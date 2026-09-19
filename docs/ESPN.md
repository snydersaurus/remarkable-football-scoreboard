# ESPN's site API: what it gives you and what it does to you

Unauthenticated, undocumented, and identical in shape across leagues — college
football and the NFL differ by a path segment and nothing else. Every fact here
was checked against live responses, not inferred.

Base: `https://site.api.espn.com/apis/site/v2/sports/football/<league>`
where `<league>` is `college-football` or `nfl`.

## The User-Agent decides whether you get data at all

ESPN sits behind Akamai, and the edge answers **403 Access Denied** to a
User-Agent it does not recognise. It matches on the leading token:

| header | result |
| --- | --- |
| *(none)* | 403 |
| `rmpp-scoreboard/1.0` | 403 |
| `Wget/1.21` | 403 |
| `Qt/6.8` | 403 |
| `Mozilla/5.0 (…) Chrome/120…` | 403 |
| `curl/8.7.1` | 200 |
| `python-requests/2.31.0` | 200 |
| `okhttp/4.12.0` | 200 |
| `curl/8.7.1 rmpp-scoreboard/1.0` | 200 |

Deterministic, reproducible, and a browser-shaped string is refused as well, so
imitating Chrome is no help. Lead with a known client token and then say who is
really calling. The failure mode is total — every request fails and the app
reads OFFLINE with nothing else to go on — so check this first, always.

The logo CDN (`a.espncdn.com`) does not care either way.

## There is no field filtering

No `fields=`, no `select=`. You cannot make a response smaller; you can only
choose a smaller endpoint. That choice is the whole performance story:

| endpoint | size | use |
| --- | --- | --- |
| `scoreboard/{eventId}` | **17KB** NFL / **23KB** CFB | one game, in full. Poll this. |
| `teams/{id}/schedule` | 210KB CFB / 246KB NFL | a whole season. Ten-minute timer. |
| `teams/{id}` | ~19KB | rank, record, conference standing. Twice an hour. |
| `scoreboard?dates=YYYYMMDD` | 406KB Thu → **1.3MB Sat** | every game that day. On demand only. |
| `teams?limit=1000` | **1.9MB** CFB | the whole league. Once ever; cache it. |

`scoreboard/{eventId}` is the single-event form of `/scoreboard` and is the
reason a 15-second poll is reasonable at all — the league-wide version carries
~18KB per game and college football has 762 teams in it. `limit=` works but
truncates by start time, so it cannot be used to find the live games.

## Shapes that will catch you

- **Ids are strings**, including numeric ones: `"id": "194"`. Reading them as
  integers silently yields 0.
- **A score has three shapes**: `{"value":70,"displayValue":"70"}` in a team's
  schedule, `"70"` in a single event, and absent before kickoff.
- **`curatedRank.current` is 99 for unranked**, not a 99th-place team. NFL
  teams have no rank at all.
- **`status` hangs off the competition** (`competitions[0].status`), not always
  off the event.
- Dates are UTC with a bare `Z` (`2026-09-19T16:00Z`). `timeValid: false` means
  the kickoff time is not set yet, not that the date is wrong.
- Bye weeks are simply **missing** from a schedule — week 8 does not appear.
  Fill the gaps yourself or the list reads like a parsing bug.

## Football's live state

`competitions[0].situation` exists only while a game is being played, and
**college sends much less than the NFL**:

| field | NFL | college |
| --- | --- | --- |
| `down`, `distance`, `yardLine` | yes | yes |
| `homeTimeouts` / `awayTimeouts` | yes | yes |
| `isRedZone` | yes | yes |
| `lastPlay` | yes | yes |
| `downDistanceText`, `shortDownDistanceText` | yes | **no** |
| `possessionText` | yes | **no** |
| `possession` | yes | **no** |

So compose the text yourself, and take possession from
`situation.lastPlay.team.id`.

- **`yardLine` counts from the team in possession's own goal line.** 65 means
  the ball is on the opponent's 35. Past 50 the field position is named for the
  other team.
- **Between plays — after a score, during the extra point, on a kickoff —
  `down` and `distance` go to `-1` and `yardLine` disappears**, while the
  situation object itself stays. Hold the last known values or the middle of
  the board empties every time anyone scores and looks broken.
- The situation object vanishing entirely means the game is over or has not
  started. That, and only that, should clear things.
- **`linescores` is empty until points are scored**, so a quarter-by-quarter
  grid built from it collapses to nothing at 0-0. Draw the four quarters
  regardless and fill them in.
- Once a game is final the last play is "End of game", which says nothing.
  `competitions[0].headlines[0].description` is ESPN's recap sentence and is
  the useful thing for that slot.

## Logos

`https://a.espncdn.com/i/teamlogos/<ncaa|nfl>/500/<id>.png` — 500x500 RGBA PNG,
~40KB. Verified present for every team on both 2026 schedules, FBS and FCS.
They are raster, not the small clean SVGs MLB serves, so greyscale them with
QImage. **Team ids collide between leagues** — 5 is the Browns and also
Marshall — so cache each league separately.

## Verifying without a device

`tools/probe.cpp` builds the real feed and prints the state map the QML reads.
It runs against live ESPN, or against saved payloads via the `CFB_FIXTURES`
seam. Fixture mode is what lets you exercise the live situation block on a day
with no game on — see `tools/fixtures-live.sh`, which rewinds a real finished
game into the third quarter.

Two notes: the live fetch wants **20+ seconds**, because two 210KB season
payloads through an emulated x86 container are slow, and at ten seconds the
output looks exactly like a parsing failure. And the probe builds against
Ubuntu's Qt, not the reMarkable SDK's host Qt — that one needs glibc 2.38 while
the SDK image is Ubuntu 22.04 (2.35), so it will not even link, and it ships no
TLS plugin, so it could not make an HTTPS request anyway.
