# Football scoreboard — reMarkable Paper Pro

A Qt Quick scoreboard for college football and the NFL, running as an AppLoad
app on a reMarkable Paper Pro Move — a window inside xochitl, not a takeover of
the display. Black on white, no animation, sized for e-ink.

**One codebase, two installs.** College football and the NFL are the same ESPN
API with a different path segment, so [src/League.h](src/League.h) holds
everything that differs — API path, logo directory, application id, default
teams — and `build.sh` emits a bundle per league from a single binary.

## The four pages

- **GAME** — score, rank, record and conference standing, quarter and clock,
  possession, down and distance, where the ball is on the field, timeouts, the
  play that just happened, quarter by quarter, and the passing / rushing /
  receiving leaders. Before kickoff: start time, TV, the line and the venue.
  After the final: ESPN's recap.
- **SCHEDULE** — both followed seasons side by side, a column each, bye weeks
  included. Tap any game to open it on the board.
- **LIVE NOW** — every game in progress across the league, as a grid of cards
  with clock, down and possession. Tap one to put it on the board. This is the
  only way to watch a game neither followed team is in.
- **TEAMS** — pick the two teams whose seasons the schedule shows. Tap a slot
  to arm it, then tap a team. College is 762 teams, so the list is alphabetical
  behind an A–Z index; the NFL's 32 show whole.

A bar along the bottom switches between them.

The launcher icon ships neutral — a football drawn by
[tools/make-icon.py](tools/make-icon.py) — and becomes the first followed
team's mark once you pick one, which AppLoad shows after the tablet restarts.

## Data

ESPN's public API. No key, no auth, no documentation.

| what | endpoint | size |
| --- | --- | --- |
| one game, in full | `scoreboard/{eventId}` | 17KB NFL / 23KB CFB |
| a team's whole season | `teams/{id}/schedule` | 210–246KB |
| rank, record, standing | `teams/{id}` | ~19KB |
| every game today | `scoreboard?dates=…` | 406KB–1.3MB |
| logos | `a.espncdn.com/i/teamlogos/{ncaa,nfl}/500/{id}.png` | ~40KB |

There is no field filtering, so the only lever is picking a smaller endpoint.
`scoreboard/{eventId}` is the single-event form of `/scoreboard` — the
league-wide one carries ~18KB per game and college football has 762 teams in
it, which is why the board polls the single event and the live page is fetched
only while it is on screen.

**Every request must carry a particular `User-Agent` or ESPN's edge answers
403 Access Denied** — see [src/EspnUserAgent.h](src/EspnUserAgent.h). This is
the first thing to check if everything reads OFFLINE.

[docs/ESPN.md](docs/ESPN.md) has the rest: the shapes that catch you, and what
football's live state does and does not contain (college sends noticeably less
than the NFL).

## Building

Needs Docker and the reMarkable SDK matching **your device's OS version** —
a newer SDK produces a binary the device cannot load, with no error you can see
through AppLoad. [docs/PLATFORM.md](docs/PLATFORM.md) explains that and the
rest of the device.

```bash
./build.sh ~/Downloads/remarkable-ferrari-image-<ver>-sdk.sh   # first time
./build.sh                                                     # after that
RM_HOST=10.11.99.1 ./deploy.sh both                            # or cfb / nfl
```

`build.sh` cross-compiles, packs a bundle per league, and verifies each one:
every qrc entry present, newer than every QML source, carrying the right
application id. rcc packages files without parsing them and does not always
rerun, so a stale or partial bundle otherwise deploys happily and shows up as
a blank app.

## Checking it without a device

```bash
./probe.sh                        # run the real feed against live ESPN
./probe.sh 2754                   # a specific team
LEAGUE=nfl ./probe.sh             # the other league
tools/fixtures.sh                 # capture real payloads
tools/fixtures-live.sh            # rewind a finished game into the 3rd quarter
./probe.sh 2754 8 fixtures-live   # ... and parse that
```

`probe.sh` builds the real `GameFeed` and prints the state map the QML reads.
Fixture mode exists because football is played one day a week: the live
situation block — down, distance, possession, last play — is in no payload you
can fetch on a Tuesday.

**The layout has never been rendered off-device.** There is no Qt on the
machine this was written on, and the SDK's host Qt ships no QPA plugin, so
`preview.sh` and `--shot` have never run. Sizes were reasoned from a working
baseball board, then checked by deploying and looking.
[CLAUDE.md](CLAUDE.md) tracks what is verified and what is not.

## Layout

```
src/GameFeed.{h,cpp}     polls ESPN, flattens everything into one QVariantMap
src/League.h             the only thing that differs between the two leagues
src/LogoStore.{h,cpp}    team logos, greyscaled and cached on disk
src/EspnUserAgent.h      the header ESPN insists on
src/AppLoadLink.{h,cpp}  the AppLoad wire protocol
src/backend_main.cpp     the backend process: network, settings, icon, wakelock
ui/                      the QML, shared by both shells
qml/Preview.qml          desktop/fullscreen shell: window, panel, rotation
tools/probe.cpp          console harness: parse and print, no QML, no device
appload-native/          manifests, icons, and the packed frontends
```

QML reads one flat state map, so adding a field is one line in the feed and one
binding in the view. There is no model plumbing.

## Licence

[MIT](LICENSE).

The code and the drawn launcher icon are covered by it. Team logos are not
ours to license: they are fetched from ESPN at runtime onto the device that
displays them, and none are redistributed here.
