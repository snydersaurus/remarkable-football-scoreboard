# Context for Claude Code

**Read [docs/PLATFORM.md](docs/PLATFORM.md) and
[docs/ESPN.md](docs/ESPN.md) first.** The device — SDK version matching,
AppLoad packaging, the socket protocol, TLS, e-ink rules — is in the first. The
API — endpoint sizes, the User-Agent that decides whether you get data at all,
what football's live state contains — is in the second. Neither is repeated
here. This file covers only this app: what is verified, what is not, and the
decisions that are easy to undo by accident.

the `mlb-scoreboard` repo is the baseball version and where most of the device
knowledge was originally paid for.

## What this is

A football scoreboard for a reMarkable Paper Pro Move. **One codebase, two
installs**: college football (following Ohio State, **194**, and Youngstown
State, **2754**) and the NFL (the Browns, **5**, and the Eagles, **21**), which
are the same ESPN API with a different path segment. `src/League.h` holds
everything that differs; the backend reads a `league` file next to its
executable, because AppLoad gives it a bare environment.

Four pages: the game, both followed seasons, everything live across the league,
and a picker for choosing which two teams to follow. Whichever team is first
becomes the launcher icon.

## State of play

**Verified.**

- Cross-compiles clean for aarch64 against the reMarkable SDK; both the
  fullscreen binary and the AppLoad backend link, and every shared QML file
  goes through qmlcachegen without complaint.
- The parsing, against real payloads: both seasons, weeks, Eastern dates,
  home/away, opponent ranks, results, kickoff times, bye weeks, current-game
  selection, records, conference standings, and the whole live block — quarter,
  clock, possession, down and distance, field position, timeouts, last play,
  quarter-by-quarter and leaders. `./probe.sh` does this; see below.
- The logo fetch and greyscale conversion, end to end, and that all 25 teams on
  both 2026 schedules (FBS and FCS) actually have a logo at ESPN's URL.

**Not verified — this is the work left:**

1. **The layout has never been rendered.** There is no Qt on this Mac, and the
   SDK's host Qt ships no QPA plugin, so neither `./preview.sh` nor `--shot`
   has ever run. Every size in `ui/Board.qml` was reasoned from the baseball
   board's tuned values, not looked at. Expect to find something clipped,
   particularly in landscape on the Move, where `vScale` shrinks everything.
   `brew install qt` and `./preview.sh --demo live --shot` is the cheap way in.
2. Nothing has run on the device.
3. The live situation block has been parsed from a *reconstructed* live payload
   (`tools/fixtures-live.sh`), not one fetched during a game. The field names
   come from ESPN's own published shape and the parser falls back where a field
   might be absent, but it has never seen the real thing. Capture one during
   any game and re-run the probe — the script says how.

## ESPN's API

Three endpoints, all unauthenticated, under
`https://site.api.espn.com/apis/site/v2/sports/football/college-football`:

- `teams/{id}/schedule` — the whole season. **210KB**, so it is on a ten minute
  timer, never the poll.
- `teams/{id}` — rank, overall record, conference standing. 20KB, twice an hour.
- `scoreboard/{eventId}` — one game in scoreboard shape: 23KB, polled every
  fifteen seconds. This is the single-event form of `/scoreboard`; the plain
  `/scoreboard` is **1.3MB** because there are 762 teams in this league, and
  polling it on a tablet is not an option. There is no field-trimming
  parameter, so this is the whole trick.

Traps, all of them found the hard way:

- **The `User-Agent` decides whether you get data at all.** ESPN's Akamai edge
  answers `403 Access Denied` to a header it does not recognise — including no
  header, `Wget/1.21`, `Qt/6.8`, a Chrome-shaped string, and
  `rmpp-scoreboard/1.0`, which is exactly what the baseball app sends and what
  MLB is happy with. It matches on the leading token and lets `curl/`,
  `python-requests/` and `okhttp/` through. `src/EspnUserAgent.h` holds the one
  string everything uses; the failure mode is total, so if the board is
  OFFLINE, check this first.
- **Ids are strings**, including numeric ones: `"id": "194"`. Reading them with
  `.toInt()` on the QJsonValue silently gives 0.
- **A score has three shapes.** `{"value":70,"displayValue":"70"}` in a team's
  schedule, `"70"` in the single-event payload, and absent before kickoff.
  `scoreOf()` handles all three; do not simplify it.
- **`curatedRank.current` is 99 for an unranked team**, not a 99th-place team.
- **`situation` is absent between games**, not empty — so every field under it
  has to be cleared explicitly, or the board keeps a down and distance up after
  the final whistle.
- **`situation.yardLine` is measured from the team in possession's own goal
  line.** 76 means the ball is on the opponent's 24. That is why `FieldStrip`
  needs no direction flag.
