#!/usr/bin/env bash
# Install one league's AppLoad app on the tablet.
#
#   RM_HOST=10.11.99.1 ./deploy.sh cfb     # College FB
#   RM_HOST=10.11.99.1 ./deploy.sh nfl     # NFL
#   RM_HOST=10.11.99.1 ./deploy.sh both
#
# manifest.json, icon.png and resources.rcc are read once, when xochitl starts,
# so a frontend change needs the restart below. Only backend/entry is
# re-executed per launch, which is why BACKEND_ONLY=1 is free.
set -euo pipefail

RM_HOST="${RM_HOST:-10.11.99.1}"
HERE="$(cd "$(dirname "$0")" && pwd)"
LEAGUE="${1:-cfb}"
BACKEND_ONLY="${BACKEND_ONLY:-0}"

if [ "$LEAGUE" = "both" ]; then
    # The restart has to come after the LAST app's files are in place: AppLoad
    # scans its directory when xochitl starts, so restarting between the two
    # leaves the second one invisible until the next restart.
    RESTARTED=1 "$0" cfb
    "$0" nfl
    exit 0
fi

OUT="$HERE/appload-native/out/$LEAGUE"
[ -f "$OUT/backend/entry" ] || { echo "nothing built for '$LEAGUE' — run ./build.sh first"; exit 1; }

APPID="$(grep -o '"id"[^,]*' "$OUT/manifest.json" | head -1 | sed 's/.*: *"//;s/"//')"
DEST="/home/root/xovi/exthome/appload/$APPID"
echo "==> $LEAGUE -> $DEST"

# Copy to a temporary name and move it into place, never straight over the
# target. The backend keeps running after you close the app -- AppLoad leaves
# it up until the socket drops -- so scp onto a live binary fails with a bare
# "dest open ... Failure" (ETXTBSY) and the deploy half-happens. A move only
# relinks the directory entry, so the running process keeps the old inode and
# the next launch gets the new one.
ssh "root@${RM_HOST}" "mkdir -p ${DEST}/backend"
scp "$OUT/backend/entry" "root@${RM_HOST}:${DEST}/backend/entry.new"
scp "$OUT/backend/league" "root@${RM_HOST}:${DEST}/backend/league.new"
ssh "root@${RM_HOST}" "chmod +x ${DEST}/backend/entry.new \
    && mv -f ${DEST}/backend/entry.new ${DEST}/backend/entry \
    && mv -f ${DEST}/backend/league.new ${DEST}/backend/league"

# Does the device's dynamic linker actually accept this binary? Run it with a
# socket that does not exist: it should get as far as its own connect failure.
# A loader error instead means the SDK does not match the device's OS -- most
# likely a newer SDK's Qt -- and launching it from AppLoad would just show a
# blank app with nothing in the journal to explain why.
echo "==> checking the binary loads on the device"
LOADED="$(ssh "root@${RM_HOST}" "${DEST}/backend/entry /tmp/appload-probe-does-not-exist.sock 2>&1" || true)"
case "$LOADED" in
    *"not found"*|*"cannot open shared object"*)
        echo "$LOADED"
        echo
        echo "The device cannot load this binary. Its Qt is:"
        ssh "root@${RM_HOST}" 'ls -l /lib/libQt6Core.so.6'
        echo "Rebuild against the SDK for that version, e.g.:"
        echo "  IMAGE=rmpp-sdk-5.7 ./build.sh"
        exit 1
        ;;
esac
# The league line proves this install knows which league it is.
echo "$LOADED" | grep -E '^(league|appload):' | sed 's/^/  /'

if [ "$BACKEND_ONLY" = "1" ]; then
    echo "backend replaced; relaunch the app from the AppLoad sidebar."
    exit 0
fi

# The shipped icon is a neutral one -- a plain football, or the league mark.
# Once someone picks a team the backend writes that team's mark over it, so
# copying the bundled icon on every deploy would silently undo their choice.
# The settings file is the record that a pick has happened.
SETTINGS="/home/root/.config/scoreboard/${LEAGUE}.json"
FILES="manifest.json resources.rcc"
if ssh "root@${RM_HOST}" "[ -f ${SETTINGS} ]"; then
    echo "  keeping the icon: ${LEAGUE} has a team picked (${SETTINGS})"
else
    FILES="$FILES icon.png"
fi

for f in $FILES; do
    scp "$OUT/$f" "root@${RM_HOST}:${DEST}/$f.new"
    ssh "root@${RM_HOST}" "mv -f ${DEST}/$f.new ${DEST}/$f"
done

# AppLoad scans its directory when xochitl starts, so a new app needs this
# before its icon appears at all.
if [ "${RESTARTED:-0}" != "1" ]; then
    echo "==> restarting xochitl so it re-reads the manifest and the bundle"
    ssh "root@${RM_HOST}" 'systemctl restart xochitl'
fi

cat <<MSG

Installed "$APPID" at ${DEST} on ${RM_HOST}.

Tap it in the AppLoad sidebar. Swipe down from the top for AppLoad's own bar
(minimize / maximize / close).

If it launches blank, the bundle is the first suspect:
  ssh root@${RM_HOST} 'journalctl -u xochitl -n 80 | grep -i -e appload -e scoreboard'
MSG