- **A finished game's last play is "End of game"**, which says nothing, so at
  the final the board shows `headlines[0].description` — ESPN's recap sentence
  — in that slot instead.
- Bye weeks are simply missing from the schedule: week 8 does not appear.
  `requestSchedule` fills the gaps, because a season list that jumps from 7 to
  9 reads like a parsing bug. Do not "fix" it by removing them.
- FCS teams work exactly the same as FBS ones. Youngstown State's early games
  are both numbered week 1, which is ESPN's data and not a bug — the bye filler
  is written to tolerate it.

## Verifying without a device: probe.sh

`./probe.sh` builds a console harness (`tools/probe.cpp`) that constructs the
real `GameFeed` and prints the state map the QML would read. This is the check
worth doing before anything is deployed.

```bash
./probe.sh                        # Ohio State, live ESPN
./probe.sh 2754                   # Youngstown State
tools/fixtures.sh                 # capture real payloads (curl only)
./probe.sh 194 20 fixtures        # parse those instead of the network
tools/fixtures-live.sh            # rewind a real game into the 3rd quarter
./probe.sh 2754 8 fixtures-live   # ... and parse that
```

Two things to know about it:

- It builds against **Ubuntu's Qt** in its own small image
  (`docker/probe.Dockerfile`), not the reMarkable SDK's host Qt. The SDK's host
  Qt cannot be used: it needs glibc 2.38 while the SDK image is Ubuntu 22.04
  (2.35) so it will not even link, and it ships no plugins at all, which means
  no TLS and therefore no HTTPS.
- The live fetch wants **20+ seconds**, because two 210KB season payloads
  through an emulated x86 container are slow. At ten seconds the schedules had
  not landed and the output looked exactly like a parsing failure.

Fixture mode is the `CFB_FIXTURES` seam in `GameFeed::get()`: it maps a request
URL onto a file name (everything after `/college-football/`, punctuation folded
to dashes) and reads that instead of the network. It exists because college
football is played one day a week, so the situation block — most of the live
board — is in no payload you can fetch on a Tuesday.

## Shared QML, one copy

The baseball app has the view twice: `qml/` for the fullscreen build and
`appload-native/ui/` for the device. Those two copies have already drifted —
`GameCard.qml` differs between them. Here there is one `ui/` directory and two
thin shells over it:

- `ui/Board.qml` is the whole view and knows nothing about how it is hosted.
- `ui/Main.qml` is the AppLoad frontend: the socket, JSON in, messages out.
- `qml/Preview.qml` is the desktop and fullscreen shell: window, panel
  geometry, rotation.

CMake aliases every file to the QML module root, so a component in `ui/` and
the shell in `qml/` resolve each other as siblings — the same way they do
inside `resources.rcc`. `application.qrc` lives at the project root for the
same reason: rcc resolves its paths relative to the `.qrc`, and the files are
one directory up from `appload-native/`.

`ui/Main.qml` is the only file qmlcachegen never compiles, because it imports
`net.asivery.AppLoad`, which does not exist off-device. `build.sh` runs qmllint
over `ui/` to cover it. That qmllint cannot resolve QtQuick in the container
and emits ~50 "not found" warnings on any file, including the baseball app's
known-good ones — read it for `Error:` lines only.

## Football, where baseball had something else

| baseball | football |
| --- | --- |
| base diamond | `FieldStrip.qml`: 100 yards, end zones, the ball, the line to gain |
| balls / strikes / outs pips | timeouts remaining, per team |
| batting-team marker | possession marker (the same slim accent bar) |
| inning / half-inning | quarter and clock |
| the pitch just thrown | the play just run (`lastPlayTag`) |
| R / H / E | quarters and T |
| pitchers of record, at the final | passing / rushing / receiving leaders |
| division place | conference standing ("1st in Big Ten") |
| today's games around the league | both followed seasons, a column each |

Two deliberate choices in there:

- **Leaders are shown at the final and at halftime only.** While play is live
  the last play is worth the space more, and on a short landscape canvas both
  do not fit.
- **The list is a season, not a day.** "Today's games" is the right list for
  baseball, which plays daily; for two college teams it would be empty six days
  a week. `ScheduleRow` drops its week column below ~620 design units, because
  the Move's portrait canvas gives each season column only ~500 and the full
  row overflowed into its neighbour.

## Conventions

Same as the baseball app, and they are not negotiable on this panel: no
animations, no gradients, no large dark fills; black on white with one accent
(`#A4123F`) used only where it carries meaning — here possession, a live game,
and the red zone; `faint` (`#5A5A5A`) for rules and borders, `muted`
(`#000000`) for secondary text, and they are not interchangeable.

The owner does not want Claude named in commit messages, branch names, or code
comments, and does not want co-author or "generated with" trailers.
